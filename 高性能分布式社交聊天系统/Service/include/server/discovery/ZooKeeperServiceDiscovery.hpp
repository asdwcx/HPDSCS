#ifndef ZOOKEEPER_SERVICE_DISCOVERY_H
#define ZOOKEEPER_SERVICE_DISCOVERY_H

#include <string>
#include <vector>
#include <map>
#include <functional>
#include <mutex>
#include <thread>
#include <condition_variable>
#include <zookeeper/zookeeper.h>

using namespace std;

// 服务实例信息
struct ServiceInstance {
    string service_name;    // 服务名称
    string instance_id;     // 实例ID
    string host;           // 主机地址
    int port;              // 端口号
    map<string, string> metadata; // 元数据
    int64_t register_time; // 注册时间
    
    ServiceInstance() : port(0), register_time(0) {}
    
    ServiceInstance(const string& name, const string& id, const string& h, int p)
        : service_name(name), instance_id(id), host(h), port(p), register_time(0) {}
    
    string to_json() const;
    static ServiceInstance from_json(const string& json_str);
    
    string get_address() const {
        return host + ":" + to_string(port);
    }
};

// 服务发现事件类型
enum class ServiceEvent {
    SERVICE_ADDED,
    SERVICE_REMOVED,
    SERVICE_UPDATED
};

// 服务发现回调函数
using ServiceEventCallback = function<void(ServiceEvent, const ServiceInstance&)>;

// ZooKeeper服务发现类
class ZooKeeperServiceDiscovery {
public:
    ZooKeeperServiceDiscovery();
    ~ZooKeeperServiceDiscovery();
    
    // 初始化连接
    bool initialize(const string& zk_hosts, int session_timeout = 30000);
    
    // 关闭连接
    void shutdown();
    
    // 服务注册
    bool register_service(const ServiceInstance& instance, bool ephemeral = true);
    
    // 服务注销
    bool unregister_service(const string& service_name, const string& instance_id);
    
    // 服务发现
    vector<ServiceInstance> discover_services(const string& service_name);
    
    // 监听服务变化
    bool watch_service(const string& service_name, ServiceEventCallback callback);
    
    // 停止监听服务
    void unwatch_service(const string& service_name);
    
    // 获取单个服务实例（负载均衡）
    ServiceInstance get_service_instance(const string& service_name, 
                                       const string& load_balance_strategy = "round_robin");
    
    // 服务健康检查
    bool is_service_healthy(const ServiceInstance& instance);
    
    // 设置服务元数据
    bool update_service_metadata(const string& service_name, const string& instance_id,
                               const map<string, string>& metadata);
    
    // 获取所有服务
    vector<string> get_all_services();
    
    // 连接状态
    bool is_connected() const { return connected_; }
    
    // 设置连接状态回调
    void set_connection_callback(function<void(bool)> callback) {
        connection_callback_ = callback;
    }

private:
    // ZooKeeper会话句柄
    zhandle_t* zk_handle_;
    
    // 连接状态
    bool connected_;
    bool initialized_;
    
    // 配置
    string zk_hosts_;
    int session_timeout_;
    
    // 服务缓存
    map<string, vector<ServiceInstance>> service_cache_;
    mutable mutex cache_mutex_;
    
    // 监听器
    map<string, ServiceEventCallback> service_watchers_;
    mutex watchers_mutex_;
    
    // 负载均衡状态
    map<string, size_t> round_robin_counters_;
    mutex lb_mutex_;
    
    // 连接状态回调
    function<void(bool)> connection_callback_;
    
    // 同步原语
    mutex connect_mutex_;
    condition_variable connect_cv_;
    
    // ZooKeeper路径
    static const string ROOT_PATH;
    static const string SERVICES_PATH;
    
    // 内部方法
    string get_service_path(const string& service_name);
    string get_instance_path(const string& service_name, const string& instance_id);
    
    bool create_path_recursively(const string& path);
    bool path_exists(const string& path);
    
    void update_service_cache(const string& service_name);
    void notify_service_event(const string& service_name, ServiceEvent event, 
                            const ServiceInstance& instance);
    
    // 负载均衡算法
    ServiceInstance select_round_robin(const vector<ServiceInstance>& instances, 
                                     const string& service_name);
    ServiceInstance select_random(const vector<ServiceInstance>& instances);
    ServiceInstance select_least_connections(const vector<ServiceInstance>& instances);
    
    // ZooKeeper回调函数
    static void global_watcher(zhandle_t* zh, int type, int state, 
                              const char* path, void* watcher_ctx);
    static void service_watcher(zhandle_t* zh, int type, int state, 
                               const char* path, void* watcher_ctx);
    
    void handle_connection_event(int state);
    void handle_service_change_event(const string& path);
    
    // 工具方法
    vector<string> split_path(const string& path);
    string join_path(const vector<string>& parts);
    
    // 禁用拷贝构造和赋值
    ZooKeeperServiceDiscovery(const ZooKeeperServiceDiscovery&) = delete;
    ZooKeeperServiceDiscovery& operator=(const ZooKeeperServiceDiscovery&) = delete;
};

// 全局服务发现实例
extern ZooKeeperServiceDiscovery* g_service_discovery;

// 便捷宏
#define REGISTER_SERVICE(instance) (g_service_discovery ? g_service_discovery->register_service(instance) : false)
#define DISCOVER_SERVICES(name) (g_service_discovery ? g_service_discovery->discover_services(name) : vector<ServiceInstance>())
#define GET_SERVICE_INSTANCE(name) (g_service_discovery ? g_service_discovery->get_service_instance(name) : ServiceInstance())

#endif // ZOOKEEPER_SERVICE_DISCOVERY_H
