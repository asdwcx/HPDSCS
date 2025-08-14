#ifndef AUTH_SERVICE_IMPL_H
#define AUTH_SERVICE_IMPL_H

#include "grpc/GrpcServiceBase.hpp"
#include "proto/chat_services.grpc.pb.h"
#include "UserModel.hpp"
#include "Redis.hpp"
#include <memory>

using namespace chat::service;

class AuthServiceImpl final : public AuthService::Service, public GrpcServiceBase {
public:
    AuthServiceImpl(const string& host, int port);
    
    // AuthService接口实现
    Status Login(ServerContext* context, const LoginRequest* request, LoginResponse* response) override;
    Status Register(ServerContext* context, const RegisterRequest* request, RegisterResponse* response) override;
    Status Logout(ServerContext* context, const LogoutRequest* request, LogoutResponse* response) override;
    Status ValidateToken(ServerContext* context, const ValidateTokenRequest* request, ValidateTokenResponse* response) override;

protected:
    void configure_service(ServerBuilder& builder) override;
    void on_service_started() override;
    void on_service_stopped() override;

private:
    // 数据访问
    UserModel user_model_;
    Redis redis_;
    
    // 工具方法
    string generate_token(int user_id);
    bool validate_token_internal(const string& token, int& user_id);
    void store_token(int user_id, const string& token);
    void remove_token(const string& token);
    
    // 转换方法
    void convert_user_to_proto(const User& user_obj, chat::service::User* user_proto);
    BaseResponse create_base_response(int errno_val, const string& errmsg);
};

#endif // AUTH_SERVICE_IMPL_H
