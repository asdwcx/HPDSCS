#ifndef DISTRIBUTED_SERVICE_COORDINATOR_H
#define DISTRIBUTED_SERVICE_COORDINATOR_H

#include "discovery/ZooKeeperServiceDiscovery.hpp"
#include "services/AuthServiceImpl.hpp"
#include "services/PrivateChatServiceImpl.hpp"
#include "FileCleanupManager.hpp"
#include "FileModel.hpp"
#include "FileTransferRedisModel.hpp"
#include <memory>
#include <vector>
#include <thread>
#include <map>
#include <string>

using namespace std;

// 服务配置结构
struct ServiceConfig {
    string service_name;
    string host;
    int port;
    bool enabled;
    map<string, string> metadata;
    
    ServiceConfig() : port(0), enabled(true) {}
    ServiceConfig(const string& name, const string& h, int p, bool e = true)
        : service_name(name), host(h), port(p), enabled(e) {}
};

// 分布式服务协调器
class DistributedServiceCoordinator {
public:
    DistributedServiceCoordinator();
    ~DistributedServiceCoordinator();
    
    // 初始化
    bool initialize(const string& config_file = "");
    
    // 启动所有服务
    bool start_all_services();
    
    // 停止所有服务
    void stop_all_services();
    
    // 等待服务停止
    void wait_for_shutdown();
    
    // 添加服务配置
    void add_service_config(const ServiceConfig& config);
    
    // 启动特定服务
    bool start_service(const string& service_name);
    
    // 停止特定服务
    void stop_service(const string& service_name);
    
    // 获取服务状态
    bool is_service_running(const string& service_name);
    
    // 获取所有服务状态
    map<string, bool> get_all_service_status();
    
    // 设置ZooKeeper连接信息
    void set_zookeeper_config(const string& hosts, int session_timeout = 30000);
    
    // 健康检查
    void start_health_monitor();
    void stop_health_monitor();
    
    // 文件清理服务管理
    bool initialize_file_cleanup_service();
    void start_file_cleanup_service();
    void stop_file_cleanup_service();
    CleanupStats get_file_cleanup_stats();

private:
    // ZooKeeper服务发现
    unique_ptr<ZooKeeperServiceDiscovery> service_discovery_;
    string zk_hosts_;
    int zk_session_timeout_;
    
    // 服务配置
    map<string, ServiceConfig> service_configs_;
    
    // 运行中的服务
    map<string, unique_ptr<GrpcServiceBase>> running_services_;
    
    // 健康监控
    thread health_monitor_thread_;
    atomic<bool> health_monitor_running_;
    
    // 文件清理服务
    unique_ptr<FileCleanupManager> file_cleanup_manager_;
    
    // 服务创建工厂方法
    unique_ptr<GrpcServiceBase> create_service(const ServiceConfig& config);
    
    // 配置文件解析
    bool load_config_file(const string& config_file);
    
    // 健康监控循环
    void health_monitor_loop();
    
    // 服务事件处理
    void handle_service_event(ServiceEvent event, const ServiceInstance& instance);
    
    // 默认服务配置
    void load_default_configs();
    
    // 工具方法
    ServiceConfig parse_service_config(const string& config_str);
    void log_service_status();
};

// 全局协调器实例
extern DistributedServiceCoordinator* g_service_coordinator;

#endif // DISTRIBUTED_SERVICE_COORDINATOR_H
