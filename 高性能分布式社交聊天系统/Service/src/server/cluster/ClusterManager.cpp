#include "cluster/ClusterManager.hpp"
#include "ProtobufMessageHelper.hpp"
#include <random>
#include <algorithm>
#include <sstream>
#include <iomanip>

// 静态常量定义
const string ClusterManager::SERVERS_PREFIX = "cluster:servers:";
const string ClusterManager::HEARTBEAT_CHANNEL = "cluster:heartbeat";
const string ClusterManager::EVENT_CHANNEL = "cluster:events";

// ServerInfo实现
string ServerInfo::to_protobuf() const {
    chat::ServerInfo proto_info;
    proto_info.set_server_id(server_id);
    proto_info.set_host(host);
    proto_info.set_port(port);
    proto_info.set_status(status);
    proto_info.set_load(load);
    proto_info.set_connections(connections);
    proto_info.set_max_connections(max_connections);
    proto_info.set_last_heartbeat(chrono::duration_cast<chrono::milliseconds>(
        last_heartbeat.time_since_epoch()).count());
    
    for (const auto& pair : metadata) {
        (*proto_info.mutable_metadata())[pair.first] = pair.second;
    }
    
    return proto_info.SerializeAsString();
}

ServerInfo ServerInfo::from_protobuf(const string& proto_str) {
    ServerInfo info;
    try {
        chat::ServerInfo proto_info;
        if (proto_info.ParseFromString(proto_str)) {
            info.server_id = proto_info.server_id();
            info.host = proto_info.host();
            info.port = proto_info.port();
            info.status = proto_info.status();
            info.load = proto_info.load();
            info.connections = proto_info.connections();
            info.max_connections = proto_info.max_connections();
            
            info.last_heartbeat = chrono::system_clock::time_point(
                chrono::milliseconds(proto_info.last_heartbeat()));
            
            for (const auto& pair : proto_info.metadata()) {
                info.metadata[pair.first] = pair.second;
            }
        }
    } catch (const exception& e) {
        // Protobuf解析失败，返回默认值
    }
    return info;
}

// ClusterManager实现
ClusterManager::ClusterManager() 
    : redis_connected_(false), health_check_running_(false), 
      health_check_interval_(10), max_failures_(3), round_robin_index_(0) {
}

ClusterManager::~ClusterManager() {
    stop_health_check();
}

bool ClusterManager::initialize(const string& redis_host, int redis_port) {
    // 连接Redis
    if (!redis_.connect(redis_host, redis_port)) {
        return false;
    }
    redis_connected_ = true;
    
    // 订阅集群事件频道
    redis_.subscribe(EVENT_CHANNEL);
    redis_.init_notify_handler([this](int channel, const string& message) {
        handle_redis_message(channel, message);
    });
    
    return true;
}

bool ClusterManager::register_server(const ServerInfo& info) {
    if (!redis_connected_) return false;
    
    lock_guard<mutex> lock(servers_mutex_);
    
    // 更新本地缓存
    servers_[info.server_id] = info;
    
    // 保存到Redis
    string key = SERVERS_PREFIX + info.server_id;
    if (!redis_.set(key, info.to_protobuf())) {
        return false;
    }
    
    // 设置过期时间（心跳超时后自动删除）
    redis_.expire(key, 60);
    
    // 发布服务器上线事件
    publish_server_event("server_online", info.server_id, info);
    
    if (on_server_online_) {
        on_server_online_(info.server_id, info);
    }
    
    return true;
}

bool ClusterManager::unregister_server(const string& server_id) {
    if (!redis_connected_) return false;
    
    lock_guard<mutex> lock(servers_mutex_);
    
    // 从本地缓存删除
    auto it = servers_.find(server_id);
    if (it != servers_.end()) {
        ServerInfo info = it->second;
        servers_.erase(it);
        
        // 从Redis删除
        string key = SERVERS_PREFIX + server_id;
        redis_.del(key);
        
        // 发布服务器下线事件
        publish_server_event("server_offline", server_id, info);
        
        if (on_server_offline_) {
            on_server_offline_(server_id, info);
        }
    }
    
    return true;
}

bool ClusterManager::update_server_status(const string& server_id, const ServerInfo& info) {
    if (!redis_connected_) return false;
    
    lock_guard<mutex> lock(servers_mutex_);
    
    // 更新本地缓存
    servers_[server_id] = info;
    
    // 更新Redis
    string key = SERVERS_PREFIX + server_id;
    if (!redis_.set(key, info.to_protobuf())) {
        return false;
    }
    
    // 刷新过期时间
    redis_.expire(key, 60);
    
    // 发布心跳事件
    json heartbeat;
    heartbeat["server_id"] = server_id;
    heartbeat["timestamp"] = chrono::duration_cast<chrono::milliseconds>(
        chrono::system_clock::now().time_since_epoch()).count();
    redis_.publish(HEARTBEAT_CHANNEL, heartbeat.dump());
    
    return true;
}

vector<ServerInfo> ClusterManager::get_available_servers() {
    lock_guard<mutex> lock(servers_mutex_);
    vector<ServerInfo> available;
    
    for (const auto& pair : servers_) {
        if (pair.second.is_online()) {
            available.push_back(pair.second);
        }
    }
    
    return available;
}

vector<ServerInfo> ClusterManager::get_all_servers() {
    lock_guard<mutex> lock(servers_mutex_);
    vector<ServerInfo> all_servers;
    
    for (const auto& pair : servers_) {
        all_servers.push_back(pair.second);
    }
    
    return all_servers;
}

ServerInfo ClusterManager::get_server_info(const string& server_id) {
    lock_guard<mutex> lock(servers_mutex_);
    auto it = servers_.find(server_id);
    if (it != servers_.end()) {
        return it->second;
    }
    return ServerInfo(); // 返回默认值
}

ServerInfo ClusterManager::select_server_round_robin() {
    lock_guard<mutex> lock(load_balance_mutex_);
    vector<ServerInfo> available = get_available_servers();
    
    if (available.empty()) {
        return ServerInfo();
    }
    
    ServerInfo selected = available[round_robin_index_ % available.size()];
    round_robin_index_++;
    return selected;
}

ServerInfo ClusterManager::select_server_least_connections() {
    vector<ServerInfo> available = get_available_servers();
    
    if (available.empty()) {
        return ServerInfo();
    }
    
    auto min_it = min_element(available.begin(), available.end(),
        [](const ServerInfo& a, const ServerInfo& b) {
            return a.connections < b.connections;
        });
    
    return *min_it;
}

ServerInfo ClusterManager::select_server_least_load() {
    vector<ServerInfo> available = get_available_servers();
    
    if (available.empty()) {
        return ServerInfo();
    }
    
    auto min_it = min_element(available.begin(), available.end(),
        [](const ServerInfo& a, const ServerInfo& b) {
            return a.get_load_score() < b.get_load_score();
        });
    
    return *min_it;
}

ServerInfo ClusterManager::select_server_random() {
    vector<ServerInfo> available = get_available_servers();
    
    if (available.empty()) {
        return ServerInfo();
    }
    
    static random_device rd;
    static mt19937 gen(rd());
    uniform_int_distribution<> dis(0, available.size() - 1);
    
    return available[dis(gen)];
}

void ClusterManager::start_health_check() {
    if (health_check_running_) return;
    
    health_check_running_ = true;
    health_check_thread_ = thread(&ClusterManager::health_check_loop, this);
}

void ClusterManager::stop_health_check() {
    if (!health_check_running_) return;
    
    health_check_running_ = false;
    if (health_check_thread_.joinable()) {
        health_check_thread_.join();
    }
}

bool ClusterManager::ping_server(const ServerInfo& server) {
    // 简单的TCP连接测试
    // 实际项目中可能需要更复杂的健康检查逻辑
    try {
        // 这里只是示例，实际需要根据协议实现
        // 可以发送心跳包或者检查特定端点
        return true; // 假设连接成功
    } catch (...) {
        return false;
    }
}

void ClusterManager::handle_server_failure(const string& server_id) {
    lock_guard<mutex> lock(servers_mutex_);
    
    failure_counts_[server_id]++;
    
    if (failure_counts_[server_id] >= max_failures_) {
        auto it = servers_.find(server_id);
        if (it != servers_.end()) {
            ServerInfo info = it->second;
            info.status = "offline";
            servers_[server_id] = info;
            
            // 发布故障事件
            publish_server_event("server_failure", server_id, info);
            
            if (on_server_failure_) {
                on_server_failure_(server_id, info);
            }
        }
    }
}

void ClusterManager::handle_server_recovery(const string& server_id) {
    lock_guard<mutex> lock(servers_mutex_);
    
    failure_counts_[server_id] = 0;
    
    auto it = servers_.find(server_id);
    if (it != servers_.end()) {
        ServerInfo info = it->second;
        info.status = "online";
        info.last_heartbeat = chrono::system_clock::now();
        servers_[server_id] = info;
        
        // 发布恢复事件
        publish_server_event("server_recovery", server_id, info);
        
        if (on_server_online_) {
            on_server_online_(server_id, info);
        }
    }
}

ClusterManager::ClusterStats ClusterManager::get_cluster_stats() {
    lock_guard<mutex> lock(servers_mutex_);
    ClusterStats stats = {};
    
    stats.total_servers = servers_.size();
    
    for (const auto& pair : servers_) {
        const ServerInfo& info = pair.second;
        if (info.is_online()) {
            stats.online_servers++;
            stats.total_connections += info.connections;
            stats.avg_load += info.load;
            stats.total_load_score += info.get_load_score();
        } else {
            stats.offline_servers++;
        }
    }
    
    if (stats.online_servers > 0) {
        stats.avg_load /= stats.online_servers;
    }
    
    return stats;
}

void ClusterManager::health_check_loop() {
    while (health_check_running_) {
        vector<ServerInfo> servers = get_all_servers();
        
        for (const auto& server : servers) {
            if (!ping_server(server)) {
                handle_server_failure(server.server_id);
            } else {
                handle_server_recovery(server.server_id);
            }
        }
        
        this_thread::sleep_for(chrono::seconds(health_check_interval_));
    }
}

void ClusterManager::publish_server_event(const string& event, const string& server_id, const ServerInfo& info) {
    if (!redis_connected_) return;
    
    chat::ClusterEvent cluster_event;
    cluster_event.set_event(event);
    cluster_event.set_server_id(server_id);
    cluster_event.set_timestamp(chrono::duration_cast<chrono::milliseconds>(
        chrono::system_clock::now().time_since_epoch()).count());
    
    // 设置服务器信息
    chat::ServerInfo* proto_info = cluster_event.mutable_server_info();
    proto_info->set_server_id(info.server_id);
    proto_info->set_host(info.host);
    proto_info->set_port(info.port);
    proto_info->set_status(info.status);
    proto_info->set_load(info.load);
    proto_info->set_connections(info.connections);
    proto_info->set_max_connections(info.max_connections);
    proto_info->set_last_heartbeat(chrono::duration_cast<chrono::milliseconds>(
        info.last_heartbeat.time_since_epoch()).count());
    
    for (const auto& pair : info.metadata) {
        (*proto_info->mutable_metadata())[pair.first] = pair.second;
    }
    
    redis_.publish(EVENT_CHANNEL, cluster_event.SerializeAsString());
}

void ClusterManager::handle_redis_message(int channel, const string& message) {
    try {
        chat::ClusterEvent cluster_event;
        if (!cluster_event.ParseFromString(message)) {
            return; // 解析失败
        }
        
        string event = cluster_event.event();
        string server_id = cluster_event.server_id();
        
        if (event == "server_online" || event == "server_recovery") {
            const chat::ServerInfo& proto_info = cluster_event.server_info();
            ServerInfo info;
            info.server_id = proto_info.server_id();
            info.host = proto_info.host();
            info.port = proto_info.port();
            info.status = proto_info.status();
            info.load = proto_info.load();
            info.connections = proto_info.connections();
            info.max_connections = proto_info.max_connections();
            info.last_heartbeat = chrono::system_clock::time_point(
                chrono::milliseconds(proto_info.last_heartbeat()));
            
            for (const auto& pair : proto_info.metadata()) {
                info.metadata[pair.first] = pair.second;
            }
            
            lock_guard<mutex> lock(servers_mutex_);
            servers_[server_id] = info;
        } else if (event == "server_offline" || event == "server_failure") {
            lock_guard<mutex> lock(servers_mutex_);
            auto it = servers_.find(server_id);
            if (it != servers_.end()) {
                it->second.status = "offline";
            }
        }
    } catch (...) {
        // 忽略解析错误
    }
}

string ClusterManager::generate_server_id() {
    auto now = chrono::system_clock::now();
    auto timestamp = chrono::duration_cast<chrono::milliseconds>(
        now.time_since_epoch()).count();
    
    stringstream ss;
    ss << "server_" << timestamp;
    return ss.str();
}
