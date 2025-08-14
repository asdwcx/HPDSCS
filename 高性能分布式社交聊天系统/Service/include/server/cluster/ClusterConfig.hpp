#ifndef CLUSTER_CONFIG_H
#define CLUSTER_CONFIG_H

#include <string>
#include <map>
#include <vector>
#include <mutex>
#include <fstream>
#include "Redis.hpp"

using namespace std;

// 配置项结构
struct ConfigItem {
    string key;
    string value;
    string type;        // string, int, bool, json
    string description; // 配置说明
    bool is_encrypted;  // 是否加密存储
    
    ConfigItem() : is_encrypted(false) {}
    ConfigItem(const string& k, const string& v, const string& t = "string", 
               const string& desc = "", bool encrypted = false)
        : key(k), value(v), type(t), description(desc), is_encrypted(encrypted) {}
    
    // 类型转换方法
    int as_int() const;
    bool as_bool() const;
    double as_double() const;
    vector<string> as_string_list() const;
    map<string, string> as_string_map() const;
    
    // 序列化
    string to_protobuf() const;
    static ConfigItem from_protobuf(const string& proto_str);
};

// 集群配置管理器
class ClusterConfig {
public:
    ClusterConfig();
    ~ClusterConfig();
    
    // 初始化
    bool initialize(const string& config_file = "", const string& redis_host = "127.0.0.1", int redis_port = 6379);
    
    // 配置读取
    string get_string(const string& key, const string& default_value = "");
    int get_int(const string& key, int default_value = 0);
    bool get_bool(const string& key, bool default_value = false);
    double get_double(const string& key, double default_value = 0.0);
    vector<string> get_string_list(const string& key);
    map<string, string> get_string_map(const string& key);
    
    // 配置设置
    bool set_string(const string& key, const string& value, bool persist = true);
    bool set_int(const string& key, int value, bool persist = true);
    bool set_bool(const string& key, bool value, bool persist = true);
    bool set_double(const string& key, double value, bool persist = true);
    bool set_string_list(const string& key, const vector<string>& value, bool persist = true);
    bool set_string_map(const string& key, const map<string, string>& value, bool persist = true);
    
    // 配置管理
    bool has_config(const string& key);
    bool remove_config(const string& key);
    vector<string> get_all_keys();
    map<string, ConfigItem> get_all_configs();
    
    // 配置同步
    bool sync_from_redis();
    bool sync_to_redis();
    bool sync_from_file();
    bool sync_to_file();
    
    // 配置监听
    using ConfigChangeCallback = function<void(const string&, const string&, const string&)>;
    void set_config_change_callback(ConfigChangeCallback callback);
    void start_config_watch();
    void stop_config_watch();
    
    // 配置验证
    bool validate_config(const string& key, const string& value);
    bool add_validator(const string& key, function<bool(const string&)> validator);
    
    // 配置加密
    bool set_encryption_key(const string& key);
    string encrypt_value(const string& value);
    string decrypt_value(const string& value);
    
    // 默认配置
    void load_default_configs();
    
    // 配置导入导出
    bool export_config(const string& file_path);
    bool import_config(const string& file_path);
    
private:
    // 存储
    map<string, ConfigItem> configs_;
    mutable mutex config_mutex_;
    
    // Redis连接
    Redis redis_;
    bool redis_connected_;
    
    // 文件配置
    string config_file_path_;
    
    // 配置监听
    bool config_watch_running_;
    thread config_watch_thread_;
    ConfigChangeCallback config_change_callback_;
    
    // 配置验证器
    map<string, function<bool(const string&)>> validators_;
    
    // 加密
    string encryption_key_;
    
    // 内部方法
    bool set_config_item(const ConfigItem& item, bool persist);
    void config_watch_loop();
    void handle_config_change(const string& key, const string& old_value, const string& new_value);
    string generate_redis_key(const string& key);
    
    // 工具方法
    vector<string> split_string(const string& str, char delimiter);
    string join_strings(const vector<string>& strings, char delimiter);
    map<string, string> parse_key_value_pairs(const string& str);
    string serialize_key_value_pairs(const map<string, string>& pairs);
    
    // 加密工具
    string base64_encode(const string& data);
    string base64_decode(const string& data);
    string simple_encrypt(const string& data, const string& key);
    string simple_decrypt(const string& data, const string& key);
    
    // Redis键前缀
    static const string CONFIG_PREFIX;
    static const string CONFIG_CHANGE_CHANNEL;
};

// 全局配置管理器实例
extern ClusterConfig* g_cluster_config;

// 便捷宏定义
#define GET_CONFIG_STRING(key, default_val) (g_cluster_config ? g_cluster_config->get_string(key, default_val) : default_val)
#define GET_CONFIG_INT(key, default_val) (g_cluster_config ? g_cluster_config->get_int(key, default_val) : default_val)
#define GET_CONFIG_BOOL(key, default_val) (g_cluster_config ? g_cluster_config->get_bool(key, default_val) : default_val)
#define SET_CONFIG_STRING(key, value) (g_cluster_config ? g_cluster_config->set_string(key, value) : false)
#define SET_CONFIG_INT(key, value) (g_cluster_config ? g_cluster_config->set_int(key, value) : false)
#define SET_CONFIG_BOOL(key, value) (g_cluster_config ? g_cluster_config->set_bool(key, value) : false)

#endif // CLUSTER_CONFIG_H
