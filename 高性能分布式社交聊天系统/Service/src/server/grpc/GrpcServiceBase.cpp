#include "grpc/GrpcServiceBase.hpp"
#include <iostream>
#include <sstream>
#include <iomanip>
#include <chrono>
#include <random>

using namespace std;
using namespace std::chrono;

// GrpcServiceBase实现
GrpcServiceBase::GrpcServiceBase(const string& service_name, const string& host, int port)
    : service_name_(service_name), host_(host), port_(port), running_(false),
      service_discovery_(nullptr), health_check_running_(false) {
    
    instance_id_ = generate_instance_id();
    
    // 初始化服务实例信息
    service_instance_.service_name = service_name;
    service_instance_.instance_id = instance_id_;
    service_instance_.host = host;
    service_instance_.port = port;
    service_instance_.metadata["version"] = "1.0.0";
    service_instance_.metadata["protocol"] = "grpc";
}

GrpcServiceBase::~GrpcServiceBase() {
    stop();
}

bool GrpcServiceBase::start() {
    if (running_) {
        cout << "Service " << service_name_ << " is already running" << endl;
        return true;
    }
    
    try {
        ServerBuilder builder;
        
        // 配置服务器地址
        string server_address = host_ + ":" + to_string(port_);
        builder.AddListeningPort(server_address, grpc::InsecureServerCredentials());
        
        // 启用健康检查服务
        grpc::EnableDefaultHealthCheckService(true);
        
        // 启用反射服务
        grpc::reflection::InitProtoReflectionServerBuilderPlugin();
        
        // 子类配置服务
        configure_service(builder);
        
        // 构建并启动服务器
        server_ = builder.BuildAndStart();
        if (!server_) {
            cerr << "Failed to start gRPC server for " << service_name_ << endl;
            return false;
        }
        
        running_ = true;
        
        cout << "gRPC service " << service_name_ << " started on " << server_address << endl;
        
        // 注册到服务发现
        if (!register_to_discovery()) {
            cout << "Warning: Failed to register service to discovery" << endl;
        }
        
        // 启动健康检查
        health_check_running_ = true;
        health_check_thread_ = thread(&GrpcServiceBase::health_check_loop, this);
        
        // 调用子类回调
        on_service_started();
        
        return true;
        
    } catch (const exception& e) {
        cerr << "Exception starting gRPC service: " << e.what() << endl;
        return false;
    }
}

void GrpcServiceBase::stop() {
    if (!running_) {
        return;
    }
    
    cout << "Stopping gRPC service " << service_name_ << "..." << endl;
    
    // 停止健康检查
    health_check_running_ = false;
    if (health_check_thread_.joinable()) {
        health_check_thread_.join();
    }
    
    // 从服务发现注销
    unregister_from_discovery();
    
    // 停止gRPC服务器
    if (server_) {
        server_->Shutdown();
        server_.reset();
    }
    
    running_ = false;
    
    // 调用子类回调
    on_service_stopped();
    
    cout << "gRPC service " << service_name_ << " stopped" << endl;
}

void GrpcServiceBase::wait_for_shutdown() {
    if (server_) {
        server_->Wait();
    }
}

bool GrpcServiceBase::register_to_discovery() {
    if (!service_discovery_) {
        service_discovery_ = g_service_discovery;
    }
    
    if (!service_discovery_) {
        return false;
    }
    
    service_instance_.register_time = duration_cast<milliseconds>(
        system_clock::now().time_since_epoch()).count();
    
    return service_discovery_->register_service(service_instance_, true);
}

void GrpcServiceBase::unregister_from_discovery() {
    if (service_discovery_) {
        service_discovery_->unregister_service(service_name_, instance_id_);
    }
}

void GrpcServiceBase::set_metadata(const string& key, const string& value) {
    service_instance_.metadata[key] = value;
    
    // 如果服务已注册，更新元数据
    if (running_ && service_discovery_) {
        service_discovery_->update_service_metadata(service_name_, instance_id_, 
                                                   service_instance_.metadata);
    }
}

string GrpcServiceBase::get_service_address() const {
    return host_ + ":" + to_string(port_);
}

Status GrpcServiceBase::create_error_status(StatusCode code, const string& message) {
    return Status(code, message);
}

void GrpcServiceBase::log_request(const string& method, const string& details) {
    auto now = system_clock::now();
    auto time_t = system_clock::to_time_t(now);
    auto ms = duration_cast<milliseconds>(now.time_since_epoch()) % 1000;
    
    cout << put_time(localtime(&time_t), "[%Y-%m-%d %H:%M:%S") 
         << "." << setfill('0') << setw(3) << ms.count() << "] "
         << "[" << service_name_ << "] " << method;
    
    if (!details.empty()) {
        cout << " - " << details;
    }
    
    cout << endl;
}

void GrpcServiceBase::log_error(const string& method, const string& error) {
    auto now = system_clock::now();
    auto time_t = system_clock::to_time_t(now);
    auto ms = duration_cast<milliseconds>(now.time_since_epoch()) % 1000;
    
    cerr << put_time(localtime(&time_t), "[%Y-%m-%d %H:%M:%S") 
         << "." << setfill('0') << setw(3) << ms.count() << "] "
         << "[ERROR] [" << service_name_ << "] " << method << " - " << error << endl;
}

string GrpcServiceBase::generate_instance_id() {
    auto now = system_clock::now();
    auto timestamp = duration_cast<milliseconds>(now.time_since_epoch()).count();
    
    static random_device rd;
    static mt19937 gen(rd());
    uniform_int_distribution<> dis(1000, 9999);
    
    stringstream ss;
    ss << service_name_ << "_" << timestamp << "_" << dis(gen);
    return ss.str();
}

void GrpcServiceBase::health_check_loop() {
    while (health_check_running_) {
        update_service_status();
        this_thread::sleep_for(chrono::seconds(10));
    }
}

void GrpcServiceBase::update_service_status() {
    if (!service_discovery_ || !running_) {
        return;
    }
    
    service_instance_.register_time = duration_cast<milliseconds>(
        system_clock::now().time_since_epoch()).count();
    
    service_discovery_->update_service_metadata(service_name_, instance_id_, 
                                               service_instance_.metadata);
}
