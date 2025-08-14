#include "DistributedServiceCoordinator.hpp"
#include "../../thirdparty/json.hpp"
#include <fstream>
#include <iostream>
#include <chrono>
#include <thread>

using json = nlohmann::json;
using namespace std;
using namespace std::chrono;

// 全局实例
DistributedServiceCoordinator* g_service_coordinator = nullptr;

DistributedServiceCoordinator::DistributedServiceCoordinator()
    : zk_session_timeout_(30000), health_monitor_running_(false) {
    
    if (!g_service_coordinator) {
        g_service_coordinator = this;
    }
    
    // 加载默认配置
    load_default_configs();
}

DistributedServiceCoordinator::~DistributedServiceCoordinator() {
    stop_all_services();
    stop_health_monitor();
    
    if (g_service_coordinator == this) {
        g_service_coordinator = nullptr;
    }
}

bool DistributedServiceCoordinator::initialize(const string& config_file) {
    cout << "Initializing Distributed Service Coordinator..." << endl;
    
    // 加载配置文件
    if (!config_file.empty()) {
        if (!load_config_file(config_file)) {
            cerr << "Failed to load config file: " << config_file << endl;
            return false;
        }
    }
    
    // 初始化ZooKeeper服务发现
    if (!zk_hosts_.empty()) {
        service_discovery_ = make_unique<ZooKeeperServiceDiscovery>();
        
        if (!service_discovery_->initialize(zk_hosts_, zk_session_timeout_)) {
            cerr << "Failed to initialize ZooKeeper service discovery" << endl;
            return false;
        }
        
        cout << "ZooKeeper service discovery initialized" << endl;
    } else {
        cout << "Warning: ZooKeeper not configured, running without service discovery" << endl;
    }
    
    // 启动健康监控
    start_health_monitor();
    
    // 初始化并启动文件清理服务
    if (!initialize_file_cleanup_service()) {
        cerr << "Warning: Failed to initialize file cleanup service" << endl;
        // 不返回false，因为文件清理不是核心功能
    }
    
    cout << "Distributed Service Coordinator initialized successfully" << endl;
    return true;
}

bool DistributedServiceCoordinator::start_all_services() {
    cout << "Starting all configured services..." << endl;
    
    // 启动文件清理服务
    if (!start_file_cleanup_service()) {
        cerr << "Warning: Failed to start file cleanup service" << endl;
        // 不影响其他服务的启动
    }
    
    bool all_success = true;
    for (const auto& pair : service_configs_) {
        const ServiceConfig& config = pair.second;
        
        if (!config.enabled) {
            cout << "Service " << config.service_name << " is disabled, skipping" << endl;
            continue;
        }
        
        if (!start_service(config.service_name)) {
            cerr << "Failed to start service: " << config.service_name << endl;
            all_success = false;
        }
    }
    
    if (all_success) {
        cout << "All services started successfully" << endl;
    } else {
        cout << "Some services failed to start" << endl;
    }
    
    log_service_status();
    return all_success;
}

void DistributedServiceCoordinator::stop_all_services() {
    cout << "Stopping all services..." << endl;
    
    // 停止文件清理服务
    stop_file_cleanup_service();
    
    for (auto& pair : running_services_) {
        cout << "Stopping service: " << pair.first << endl;
        pair.second->stop();
    }
    
    running_services_.clear();
    cout << "All services stopped" << endl;
}

void DistributedServiceCoordinator::wait_for_shutdown() {
    // 等待所有服务停止
    for (auto& pair : running_services_) {
        pair.second->wait_for_shutdown();
    }
}

void DistributedServiceCoordinator::add_service_config(const ServiceConfig& config) {
    service_configs_[config.service_name] = config;
    cout << "Added service config: " << config.service_name 
         << " at " << config.host << ":" << config.port << endl;
}

bool DistributedServiceCoordinator::start_service(const string& service_name) {
    auto config_it = service_configs_.find(service_name);
    if (config_it == service_configs_.end()) {
        cerr << "Service config not found: " << service_name << endl;
        return false;
    }
    
    const ServiceConfig& config = config_it->second;
    
    // 检查服务是否已经在运行
    if (running_services_.find(service_name) != running_services_.end()) {
        cout << "Service " << service_name << " is already running" << endl;
        return true;
    }
    
    cout << "Starting service: " << service_name << " at " 
         << config.host << ":" << config.port << endl;
    
    try {
        // 创建服务实例
        auto service = create_service(config);
        if (!service) {
            cerr << "Failed to create service instance: " << service_name << endl;
            return false;
        }
        
        // 设置服务发现
        if (service_discovery_) {
            service->set_service_discovery(service_discovery_.get());
        }
        
        // 启动服务
        if (!service->start()) {
            cerr << "Failed to start service: " << service_name << endl;
            return false;
        }
        
        // 保存运行中的服务
        running_services_[service_name] = move(service);
        
        cout << "Service " << service_name << " started successfully" << endl;
        return true;
        
    } catch (const exception& e) {
        cerr << "Exception starting service " << service_name << ": " << e.what() << endl;
        return false;
    }
}

void DistributedServiceCoordinator::stop_service(const string& service_name) {
    auto it = running_services_.find(service_name);
    if (it == running_services_.end()) {
        cout << "Service " << service_name << " is not running" << endl;
        return;
    }
    
    cout << "Stopping service: " << service_name << endl;
    it->second->stop();
    running_services_.erase(it);
    cout << "Service " << service_name << " stopped" << endl;
}

bool DistributedServiceCoordinator::is_service_running(const string& service_name) {
    auto it = running_services_.find(service_name);
    return it != running_services_.end() && it->second->is_running();
}

map<string, bool> DistributedServiceCoordinator::get_all_service_status() {
    map<string, bool> status;
    
    for (const auto& pair : service_configs_) {
        const string& service_name = pair.first;
        status[service_name] = is_service_running(service_name);
    }
    
    return status;
}

void DistributedServiceCoordinator::set_zookeeper_config(const string& hosts, int session_timeout) {
    zk_hosts_ = hosts;
    zk_session_timeout_ = session_timeout;
}

void DistributedServiceCoordinator::start_health_monitor() {
    if (health_monitor_running_) {
        return;
    }
    
    health_monitor_running_ = true;
    health_monitor_thread_ = thread(&DistributedServiceCoordinator::health_monitor_loop, this);
    cout << "Health monitor started" << endl;
}

void DistributedServiceCoordinator::stop_health_monitor() {
    if (!health_monitor_running_) {
        return;
    }
    
    health_monitor_running_ = false;
    if (health_monitor_thread_.joinable()) {
        health_monitor_thread_.join();
    }
    cout << "Health monitor stopped" << endl;
}

// 私有方法实现
unique_ptr<GrpcServiceBase> DistributedServiceCoordinator::create_service(const ServiceConfig& config) {
    if (config.service_name == "AuthService") {
        return make_unique<AuthServiceImpl>(config.host, config.port);
    } else if (config.service_name == "PrivateChatService") {
        return make_unique<PrivateChatServiceImpl>(config.host, config.port);
    }
    // 可以在这里添加更多服务类型
    
    cerr << "Unknown service type: " << config.service_name << endl;
    return nullptr;
}

bool DistributedServiceCoordinator::load_config_file(const string& config_file) {
    try {
        ifstream file(config_file);
        if (!file.is_open()) {
            cerr << "Cannot open config file: " << config_file << endl;
            return false;
        }
        
        json config;
        file >> config;
        
        // 解析ZooKeeper配置
        if (config.contains("zookeeper")) {
            auto zk_config = config["zookeeper"];
            if (zk_config.contains("hosts")) {
                zk_hosts_ = zk_config["hosts"];
            }
            if (zk_config.contains("session_timeout")) {
                zk_session_timeout_ = zk_config["session_timeout"];
            }
        }
        
        // 解析服务配置
        if (config.contains("services")) {
            auto services = config["services"];
            for (auto& [service_name, service_config] : services.items()) {
                ServiceConfig cfg;
                cfg.service_name = service_name;
                cfg.host = service_config.value("host", "127.0.0.1");
                cfg.port = service_config.value("port", 0);
                cfg.enabled = service_config.value("enabled", true);
                
                if (service_config.contains("metadata")) {
                    cfg.metadata = service_config["metadata"];
                }
                
                service_configs_[service_name] = cfg;
            }
        }
        
        cout << "Config file loaded: " << config_file << endl;
        return true;
        
    } catch (const exception& e) {
        cerr << "Error loading config file: " << e.what() << endl;
        return false;
    }
}

void DistributedServiceCoordinator::health_monitor_loop() {
    while (health_monitor_running_) {
        // 检查服务健康状态
        for (const auto& pair : running_services_) {
            const string& service_name = pair.first;
            const auto& service = pair.second;
            
            if (!service->is_running()) {
                cout << "Warning: Service " << service_name << " is not running" << endl;
                
                // 尝试重启服务
                cout << "Attempting to restart service: " << service_name << endl;
                stop_service(service_name);
                if (start_service(service_name)) {
                    cout << "Service " << service_name << " restarted successfully" << endl;
                } else {
                    cerr << "Failed to restart service: " << service_name << endl;
                }
            }
        }
        
        // 每30秒检查一次
        this_thread::sleep_for(chrono::seconds(30));
    }
}

void DistributedServiceCoordinator::handle_service_event(ServiceEvent event, const ServiceInstance& instance) {
    switch (event) {
        case ServiceEvent::SERVICE_ADDED:
            cout << "Service instance added: " << instance.service_name 
                 << " (" << instance.instance_id << ") at " << instance.get_address() << endl;
            break;
        case ServiceEvent::SERVICE_REMOVED:
            cout << "Service instance removed: " << instance.service_name 
                 << " (" << instance.instance_id << ") at " << instance.get_address() << endl;
            break;
        case ServiceEvent::SERVICE_UPDATED:
            cout << "Service instance updated: " << instance.service_name 
                 << " (" << instance.instance_id << ") at " << instance.get_address() << endl;
            break;
    }
}

void DistributedServiceCoordinator::load_default_configs() {
    // 认证服务
    ServiceConfig auth_config("AuthService", "127.0.0.1", 50001);
    auth_config.metadata["description"] = "User authentication service";
    service_configs_["AuthService"] = auth_config;
    
    // 私聊服务
    ServiceConfig private_chat_config("PrivateChatService", "127.0.0.1", 50002);
    private_chat_config.metadata["description"] = "Private chat messaging service";
    service_configs_["PrivateChatService"] = private_chat_config;
    
    // 群聊服务
    ServiceConfig group_chat_config("GroupChatService", "127.0.0.1", 50003);
    group_chat_config.metadata["description"] = "Group chat messaging service";
    service_configs_["GroupChatService"] = group_chat_config;
    
    // 好友服务
    ServiceConfig friend_config("FriendService", "127.0.0.1", 50004);
    friend_config.metadata["description"] = "Friend management service";
    service_configs_["FriendService"] = friend_config;
    
    // 文件服务
    ServiceConfig file_config("FileService", "127.0.0.1", 50005);
    file_config.metadata["description"] = "File transfer service";
    service_configs_["FileService"] = file_config;
    
    // 位置服务
    ServiceConfig location_config("LocationService", "127.0.0.1", 50006);
    location_config.metadata["description"] = "Location-based service";
    service_configs_["LocationService"] = location_config;
    
    // 通知服务
    ServiceConfig notification_config("NotificationService", "127.0.0.1", 50007);
    notification_config.metadata["description"] = "Real-time notification service";
    service_configs_["NotificationService"] = notification_config;
    
    cout << "Default service configurations loaded" << endl;
}

ServiceConfig DistributedServiceCoordinator::parse_service_config(const string& config_str) {
    // 简单的配置字符串解析：service_name:host:port:enabled
    ServiceConfig config;
    
    size_t pos1 = config_str.find(':');
    if (pos1 == string::npos) return config;
    
    config.service_name = config_str.substr(0, pos1);
    
    size_t pos2 = config_str.find(':', pos1 + 1);
    if (pos2 == string::npos) return config;
    
    config.host = config_str.substr(pos1 + 1, pos2 - pos1 - 1);
    
    size_t pos3 = config_str.find(':', pos2 + 1);
    if (pos3 == string::npos) {
        config.port = stoi(config_str.substr(pos2 + 1));
    } else {
        config.port = stoi(config_str.substr(pos2 + 1, pos3 - pos2 - 1));
        string enabled_str = config_str.substr(pos3 + 1);
        config.enabled = (enabled_str == "true" || enabled_str == "1");
    }
    
    return config;
}

void DistributedServiceCoordinator::log_service_status() {
    cout << "\n========== Service Status ==========" << endl;
    
    for (const auto& pair : service_configs_) {
        const string& service_name = pair.first;
        const ServiceConfig& config = pair.second;
        bool running = is_service_running(service_name);
        
        cout << service_name << " [" << config.host << ":" << config.port << "] - "
             << (running ? "RUNNING" : "STOPPED") 
             << (config.enabled ? "" : " (DISABLED)") << endl;
    }
    
    cout << "===================================\n" << endl;
}

// 文件清理服务管理方法实现
bool DistributedServiceCoordinator::initialize_file_cleanup_service() {
    try {
        // 创建文件清理管理器
        file_cleanup_manager_ = make_unique<FileCleanupManager>();
        
        // 加载配置文件
        string config_file = "./config/file_cleanup.json";
        if (!file_cleanup_manager_->load_config(config_file)) {
            cerr << "Warning: Failed to load file cleanup config from " << config_file 
                 << ", using default configuration" << endl;
            // 使用默认配置
        }
        
        cout << "File cleanup service initialized successfully" << endl;
        return true;
    } catch (const exception& e) {
        cerr << "Error initializing file cleanup service: " << e.what() << endl;
        return false;
    }
}

bool DistributedServiceCoordinator::start_file_cleanup_service() {
    if (!file_cleanup_manager_) {
        cerr << "File cleanup manager not initialized" << endl;
        return false;
    }
    
    try {
        if (file_cleanup_manager_->start_cleanup_service()) {
            cout << "File cleanup service started successfully" << endl;
            return true;
        } else {
            cerr << "Failed to start file cleanup service" << endl;
            return false;
        }
    } catch (const exception& e) {
        cerr << "Error starting file cleanup service: " << e.what() << endl;
        return false;
    }
}

void DistributedServiceCoordinator::stop_file_cleanup_service() {
    if (file_cleanup_manager_) {
        try {
            file_cleanup_manager_->stop_cleanup_service();
            cout << "File cleanup service stopped" << endl;
        } catch (const exception& e) {
            cerr << "Error stopping file cleanup service: " << e.what() << endl;
        }
    }
}

string DistributedServiceCoordinator::get_file_cleanup_stats() {
    if (!file_cleanup_manager_) {
        return "File cleanup service not initialized";
    }
    
    try {
        auto stats = file_cleanup_manager_->get_cleanup_statistics();
        
        stringstream ss;
        ss << "========== File Cleanup Statistics ==========\n";
        ss << "Service Status: " << (file_cleanup_manager_->is_cleanup_service_running() ? "RUNNING" : "STOPPED") << "\n";
        ss << "Total Cleanups: " << stats.total_cleanup_runs << "\n";
        ss << "Files Deleted: " << stats.files_deleted << "\n";
        ss << "Space Freed: " << (stats.space_freed_bytes / 1024.0 / 1024.0) << " MB\n";
        ss << "Last Cleanup: " << stats.last_cleanup_time << "\n";
        ss << "Errors: " << stats.error_count << "\n";
        ss << "Last Error: " << stats.last_error_message << "\n";
        ss << "============================================";
        
        return ss.str();
    } catch (const exception& e) {
        return string("Error getting cleanup stats: ") + e.what();
    }
}
