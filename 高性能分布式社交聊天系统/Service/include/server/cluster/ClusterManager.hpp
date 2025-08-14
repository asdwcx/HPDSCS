#ifndef CLUSTER_MANAGER_H
#define CLUSTER_MANAGER_H

#include <string>
#include <vector>
#include <map>
#include <mutex>
#include <thread>
#include <chrono>
#include <functional>
#include "Redis.hpp"

using namespace std;

// 服务器信息结构
struct ServerInfo {
    string server_id;           // 服务器唯一ID
    string host;                // 服务器地址
    int port;                   // 服务器端口
    string status;              // 服务器状态 online/offline/maintenance
    int load;                   // 当前负载 (0-100)
    int connections;            // 当前连接数
    int max_connections;        // 最大连接数
    chrono::system_clock::time_point last_heartbeat;  // 最后心跳时间
    map<string, string> metadata;  // 额外元数据
    
    ServerInfo() : port(0), load(0), connections(0), max_connections(1000), 
                   last_heartbeat(chrono::system_clock::now()) {}
    
    // 序列化为Protobuf
    string to_protobuf() const;
    
    // 从Protobuf反序列化
    static ServerInfo from_protobuf(const string& proto_str);
    
    // 检查是否在线
    bool is_online() const {
        auto now = chrono::system_clock::now();
        auto duration = chrono::duration_cast<chrono::seconds>(now - last_heartbeat);
        return status == "online" && duration.count() < 30; // 30秒内有心跳认为在线
    }
    
    // 计算负载评分 (越小越好)
    double get_load_score() const {
        if (status != "online") return 1000.0; // 离线服务器评分最高
        double conn_ratio = (double)connections / max_connections;
        double load_ratio = (double)load / 100.0;
        return conn_ratio * 0.7 + load_ratio * 0.3; // 连接数权重70%，CPU负载30%
    }
};

// 集群管理器
class ClusterManager {
public:
    ClusterManager();
    ~ClusterManager();
    
    // 初始化集群管理器
    bool initialize(const string& redis_host = "127.0.0.1", int redis_port = 6379);
    
    // 服务器注册与管理
    bool register_server(const ServerInfo& info);
    bool unregister_server(const string& server_id);
    bool update_server_status(const string& server_id, const ServerInfo& info);
    
    // 服务器查询
    vector<ServerInfo> get_available_servers();
    vector<ServerInfo> get_all_servers();
    ServerInfo get_server_info(const string& server_id);
    
    // 负载均衡算法
    ServerInfo select_server_round_robin();          // 轮询
    ServerInfo select_server_least_connections();    // 最少连接
    ServerInfo select_server_least_load();           // 最小负载
    ServerInfo select_server_random();               // 随机选择
    
    // 健康检查
    void start_health_check();
    void stop_health_check();
    bool ping_server(const ServerInfo& server);
    
    // 故障处理
    void handle_server_failure(const string& server_id);
    void handle_server_recovery(const string& server_id);
    
    // 配置管理
    void set_health_check_interval(int seconds) { health_check_interval_ = seconds; }
    void set_max_failures(int count) { max_failures_ = count; }
    
    // 事件回调
    using ServerEventCallback = function<void(const string&, const ServerInfo&)>;
    void set_server_online_callback(ServerEventCallback callback) { on_server_online_ = callback; }
    void set_server_offline_callback(ServerEventCallback callback) { on_server_offline_ = callback; }
    void set_server_failure_callback(ServerEventCallback callback) { on_server_failure_ = callback; }
    
    // 统计信息
    struct ClusterStats {
        int total_servers;
        int online_servers;
        int offline_servers;
        int total_connections;
        double avg_load;
        double total_load_score;
    };
    ClusterStats get_cluster_stats();
    
private:
    // Redis连接
    Redis redis_;
    bool redis_connected_;
    
    // 服务器信息存储
    map<string, ServerInfo> servers_;
    map<string, int> failure_counts_;
    mutable mutex servers_mutex_;
    
    // 健康检查
    bool health_check_running_;
    thread health_check_thread_;
    int health_check_interval_;  // 秒
    int max_failures_;
    
    // 负载均衡状态
    size_t round_robin_index_;
    mutable mutex load_balance_mutex_;
    
    // 事件回调
    ServerEventCallback on_server_online_;
    ServerEventCallback on_server_offline_;
    ServerEventCallback on_server_failure_;
    
    // 内部方法
    void health_check_loop();
    void publish_server_event(const string& event, const string& server_id, const ServerInfo& info);
    void handle_redis_message(int channel, const string& message);
    string generate_server_id();
    
    // Redis键名常量
    static const string SERVERS_PREFIX;
    static const string HEARTBEAT_CHANNEL;
    static const string EVENT_CHANNEL;
};

#endif // CLUSTER_MANAGER_H
