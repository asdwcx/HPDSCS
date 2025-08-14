#include "services/PrivateChatServiceImpl.hpp"
#include <chrono>

using namespace std;
using namespace std::chrono;

PrivateChatServiceImpl::PrivateChatServiceImpl(const string& host, int port)
    : GrpcServiceBase("PrivateChatService", host, port) {
    
    // 连接Redis
    if (!redis_.connect()) {
        cerr << "Warning: Failed to connect to Redis in PrivateChatService" << endl;
    }
}

void PrivateChatServiceImpl::configure_service(ServerBuilder& builder) {
    builder.RegisterService(static_cast<PrivateChatService::Service*>(this));
}

void PrivateChatServiceImpl::on_service_started() {
    GRPC_LOG_REQUEST("PrivateChatService started");
}

void PrivateChatServiceImpl::on_service_stopped() {
    GRPC_LOG_REQUEST("PrivateChatService stopped");
}

Status PrivateChatServiceImpl::SendMessage(ServerContext* context, const SendMessageRequest* request, SendMessageResponse* response) {
    GRPC_LOG_REQUEST("SendMessage", "from: " + to_string(request->from_id()) + " to: " + to_string(request->to_id()));
    
    // 验证请求参数
    GRPC_VALIDATE_REQUEST(request->from_id() > 0, "Invalid from_id");
    GRPC_VALIDATE_REQUEST(request->to_id() > 0, "Invalid to_id");
    GRPC_VALIDATE_REQUEST(!request->message().empty(), "Message cannot be empty");
    GRPC_VALIDATE_REQUEST(request->from_id() != request->to_id(), "Cannot send message to yourself");
    
    try {
        // 检查发送者是否存在
        User from_user = user_model_.query(request->from_id());
        if (from_user.getId() == -1) {
            auto base = create_base_response(1, "发送者不存在");
            *response->mutable_base() = base;
            return Status::OK;
        }
        
        // 检查接收者是否存在
        User to_user = user_model_.query(request->to_id());
        if (to_user.getId() == -1) {
            auto base = create_base_response(2, "接收者不存在");
            *response->mutable_base() = base;
            return Status::OK;
        }
        
        // 创建消息对象
        PrivateMessage message;
        message.setFromId(request->from_id());
        message.setToId(request->to_id());
        message.setMessage(request->message());
        message.setMessageType(request->message_type().empty() ? "text" : request->message_type());
        
        auto now = system_clock::now();
        message.setTimestamp(now);
        message.setIsRead(false);
        
        // 保存消息到数据库
        bool success = message_model_.insert(message);
        if (!success) {
            auto base = create_base_response(3, "消息发送失败");
            *response->mutable_base() = base;
            return Status::OK;
        }
        
        // 获取消息ID（假设insert方法会设置ID）
        int64_t message_id = message.getId();
        
        // 构建响应
        auto base = create_base_response(0, "消息发送成功");
        *response->mutable_base() = base;
        response->set_message_id(message_id);
        
        // 推送消息给接收者（如果在线）
        chat::service::PrivateMessage proto_message;
        convert_message_to_proto(message, &proto_message);
        push_message_to_user(request->to_id(), proto_message);
        
        GRPC_LOG_REQUEST("SendMessage", "success, message_id: " + to_string(message_id));
        return Status::OK;
        
    } catch (const exception& e) {
        GRPC_LOG_ERROR("SendMessage", e.what());
        auto base = create_base_response(-1, "服务器内部错误");
        *response->mutable_base() = base;
        return Status::OK;
    }
}

Status PrivateChatServiceImpl::GetChatHistory(ServerContext* context, const GetChatHistoryRequest* request, GetChatHistoryResponse* response) {
    GRPC_LOG_REQUEST("GetChatHistory", "user: " + to_string(request->user_id()) + " friend: " + to_string(request->friend_id()));
    
    // 验证请求参数
    GRPC_VALIDATE_REQUEST(request->user_id() > 0, "Invalid user_id");
    GRPC_VALIDATE_REQUEST(request->friend_id() > 0, "Invalid friend_id");
    GRPC_VALIDATE_REQUEST(request->page() >= 0, "Invalid page number");
    GRPC_VALIDATE_REQUEST(request->page_size() > 0 && request->page_size() <= 100, "Invalid page size");
    
    try {
        // 验证访问权限
        if (!validate_message_access(request->user_id(), request->user_id(), request->friend_id())) {
            auto base = create_base_response(1, "无权限访问聊天记录");
            *response->mutable_base() = base;
            return Status::OK;
        }
        
        // 查询聊天记录
        vector<PrivateMessage> messages = message_model_.queryHistoryPaged(
            request->user_id(), request->friend_id(), 
            request->page(), request->page_size());
        
        // 获取总数
        int total_count = message_model_.getHistoryCount(request->user_id(), request->friend_id());
        
        // 构建响应
        auto base = create_base_response(0, "查询成功");
        *response->mutable_base() = base;
        response->set_total_count(total_count);
        
        for (const auto& msg : messages) {
            auto proto_msg = response->add_messages();
            convert_message_to_proto(msg, proto_msg);
        }
        
        GRPC_LOG_REQUEST("GetChatHistory", "success, count: " + to_string(messages.size()));
        return Status::OK;
        
    } catch (const exception& e) {
        GRPC_LOG_ERROR("GetChatHistory", e.what());
        auto base = create_base_response(-1, "服务器内部错误");
        *response->mutable_base() = base;
        return Status::OK;
    }
}

Status PrivateChatServiceImpl::SearchMessages(ServerContext* context, const SearchMessagesRequest* request, SearchMessagesResponse* response) {
    GRPC_LOG_REQUEST("SearchMessages", "user: " + to_string(request->user_id()) + " keyword: " + request->keyword());
    
    // 验证请求参数
    GRPC_VALIDATE_REQUEST(request->user_id() > 0, "Invalid user_id");
    GRPC_VALIDATE_REQUEST(request->friend_id() > 0, "Invalid friend_id");
    GRPC_VALIDATE_REQUEST(!request->keyword().empty(), "Keyword cannot be empty");
    GRPC_VALIDATE_REQUEST(request->page() >= 0, "Invalid page number");
    GRPC_VALIDATE_REQUEST(request->page_size() > 0 && request->page_size() <= 100, "Invalid page size");
    
    try {
        // 验证访问权限
        if (!validate_message_access(request->user_id(), request->user_id(), request->friend_id())) {
            auto base = create_base_response(1, "无权限搜索聊天记录");
            *response->mutable_base() = base;
            return Status::OK;
        }
        
        // 搜索消息
        vector<PrivateMessage> messages = message_model_.searchMessages(
            request->user_id(), request->friend_id(), request->keyword(),
            request->page(), request->page_size());
        
        // 获取搜索结果总数
        int total_count = message_model_.getSearchCount(
            request->user_id(), request->friend_id(), request->keyword());
        
        // 构建响应
        auto base = create_base_response(0, "搜索成功");
        *response->mutable_base() = base;
        response->set_total_count(total_count);
        
        for (const auto& msg : messages) {
            auto proto_msg = response->add_messages();
            convert_message_to_proto(msg, proto_msg);
        }
        
        GRPC_LOG_REQUEST("SearchMessages", "success, count: " + to_string(messages.size()));
        return Status::OK;
        
    } catch (const exception& e) {
        GRPC_LOG_ERROR("SearchMessages", e.what());
        auto base = create_base_response(-1, "服务器内部错误");
        *response->mutable_base() = base;
        return Status::OK;
    }
}

Status PrivateChatServiceImpl::GetUnreadCount(ServerContext* context, const GetUnreadCountRequest* request, GetUnreadCountResponse* response) {
    GRPC_LOG_REQUEST("GetUnreadCount", "user: " + to_string(request->user_id()));
    
    // 验证请求参数
    GRPC_VALIDATE_REQUEST(request->user_id() > 0, "Invalid user_id");
    
    try {
        // 获取未读消息数量
        int unread_count = message_model_.getUnreadCount(request->user_id());
        
        // 构建响应
        auto base = create_base_response(0, "查询成功");
        *response->mutable_base() = base;
        response->set_unread_count(unread_count);
        
        GRPC_LOG_REQUEST("GetUnreadCount", "success, count: " + to_string(unread_count));
        return Status::OK;
        
    } catch (const exception& e) {
        GRPC_LOG_ERROR("GetUnreadCount", e.what());
        auto base = create_base_response(-1, "服务器内部错误");
        *response->mutable_base() = base;
        return Status::OK;
    }
}

Status PrivateChatServiceImpl::MarkAsRead(ServerContext* context, const MarkAsReadRequest* request, MarkAsReadResponse* response) {
    GRPC_LOG_REQUEST("MarkAsRead", "user: " + to_string(request->user_id()) + " friend: " + to_string(request->friend_id()));
    
    // 验证请求参数
    GRPC_VALIDATE_REQUEST(request->user_id() > 0, "Invalid user_id");
    GRPC_VALIDATE_REQUEST(request->friend_id() > 0, "Invalid friend_id");
    
    try {
        // 标记消息为已读
        bool success = message_model_.markAsRead(request->user_id(), request->friend_id());
        
        if (!success) {
            auto base = create_base_response(1, "标记已读失败");
            *response->mutable_base() = base;
            return Status::OK;
        }
        
        // 构建响应
        auto base = create_base_response(0, "标记已读成功");
        *response->mutable_base() = base;
        
        GRPC_LOG_REQUEST("MarkAsRead", "success");
        return Status::OK;
        
    } catch (const exception& e) {
        GRPC_LOG_ERROR("MarkAsRead", e.what());
        auto base = create_base_response(-1, "服务器内部错误");
        *response->mutable_base() = base;
        return Status::OK;
    }
}

// 私有方法实现
void PrivateChatServiceImpl::convert_message_to_proto(const PrivateMessage& msg_obj, chat::service::PrivateMessage* msg_proto) {
    msg_proto->set_id(msg_obj.getId());
    msg_proto->set_from_id(msg_obj.getFromId());
    msg_proto->set_to_id(msg_obj.getToId());
    msg_proto->set_message(msg_obj.getMessage());
    msg_proto->set_message_type(msg_obj.getMessageType());
    msg_proto->set_timestamp(duration_cast<milliseconds>(msg_obj.getTimestamp().time_since_epoch()).count());
    msg_proto->set_is_read(msg_obj.getIsRead());
}

BaseResponse PrivateChatServiceImpl::create_base_response(int errno_val, const string& errmsg) {
    BaseResponse response;
    response.set_errno(errno_val);
    response.set_errmsg(errmsg);
    return response;
}

void PrivateChatServiceImpl::push_message_to_user(int user_id, const chat::service::PrivateMessage& message) {
    if (!redis_.connected()) {
        return;
    }
    
    // 通过Redis发布消息推送事件
    string channel = "user_message:" + to_string(user_id);
    string message_data = message.SerializeAsString();
    
    redis_.publish(channel, message_data);
}

bool PrivateChatServiceImpl::validate_message_access(int user_id, int from_id, int to_id) {
    // 用户只能访问自己参与的对话
    return (user_id == from_id || user_id == to_id);
}
