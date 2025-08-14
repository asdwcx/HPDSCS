#include "services/AuthServiceImpl.hpp"
#include <random>
#include <sstream>
#include <iomanip>
#include <chrono>

using namespace std;
using namespace std::chrono;

AuthServiceImpl::AuthServiceImpl(const string& host, int port)
    : GrpcServiceBase("AuthService", host, port) {
    
    // 连接Redis
    if (!redis_.connect()) {
        cerr << "Warning: Failed to connect to Redis in AuthService" << endl;
    }
}

void AuthServiceImpl::configure_service(ServerBuilder& builder) {
    builder.RegisterService(static_cast<AuthService::Service*>(this));
}

void AuthServiceImpl::on_service_started() {
    GRPC_LOG_REQUEST("AuthService started");
}

void AuthServiceImpl::on_service_stopped() {
    GRPC_LOG_REQUEST("AuthService stopped");
}

Status AuthServiceImpl::Login(ServerContext* context, const LoginRequest* request, LoginResponse* response) {
    GRPC_LOG_REQUEST("Login", "email: " + request->email());
    
    // 验证请求参数
    GRPC_VALIDATE_REQUEST(!request->email().empty(), "Email cannot be empty");
    GRPC_VALIDATE_REQUEST(!request->password().empty(), "Password cannot be empty");
    
    try {
        // 查询用户
        User user = user_model_.query(request->email());
        if (user.getId() == -1) {
            auto base = create_base_response(1, "用户不存在");
            *response->mutable_base() = base;
            return Status::OK;
        }
        
        // 验证密码
        if (user.getPassword() != request->password()) {
            auto base = create_base_response(2, "密码错误");
            *response->mutable_base() = base;
            return Status::OK;
        }
        
        // 生成token
        string token = generate_token(user.getId());
        store_token(user.getId(), token);
        
        // 更新用户在线状态
        user.setState("online");
        user_model_.updateState(user);
        
        // 构建响应
        auto base = create_base_response(0, "登录成功");
        *response->mutable_base() = base;
        response->set_token(token);
        
        convert_user_to_proto(user, response->mutable_user());
        
        GRPC_LOG_REQUEST("Login", "success for user: " + to_string(user.getId()));
        return Status::OK;
        
    } catch (const exception& e) {
        GRPC_LOG_ERROR("Login", e.what());
        auto base = create_base_response(-1, "服务器内部错误");
        *response->mutable_base() = base;
        return Status::OK;
    }
}

Status AuthServiceImpl::Register(ServerContext* context, const RegisterRequest* request, RegisterResponse* response) {
    GRPC_LOG_REQUEST("Register", "email: " + request->email());
    
    // 验证请求参数
    GRPC_VALIDATE_REQUEST(!request->name().empty(), "Name cannot be empty");
    GRPC_VALIDATE_REQUEST(!request->email().empty(), "Email cannot be empty");
    GRPC_VALIDATE_REQUEST(!request->password().empty(), "Password cannot be empty");
    
    try {
        // 检查用户是否已存在
        User existing_user = user_model_.query(request->email());
        if (existing_user.getId() != -1) {
            auto base = create_base_response(1, "用户已存在");
            *response->mutable_base() = base;
            return Status::OK;
        }
        
        // 创建新用户
        User new_user;
        new_user.setName(request->name());
        new_user.setEmail(request->email());
        new_user.setPassword(request->password());
        new_user.setState("offline");
        
        bool success = user_model_.insert(new_user);
        if (!success) {
            auto base = create_base_response(2, "注册失败");
            *response->mutable_base() = base;
            return Status::OK;
        }
        
        // 查询新创建的用户（获取ID）
        User created_user = user_model_.query(request->email());
        
        // 构建响应
        auto base = create_base_response(0, "注册成功");
        *response->mutable_base() = base;
        
        convert_user_to_proto(created_user, response->mutable_user());
        
        GRPC_LOG_REQUEST("Register", "success for user: " + to_string(created_user.getId()));
        return Status::OK;
        
    } catch (const exception& e) {
        GRPC_LOG_ERROR("Register", e.what());
        auto base = create_base_response(-1, "服务器内部错误");
        *response->mutable_base() = base;
        return Status::OK;
    }
}

Status AuthServiceImpl::Logout(ServerContext* context, const LogoutRequest* request, LogoutResponse* response) {
    GRPC_LOG_REQUEST("Logout", "user_id: " + to_string(request->user_id()));
    
    // 验证请求参数
    GRPC_VALIDATE_REQUEST(request->user_id() > 0, "Invalid user ID");
    GRPC_VALIDATE_REQUEST(!request->token().empty(), "Token cannot be empty");
    
    try {
        // 验证token
        int token_user_id;
        if (!validate_token_internal(request->token(), token_user_id) || 
            token_user_id != request->user_id()) {
            auto base = create_base_response(1, "无效的token");
            *response->mutable_base() = base;
            return Status::OK;
        }
        
        // 更新用户离线状态
        User user;
        user.setId(request->user_id());
        user.setState("offline");
        user_model_.updateState(user);
        
        // 删除token
        remove_token(request->token());
        
        // 构建响应
        auto base = create_base_response(0, "登出成功");
        *response->mutable_base() = base;
        
        GRPC_LOG_REQUEST("Logout", "success for user: " + to_string(request->user_id()));
        return Status::OK;
        
    } catch (const exception& e) {
        GRPC_LOG_ERROR("Logout", e.what());
        auto base = create_base_response(-1, "服务器内部错误");
        *response->mutable_base() = base;
        return Status::OK;
    }
}

Status AuthServiceImpl::ValidateToken(ServerContext* context, const ValidateTokenRequest* request, ValidateTokenResponse* response) {
    GRPC_VALIDATE_REQUEST(!request->token().empty(), "Token cannot be empty");
    
    try {
        int user_id;
        if (!validate_token_internal(request->token(), user_id)) {
            auto base = create_base_response(1, "无效的token");
            *response->mutable_base() = base;
            return Status::OK;
        }
        
        // 查询用户信息
        User user = user_model_.query(user_id);
        if (user.getId() == -1) {
            auto base = create_base_response(2, "用户不存在");
            *response->mutable_base() = base;
            return Status::OK;
        }
        
        // 构建响应
        auto base = create_base_response(0, "token有效");
        *response->mutable_base() = base;
        
        convert_user_to_proto(user, response->mutable_user());
        
        return Status::OK;
        
    } catch (const exception& e) {
        GRPC_LOG_ERROR("ValidateToken", e.what());
        auto base = create_base_response(-1, "服务器内部错误");
        *response->mutable_base() = base;
        return Status::OK;
    }
}

// 私有方法实现
string AuthServiceImpl::generate_token(int user_id) {
    auto now = system_clock::now();
    auto timestamp = duration_cast<milliseconds>(now.time_since_epoch()).count();
    
    static random_device rd;
    static mt19937 gen(rd());
    uniform_int_distribution<> dis(100000, 999999);
    
    stringstream ss;
    ss << user_id << "_" << timestamp << "_" << dis(gen);
    return ss.str();
}

bool AuthServiceImpl::validate_token_internal(const string& token, int& user_id) {
    if (!redis_.connected()) {
        return false;
    }
    
    string key = "auth:token:" + token;
    string user_id_str = redis_.get(key);
    
    if (user_id_str.empty()) {
        return false;
    }
    
    try {
        user_id = stoi(user_id_str);
        return true;
    } catch (...) {
        return false;
    }
}

void AuthServiceImpl::store_token(int user_id, const string& token) {
    if (!redis_.connected()) {
        return;
    }
    
    string key = "auth:token:" + token;
    string value = to_string(user_id);
    
    // 设置token，24小时过期
    redis_.set(key, value);
    redis_.expire(key, 24 * 60 * 60);
    
    // 存储用户的token映射（用于清理旧token）
    string user_token_key = "auth:user_token:" + to_string(user_id);
    redis_.set(user_token_key, token);
    redis_.expire(user_token_key, 24 * 60 * 60);
}

void AuthServiceImpl::remove_token(const string& token) {
    if (!redis_.connected()) {
        return;
    }
    
    string key = "auth:token:" + token;
    redis_.del(key);
}

void AuthServiceImpl::convert_user_to_proto(const User& user_obj, chat::service::User* user_proto) {
    user_proto->set_id(user_obj.getId());
    user_proto->set_name(user_obj.getName());
    user_proto->set_email(user_obj.getEmail());
    user_proto->set_state(user_obj.getState());
    user_proto->set_create_time(user_obj.getCreateTime().time_since_epoch().count());
    user_proto->set_last_online(user_obj.getLastOnline().time_since_epoch().count());
}

BaseResponse AuthServiceImpl::create_base_response(int errno_val, const string& errmsg) {
    BaseResponse response;
    response.set_errno(errno_val);
    response.set_errmsg(errmsg);
    return response;
}
