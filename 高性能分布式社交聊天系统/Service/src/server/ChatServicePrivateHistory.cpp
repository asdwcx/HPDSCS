#include "ChatService.hpp"
#include "public.hpp"
#include <iostream>
#include <vector>
#include <map>
#include <string>
#include "json.hpp"

using json = nlohmann::json;
using namespace std;
using namespace muduo;
using namespace muduo::net;

//查询一对一聊天历史记录
void ChatService::private_history(const TcpConnectionPtr &conn, const chat::MessageWrapper &wrapper, Timestamp time)
{
    try {
        json js = json::parse(wrapper.data());
        int userid = js["id"].get<int>();
        int target_userid = js["target_id"].get<int>();
        int count = js.value("count", 20); // 默认查询20条
        string before_time = js.value("before_time", ""); // 分页参数
        
        // 验证好友关系
        vector<User> friend_list = friend_model_.query(userid);
        bool is_friend = false;
        for (const User& friend_user : friend_list)
        {
            if (friend_user.get_id() == target_userid)
            {
                is_friend = true;
                break;
            }
        }
        
        json response;
        response["msgid"] = PRIVATE_HISTORY_MSG;
        
        if (!is_friend)
        {
            response["errno"] = 1;
            response["errmsg"] = "只能查询好友间的聊天记录";
        }
        else
        {
            // 查询聊天历史
            auto history = private_message_model_.query_private_history(userid, target_userid, count, before_time);
            
            response["errno"] = 0;
            response["errmsg"] = "查询成功";
            response["target_id"] = target_userid;
            response["count"] = history.size();
            
            json messages = json::array();
            for (const auto& msg : history)
            {
                json msg_json;
                msg_json["from_id"] = msg.first.first;
                msg_json["from_name"] = msg.first.second;
                msg_json["message"] = msg.second.first;
                msg_json["time"] = msg.second.second;
                messages.push_back(msg_json);
            }
            response["messages"] = messages;
        }
        
        string response_str = response.dump();
        conn->send(response_str);
        
    } catch (const exception& e) {
        LOG_ERROR << "private history parse error: " << e.what();
        json error_response;
        error_response["msgid"] = PRIVATE_HISTORY_MSG;
        error_response["errno"] = 2;
        error_response["errmsg"] = "请求格式错误";
        string error_str = error_response.dump();
        conn->send(error_str);
    }
}

//搜索一对一聊天消息
void ChatService::private_search(const TcpConnectionPtr &conn, const chat::MessageWrapper &wrapper, Timestamp time)
{
    try {
        json js = json::parse(wrapper.data());
        int userid = js["id"].get<int>();
        int target_userid = js["target_id"].get<int>();
        string keyword = js["keyword"].get<string>();
        int limit = js.value("limit", 50); // 默认限制50条
        
        // 验证好友关系
        vector<User> friend_list = friend_model_.query(userid);
        bool is_friend = false;
        for (const User& friend_user : friend_list)
        {
            if (friend_user.get_id() == target_userid)
            {
                is_friend = true;
                break;
            }
        }
        
        json response;
        response["msgid"] = PRIVATE_SEARCH_MSG;
        
        if (!is_friend)
        {
            response["errno"] = 1;
            response["errmsg"] = "只能搜索好友间的聊天记录";
        }
        else if (keyword.empty())
        {
            response["errno"] = 2;
            response["errmsg"] = "搜索关键词不能为空";
        }
        else
        {
            // 搜索聊天消息
            auto results = private_message_model_.search_private_messages(userid, target_userid, keyword, limit);
            
            response["errno"] = 0;
            response["errmsg"] = "搜索成功";
            response["target_id"] = target_userid;
            response["keyword"] = keyword;
            response["count"] = results.size();
            
            json messages = json::array();
            for (const auto& msg : results)
            {
                json msg_json;
                msg_json["from_id"] = msg.first.first;
                msg_json["from_name"] = msg.first.second;
                msg_json["message"] = msg.second.first;
                msg_json["time"] = msg.second.second;
                messages.push_back(msg_json);
            }
            response["messages"] = messages;
        }
        
        string response_str = response.dump();
        conn->send(response_str);
        
    } catch (const exception& e) {
        LOG_ERROR << "private search parse error: " << e.what();
        json error_response;
        error_response["msgid"] = PRIVATE_SEARCH_MSG;
        error_response["errno"] = 3;
        error_response["errmsg"] = "请求格式错误";
        string error_str = error_response.dump();
        conn->send(error_str);
    }
}

//查询未读消息数量
void ChatService::private_unread_count(const TcpConnectionPtr &conn, const chat::MessageWrapper &wrapper, Timestamp time)
{
    try {
        json js = json::parse(wrapper.data());
        int userid = js["id"].get<int>();
        int from_userid = js.value("from_id", 0); // 0表示查询所有用户的未读消息
        
        json response;
        response["msgid"] = PRIVATE_UNREAD_COUNT_MSG;
        
        int unread_count = private_message_model_.get_unread_message_count(userid, from_userid);
        
        response["errno"] = 0;
        response["errmsg"] = "查询成功";
        response["unread_count"] = unread_count;
        if (from_userid != 0)
        {
            response["from_id"] = from_userid;
        }
        
        string response_str = response.dump();
        conn->send(response_str);
        
    } catch (const exception& e) {
        LOG_ERROR << "private unread count parse error: " << e.what();
        json error_response;
        error_response["msgid"] = PRIVATE_UNREAD_COUNT_MSG;
        error_response["errno"] = 1;
        error_response["errmsg"] = "请求格式错误";
        string error_str = error_response.dump();
        conn->send(error_str);
    }
}

//查询会话列表
void ChatService::conversation_list(const TcpConnectionPtr &conn, const chat::MessageWrapper &wrapper, Timestamp time)
{
    try {
        json js = json::parse(wrapper.data());
        int userid = js["id"].get<int>();
        int limit = js.value("limit", 20); // 默认查询20个会话
        
        json response;
        response["msgid"] = CONVERSATION_LIST_MSG;
        
        // 查询会话列表
        auto conversations = private_message_model_.get_conversation_list(userid, limit);
        
        response["errno"] = 0;
        response["errmsg"] = "查询成功";
        response["count"] = conversations.size();
        
        json conv_list = json::array();
        for (const auto& conv : conversations)
        {
            json conv_json;
            conv_json["user_id"] = conv.first.get_id();
            conv_json["user_name"] = conv.first.get_name();
            conv_json["user_state"] = conv.first.get_state();
            conv_json["last_message"] = conv.second.first;
            conv_json["last_time"] = conv.second.second;
            
            // 查询未读消息数量
            int unread_count = private_message_model_.get_unread_message_count(userid, conv.first.get_id());
            conv_json["unread_count"] = unread_count;
            
            conv_list.push_back(conv_json);
        }
        response["conversations"] = conv_list;
        
        string response_str = response.dump();
        conn->send(response_str);
        
    } catch (const exception& e) {
        LOG_ERROR << "conversation list parse error: " << e.what();
        json error_response;
        error_response["msgid"] = CONVERSATION_LIST_MSG;
        error_response["errno"] = 1;
        error_response["errmsg"] = "请求格式错误";
        string error_str = error_response.dump();
        conn->send(error_str);
    }
}
