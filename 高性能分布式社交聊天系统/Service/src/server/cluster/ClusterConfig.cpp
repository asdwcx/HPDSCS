#include "cluster/ClusterConfig.hpp"
#include "ProtobufMessageHelper.hpp"
#include "../../thirdparty/json.hpp"  // 保留用于配置文件导入导出
#include <iostream>
#include <sstream>
#include <algorithm>
#include <fstream>

using json = nlohmann::json;

// 静态常量定义
const string ClusterConfig::CONFIG_PREFIX = "cluster:config:";
const string ClusterConfig::CONFIG_CHANGE_CHANNEL = "cluster:config:changes";

// 全局配置实例
ClusterConfig* g_cluster_config = nullptr;

// ConfigItem实现
int ConfigItem::as_int() const {
    try {
        return stoi(value);
    } catch (...) {
        return 0;
    }
}

bool ConfigItem::as_bool() const {
    string lower_val = value;
    transform(lower_val.begin(), lower_val.end(), lower_val.begin(), ::tolower);
    return lower_val == "true" || lower_val == "1" || lower_val == "yes" || lower_val == "on";
}

double ConfigItem::as_double() const {
    try {
        return stod(value);
    } catch (...) {
        return 0.0;
    }
}

vector<string> ConfigItem::as_string_list() const {
    vector<string> result;
    stringstream ss(value);
    string item;
    while (getline(ss, item, ',')) {
        // 移除首尾空格
        item.erase(0, item.find_first_not_of(" \t"));
        item.erase(item.find_last_not_of(" \t") + 1);
        if (!item.empty()) {
            result.push_back(item);
        }
    }
    return result;
}

map<string, string> ConfigItem::as_string_map() const {
    map<string, string> result;
    vector<string> pairs = as_string_list();
    for (const string& pair : pairs) {
        size_t pos = pair.find('=');
        if (pos != string::npos) {
            string key = pair.substr(0, pos);
            string val = pair.substr(pos + 1);
            // 移除首尾空格
            key.erase(0, key.find_first_not_of(" \t"));
            key.erase(key.find_last_not_of(" \t") + 1);
            val.erase(0, val.find_first_not_of(" \t"));
            val.erase(val.find_last_not_of(" \t") + 1);
            result[key] = val;
        }
    }
    return result;
}

string ConfigItem::to_protobuf() const {
    chat::ConfigItem proto_item;
    proto_item.set_key(key);
    proto_item.set_value(value);
    proto_item.set_type(type);
    proto_item.set_description(description);
    proto_item.set_is_encrypted(is_encrypted);
    return proto_item.SerializeAsString();
}

ConfigItem ConfigItem::from_protobuf(const string& proto_str) {
    ConfigItem item;
    try {
        chat::ConfigItem proto_item;
        if (proto_item.ParseFromString(proto_str)) {
            item.key = proto_item.key();
            item.value = proto_item.value();
            item.type = proto_item.type();
            item.description = proto_item.description();
            item.is_encrypted = proto_item.is_encrypted();
        }
    } catch (...) {
        // 解析失败，返回默认值
    }
    return item;
}

// 临时JSON方法，仅用于配置文件导入导出
string ConfigItem::to_json_for_export() const {
    json j;
    j["key"] = key;
    j["value"] = value;
    j["type"] = type;
    j["description"] = description;
    j["is_encrypted"] = is_encrypted;
    return j.dump();
}

ConfigItem ConfigItem::from_json_for_import(const string& json_str) {
    ConfigItem item;
    try {
        json j = json::parse(json_str);
        item.key = j["key"];
        item.value = j["value"];
        item.type = j["type"];
        item.description = j["description"];
        item.is_encrypted = j["is_encrypted"];
    } catch (...) {
        // 解析失败，返回默认值
    }
    return item;
}

// ClusterConfig实现
ClusterConfig::ClusterConfig() 
    : redis_connected_(false), config_watch_running_(false) {
    if (!g_cluster_config) {
        g_cluster_config = this;
    }
}

ClusterConfig::~ClusterConfig() {
    stop_config_watch();
    if (g_cluster_config == this) {
        g_cluster_config = nullptr;
    }
}

bool ClusterConfig::initialize(const string& config_file, const string& redis_host, int redis_port) {
    config_file_path_ = config_file;
    
    // 连接Redis
    if (!redis_.connect(redis_host, redis_port)) {
        cout << "Warning: Failed to connect to Redis, using local config only" << endl;
        redis_connected_ = false;
    } else {
        redis_connected_ = true;
        // 订阅配置变更频道
        redis_.subscribe(CONFIG_CHANGE_CHANNEL);
    }
    
    // 加载默认配置
    load_default_configs();
    
    // 从文件加载配置
    if (!config_file.empty()) {
        sync_from_file();
    }
    
    // 从Redis同步配置
    if (redis_connected_) {
        sync_from_redis();
    }
    
    return true;
}

string ClusterConfig::get_string(const string& key, const string& default_value) {
    lock_guard<mutex> lock(config_mutex_);
    auto it = configs_.find(key);
    if (it != configs_.end()) {
        string value = it->second.value;
        if (it->second.is_encrypted) {
            value = decrypt_value(value);
        }
        return value;
    }
    return default_value;
}

int ClusterConfig::get_int(const string& key, int default_value) {
    string str_val = get_string(key, to_string(default_value));
    try {
        return stoi(str_val);
    } catch (...) {
        return default_value;
    }
}

bool ClusterConfig::get_bool(const string& key, bool default_value) {
    string str_val = get_string(key, default_value ? "true" : "false");
    transform(str_val.begin(), str_val.end(), str_val.begin(), ::tolower);
    return str_val == "true" || str_val == "1" || str_val == "yes" || str_val == "on";
}

double ClusterConfig::get_double(const string& key, double default_value) {
    string str_val = get_string(key, to_string(default_value));
    try {
        return stod(str_val);
    } catch (...) {
        return default_value;
    }
}

vector<string> ClusterConfig::get_string_list(const string& key) {
    string str_val = get_string(key, "");
    if (str_val.empty()) return vector<string>();
    
    ConfigItem temp("", str_val);
    return temp.as_string_list();
}

map<string, string> ClusterConfig::get_string_map(const string& key) {
    string str_val = get_string(key, "");
    if (str_val.empty()) return map<string, string>();
    
    ConfigItem temp("", str_val);
    return temp.as_string_map();
}

bool ClusterConfig::set_string(const string& key, const string& value, bool persist) {
    // 验证配置
    if (!validate_config(key, value)) {
        return false;
    }
    
    ConfigItem item(key, value, "string");
    return set_config_item(item, persist);
}

bool ClusterConfig::set_int(const string& key, int value, bool persist) {
    return set_string(key, to_string(value), persist);
}

bool ClusterConfig::set_bool(const string& key, bool value, bool persist) {
    return set_string(key, value ? "true" : "false", persist);
}

bool ClusterConfig::set_double(const string& key, double value, bool persist) {
    return set_string(key, to_string(value), persist);
}

bool ClusterConfig::set_string_list(const string& key, const vector<string>& value, bool persist) {
    string str_value = join_strings(value, ',');
    return set_string(key, str_value, persist);
}

bool ClusterConfig::set_string_map(const string& key, const map<string, string>& value, bool persist) {
    string str_value = serialize_key_value_pairs(value);
    return set_string(key, str_value, persist);
}

bool ClusterConfig::has_config(const string& key) {
    lock_guard<mutex> lock(config_mutex_);
    return configs_.find(key) != configs_.end();
}

bool ClusterConfig::remove_config(const string& key) {
    lock_guard<mutex> lock(config_mutex_);
    
    string old_value;
    auto it = configs_.find(key);
    if (it != configs_.end()) {
        old_value = it->second.value;
        configs_.erase(it);
        
        // 从Redis删除
        if (redis_connected_) {
            redis_.del(generate_redis_key(key));
        }
        
        // 触发变更回调
        handle_config_change(key, old_value, "");
        return true;
    }
    return false;
}

vector<string> ClusterConfig::get_all_keys() {
    lock_guard<mutex> lock(config_mutex_);
    vector<string> keys;
    for (const auto& pair : configs_) {
        keys.push_back(pair.first);
    }
    return keys;
}

map<string, ConfigItem> ClusterConfig::get_all_configs() {
    lock_guard<mutex> lock(config_mutex_);
    return configs_;
}

bool ClusterConfig::sync_from_redis() {
    if (!redis_connected_) return false;
    
    // 获取所有配置键
    vector<string> keys = redis_.keys(CONFIG_PREFIX + "*");
    
    lock_guard<mutex> lock(config_mutex_);
    for (const string& redis_key : keys) {
        string value = redis_.get(redis_key);
        if (!value.empty()) {
            ConfigItem item = ConfigItem::from_protobuf(value);
            configs_[item.key] = item;
        }
    }
    
    return true;
}

bool ClusterConfig::sync_to_redis() {
    if (!redis_connected_) return false;
    
    lock_guard<mutex> lock(config_mutex_);
    for (const auto& pair : configs_) {
        string redis_key = generate_redis_key(pair.first);
        redis_.set(redis_key, pair.second.to_protobuf());
    }
    
    return true;
}

bool ClusterConfig::sync_from_file() {
    if (config_file_path_.empty()) return false;
    
    ifstream file(config_file_path_);
    if (!file.is_open()) return false;
    
    string line;
    lock_guard<mutex> lock(config_mutex_);
    
    while (getline(file, line)) {
        // 跳过注释和空行
        if (line.empty() || line[0] == '#' || line[0] == ';') continue;
        
        size_t pos = line.find('=');
        if (pos != string::npos) {
            string key = line.substr(0, pos);
            string value = line.substr(pos + 1);
            
            // 移除首尾空格
            key.erase(0, key.find_first_not_of(" \t"));
            key.erase(key.find_last_not_of(" \t") + 1);
            value.erase(0, value.find_first_not_of(" \t"));
            value.erase(value.find_last_not_of(" \t") + 1);
            
            ConfigItem item(key, value, "string");
            configs_[key] = item;
        }
    }
    
    file.close();
    return true;
}

bool ClusterConfig::sync_to_file() {
    if (config_file_path_.empty()) return false;
    
    ofstream file(config_file_path_);
    if (!file.is_open()) return false;
    
    lock_guard<mutex> lock(config_mutex_);
    
    file << "# Cluster Configuration File" << endl;
    file << "# Generated at: " << chrono::system_clock::now().time_since_epoch().count() << endl;
    file << endl;
    
    for (const auto& pair : configs_) {
        const ConfigItem& item = pair.second;
        if (!item.description.empty()) {
            file << "# " << item.description << endl;
        }
        file << item.key << "=" << item.value << endl;
        file << endl;
    }
    
    file.close();
    return true;
}

void ClusterConfig::set_config_change_callback(ConfigChangeCallback callback) {
    config_change_callback_ = callback;
}

void ClusterConfig::start_config_watch() {
    if (config_watch_running_ || !redis_connected_) return;
    
    config_watch_running_ = true;
    config_watch_thread_ = thread(&ClusterConfig::config_watch_loop, this);
}

void ClusterConfig::stop_config_watch() {
    if (!config_watch_running_) return;
    
    config_watch_running_ = false;
    if (config_watch_thread_.joinable()) {
        config_watch_thread_.join();
    }
}

bool ClusterConfig::validate_config(const string& key, const string& value) {
    auto it = validators_.find(key);
    if (it != validators_.end()) {
        return it->second(value);
    }
    return true; // 没有验证器时默认通过
}

bool ClusterConfig::add_validator(const string& key, function<bool(const string&)> validator) {
    validators_[key] = validator;
    return true;
}

bool ClusterConfig::set_encryption_key(const string& key) {
    encryption_key_ = key;
    return true;
}

string ClusterConfig::encrypt_value(const string& value) {
    if (encryption_key_.empty()) return value;
    return simple_encrypt(value, encryption_key_);
}

string ClusterConfig::decrypt_value(const string& value) {
    if (encryption_key_.empty()) return value;
    return simple_decrypt(value, encryption_key_);
}

void ClusterConfig::load_default_configs() {
    lock_guard<mutex> lock(config_mutex_);
    
    // 服务器配置
    configs_["server.port"] = ConfigItem("server.port", "8000", "int", "服务器监听端口");
    configs_["server.host"] = ConfigItem("server.host", "0.0.0.0", "string", "服务器监听地址");
    configs_["server.max_connections"] = ConfigItem("server.max_connections", "1000", "int", "最大连接数");
    configs_["server.thread_pool_size"] = ConfigItem("server.thread_pool_size", "4", "int", "线程池大小");
    
    // Redis配置
    configs_["redis.host"] = ConfigItem("redis.host", "127.0.0.1", "string", "Redis服务器地址");
    configs_["redis.port"] = ConfigItem("redis.port", "6379", "int", "Redis服务器端口");
    configs_["redis.password"] = ConfigItem("redis.password", "", "string", "Redis密码", true);
    configs_["redis.db"] = ConfigItem("redis.db", "0", "int", "Redis数据库编号");
    
    // MySQL配置
    configs_["mysql.host"] = ConfigItem("mysql.host", "127.0.0.1", "string", "MySQL服务器地址");
    configs_["mysql.port"] = ConfigItem("mysql.port", "3306", "int", "MySQL服务器端口");
    configs_["mysql.user"] = ConfigItem("mysql.user", "root", "string", "MySQL用户名");
    configs_["mysql.password"] = ConfigItem("mysql.password", "", "string", "MySQL密码", true);
    configs_["mysql.database"] = ConfigItem("mysql.database", "chat", "string", "MySQL数据库名");
    
    // 集群配置
    configs_["cluster.server_id"] = ConfigItem("cluster.server_id", "", "string", "服务器唯一ID");
    configs_["cluster.health_check_interval"] = ConfigItem("cluster.health_check_interval", "10", "int", "健康检查间隔(秒)");
    configs_["cluster.max_failures"] = ConfigItem("cluster.max_failures", "3", "int", "最大失败次数");
    configs_["cluster.load_balance_strategy"] = ConfigItem("cluster.load_balance_strategy", "least_connections", "string", "负载均衡策略");
    
    // 文件传输配置
    configs_["file.max_size"] = ConfigItem("file.max_size", "104857600", "int", "最大文件大小(字节)");
    configs_["file.chunk_size"] = ConfigItem("file.chunk_size", "65536", "int", "文件分片大小(字节)");
    configs_["file.upload_path"] = ConfigItem("file.upload_path", "./uploads", "string", "文件上传路径");
    
    // 安全配置
    configs_["security.enable_ssl"] = ConfigItem("security.enable_ssl", "false", "bool", "启用SSL");
    configs_["security.cert_file"] = ConfigItem("security.cert_file", "", "string", "SSL证书文件");
    configs_["security.key_file"] = ConfigItem("security.key_file", "", "string", "SSL私钥文件");
    configs_["security.max_login_attempts"] = ConfigItem("security.max_login_attempts", "5", "int", "最大登录尝试次数");
}

bool ClusterConfig::export_config(const string& file_path) {
    ofstream file(file_path);
    if (!file.is_open()) return false;
    
    lock_guard<mutex> lock(config_mutex_);
    
    json j;
    for (const auto& pair : configs_) {
        j[pair.first] = json::parse(pair.second.to_json_for_export());
    }
    
    file << j.dump(4) << endl;
    file.close();
    return true;
}

bool ClusterConfig::import_config(const string& file_path) {
    ifstream file(file_path);
    if (!file.is_open()) return false;
    
    string content((istreambuf_iterator<char>(file)), istreambuf_iterator<char>());
    file.close();
    
    try {
        json j = json::parse(content);
        lock_guard<mutex> lock(config_mutex_);
        
        for (auto& [key, value] : j.items()) {
            ConfigItem item = ConfigItem::from_json_for_import(value.dump());
            configs_[key] = item;
        }
        return true;
    } catch (...) {
        return false;
    }
}

// 内部方法实现
bool ClusterConfig::set_config_item(const ConfigItem& item, bool persist) {
    string old_value;
    
    {
        lock_guard<mutex> lock(config_mutex_);
        auto it = configs_.find(item.key);
        if (it != configs_.end()) {
            old_value = it->second.value;
        }
        
        ConfigItem new_item = item;
        if (new_item.is_encrypted) {
            new_item.value = encrypt_value(new_item.value);
        }
        
        configs_[item.key] = new_item;
    }
    
        // 持久化到Redis
        if (persist && redis_connected_) {
            string redis_key = generate_redis_key(item.key);
            redis_.set(redis_key, item.to_protobuf());        // 发布配置变更事件
        json change_event;
        change_event["key"] = item.key;
        change_event["old_value"] = old_value;
        change_event["new_value"] = item.value;
        change_event["timestamp"] = chrono::duration_cast<chrono::milliseconds>(
            chrono::system_clock::now().time_since_epoch()).count();
        redis_.publish(CONFIG_CHANGE_CHANNEL, change_event.dump());
    }
    
    // 触发变更回调
    handle_config_change(item.key, old_value, item.value);
    
    return true;
}

void ClusterConfig::config_watch_loop() {
    // Redis配置变更监听在Redis类中已经实现
    while (config_watch_running_) {
        this_thread::sleep_for(chrono::seconds(1));
    }
}

void ClusterConfig::handle_config_change(const string& key, const string& old_value, const string& new_value) {
    if (config_change_callback_) {
        config_change_callback_(key, old_value, new_value);
    }
}

string ClusterConfig::generate_redis_key(const string& key) {
    return CONFIG_PREFIX + key;
}

// 工具方法实现
vector<string> ClusterConfig::split_string(const string& str, char delimiter) {
    vector<string> result;
    stringstream ss(str);
    string item;
    while (getline(ss, item, delimiter)) {
        result.push_back(item);
    }
    return result;
}

string ClusterConfig::join_strings(const vector<string>& strings, char delimiter) {
    if (strings.empty()) return "";
    
    stringstream ss;
    ss << strings[0];
    for (size_t i = 1; i < strings.size(); ++i) {
        ss << delimiter << strings[i];
    }
    return ss.str();
}

map<string, string> ClusterConfig::parse_key_value_pairs(const string& str) {
    map<string, string> result;
    vector<string> pairs = split_string(str, ',');
    for (const string& pair : pairs) {
        size_t pos = pair.find('=');
        if (pos != string::npos) {
            result[pair.substr(0, pos)] = pair.substr(pos + 1);
        }
    }
    return result;
}

string ClusterConfig::serialize_key_value_pairs(const map<string, string>& pairs) {
    vector<string> result;
    for (const auto& pair : pairs) {
        result.push_back(pair.first + "=" + pair.second);
    }
    return join_strings(result, ',');
}

// 简单的Base64编码实现
string ClusterConfig::base64_encode(const string& data) {
    // 简化的Base64实现，实际项目建议使用专业库
    const string chars = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    string result;
    int val = 0, valb = -6;
    for (unsigned char c : data) {
        val = (val << 8) + c;
        valb += 8;
        while (valb >= 0) {
            result.push_back(chars[(val >> valb) & 0x3F]);
            valb -= 6;
        }
    }
    if (valb > -6) result.push_back(chars[((val << 8) >> (valb + 8)) & 0x3F]);
    while (result.size() % 4) result.push_back('=');
    return result;
}

string ClusterConfig::base64_decode(const string& data) {
    // 简化的Base64解码实现
    const int T[128] = {
        -1,-1,-1,-1, -1,-1,-1,-1, -1,-1,-1,-1, -1,-1,-1,-1,
        -1,-1,-1,-1, -1,-1,-1,-1, -1,-1,-1,-1, -1,-1,-1,-1,
        -1,-1,-1,-1, -1,-1,-1,-1, -1,-1,-1,62, -1,-1,-1,63,
        52,53,54,55, 56,57,58,59, 60,61,-1,-1, -1,-1,-1,-1,
        -1, 0, 1, 2,  3, 4, 5, 6,  7, 8, 9,10, 11,12,13,14,
        15,16,17,18, 19,20,21,22, 23,24,25,-1, -1,-1,-1,-1,
        -1,26,27,28, 29,30,31,32, 33,34,35,36, 37,38,39,40,
        41,42,43,44, 45,46,47,48, 49,50,51,-1, -1,-1,-1,-1
    };
    string result;
    int val = 0, valb = -8;
    for (unsigned char c : data) {
        if (T[c] == -1) break;
        val = (val << 6) + T[c];
        valb += 6;
        if (valb >= 0) {
            result.push_back(char((val >> valb) & 0xFF));
            valb -= 8;
        }
    }
    return result;
}

string ClusterConfig::simple_encrypt(const string& data, const string& key) {
    string result = data;
    for (size_t i = 0; i < result.size(); ++i) {
        result[i] ^= key[i % key.size()];
    }
    return base64_encode(result);
}

string ClusterConfig::simple_decrypt(const string& data, const string& key) {
    string decoded = base64_decode(data);
    for (size_t i = 0; i < decoded.size(); ++i) {
        decoded[i] ^= key[i % key.size()];
    }
    return decoded;
}
