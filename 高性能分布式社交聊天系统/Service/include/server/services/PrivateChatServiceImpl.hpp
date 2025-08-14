#ifndef PRIVATE_CHAT_SERVICE_IMPL_H
#define PRIVATE_CHAT_SERVICE_IMPL_H

#include "grpc/GrpcServiceBase.hpp"
#include "proto/chat_services.grpc.pb.h"
#include "PrivateMessageModel.hpp"
#include "UserModel.hpp"
#include "Redis.hpp"
#include <memory>

using namespace chat::service;

class PrivateChatServiceImpl final : public PrivateChatService::Service, public GrpcServiceBase {
public:
    PrivateChatServiceImpl(const string& host, int port);
    
    // PrivateChatService接口实现
    Status SendMessage(ServerContext* context, const SendMessageRequest* request, SendMessageResponse* response) override;
    Status GetChatHistory(ServerContext* context, const GetChatHistoryRequest* request, GetChatHistoryResponse* response) override;
    Status SearchMessages(ServerContext* context, const SearchMessagesRequest* request, SearchMessagesResponse* response) override;
    Status GetUnreadCount(ServerContext* context, const GetUnreadCountRequest* request, GetUnreadCountResponse* response) override;
    Status MarkAsRead(ServerContext* context, const MarkAsReadRequest* request, MarkAsReadResponse* response) override;

protected:
    void configure_service(ServerBuilder& builder) override;
    void on_service_started() override;
    void on_service_stopped() override;

private:
    // 数据访问
    PrivateMessageModel message_model_;
    UserModel user_model_;
    Redis redis_;
    
    // 工具方法
    void convert_message_to_proto(const PrivateMessage& msg_obj, chat::service::PrivateMessage* msg_proto);
    BaseResponse create_base_response(int errno_val, const string& errmsg);
    
    // 消息推送
    void push_message_to_user(int user_id, const chat::service::PrivateMessage& message);
    
    // 验证权限
    bool validate_message_access(int user_id, int from_id, int to_id);
};

#endif // PRIVATE_CHAT_SERVICE_IMPL_H
