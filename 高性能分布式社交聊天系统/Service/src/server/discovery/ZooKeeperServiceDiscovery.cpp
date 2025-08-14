#include "discovery/ZooKeeperServiceDiscovery.hpp"
#include "ProtobufMessageHelper.hpp"
#include <iostream>
#include <sstream>
#include <random>
#include <chrono>
#include <algorithm>

using namespace std::chrono;

// 静态常量定义
const string ZooKeeperServiceDiscovery::ROOT_PATH = "/chat_services";
const string ZooKeeperServiceDiscovery::SERVICES_PATH = ROOT_PATH + "/services";

// 全局实例
ZooKeeperServiceDiscovery* g_service_discovery = nullptr;

// ServiceInstance实现
string ServiceInstance::to_protobuf() const {
    chat::ServiceInstance pb_instance = ServiceDiscoveryMessageBuilder::create_service_instance(
        instance_id, service_name, host, port, {}, metadata);
    return ProtobufMessageHelper::serialize_message(pb_instance);
}

ServiceInstance ServiceInstance::from_protobuf(const string& protobuf_data) {
    ServiceInstance instance;
    try {
        chat::ServiceInstance pb_instance;
        if (ProtobufMessageHelper::deserialize_message(protobuf_data, pb_instance)) {
            instance.instance_id = pb_instance.id();
            instance.service_name = pb_instance.name();
            instance.host = pb_instance.host();
            instance.port = pb_instance.port();
            instance.register_time = pb_instance.timestamp();
            
            // 转换metadata
            for (const auto& pair : pb_instance.metadata()) {
                instance.metadata[pair.first] = pair.second;
            }
        }
    } catch (const exception& e) {
        // Protobuf解析失败，返回默认值
        LOG_ERROR << "Failed to parse ServiceInstance from protobuf: " << e.what();
    }
    return instance;
}

// ZooKeeperServiceDiscovery实现
ZooKeeperServiceDiscovery::ZooKeeperServiceDiscovery() 
    : zk_handle_(nullptr), connected_(false), initialized_(false), session_timeout_(30000) {
    if (!g_service_discovery) {
        g_service_discovery = this;
    }
}

ZooKeeperServiceDiscovery::~ZooKeeperServiceDiscovery() {
    shutdown();
    if (g_service_discovery == this) {
        g_service_discovery = nullptr;
    }
}

bool ZooKeeperServiceDiscovery::initialize(const string& zk_hosts, int session_timeout) {
    if (initialized_) {
        return true;
    }
    
    zk_hosts_ = zk_hosts;
    session_timeout_ = session_timeout;
    
    // 初始化ZooKeeper连接
    zk_handle_ = zookeeper_init(zk_hosts.c_str(), global_watcher, session_timeout, 0, this, 0);
    if (!zk_handle_) {
        cerr << "Failed to initialize ZooKeeper connection" << endl;
        return false;
    }
    
    // 等待连接建立
    unique_lock<mutex> lock(connect_mutex_);
    if (!connect_cv_.wait_for(lock, chrono::seconds(10), [this] { return connected_; })) {
        cerr << "ZooKeeper connection timeout" << endl;
        zookeeper_close(zk_handle_);
        zk_handle_ = nullptr;
        return false;
    }
    
    // 创建根路径
    if (!create_path_recursively(SERVICES_PATH)) {
        cerr << "Failed to create services path" << endl;
        shutdown();
        return false;
    }
    
    initialized_ = true;
    cout << "ZooKeeper service discovery initialized successfully" << endl;
    return true;
}

void ZooKeeperServiceDiscovery::shutdown() {
    if (!initialized_) return;
    
    // 清理监听器
    {
        lock_guard<mutex> lock(watchers_mutex_);
        service_watchers_.clear();
    }
    
    // 关闭ZooKeeper连接
    if (zk_handle_) {
        zookeeper_close(zk_handle_);
        zk_handle_ = nullptr;
    }
    
    connected_ = false;
    initialized_ = false;
    
    cout << "ZooKeeper service discovery shutdown" << endl;
}

bool ZooKeeperServiceDiscovery::register_service(const ServiceInstance& instance, bool ephemeral) {
    if (!connected_) {
        cerr << "ZooKeeper not connected" << endl;
        return false;
    }
    
    string service_path = get_service_path(instance.service_name);
    string instance_path = get_instance_path(instance.service_name, instance.instance_id);
    
    // 创建服务路径
    if (!create_path_recursively(service_path)) {
        cerr << "Failed to create service path: " << service_path << endl;
        return false;
    }
    
    // 准备实例数据
    ServiceInstance reg_instance = instance;
    reg_instance.register_time = duration_cast<milliseconds>(
        system_clock::now().time_since_epoch()).count();
    
    string data = reg_instance.to_protobuf();
    
    // 创建实例节点
    int flags = ephemeral ? ZOO_EPHEMERAL : 0;
    int ret = zoo_create(zk_handle_, instance_path.c_str(), data.c_str(), data.length(),
                        &ZOO_OPEN_ACL_UNSAFE, flags, nullptr, 0);
    
    if (ret != ZOK && ret != ZNODEEXISTS) {
        cerr << "Failed to register service instance: " << zerror(ret) << endl;
        return false;
    }
    
    // 如果节点已存在，更新数据
    if (ret == ZNODEEXISTS) {
        ret = zoo_set(zk_handle_, instance_path.c_str(), data.c_str(), data.length(), -1);
        if (ret != ZOK) {
            cerr << "Failed to update service instance: " << zerror(ret) << endl;
            return false;
        }
    }
    
    cout << "Service registered: " << instance.service_name 
         << " (" << instance.instance_id << ") at " << instance.get_address() << endl;
    
    return true;
}

bool ZooKeeperServiceDiscovery::unregister_service(const string& service_name, const string& instance_id) {
    if (!connected_) {
        return false;
    }
    
    string instance_path = get_instance_path(service_name, instance_id);
    
    int ret = zoo_delete(zk_handle_, instance_path.c_str(), -1);
    if (ret != ZOK && ret != ZNONODE) {
        cerr << "Failed to unregister service instance: " << zerror(ret) << endl;
        return false;
    }
    
    cout << "Service unregistered: " << service_name << " (" << instance_id << ")" << endl;
    return true;
}

vector<ServiceInstance> ZooKeeperServiceDiscovery::discover_services(const string& service_name) {
    vector<ServiceInstance> instances;
    
    if (!connected_) {
        return instances;
    }
    
    string service_path = get_service_path(service_name);
    
    // 获取子节点列表
    struct String_vector children;
    int ret = zoo_get_children(zk_handle_, service_path.c_str(), 0, &children);
    
    if (ret != ZOK) {
        if (ret != ZNONODE) {
            cerr << "Failed to get service children: " << zerror(ret) << endl;
        }
        return instances;
    }
    
    // 获取每个实例的数据
    for (int i = 0; i < children.count; ++i) {
        string instance_path = service_path + "/" + children.data[i];
        
        char buffer[4096];
        int buffer_len = sizeof(buffer);
        ret = zoo_get(zk_handle_, instance_path.c_str(), 0, buffer, &buffer_len, nullptr);
        
        if (ret == ZOK && buffer_len > 0) {
            buffer[buffer_len] = '\0';
            ServiceInstance instance = ServiceInstance::from_protobuf(string(buffer));
            if (!instance.service_name.empty()) {
                instances.push_back(instance);
            }
        }
    }
    
    deallocate_String_vector(&children);
    
    // 更新缓存
    {
        lock_guard<mutex> lock(cache_mutex_);
        service_cache_[service_name] = instances;
    }
    
    return instances;
}

bool ZooKeeperServiceDiscovery::watch_service(const string& service_name, ServiceEventCallback callback) {
    if (!connected_) {
        return false;
    }
    
    {
        lock_guard<mutex> lock(watchers_mutex_);
        service_watchers_[service_name] = callback;
    }
    
    string service_path = get_service_path(service_name);
    
    // 设置监听器
    struct String_vector children;
    int ret = zoo_get_children(zk_handle_, service_path.c_str(), 1, &children);
    
    if (ret != ZOK && ret != ZNONODE) {
        cerr << "Failed to watch service: " << zerror(ret) << endl;
        return false;
    }
    
    if (ret == ZOK) {
        deallocate_String_vector(&children);
    }
    
    // 初始化服务缓存
    update_service_cache(service_name);
    
    cout << "Started watching service: " << service_name << endl;
    return true;
}

void ZooKeeperServiceDiscovery::unwatch_service(const string& service_name) {
    lock_guard<mutex> lock(watchers_mutex_);
    service_watchers_.erase(service_name);
    cout << "Stopped watching service: " << service_name << endl;
}

ServiceInstance ZooKeeperServiceDiscovery::get_service_instance(const string& service_name, 
                                                              const string& load_balance_strategy) {
    vector<ServiceInstance> instances = discover_services(service_name);
    
    if (instances.empty()) {
        return ServiceInstance();
    }
    
    if (load_balance_strategy == "round_robin") {
        return select_round_robin(instances, service_name);
    } else if (load_balance_strategy == "random") {
        return select_random(instances);
    } else if (load_balance_strategy == "least_connections") {
        return select_least_connections(instances);
    } else {
        // 默认使用轮询
        return select_round_robin(instances, service_name);
    }
}

bool ZooKeeperServiceDiscovery::is_service_healthy(const ServiceInstance& instance) {
    // 简单的健康检查实现
    // 实际项目中可能需要更复杂的健康检查逻辑
    
    if (!connected_) {
        return false;
    }
    
    string instance_path = get_instance_path(instance.service_name, instance.instance_id);
    
    struct Stat stat;
    int ret = zoo_exists(zk_handle_, instance_path.c_str(), 0, &stat);
    
    return ret == ZOK;
}

bool ZooKeeperServiceDiscovery::update_service_metadata(const string& service_name, 
                                                       const string& instance_id,
                                                       const map<string, string>& metadata) {
    if (!connected_) {
        return false;
    }
    
    string instance_path = get_instance_path(service_name, instance_id);
    
    // 获取当前数据
    char buffer[4096];
    int buffer_len = sizeof(buffer);
    int ret = zoo_get(zk_handle_, instance_path.c_str(), 0, buffer, &buffer_len, nullptr);
    
    if (ret != ZOK) {
        cerr << "Failed to get service instance data: " << zerror(ret) << endl;
        return false;
    }
    
    buffer[buffer_len] = '\0';
    ServiceInstance instance = ServiceInstance::from_protobuf(string(buffer));
    
    // 更新元数据
    instance.metadata = metadata;
    
    string data = instance.to_protobuf();
    ret = zoo_set(zk_handle_, instance_path.c_str(), data.c_str(), data.length(), -1);
    
    if (ret != ZOK) {
        cerr << "Failed to update service metadata: " << zerror(ret) << endl;
        return false;
    }
    
    return true;
}

vector<string> ZooKeeperServiceDiscovery::get_all_services() {
    vector<string> services;
    
    if (!connected_) {
        return services;
    }
    
    struct String_vector children;
    int ret = zoo_get_children(zk_handle_, SERVICES_PATH.c_str(), 0, &children);
    
    if (ret == ZOK) {
        for (int i = 0; i < children.count; ++i) {
            services.push_back(children.data[i]);
        }
        deallocate_String_vector(&children);
    }
    
    return services;
}

// 内部方法实现
string ZooKeeperServiceDiscovery::get_service_path(const string& service_name) {
    return SERVICES_PATH + "/" + service_name;
}

string ZooKeeperServiceDiscovery::get_instance_path(const string& service_name, const string& instance_id) {
    return get_service_path(service_name) + "/" + instance_id;
}

bool ZooKeeperServiceDiscovery::create_path_recursively(const string& path) {
    if (path_exists(path)) {
        return true;
    }
    
    vector<string> parts = split_path(path);
    string current_path;
    
    for (const string& part : parts) {
        if (part.empty()) continue;
        
        current_path += "/" + part;
        
        if (!path_exists(current_path)) {
            int ret = zoo_create(zk_handle_, current_path.c_str(), "", 0,
                               &ZOO_OPEN_ACL_UNSAFE, 0, nullptr, 0);
            
            if (ret != ZOK && ret != ZNODEEXISTS) {
                cerr << "Failed to create path: " << current_path << " - " << zerror(ret) << endl;
                return false;
            }
        }
    }
    
    return true;
}

bool ZooKeeperServiceDiscovery::path_exists(const string& path) {
    if (!zk_handle_) return false;
    
    struct Stat stat;
    int ret = zoo_exists(zk_handle_, path.c_str(), 0, &stat);
    return ret == ZOK;
}

void ZooKeeperServiceDiscovery::update_service_cache(const string& service_name) {
    vector<ServiceInstance> current_instances = discover_services(service_name);
    vector<ServiceInstance> cached_instances;
    
    {
        lock_guard<mutex> lock(cache_mutex_);
        auto it = service_cache_.find(service_name);
        if (it != service_cache_.end()) {
            cached_instances = it->second;
        }
        service_cache_[service_name] = current_instances;
    }
    
    // 比较变化并触发事件
    set<string> current_ids, cached_ids;
    map<string, ServiceInstance> current_map, cached_map;
    
    for (const auto& instance : current_instances) {
        current_ids.insert(instance.instance_id);
        current_map[instance.instance_id] = instance;
    }
    
    for (const auto& instance : cached_instances) {
        cached_ids.insert(instance.instance_id);
        cached_map[instance.instance_id] = instance;
    }
    
    // 检查新增的服务
    for (const string& id : current_ids) {
        if (cached_ids.find(id) == cached_ids.end()) {
            notify_service_event(service_name, ServiceEvent::SERVICE_ADDED, current_map[id]);
        }
    }
    
    // 检查删除的服务
    for (const string& id : cached_ids) {
        if (current_ids.find(id) == current_ids.end()) {
            notify_service_event(service_name, ServiceEvent::SERVICE_REMOVED, cached_map[id]);
        }
    }
    
    // 检查更新的服务
    for (const string& id : current_ids) {
        if (cached_ids.find(id) != cached_ids.end()) {
            if (current_map[id].to_protobuf() != cached_map[id].to_protobuf()) {
                notify_service_event(service_name, ServiceEvent::SERVICE_UPDATED, current_map[id]);
            }
        }
    }
}

void ZooKeeperServiceDiscovery::notify_service_event(const string& service_name, ServiceEvent event, 
                                                    const ServiceInstance& instance) {
    lock_guard<mutex> lock(watchers_mutex_);
    auto it = service_watchers_.find(service_name);
    if (it != service_watchers_.end() && it->second) {
        it->second(event, instance);
    }
}

// 负载均衡算法实现
ServiceInstance ZooKeeperServiceDiscovery::select_round_robin(const vector<ServiceInstance>& instances, 
                                                             const string& service_name) {
    lock_guard<mutex> lock(lb_mutex_);
    size_t& counter = round_robin_counters_[service_name];
    size_t index = counter % instances.size();
    counter++;
    return instances[index];
}

ServiceInstance ZooKeeperServiceDiscovery::select_random(const vector<ServiceInstance>& instances) {
    static random_device rd;
    static mt19937 gen(rd());
    uniform_int_distribution<> dis(0, instances.size() - 1);
    return instances[dis(gen)];
}

ServiceInstance ZooKeeperServiceDiscovery::select_least_connections(const vector<ServiceInstance>& instances) {
    // 简化实现，实际项目中需要维护连接数统计
    return select_random(instances);
}

// ZooKeeper回调函数
void ZooKeeperServiceDiscovery::global_watcher(zhandle_t* zh, int type, int state, 
                                              const char* path, void* watcher_ctx) {
    ZooKeeperServiceDiscovery* self = static_cast<ZooKeeperServiceDiscovery*>(watcher_ctx);
    if (self) {
        self->handle_connection_event(state);
        
        if (type == ZOO_CHILD_EVENT && path) {
            self->handle_service_change_event(string(path));
        }
    }
}

void ZooKeeperServiceDiscovery::service_watcher(zhandle_t* zh, int type, int state, 
                                               const char* path, void* watcher_ctx) {
    ZooKeeperServiceDiscovery* self = static_cast<ZooKeeperServiceDiscovery*>(watcher_ctx);
    if (self && type == ZOO_CHILD_EVENT && path) {
        self->handle_service_change_event(string(path));
    }
}

void ZooKeeperServiceDiscovery::handle_connection_event(int state) {
    bool old_connected = connected_;
    
    switch (state) {
        case ZOO_CONNECTED_STATE:
            connected_ = true;
            cout << "ZooKeeper connected" << endl;
            break;
        case ZOO_EXPIRED_SESSION_STATE:
            connected_ = false;
            cout << "ZooKeeper session expired" << endl;
            break;
        case ZOO_AUTH_FAILED_STATE:
            connected_ = false;
            cout << "ZooKeeper authentication failed" << endl;
            break;
        case ZOO_CONNECTING_STATE:
            cout << "ZooKeeper connecting..." << endl;
            break;
        default:
            connected_ = false;
            break;
    }
    
    if (old_connected != connected_) {
        connect_cv_.notify_all();
        if (connection_callback_) {
            connection_callback_(connected_);
        }
    }
}

void ZooKeeperServiceDiscovery::handle_service_change_event(const string& path) {
    // 解析服务名称
    size_t pos = path.find_last_of('/');
    if (pos == string::npos) return;
    
    string parent_path = path.substr(0, pos);
    pos = parent_path.find_last_of('/');
    if (pos == string::npos) return;
    
    string service_name = parent_path.substr(pos + 1);
    
    // 更新服务缓存
    update_service_cache(service_name);
    
    // 重新设置监听器
    struct String_vector children;
    int ret = zoo_get_children(zk_handle_, parent_path.c_str(), 1, &children);
    if (ret == ZOK) {
        deallocate_String_vector(&children);
    }
}

// 工具方法
vector<string> ZooKeeperServiceDiscovery::split_path(const string& path) {
    vector<string> parts;
    stringstream ss(path);
    string part;
    
    while (getline(ss, part, '/')) {
        if (!part.empty()) {
            parts.push_back(part);
        }
    }
    
    return parts;
}

string ZooKeeperServiceDiscovery::join_path(const vector<string>& parts) {
    if (parts.empty()) return "/";
    
    string path;
    for (const string& part : parts) {
        path += "/" + part;
    }
    return path;
}
