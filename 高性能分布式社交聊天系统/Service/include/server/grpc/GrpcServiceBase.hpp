#ifndef GRPC_SERVICE_BASE_H
#define GRPC_SERVICE_BASE_H

#include <grpcpp/grpcpp.h>
#include <grpcpp/health_check_service_interface.h>
#include <grpcpp/ext/proto_server_reflection_plugin.h>
#include <memory>
#include <string>
#include <thread>
#include <atomic>
#include <functional>
#include "discovery/ZooKeeperServiceDiscovery.hpp"

using grpc::Server;
using grpc::ServerBuilder;
using grpc::ServerContext;
using grpc::Status;
using grpc::StatusCode;

// gRPC服务基类
class GrpcServiceBase {
public:
    GrpcServiceBase(const string& service_name, const string& host, int port);
    virtual ~GrpcServiceBase();
    
    // 启动服务
    bool start();
    
    // 停止服务
    void stop();
    
    // 等待服务停止
    void wait_for_shutdown();
    
    // 注册服务到ZooKeeper
    bool register_to_discovery();
    
    // 从ZooKeeper注销服务
    void unregister_from_discovery();
    
    // 设置服务元数据
    void set_metadata(const string& key, const string& value);
    
    // 获取服务地址
    string get_service_address() const;
    
    // 检查服务是否运行
    bool is_running() const { return running_; }
    
    // 设置服务发现实例
    void set_service_discovery(ZooKeeperServiceDiscovery* discovery) {
        service_discovery_ = discovery;
    }

protected:
    // 子类需要实现的方法
    virtual void configure_service(ServerBuilder& builder) = 0;
    virtual void on_service_started() {}
    virtual void on_service_stopped() {}
    
    // 工具方法
    Status create_error_status(StatusCode code, const string& message);
    void log_request(const string& method, const string& details = "");
    void log_error(const string& method, const string& error);
    
    // 生成唯一的实例ID
    string generate_instance_id();

private:
    // 基本信息
    string service_name_;
    string host_;
    int port_;
    string instance_id_;
    
    // gRPC服务器
    unique_ptr<Server> server_;
    atomic<bool> running_;
    
    // 服务发现
    ZooKeeperServiceDiscovery* service_discovery_;
    ServiceInstance service_instance_;
    
    // 健康检查
    thread health_check_thread_;
    atomic<bool> health_check_running_;
    
    void health_check_loop();
    void update_service_status();
};

// gRPC客户端基类
template<typename ServiceStub>
class GrpcClientBase {
public:
    GrpcClientBase(const string& service_name, ZooKeeperServiceDiscovery* discovery = nullptr)
        : service_name_(service_name), service_discovery_(discovery) {
        if (!service_discovery_) {
            service_discovery_ = g_service_discovery;
        }
    }
    
    virtual ~GrpcClientBase() = default;
    
    // 获取服务stub
    shared_ptr<ServiceStub> get_stub() {
        auto instance = get_service_instance();
        if (instance.service_name.empty()) {
            return nullptr;
        }
        
        string address = instance.get_address();
        auto it = stubs_.find(address);
        
        if (it == stubs_.end()) {
            auto channel = grpc::CreateChannel(address, grpc::InsecureChannelCredentials());
            auto stub = ServiceStub::NewStub(channel);
            stubs_[address] = stub;
            return stub;
        }
        
        return it->second;
    }
    
    // 执行RPC调用
    template<typename Request, typename Response>
    Status call_rpc(function<Status(ServiceStub*, grpc::ClientContext*, const Request&, Response*)> rpc_func,
                   const Request& request, Response& response, int timeout_ms = 5000) {
        auto stub = get_stub();
        if (!stub) {
            return Status(StatusCode::UNAVAILABLE, "No available service instance");
        }
        
        grpc::ClientContext context;
        context.set_deadline(chrono::system_clock::now() + chrono::milliseconds(timeout_ms));
        
        return rpc_func(stub.get(), &context, request, &response);
    }
    
    // 获取所有可用实例
    vector<ServiceInstance> get_all_instances() {
        if (!service_discovery_) {
            return vector<ServiceInstance>();
        }
        return service_discovery_->discover_services(service_name_);
    }

protected:
    ServiceInstance get_service_instance() {
        if (!service_discovery_) {
            return ServiceInstance();
        }
        return service_discovery_->get_service_instance(service_name_);
    }

private:
    string service_name_;
    ZooKeeperServiceDiscovery* service_discovery_;
    map<string, shared_ptr<ServiceStub>> stubs_;
};

// 便捷宏定义
#define GRPC_LOG_REQUEST(method) log_request(method)
#define GRPC_LOG_ERROR(method, error) log_error(method, error)
#define GRPC_CREATE_ERROR(code, msg) create_error_status(code, msg)

#define GRPC_VALIDATE_REQUEST(condition, error_msg) \
    if (!(condition)) { \
        return GRPC_CREATE_ERROR(StatusCode::INVALID_ARGUMENT, error_msg); \
    }

#define GRPC_CALL_RPC(client, rpc_method, request, response) \
    client.call_rpc([](auto stub, auto ctx, const auto& req, auto* resp) { \
        return stub->rpc_method(ctx, req, resp); \
    }, request, response)

#endif // GRPC_SERVICE_BASE_H
