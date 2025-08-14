#include <muduo/base/Logging.h>
#include <vector>
#include <map>
#include <iostream>
#include "ChatService.hpp"
#include "public.hpp"
#include "message.pb.h"
#include "ProtobufHelper.hpp"

using namespace muduo;
using namespace std;

//获取单例对象
ChatService *ChatService::instance()
{
    static ChatService service;
    return &service;
}

//注册消息以及对应的回调操作
ChatService::ChatService()
{
    msg_handler_map_.insert({LOGIN_MSG, bind(&ChatService::login, this, _1, _2, _3)});
    msg_handler_map_.insert({LOGINOUT_MSG, bind(&ChatService::loginout, this, _1, _2, _3)});
    msg_handler_map_.insert({REG_MSG, bind(&ChatService::regist, this, _1, _2, _3)});
    msg_handler_map_.insert({ONE_CHAT_MSG, bind(&ChatService::one_chat, this, _1, _2, _3)});
    msg_handler_map_.insert({ADD_FRIEND_MSG, bind(&ChatService::add_friend, this, _1, _2, _3)});
    msg_handler_map_.insert({CREATE_GROUP_MSG, bind(&ChatService::create_group, this, _1, _2, _3)});
    msg_handler_map_.insert({ADD_GROUP_MSG, bind(&ChatService::add_group, this, _1, _2, _3)});
    msg_handler_map_.insert({GROUP_CHAT_MSG, bind(&ChatService::group_chat, this, _1, _2, _3)});

    if (redis_.connect())
    {
        redis_.init_notify_handler(bind(&ChatService::redis_subscribe_message_handler, this, _1, _2));
    }
}

//获取消息对应的处理器
MsgHandler ChatService::get_handler(int msgid)
{
    //记录错误日志，msgid没有对应的事件处理回调
    auto it = msg_handler_map_.find(msgid);
    //如果没有对应的msgid
    if (it == msg_handler_map_.end())
    {
        //返回一个默认处理器，打印错误日志
        return [=](const TcpConnectionPtr &conn, const chat::MessageWrapper &msg, Timestamp time) {
            LOG_ERROR << "msgid: " << msgid << " can not find handler!";
        };
    }
    else
    {
        return msg_handler_map_[msgid];
    }
}

//登录
void ChatService::login(const TcpConnectionPtr &conn, const chat::MessageWrapper &msg, Timestamp time)
{
    // 解析登录请求
    chat::LoginRequest request;
    if (!request.ParseFromString(msg.data()))
    {
        LOG_ERROR << "login request parse error!";
        return;
    }

    int id = request.id();
    string password = request.password();

    User user = user_model_.query(id);

    chat::LoginResponse response;
    response.set_msgid(chat::LOGIN_MSG_ACK);

    if (user.get_id() == id && user.get_password() == password)
    {
        //用户在线
        if (user.get_state() == "online")
        {
            response.set_errno(2);
            response.set_errmsg("id is online");
        }
        else
        {
            //登陆成功
            {
                //记录用户连接信息，注意线程安全
                lock_guard<mutex> lock(conn_mutex_);
                user_connection_map_.insert({id, conn});
            }

            //订阅Redis
            redis_.subscribe(id);

            //更新用户状态信息
            user.set_state("online");
            user_model_.update_state(user);

            response.set_errno(0);
            response.set_errmsg("");
            
            // 设置用户信息
            chat::User* proto_user = response.mutable_user();
            *proto_user = ProtobufHelper::UserToProto(user);

            //查询用户是否有离线消息
            vector<string> vec = offline_message_model_.query(id);
            //增加离线消息
            if (!vec.empty())
            {
                for (const string& offline_msg : vec)
                {
                    chat::OfflineMessage* msg_ptr = response.add_offline_msgs();
                    // 这里需要解析离线消息的格式，假设格式为 "from_id:message:time"
                    // 实际项目中需要根据数据库存储格式调整
                    msg_ptr->set_message(offline_msg);
                }
                //读取完后删除消息
                offline_message_model_.remove(id);
            }

            //查询该用户的好友信息并返回
            vector<User> user_vec = friend_model_.query(id);
            if (!user_vec.empty())
            {
                for (User &friend_user : user_vec)
                {
                    chat::Friend* friend_ptr = response.add_friends();
                    *friend_ptr = ProtobufHelper::UserToFriend(friend_user);
                }
            }

            //查询该用户的群组信息并返回
            vector<Group> group_vec = group_model_.query_group(id);
            if (!group_vec.empty())
            {
                for (Group &group : group_vec)
                {
                    chat::Group* group_ptr = response.add_groups();
                    *group_ptr = ProtobufHelper::GroupToProto(group);
                }
            }
        }
    }
    else
    {
        //用户不存在或密码错误
        response.set_errno(1);
        response.set_errmsg("id or password error");
    }

    // 发送响应
    string response_str = response.SerializeAsString();
    conn->send(response_str);
}

//注册
void ChatService::regist(const TcpConnectionPtr &conn, const chat::MessageWrapper &msg, Timestamp time)
{
    // 解析注册请求
    chat::RegisterRequest request;
    if (!request.ParseFromString(msg.data()))
    {
        LOG_ERROR << "register request parse error!";
        return;
    }

    string name = request.name();
    string password = request.password();

    User user;
    user.set_name(name);
    user.set_password(password);

    chat::RegisterResponse response;
    response.set_msgid(chat::REG_MSG_ACK);

    bool state = user_model_.insert(user);
    if (state)
    {
        //注册成功
        response.set_errno(0);
        response.set_errmsg("");
        response.set_id(user.get_id());
    }
    else
    {
        //注册失败
        response.set_errno(1);
        response.set_errmsg("register failed");
    }

    // 发送响应
    string response_str = response.SerializeAsString();
    conn->send(response_str);
}

//处理客户端异常退出
void ChatService::client_close_exception(const TcpConnectionPtr &conn)
{
    User user;
    {
        lock_guard<mutex> lock(conn_mutex_);
        for (auto it = user_connection_map_.begin(); it != user_connection_map_.end(); ++it)
        {
            if (it->second == conn)
            {
                //从map表删除用户连接信息
                user.set_id(it->first);
                user_connection_map_.erase(it);
                break;
            }
        }
    }

    //取消redis订阅通道
    redis_.unsubscribe(user.get_id());

    //更新用户状态信息
    if (user.get_id() != -1)
    {
        user.set_state("offline");
        user_model_.update_state(user);
    }
}

//一对一聊天业务
void ChatService::one_chat(const TcpConnectionPtr &conn, const chat::MessageWrapper &wrapper, Timestamp time)
{
    // 解析一对一聊天消息
    chat::OneChatMessage msg;
    if (!msg.ParseFromString(wrapper.data()))
    {
        LOG_ERROR << "one chat message parse error!";
        return;
    }

    int to_id = msg.to();

    {
        lock_guard<mutex> lock(conn_mutex_);
        auto it = user_connection_map_.find(to_id);
        if (it != user_connection_map_.end())
        {
            //to_id在线，转发消息,服务器主动推送消息给to_id用户
            string msg_str = msg.SerializeAsString();
            it->second->send(msg_str);
            return;
        }
    }

    //查询to_id是否在线
    User user = user_model_.query(to_id);
    if (user.get_state() == "online")
    {
        redis_.publish(to_id, wrapper.SerializeAsString());
    }
    else
    {
        //to_id不在线，存储离线消息
        offline_message_model_.insert(to_id, wrapper.SerializeAsString());
    }
}

//添加好友业务
bool ChatService::add_friend(const TcpConnectionPtr &conn, const chat::MessageWrapper &wrapper, Timestamp time)
{
    // 解析添加好友请求
    chat::AddFriendRequest request;
    if (!request.ParseFromString(wrapper.data()))
    {
        LOG_ERROR << "add friend request parse error!";
        return false;
    }

    int userid = request.id();
    int friendid = request.friendid();

    //存储好友信息
    bool result = friend_model_.insert(userid, friendid);
    
    chat::AddFriendResponse response;
    response.set_msgid(chat::ADD_FRIEND_MSG);
    response.set_errno(result ? 0 : 1);
    response.set_errmsg(result ? "add friend success" : "add friend failed");

    string response_str = response.SerializeAsString();
    conn->send(response_str);
    
    return result;
}

//创建群组业务
bool ChatService::create_group(const TcpConnectionPtr &conn, const chat::MessageWrapper &wrapper, Timestamp time)
{
    // 解析创建群组请求
    chat::CreateGroupRequest request;
    if (!request.ParseFromString(wrapper.data()))
    {
        LOG_ERROR << "create group request parse error!";
        return false;
    }

    int userid = request.id();
    string name = request.groupname();
    string desc = request.groupdesc();

    //存储新创建的群组信息
    Group group(-1, name, desc);
    bool result = group_model_.create_group(group);
    if (result)
    {
        //存储群组创建人信息
        group_model_.add_group(userid, group.get_id(), "creator");
    }

    chat::CreateGroupResponse response;
    response.set_msgid(chat::CREATE_GROUP_MSG);
    response.set_errno(result ? 0 : 1);
    response.set_errmsg(result ? "create group success" : "create group failed");

    string response_str = response.SerializeAsString();
    conn->send(response_str);
    
    return result;
}

//加入群组业务
bool ChatService::add_group(const TcpConnectionPtr &conn, const chat::MessageWrapper &wrapper, Timestamp time)
{
    // 解析加入群组请求
    chat::AddGroupRequest request;
    if (!request.ParseFromString(wrapper.data()))
    {
        LOG_ERROR << "add group request parse error!";
        return false;
    }

    int userid = request.id();
    int groupid = request.groupid();

    bool result = group_model_.add_group(userid, groupid, "normal");

    chat::AddGroupResponse response;
    response.set_msgid(chat::ADD_GROUP_MSG);
    response.set_errno(result ? 0 : 1);
    response.set_errmsg(result ? "add group success" : "add group failed");

    string response_str = response.SerializeAsString();
    conn->send(response_str);
    
    return result;
}

//群组聊天业务
void ChatService::group_chat(const TcpConnectionPtr &conn, const chat::MessageWrapper &wrapper, Timestamp time)
{
    // 解析群聊消息
    chat::GroupChatMessage msg;
    if (!msg.ParseFromString(wrapper.data()))
    {
        LOG_ERROR << "group chat message parse error!";
        return;
    }

    int userid = msg.id();
    int groupid = msg.groupid();

    vector<int> user_id_vec = group_model_.query_group_user(userid, groupid);

    lock_guard<mutex> lock(conn_mutex_);
    for (int id : user_id_vec)
    {
        auto it = user_connection_map_.find(id);
        if (it != user_connection_map_.end())
        {
            //转发群消息
            string msg_str = msg.SerializeAsString();
            it->second->send(msg_str);
        }
        else
        {
            //查询id是否在线
            User user = user_model_.query(id);
            if (user.get_state() == "online")
            {
                redis_.publish(id, wrapper.SerializeAsString());
            }
            else
            {
                //存储离线消息
                offline_message_model_.insert(id, wrapper.SerializeAsString());
            }
        }
    }
}

//注销业务
void ChatService::loginout(const TcpConnectionPtr &conn, const chat::MessageWrapper &wrapper, Timestamp time)
{
    // 解析注销请求
    chat::LoginoutRequest request;
    if (!request.ParseFromString(wrapper.data()))
    {
        LOG_ERROR << "loginout request parse error!";
        return;
    }

    int userid = request.id();

    {
        lock_guard<mutex> lock(conn_mutex_);
        auto it = user_connection_map_.find(userid);
        if (it != user_connection_map_.end())
        {
            user_connection_map_.erase(it);
        }
    }

    //取消redis订阅通道
    redis_.unsubscribe(userid);

    //更新用户状态信息
    User user(userid, "", "", "offline");
    user_model_.update_state(user);

    chat::LoginoutResponse response;
    response.set_msgid(chat::LOGINOUT_MSG);
    response.set_errno(0);
    response.set_errmsg("loginout success");

    string response_str = response.SerializeAsString();
    conn->send(response_str);
}

//redis订阅消息触发的回调函数
void ChatService::redis_subscribe_message_handler(int userid, string msg)
{
    lock_guard<mutex> lock(conn_mutex_);
    auto it = user_connection_map_.find(userid);
    if (it != user_connection_map_.end())
    {
        it->second->send(msg);
        return;
    }

    //存储该用户的离线消息
    offline_message_model_.insert(userid, msg);
}

//服务器异常，业务重置
void ChatService::reset()
{
    //把online状态的用户，设置成offline
    user_model_.reset_state();
}
