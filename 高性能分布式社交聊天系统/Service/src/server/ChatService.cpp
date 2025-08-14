#include <muduo/base/Logging.h>
#include <vector>
#include <map>
#include <iostream>
#include <chrono>
#include <iomanip>
#include <sstream>
#include <atomic>
#include <random>
#include <fstream>
#include <iterator>
#include "ChatService.hpp"
#include "public.hpp"
#include "message.pb.h"
#include "ProtobufHelper.hpp"
#include "json/json.h"
#include "../include/Base64Utils.hpp"
#include "cluster/ClusterManager.hpp"
#include "cluster/ClusterConfig.hpp"

using namespace muduo;
using namespace std;
using namespace chrono;

// 静态成员变量定义
string ChatService::server_id_ = "server-001";

//获取单例对象
ChatService *ChatService::instance()
{
    static ChatService service;
    return &service;
}

//注册消息以及对应的回调操作
ChatService::ChatService() : cluster_manager_(nullptr), cluster_config_(nullptr)
{
    msg_handler_map_.insert({LOGIN_MSG, bind(&ChatService::login, this, _1, _2, _3)});
    msg_handler_map_.insert({LOGINOUT_MSG, bind(&ChatService::loginout, this, _1, _2, _3)});
    msg_handler_map_.insert({REG_MSG, bind(&ChatService::regist, this, _1, _2, _3)});
    msg_handler_map_.insert({ONE_CHAT_MSG, bind(&ChatService::one_chat, this, _1, _2, _3)});
    msg_handler_map_.insert({ADD_FRIEND_MSG, bind(&ChatService::add_friend_request, this, _1, _2, _3)});
    msg_handler_map_.insert({ADD_FRIEND_RESPONSE, bind(&ChatService::add_friend_reply, this, _1, _2, _3)});
    msg_handler_map_.insert({CREATE_GROUP_MSG, bind(&ChatService::create_group, this, _1, _2, _3)});
    msg_handler_map_.insert({ADD_GROUP_MSG, bind(&ChatService::add_group, this, _1, _2, _3)});
    msg_handler_map_.insert({GROUP_CHAT_MSG, bind(&ChatService::group_chat, this, _1, _2, _3)});
    msg_handler_map_.insert({JOIN_GROUP_MSG, bind(&ChatService::join_group_request, this, _1, _2, _3)});
    msg_handler_map_.insert({APPROVE_JOIN_MSG, bind(&ChatService::approve_join_request, this, _1, _2, _3)});
    msg_handler_map_.insert({GROUP_HISTORY_MSG, bind(&ChatService::group_history, this, _1, _2, _3)});
    msg_handler_map_.insert({GROUP_SEARCH_MSG, bind(&ChatService::group_search, this, _1, _2, _3)});
    msg_handler_map_.insert({GROUP_INFO_MSG, bind(&ChatService::group_info, this, _1, _2, _3)});
    msg_handler_map_.insert({QUIT_GROUP_MSG, bind(&ChatService::quit_group, this, _1, _2, _3)});
    msg_handler_map_.insert({PRIVATE_HISTORY_MSG, bind(&ChatService::private_history, this, _1, _2, _3)});
    msg_handler_map_.insert({PRIVATE_SEARCH_MSG, bind(&ChatService::private_search, this, _1, _2, _3)});
    msg_handler_map_.insert({PRIVATE_UNREAD_COUNT_MSG, bind(&ChatService::private_unread_count, this, _1, _2, _3)});
    msg_handler_map_.insert({CONVERSATION_LIST_MSG, bind(&ChatService::conversation_list, this, _1, _2, _3)});
    
    // 位置服务消息处理器
    msg_handler_map_.insert({UPDATE_LOCATION_MSG, bind(&ChatService::update_location, this, _1, _2, _3)});
    msg_handler_map_.insert({FIND_NEARBY_MSG, bind(&ChatService::find_nearby, this, _1, _2, _3)});
    msg_handler_map_.insert({SET_LOCATION_VISIBILITY_MSG, bind(&ChatService::set_location_visibility, this, _1, _2, _3)});
    msg_handler_map_.insert({GET_LOCATION_MSG, bind(&ChatService::get_location, this, _1, _2, _3)});

    // 文件传输消息处理器
    msg_handler_map_.insert({FILE_UPLOAD_REQ, bind(&ChatService::file_upload_request, this, _1, _2, _3)});
    msg_handler_map_.insert({FILE_CHUNK_MSG, bind(&ChatService::file_chunk_transfer, this, _1, _2, _3)});
    msg_handler_map_.insert({FILE_DOWNLOAD_REQ, bind(&ChatService::file_download_request, this, _1, _2, _3)});
    msg_handler_map_.insert({FILE_CHUNK_DOWNLOAD_REQ, bind(&ChatService::file_chunk_download_request, this, _1, _2, _3)});
    msg_handler_map_.insert({FILE_SEND_NOTIFY, bind(&ChatService::file_send_notify, this, _1, _2, _3)});
    msg_handler_map_.insert({FILE_RECEIVE_CONFIRM, bind(&ChatService::file_receive_confirm, this, _1, _2, _3)});

    // 初始化文件清理管理器
    try {
        file_cleanup_manager_ = make_unique<FileCleanupManager>(file_model_, file_transfer_redis_);
        
        // 配置默认清理参数
        CleanupConfig cleanup_config;
        cleanup_config.cleanup_interval_minutes = 30;  // 30分钟清理一次
        cleanup_config.temp_file_expire_hours = 24;    // 临时文件24小时过期
        cleanup_config.session_expire_hours = 48;      // 会话48小时过期
        cleanup_config.auto_start = true;              // 自动启动
        file_cleanup_manager_->set_cleanup_config(cleanup_config);
        
        // 设置清理完成回调
        file_cleanup_manager_->set_cleanup_callback([this](const CleanupStats& stats) {
            LOG_INFO << "文件清理完成 - 临时文件: " << stats.total_temp_files_cleaned 
                     << "个, 过期会话: " << stats.expired_sessions_cleaned << "个";
        });
        
        LOG_INFO << "文件清理管理器初始化成功";
    } catch (const exception& e) {
        LOG_ERROR << "文件清理管理器初始化失败: " << e.what();
    }

    // 初始化集群功能
    initialize_cluster();

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
    int from_id = -1;
    int to_id = -1;
    string message = "";
    string msg_time = "";
    string sender_name = "";
    
    // 尝试解析JSON格式（兼容现有客户端）
    try {
        json js = json::parse(wrapper.data());
        from_id = js["id"].get<int>();
        to_id = js["to"].get<int>();
        message = js["msg"].get<string>();
        msg_time = js["time"].get<string>();
        sender_name = js["name"].get<string>();
    } catch (...) {
        // 如果JSON解析失败，尝试protobuf格式
        chat::OneChatMessage msg;
        if (!msg.ParseFromString(wrapper.data()))
        {
            LOG_ERROR << "one chat message parse error!";
            return;
        }
        
        from_id = msg.id();
        to_id = msg.to();
        message = msg.msg();
        msg_time = msg.time();
        sender_name = msg.name();
    }
    
    // 🔍 验证好友关系 - 只有好友之间才能聊天
    vector<User> friend_list = friend_model_.query(from_id);
    bool is_friend = false;
    for (const User& friend_user : friend_list)
    {
        if (friend_user.get_id() == to_id)
        {
            is_friend = true;
            break;
        }
    }
    
    if (!is_friend)
    {
        // 非好友关系，拒绝发送消息
        LOG_INFO << "User " << from_id << " tried to send message to non-friend " << to_id;
        
        json error_response;
        error_response["msgid"] = ONE_CHAT_MSG;
        error_response["errno"] = 1;
        error_response["errmsg"] = "不能向非好友发送消息，请先添加对方为好友";
        error_response["time"] = msg_time;
        
        string error_str = error_response.dump();
        conn->send(error_str);
        return;
    }
    
    // 验证目标用户是否存在
    User target_user = user_model_.query(to_id);
    if (target_user.get_id() == -1)
    {
        json error_response;
        error_response["msgid"] = ONE_CHAT_MSG;
        error_response["errno"] = 2;
        error_response["errmsg"] = "目标用户不存在";
        error_response["time"] = msg_time;
        
        string error_str = error_response.dump();
        conn->send(error_str);
        return;
    }
    
    // 不能给自己发消息
    if (from_id == to_id)
    {
        json error_response;
        error_response["msgid"] = ONE_CHAT_MSG;
        error_response["errno"] = 3;
        error_response["errmsg"] = "不能给自己发送消息";
        error_response["time"] = msg_time;
        
        string error_str = error_response.dump();
        conn->send(error_str);
        return;
    }

    // 💾 存储消息到历史记录（无论用户是否在线都要存储）
    private_message_model_.insert_private_message(from_id, to_id, message, msg_time);

    {
        lock_guard<mutex> lock(conn_mutex_);
        auto it = user_connection_map_.find(to_id);
        if (it != user_connection_map_.end())
        {
            //to_id在线，转发消息,服务器主动推送消息给to_id用户
            json forward_msg;
            forward_msg["msgid"] = ONE_CHAT_MSG;
            forward_msg["id"] = from_id;
            forward_msg["name"] = sender_name;
            forward_msg["to"] = to_id;
            forward_msg["msg"] = message;
            forward_msg["time"] = msg_time;
            
            string msg_str = forward_msg.dump();
            it->second->send(msg_str);
            
            // 标记消息为已读（因为用户在线并立即收到了消息）
            private_message_model_.mark_messages_as_read(to_id, from_id);
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
        json offline_msg;
        offline_msg["msgid"] = ONE_CHAT_MSG;
        offline_msg["id"] = from_id;
        offline_msg["name"] = sender_name;
        offline_msg["to"] = to_id;
        offline_msg["msg"] = message;
        offline_msg["time"] = msg_time;
        
        offline_message_model_.insert(to_id, offline_msg.dump());
    }
}

//添加好友请求业务（第1步：用户A发起请求）
void ChatService::add_friend_request(const TcpConnectionPtr &conn, const chat::MessageWrapper &wrapper, Timestamp time)
{
    // 解析添加好友请求
    chat::AddFriendRequest request;
    if (!request.ParseFromString(wrapper.data()))
    {
        LOG_ERROR << "add friend request parse error!";
        return;
    }

    int userid = request.id();        // 发起者ID
    int friendid = request.friendid(); // 目标用户ID
    string message = request.message(); // 请求消息

    // 验证目标用户是否存在
    User target_user = user_model_.query(friendid);
    if (target_user.get_id() == -1)
    {
        // 用户不存在，直接返回错误结果
        chat::AddFriendResult result;
        result.set_msgid(chat::ADD_FRIEND_RESULT);
        result.set_errno(3);
        result.set_errmsg("目标用户不存在");
        result.set_friend_id(friendid);
        result.set_friend_name("");
        result.set_reply_msg("");

        chat::MessageWrapper wrapper_msg;
        wrapper_msg.set_msgid(chat::ADD_FRIEND_RESULT);
        wrapper_msg.set_data(result.SerializeAsString());
        conn->send(wrapper_msg.SerializeAsString());
        return;
    }

    // 检查是否已经是好友
    vector<User> friends = friend_model_.query(userid);
    for (const auto& friend_user : friends)
    {
        if (friend_user.get_id() == friendid)
        {
            // 已经是好友
            chat::AddFriendResult result;
            result.set_msgid(chat::ADD_FRIEND_RESULT);
            result.set_errno(4);
            result.set_errmsg("你们已经是好友了");
            result.set_friend_id(friendid);
            result.set_friend_name(target_user.get_name());
            result.set_reply_msg("");

            chat::MessageWrapper wrapper_msg;
            wrapper_msg.set_msgid(chat::ADD_FRIEND_RESULT);
            wrapper_msg.set_data(result.SerializeAsString());
            conn->send(wrapper_msg.SerializeAsString());
            return;
        }
    }

    // 获取发起者信息
    User sender_user = user_model_.query(userid);
    
    // 创建好友请求通知
    chat::AddFriendNotify notify;
    notify.set_msgid(chat::ADD_FRIEND_NOTIFY);
    notify.set_from_id(userid);
    notify.set_from_name(sender_user.get_name());
    notify.set_message(message);

    chat::MessageWrapper notify_wrapper;
    notify_wrapper.set_msgid(chat::ADD_FRIEND_NOTIFY);
    notify_wrapper.set_data(notify.SerializeAsString());
    
    // 检查目标用户是否在线
    {
        lock_guard<mutex> lock(conn_mutex_);
        auto it = user_connection_map_.find(friendid);
        if (it != user_connection_map_.end())
        {
            // 目标用户在线，直接发送通知
            it->second->send(notify_wrapper.SerializeAsString());
        }
        else
        {
            // 目标用户离线，存储为离线消息
            offline_message_model_.insert(friendid, notify_wrapper.SerializeAsString());
        }
    }
}

//添加好友回复业务（第3步：用户B回复）
void ChatService::add_friend_reply(const TcpConnectionPtr &conn, const chat::MessageWrapper &wrapper, Timestamp time)
{
    // 解析好友请求回复
    chat::AddFriendReply reply;
    if (!reply.ParseFromString(wrapper.data()))
    {
        LOG_ERROR << "add friend reply parse error!";
        return;
    }

    int from_id = reply.from_id();    // 发起者ID
    int to_id = reply.to_id();        // 回复者ID（自己）
    bool accept = reply.accept();     // 是否同意
    string reply_message = reply.message(); // 回复消息

    // 获取双方用户信息
    User from_user = user_model_.query(from_id);
    User to_user = user_model_.query(to_id);

    // 创建最终结果消息
    chat::AddFriendResult result;
    result.set_msgid(chat::ADD_FRIEND_RESULT);
    result.set_friend_id(to_id);
    result.set_friend_name(to_user.get_name());
    result.set_reply_msg(reply_message);

    if (accept)
    {
        // 同意添加好友，建立双向好友关系
        bool success1 = friend_model_.insert(from_id, to_id);
        bool success2 = friend_model_.insert(to_id, from_id);
        
        if (success1 && success2)
        {
            result.set_errno(0);
            result.set_errmsg("好友添加成功");
        }
        else
        {
            result.set_errno(1);
            result.set_errmsg("系统错误，好友添加失败");
        }
    }
    else
    {
        // 拒绝添加好友
        result.set_errno(2);
        result.set_errmsg("对方拒绝了你的好友请求");
    }

    // 发送结果给发起者
    chat::MessageWrapper result_wrapper;
    result_wrapper.set_msgid(chat::ADD_FRIEND_RESULT);
    result_wrapper.set_data(result.SerializeAsString());

    {
        lock_guard<mutex> lock(conn_mutex_);
        auto it = user_connection_map_.find(from_id);
        if (it != user_connection_map_.end())
        {
            // 发起者在线，直接发送结果
            it->second->send(result_wrapper.SerializeAsString());
        }
        else
        {
            // 发起者离线，存储为离线消息
            offline_message_model_.insert(from_id, result_wrapper.SerializeAsString());
        }
    }
}

//创建群组业务
void ChatService::create_group(const TcpConnectionPtr &conn, const chat::MessageWrapper &wrapper, Timestamp time)
{
    // 解析创建群组请求
    chat::CreateGroupRequest request;
    if (!request.ParseFromString(wrapper.data()))
    {
        LOG_ERROR << "create group request parse error!";
        return;
    }

    int userid = request.id();
    string name = request.groupname();
    string desc = request.groupdesc();
    
    // 获取创建者信息
    User creator = user_model_.query(userid);
    
    // 创建响应消息
    chat::CreateGroupResponse response;
    response.set_msgid(chat::CREATE_GROUP_MSG);
    
    // 检查群名是否重复（可选实现）
    // TODO: 实现群名重复检查逻辑
    
    // 验证成员ID有效性
    vector<int> member_ids;
    vector<User> valid_members;
    
    // 添加创建者自己
    member_ids.push_back(userid);
    valid_members.push_back(creator);
    
    // 验证请求中的成员ID
    for (int i = 0; i < request.member_ids_size(); ++i)
    {
        int member_id = request.member_ids(i);
        if (member_id == userid) continue; // 跳过创建者自己
        
        User member = user_model_.query(member_id);
        if (member.get_id() != -1) // 用户存在
        {
            member_ids.push_back(member_id);
            valid_members.push_back(member);
        }
        else
        {
            // 有无效的成员ID
            response.set_errno(2);
            response.set_errmsg("用户ID " + to_string(member_id) + " 不存在");
            
            chat::MessageWrapper response_wrapper;
            response_wrapper.set_msgid(chat::CREATE_GROUP_MSG);
            response_wrapper.set_data(response.SerializeAsString());
            conn->send(response_wrapper.SerializeAsString());
            return;
        }
    }
    
    // 创建群组
    Group group(-1, name, desc);
    bool result = group_model_.create_group(group);
    
    if (result)
    {
        int group_id = group.get_id();
        
        // 添加所有成员到群组
        bool all_success = true;
        for (size_t i = 0; i < member_ids.size(); ++i)
        {
            string role = (member_ids[i] == userid) ? "creator" : "normal";
            if (!group_model_.add_group(member_ids[i], group_id, role))
            {
                all_success = false;
                break;
            }
        }
        
        if (all_success)
        {
            // 设置成功响应
            response.set_errno(0);
            response.set_errmsg("群聊创建成功");
            response.set_group_id(group_id);
            response.set_group_name(name);
            
            // 添加成员信息到响应
            for (const auto& member : valid_members)
            {
                chat::User* user_ptr = response.add_members();
                user_ptr->set_id(member.get_id());
                user_ptr->set_name(member.get_name());
                user_ptr->set_state(member.get_state());
            }
            
            // 发送群邀请通知给其他成员（除了创建者）
            chat::GroupInviteNotify invite_notify;
            invite_notify.set_msgid(chat::GROUP_INVITE_NOTIFY);
            invite_notify.set_group_id(group_id);
            invite_notify.set_group_name(name);
            invite_notify.set_group_desc(desc);
            invite_notify.set_inviter_id(userid);
            invite_notify.set_inviter_name(creator.get_name());
            
            chat::MessageWrapper invite_wrapper;
            invite_wrapper.set_msgid(chat::GROUP_INVITE_NOTIFY);
            invite_wrapper.set_data(invite_notify.SerializeAsString());
            
            {
                lock_guard<mutex> lock(conn_mutex_);
                for (int member_id : member_ids)
                {
                    if (member_id == userid) continue; // 跳过创建者
                    
                    auto it = user_connection_map_.find(member_id);
                    if (it != user_connection_map_.end())
                    {
                        // 成员在线，发送邀请通知
                        it->second->send(invite_wrapper.SerializeAsString());
                    }
                    else
                    {
                        // 成员离线，存储为离线消息
                        offline_message_model_.insert(member_id, invite_wrapper.SerializeAsString());
                    }
                }
            }
        }
        else
        {
            response.set_errno(3);
            response.set_errmsg("添加群成员失败");
        }
    }
    else
    {
        response.set_errno(1);
        response.set_errmsg("创建群组失败");
    }
    
    // 发送响应给创建者
    chat::MessageWrapper response_wrapper;
    response_wrapper.set_msgid(chat::CREATE_GROUP_MSG);
    response_wrapper.set_data(response.SerializeAsString());
    conn->send(response_wrapper.SerializeAsString());
}

//申请加入群聊业务（用户发起申请）
void ChatService::join_group_request(const TcpConnectionPtr &conn, const chat::MessageWrapper &wrapper, Timestamp time)
{
    // 解析申请加入群聊请求
    chat::JoinGroupRequest request;
    if (!request.ParseFromString(wrapper.data()))
    {
        LOG_ERROR << "join group request parse error!";
        return;
    }

    int user_id = request.user_id();
    int group_id = request.group_id();

    // 验证群组是否存在
    Group group = group_model_.query_group(group_id);
    if (group.get_id() == -1)
    {
        // 群组不存在
        chat::JoinGroupResult result;
        result.set_msgid(chat::JOIN_GROUP_RESULT);
        result.set_errno(3);
        result.set_errmsg("群组不存在");
        result.set_group_id(group_id);
        result.set_group_name("");
        result.set_reply_msg("");

        chat::MessageWrapper result_wrapper;
        result_wrapper.set_msgid(chat::JOIN_GROUP_RESULT);
        result_wrapper.set_data(result.SerializeAsString());
        conn->send(result_wrapper.SerializeAsString());
        return;
    }

    // 检查用户是否已经在群内
    vector<int> user_groups = group_model_.query_groups(user_id);
    for (int gid : user_groups)
    {
        if (gid == group_id)
        {
            // 已经是群成员
            chat::JoinGroupResult result;
            result.set_msgid(chat::JOIN_GROUP_RESULT);
            result.set_errno(4);
            result.set_errmsg("你已经是该群的成员了");
            result.set_group_id(group_id);
            result.set_group_name(group.get_name());
            result.set_reply_msg("");

            chat::MessageWrapper result_wrapper;
            result_wrapper.set_msgid(chat::JOIN_GROUP_RESULT);
            result_wrapper.set_data(result.SerializeAsString());
            conn->send(result_wrapper.SerializeAsString());
            return;
        }
    }

    // 获取申请者信息
    User applicant = user_model_.query(user_id);
    
    // 获取群主和管理员列表
    vector<int> group_members = group_model_.query_group_users(-1, group_id);
    vector<int> admins;
    
    for (int member_id : group_members)
    {
        string role = group_model_.query_group_user_role(member_id, group_id);
        if (role == "creator" || role == "admin")
        {
            admins.push_back(member_id);
        }
    }

    if (admins.empty())
    {
        // 没有管理员，不能处理申请
        chat::JoinGroupResult result;
        result.set_msgid(chat::JOIN_GROUP_RESULT);
        result.set_errno(1);
        result.set_errmsg("群组无管理员，无法处理申请");
        result.set_group_id(group_id);
        result.set_group_name(group.get_name());
        result.set_reply_msg("");

        chat::MessageWrapper result_wrapper;
        result_wrapper.set_msgid(chat::JOIN_GROUP_RESULT);
        result_wrapper.set_data(result.SerializeAsString());
        conn->send(result_wrapper.SerializeAsString());
        return;
    }

    // 创建加群申请通知
    chat::JoinGroupNotify notify;
    notify.set_msgid(chat::JOIN_GROUP_NOTIFY);
    notify.set_group_id(group_id);
    notify.set_group_name(group.get_name());
    notify.set_applicant_id(user_id);
    notify.set_applicant_name(applicant.get_name());
    notify.set_apply_time(to_string(time.microSecondsSinceEpoch()));

    chat::MessageWrapper notify_wrapper;
    notify_wrapper.set_msgid(chat::JOIN_GROUP_NOTIFY);
    notify_wrapper.set_data(notify.SerializeAsString());

    // 发送通知给所有管理员
    {
        lock_guard<mutex> lock(conn_mutex_);
        for (int admin_id : admins)
        {
            auto it = user_connection_map_.find(admin_id);
            if (it != user_connection_map_.end())
            {
                // 管理员在线，直接发送通知
                it->second->send(notify_wrapper.SerializeAsString());
            }
            else
            {
                // 管理员离线，存储为离线消息
                offline_message_model_.insert(admin_id, notify_wrapper.SerializeAsString());
            }
        }
    }
}

//审核加群申请业务（管理员审核）
void ChatService::approve_join_request(const TcpConnectionPtr &conn, const chat::MessageWrapper &wrapper, Timestamp time)
{
    // 解析审核请求
    chat::ApproveJoinRequest request;
    if (!request.ParseFromString(wrapper.data()))
    {
        LOG_ERROR << "approve join request parse error!";
        return;
    }

    int group_id = request.group_id();
    int applicant_id = request.applicant_id();
    int approver_id = request.approver_id();
    bool approve = request.approve();
    string reply_message = request.message();

    // 验证审核者权限
    string role = group_model_.query_group_user_role(approver_id, group_id);
    if (role != "creator" && role != "admin")
    {
        // 没有审核权限
        return;
    }

    // 获取群组和申请者信息
    Group group = group_model_.query_group(group_id);
    User applicant = user_model_.query(applicant_id);

    // 创建结果消息
    chat::JoinGroupResult result;
    result.set_msgid(chat::JOIN_GROUP_RESULT);
    result.set_group_id(group_id);
    result.set_group_name(group.get_name());
    result.set_reply_msg(reply_message);

    if (approve)
    {
        // 同意加入群聊
        bool success = group_model_.add_group(applicant_id, group_id, "normal");
        
        if (success)
        {
            result.set_errno(0);
            result.set_errmsg("加群申请已通过");

            // 发送群通知给所有成员（新成员加入）
            chat::GroupNotifyMessage group_notify;
            group_notify.set_msgid(chat::GROUP_NOTIFY);
            group_notify.set_group_id(group_id);
            group_notify.set_group_name(group.get_name());
            group_notify.set_user_id(applicant_id);
            group_notify.set_user_name(applicant.get_name());
            group_notify.set_notify_type("join");
            group_notify.set_message(applicant.get_name() + " 加入了群聊");

            chat::MessageWrapper notify_wrapper;
            notify_wrapper.set_msgid(chat::GROUP_NOTIFY);
            notify_wrapper.set_data(group_notify.SerializeAsString());

            // 获取群内所有成员
            vector<int> all_members = group_model_.query_group_users(-1, group_id);
            
            {
                lock_guard<mutex> lock(conn_mutex_);
                for (int member_id : all_members)
                {
                    auto it = user_connection_map_.find(member_id);
                    if (it != user_connection_map_.end())
                    {
                        it->second->send(notify_wrapper.SerializeAsString());
                    }
                    else
                    {
                        offline_message_model_.insert(member_id, notify_wrapper.SerializeAsString());
                    }
                }
            }
        }
        else
        {
            result.set_errno(1);
            result.set_errmsg("系统错误，加群失败");
        }
    }
    else
    {
        // 拒绝加入群聊
        result.set_errno(2);
        result.set_errmsg("你的加群申请被拒绝");
    }

    // 发送结果给申请者
    chat::MessageWrapper result_wrapper;
    result_wrapper.set_msgid(chat::JOIN_GROUP_RESULT);
    result_wrapper.set_data(result.SerializeAsString());

    {
        lock_guard<mutex> lock(conn_mutex_);
        auto it = user_connection_map_.find(applicant_id);
        if (it != user_connection_map_.end())
        {
            // 申请者在线，直接发送结果
            it->second->send(result_wrapper.SerializeAsString());
        }
        else
        {
            // 申请者离线，存储为离线消息
            offline_message_model_.insert(applicant_id, result_wrapper.SerializeAsString());
        }
    }
}

//加入群组业务（保留旧接口兼容性）
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
    string message = msg.msg();
    string client_time = msg.time(); // 保存客户端时间戳

    // 验证用户是否为群成员
    string user_role = group_model_.query_group_user_role(userid, groupid);
    if (user_role.empty())
    {
        LOG_ERROR << "user " << userid << " is not a member of group " << groupid;
        return;
    }

    // 🔧 简化的时序机制：仅使用高精度服务器时间戳
    string server_time = getCurrentServerTimestamp();
    
    // 🔧 更新消息的服务器时间戳
    chat::GroupChatMessage updated_msg = msg;
    updated_msg.set_time(server_time);
    
    // 🎯 简化的消息排序机制说明：
    // 
    // 新设计：仅使用微秒精度服务器时间戳
    // 优势：
    // 1. 简单高效，无需复杂的序列号管理
    // 2. 微秒精度足以处理绝大多数并发场景
    // 3. 极少数相同时间戳的消息顺序随意，不影响用户体验
    // 
    // 排序规则：
    // ORDER BY server_time ASC
    // 
    // 示例：
    // 10:30:01.004001 - 用户C: "消息3"
    // 10:30:01.004002 - 用户D: "消息4"
    // 结果：简单直接的时间顺序排序

    // 存储消息到历史记录（使用简化的时间戳方法）
    group_model_.insert_group_message_with_time(groupid, userid, message, 
                                                 server_time, client_time);

    vector<int> user_id_vec = group_model_.query_group_users(userid, groupid);

    // 创建包含时间戳的消息包装器
    chat::MessageWrapper updated_wrapper = wrapper;
    updated_wrapper.set_data(updated_msg.SerializeAsString());

    lock_guard<mutex> lock(conn_mutex_);
    for (int id : user_id_vec)
    {
        auto it = user_connection_map_.find(id);
        if (it != user_connection_map_.end())
        {
            //转发群消息（使用更新后的消息）
            string msg_str = updated_wrapper.SerializeAsString();
            it->second->send(msg_str);
        }
        else
        {
            //查询id是否在线
            User user = user_model_.query(id);
            if (user.get_state() == "online")
            {
                redis_.publish(id, updated_wrapper.SerializeAsString());
            }
            else
            {
                //存储离线消息（使用更新后的消息）
                offline_message_model_.insert(id, updated_wrapper.SerializeAsString());
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

//查询群聊历史记录
void ChatService::group_history(const TcpConnectionPtr &conn, const chat::MessageWrapper &msg, Timestamp time)
{
    // 解析JSON格式消息
    json js = json::parse(msg.data());
    
    int user_id = js["user_id"].get<int>();
    int group_id = js["group_id"].get<int>();
    int count = js["count"].get<int>();
    string before_time = js.count("before_time") ? js["before_time"].get<string>() : "";
    
    json response;
    response["msgid"] = GROUP_HISTORY_MSG;
    
    // 验证用户是否为群成员
    string user_role = group_model_.query_group_user_role(user_id, group_id);
    if (user_role.empty())
    {
        response["errno"] = 1;
        response["errmsg"] = "您不是该群的成员";
        string response_str = response.dump();
        conn->send(response_str);
        return;
    }
    
    // 查询历史消息 - 使用简化的时间戳查询方法
    auto history = group_model_.query_group_history(group_id, count, before_time);
    
    json messages = json::array();
    for (auto &msg_item : history)
    {
        json hist_msg;
        hist_msg["user_id"] = msg_item.first.first;
        hist_msg["user_name"] = msg_item.first.second;
        hist_msg["content"] = msg_item.second.first;
        hist_msg["time"] = msg_item.second.second;
        
        // 设置用户角色
        string role = group_model_.query_group_user_role(msg_item.first.first, group_id);
        hist_msg["user_role"] = role;
        
        messages.push_back(hist_msg);
    }
    
    response["errno"] = 0;
    response["errmsg"] = "查询成功";
    response["messages"] = messages;
    
    string response_str = response.dump();
    conn->send(response_str);
}

//搜索群消息
void ChatService::group_search(const TcpConnectionPtr &conn, const chat::MessageWrapper &msg, Timestamp time)
{
    // 解析JSON格式消息
    json js = json::parse(msg.data());
    
    int user_id = js["user_id"].get<int>();
    int group_id = js["group_id"].get<int>();
    string keyword = js["keyword"].get<string>();
    int limit = js.count("limit") ? js["limit"].get<int>() : 50;
    
    json response;
    response["msgid"] = GROUP_SEARCH_MSG;
    response["keyword"] = keyword;
    
    // 验证用户是否为群成员
    string user_role = group_model_.query_group_user_role(user_id, group_id);
    if (user_role.empty())
    {
        response["errno"] = 1;
        response["errmsg"] = "您不是该群的成员";
        string response_str = response.dump();
        conn->send(response_str);
        return;
    }
    
    // 搜索消息
    auto results = group_model_.search_group_messages(group_id, keyword, limit);
    
    json result_array = json::array();
    for (auto &msg_item : results)
    {
        json result_msg;
        result_msg["user_id"] = msg_item.first.first;
        result_msg["user_name"] = msg_item.first.second;
        result_msg["content"] = msg_item.second.first;
        result_msg["time"] = msg_item.second.second;
        
        // 设置用户角色
        string role = group_model_.query_group_user_role(msg_item.first.first, group_id);
        result_msg["user_role"] = role;
        
        result_array.push_back(result_msg);
    }
    
    response["errno"] = 0;
    response["errmsg"] = "搜索完成";
    response["results"] = result_array;
    
    string response_str = response.dump();
    conn->send(response_str);
}

//查询群信息
void ChatService::group_info(const TcpConnectionPtr &conn, const chat::MessageWrapper &msg, Timestamp time)
{
    // 解析JSON格式消息
    json js = json::parse(msg.data());
    
    int user_id = js["user_id"].get<int>();
    int group_id = js["group_id"].get<int>();
    
    json response;
    response["msgid"] = GROUP_INFO_MSG;
    
    // 验证用户是否为群成员
    string user_role = group_model_.query_group_user_role(user_id, group_id);
    if (user_role.empty())
    {
        response["errno"] = 1;
        response["errmsg"] = "您不是该群的成员";
        string response_str = response.dump();
        conn->send(response_str);
        return;
    }
    
    // 查询群信息
    Group group = group_model_.query_group_detail(group_id);
    if (group.get_id() == -1)
    {
        response["errno"] = 2;
        response["errmsg"] = "群组不存在";
        string response_str = response.dump();
        conn->send(response_str);
        return;
    }
    
    // 设置群信息
    json group_info;
    group_info["id"] = group.get_id();
    group_info["name"] = group.get_name();
    group_info["desc"] = group.get_desc();
    response["group_info"] = group_info;
    
    // 查询群成员信息
    auto members = group_model_.query_group_members(group_id);
    json members_array = json::array();
    for (auto &member : members)
    {
        json member_info;
        member_info["user_id"] = member.first.get_id();
        member_info["user_name"] = member.first.get_name();
        member_info["role"] = member.second;
        member_info["state"] = member.first.get_state();
        members_array.push_back(member_info);
    }
    response["members"] = members_array;
    
    response["errno"] = 0;
    response["errmsg"] = "查询成功";
    
    string response_str = response.dump();
    conn->send(response_str);
}

//退出群聊
void ChatService::quit_group(const TcpConnectionPtr &conn, const chat::MessageWrapper &msg, Timestamp time)
{
    // 解析JSON格式消息
    json js = json::parse(msg.data());
    
    int user_id = js["user_id"].get<int>();
    int group_id = js["group_id"].get<int>();
    
    json response;
    response["msgid"] = QUIT_GROUP_MSG;
    response["group_id"] = group_id;
    
    // 验证用户是否为群成员
    string user_role = group_model_.query_group_user_role(user_id, group_id);
    if (user_role.empty())
    {
        response["errno"] = 1;
        response["errmsg"] = "您不在该群中";
        string response_str = response.dump();
        conn->send(response_str);
        return;
    }
    
    // 获取群信息
    Group group = group_model_.query_group_detail(group_id);
    response["group_name"] = group.get_name();
    
    // 群主不能直接退出（需要转让群主或解散群）
    if (user_role == "creator")
    {
        response["errno"] = 2;
        response["errmsg"] = "群主不能直接退出群聊，请先转让群主或解散群";
        string response_str = response.dump();
        conn->send(response_str);
        return;
    }
    
    // 从群中移除用户
    if (group_model_.remove_from_group(user_id, group_id))
    {
        response["errno"] = 0;
        response["errmsg"] = "成功退出群聊";
        
        // 通知群内其他成员
        User user = user_model_.query(user_id);
        json notify;
        notify["msgid"] = GROUP_NOTIFY;
        notify["group_id"] = group_id;
        notify["group_name"] = group.get_name();
        notify["user_id"] = user_id;
        notify["user_name"] = user.get_name();
        notify["notify_type"] = "leave";
        notify["message"] = user.get_name() + " 离开了群聊";
        
        string notify_str = notify.dump();
        
        // 向群内其他成员发送通知
        vector<int> group_users = group_model_.query_group_users(0, group_id);
        lock_guard<mutex> lock(conn_mutex_);
        for (int member_id : group_users)
        {
            if (member_id != user_id) // 不发给退群的用户
            {
                auto it = user_connection_map_.find(member_id);
                if (it != user_connection_map_.end())
                {
                    it->second->send(notify_str);
                }
                else
                {
                    offline_message_model_.insert(member_id, notify_str);
                }
            }
        }
    }
    else
    {
        response["errno"] = 3;
        response["errmsg"] = "退出群聊失败";
    }
    
    string response_str = response.dump();
    conn->send(response_str);
}

//查询私聊历史记录
void ChatService::private_history(const TcpConnectionPtr &conn, const chat::MessageWrapper &msg, Timestamp time)
{
    // 解析JSON格式消息
    json js = json::parse(msg.data());
    
    int user_id = js["user_id"].get<int>();
    int friend_id = js["friend_id"].get<int>();
    int count = js["count"].get<int>();
    string before_time = js.count("before_time") ? js["before_time"].get<string>() : "";
    
    json response;
    response["msgid"] = PRIVATE_HISTORY_MSG;
    
    // 验证好友关系
    vector<User> friends = friend_model_.query(user_id);
    bool is_friend = false;
    for (const auto& friend_user : friends) {
        if (friend_user.get_id() == friend_id) {
            is_friend = true;
            break;
        }
    }
    
    if (!is_friend) {
        response["errno"] = 1;
        response["errmsg"] = "您与该用户不是好友关系";
        string response_str = response.dump();
        conn->send(response_str);
        return;
    }
    
    // 查询私聊历史记录
    auto history = private_message_model_.query_private_history(user_id, friend_id, count, before_time);
    
    json messages = json::array();
    for (auto &msg_item : history) {
        json hist_msg;
        hist_msg["user_id"] = msg_item.first.first;
        hist_msg["user_name"] = msg_item.first.second;
        hist_msg["content"] = msg_item.second.first;
        hist_msg["time"] = msg_item.second.second;
        messages.push_back(hist_msg);
    }
    
    response["errno"] = 0;
    response["errmsg"] = "查询成功";
    response["messages"] = messages;
    
    string response_str = response.dump();
    conn->send(response_str);
}

//搜索私聊消息
void ChatService::private_search(const TcpConnectionPtr &conn, const chat::MessageWrapper &msg, Timestamp time)
{
    // 解析JSON格式消息
    json js = json::parse(msg.data());
    
    int user_id = js["user_id"].get<int>();
    int friend_id = js["friend_id"].get<int>();
    string keyword = js["keyword"].get<string>();
    int limit = js.count("limit") ? js["limit"].get<int>() : 50;
    
    json response;
    response["msgid"] = PRIVATE_SEARCH_MSG;
    response["keyword"] = keyword;
    
    // 验证好友关系
    vector<User> friends = friend_model_.query(user_id);
    bool is_friend = false;
    for (const auto& friend_user : friends) {
        if (friend_user.get_id() == friend_id) {
            is_friend = true;
            break;
        }
    }
    
    if (!is_friend) {
        response["errno"] = 1;
        response["errmsg"] = "您与该用户不是好友关系";
        string response_str = response.dump();
        conn->send(response_str);
        return;
    }
    
    // 搜索私聊消息
    auto results = private_message_model_.search_private_messages(user_id, friend_id, keyword, limit);
    
    json result_array = json::array();
    for (auto &msg_item : results) {
        json result_msg;
        result_msg["user_id"] = msg_item.first.first;
        result_msg["user_name"] = msg_item.first.second;
        result_msg["content"] = msg_item.second.first;
        result_msg["time"] = msg_item.second.second;
        result_array.push_back(result_msg);
    }
    
    response["errno"] = 0;
    response["errmsg"] = "搜索完成";
    response["results"] = result_array;
    
    string response_str = response.dump();
    conn->send(response_str);
}

//查询未读消息数量
void ChatService::private_unread_count(const TcpConnectionPtr &conn, const chat::MessageWrapper &msg, Timestamp time)
{
    // 解析JSON格式消息
    json js = json::parse(msg.data());
    
    int user_id = js["user_id"].get<int>();
    
    json response;
    response["msgid"] = PRIVATE_UNREAD_COUNT_MSG;
    
    // 查询未读消息数量 - 简化实现
    json counts_array = json::array();
    vector<User> friends = friend_model_.query(user_id);
    for (const auto& friend_user : friends) {
        int unread_count = private_message_model_.get_unread_message_count(user_id, friend_user.get_id());
        if (unread_count > 0) {
            json count_info;
            count_info["friend_id"] = friend_user.get_id();
            count_info["unread_count"] = unread_count;
            counts_array.push_back(count_info);
        }
    }
    
    response["errno"] = 0;
    response["errmsg"] = "查询成功";
    response["unread_counts"] = counts_array;
    
    string response_str = response.dump();
    conn->send(response_str);
}

//查询会话列表
void ChatService::conversation_list(const TcpConnectionPtr &conn, const chat::MessageWrapper &msg, Timestamp time)
{
    // 解析JSON格式消息
    json js = json::parse(msg.data());
    
    int user_id = js["user_id"].get<int>();
    
    json response;
    response["msgid"] = CONVERSATION_LIST_MSG;
    
    // 查询会话列表（包括私聊和群聊）
    json conversations = json::array();
    
    // 添加好友会话
    vector<User> friends = friend_model_.query(user_id);
    for (const auto& friend_user : friends) {
        // 获取最后一条消息
        auto last_messages = private_message_model_.query_private_history(user_id, friend_user.get_id(), 1);
        if (!last_messages.empty()) {
            auto last_msg = last_messages[0];
            json conversation;
            conversation["type"] = "private";
            conversation["id"] = friend_user.get_id();
            conversation["name"] = friend_user.get_name();
            conversation["last_message"] = last_msg.second.first;     // 消息内容
            conversation["last_time"] = last_msg.second.second;       // 时间
            conversation["unread_count"] = private_message_model_.get_unread_message_count(user_id, friend_user.get_id());
            conversations.push_back(conversation);
        }
    }
    
    // 添加群聊会话
    vector<Group> groups = group_model_.query_group(user_id);
    for (const auto& group : groups) {
        // 获取最后一条群消息
        auto last_messages = group_model_.query_group_history(group.get_id(), 1);
        if (!last_messages.empty()) {
            json conversation;
            conversation["type"] = "group";
            conversation["id"] = group.get_id();
            conversation["name"] = group.get_name();
            conversation["last_message"] = last_messages[0].second.first;  // 消息内容
            conversation["last_time"] = last_messages[0].second.second;     // 时间
            conversation["unread_count"] = 0; // 群聊未读数暂时设为0
            conversations.push_back(conversation);
        }
    }
    
    response["errno"] = 0;
    response["errmsg"] = "查询成功";
    response["conversations"] = conversations;
    
    string response_str = response.dump();
    conn->send(response_str);
}

// 获取当前服务器时间戳（精确到微秒，确保分布式环境下的时序正确性）
string ChatService::getCurrentServerTimestamp()
{
    auto now = system_clock::now();
    auto time_t = system_clock::to_time_t(now);
    auto us = duration_cast<microseconds>(now.time_since_epoch()) % 1000000;
    
    stringstream ss;
    ss << put_time(localtime(&time_t), "%Y-%m-%d %H:%M:%S");
    ss << "." << setfill('0') << setw(6) << us.count();
    
    return ss.str();
}

/**
 * 发送文件传输完成通知
 * @param session_id 传输会话ID
 */
void ChatService::notify_file_transfer_complete(const string& session_id) {
    try {
        // 1. 获取传输会话信息
        FileTransferSession session = file_model_.query_transfer_session(session_id);
        if (session.session_id.empty()) {
            LOG_ERROR << "Transfer session not found: " << session_id;
            return;
        }
        
        // 2. 构造完成通知消息
        json notify;
        notify["msgid"] = FILE_SEND_NOTIFY;
        notify["session_id"] = session_id;
        notify["file_id"] = session.file_id;
        notify["file_name"] = session.file_name;
        notify["file_size"] = session.file_size;
        notify["upload_time"] = getCurrentTime();
        notify["status"] = "completed";
        
        // 3. 发送给发送者
        {
            lock_guard<mutex> lock(conn_mutex_);
            auto it = user_connection_map_.find(session.sender_id);
            if (it != user_connection_map_.end()) {
                it->second->send(notify.dump());
                LOG_INFO << "File upload completion notification sent to sender " << session.sender_id;
            }
        }
        
        // 4. 如果是发送给特定用户，也要通知接收者
        if (session.receiver_id != -1) {
            // 一对一文件发送通知
            json receiver_notify = notify;
            receiver_notify["msgid"] = FILE_RECEIVE_CONFIRM;
            receiver_notify["sender_id"] = session.sender_id;
            
            {
                lock_guard<mutex> lock(conn_mutex_);
                auto it = user_connection_map_.find(session.receiver_id);
                if (it != user_connection_map_.end()) {
                    // 接收者在线，直接发送通知
                    it->second->send(receiver_notify.dump());
                    LOG_INFO << "File receive notification sent to online user " << session.receiver_id;
                } else {
                    // 接收者离线，存储为离线消息
                    offline_message_model_.insert(session.receiver_id, receiver_notify.dump());
                    LOG_INFO << "File receive notification stored as offline message for user " << session.receiver_id;
                }
            }
        }
        
        // 5. 如果是发送给群组，通知所有群成员
        if (session.group_id != -1) {
            // 获取群组成员列表
            vector<int> group_members = group_model_.query_group_users(session.group_id);
            
            json group_notify = notify;
            group_notify["msgid"] = FILE_RECEIVE_CONFIRM;
            group_notify["sender_id"] = session.sender_id;
            group_notify["group_id"] = session.group_id;
            
            {
                lock_guard<mutex> lock(conn_mutex_);
                for (int member_id : group_members) {
                    if (member_id == session.sender_id) continue; // 跳过发送者
                    
                    auto it = user_connection_map_.find(member_id);
                    if (it != user_connection_map_.end()) {
                        // 成员在线，直接发送通知
                        it->second->send(group_notify.dump());
                        LOG_DEBUG << "File notification sent to group member " << member_id;
                    } else {
                        // 成员离线，存储为离线消息
                        offline_message_model_.insert(member_id, group_notify.dump());
                        LOG_DEBUG << "File notification stored as offline message for group member " << member_id;
                    }
                }
            }
            
            LOG_INFO << "File notification sent to group " << session.group_id 
                     << " (" << group_members.size() << " members)";
        }
        
        LOG_INFO << "File transfer completion notification processed for session " << session_id;
        
    } catch (const exception& e) {
        LOG_ERROR << "Error sending file transfer completion notification for session " 
                  << session_id << ": " << e.what();
    }
}

/**
 * 处理文件上传请求
 * @param conn 客户端连接
 * @param msg 消息包装器
 * @param time 时间戳
 */
void ChatService::file_upload_request(const TcpConnectionPtr &conn, const chat::MessageWrapper &msg, Timestamp time) {
    try {
        // 解析JSON消息
        json js = json::parse(msg.data());
        int user_id = js["id"];
        string file_name = js["file_name"];
        int file_size = js["file_size"];
        string file_type = js["file_type"];
        int total_chunks = js["total_chunks"];
        int receiver_id = js.value("receiver_id", -1);
        int group_id = js.value("group_id", -1);
        
        LOG_INFO << "File upload request from user " << user_id 
                 << " file: " << file_name << " (" << file_size << " bytes)";
        
        // 验证文件类型和大小
        if (!file_model_.is_valid_file_type(file_name)) {
            json error_response;
            error_response["msgid"] = FILE_UPLOAD_RSP;
            error_response["errno"] = 1;
            error_response["errmsg"] = "不支持的文件类型";
            
            conn->send(error_response.dump());
            return;
        }
        
        if (!file_model_.is_valid_file_size(file_size)) {
            json error_response;
            error_response["msgid"] = FILE_UPLOAD_RSP;
            error_response["errno"] = 2;
            error_response["errmsg"] = "文件大小超过限制";
            
            conn->send(error_response.dump());
            return;
        }
        
        // 生成唯一的session_id和file_id
        string session_id = file_model_.generate_session_id();
        string file_id = "file_" + to_string(chrono::duration_cast<chrono::milliseconds>(
            chrono::system_clock::now().time_since_epoch()).count());
        
        // 创建文件传输会话
        FileTransferSession session;
        session.session_id = session_id;
        session.file_id = file_id;
        session.sender_id = user_id;
        session.receiver_id = receiver_id;
        session.group_id = group_id;
        session.total_chunks = total_chunks;
        session.received_chunks = 0;
        
        bool success = file_model_.create_transfer_session(session);
        
        // 返回响应
        json response;
        response["msgid"] = FILE_UPLOAD_RSP;
        if (success) {
            response["errno"] = 0;
            response["errmsg"] = "文件上传会话创建成功";
            response["session_id"] = session_id;
            response["file_id"] = file_id;
            response["chunk_size"] = 64 * 1024; // 64KB
            
            LOG_INFO << "File upload session created: " << session_id;
        } else {
            response["errno"] = 3;
            response["errmsg"] = "创建文件传输会话失败";
            
            LOG_ERROR << "Failed to create file transfer session for user " << user_id;
        }
        
        conn->send(response.dump());
        
    } catch (const exception& e) {
        LOG_ERROR << "Error processing file upload request: " << e.what();
        
        json error_response;
        error_response["msgid"] = FILE_UPLOAD_RSP;
        error_response["errno"] = 4;
        error_response["errmsg"] = "服务器内部错误";
        
        conn->send(error_response.dump());
    }
}

/**
 * 处理文件分片传输
 * @param conn 客户端连接
 * @param msg 消息包装器
 * @param time 时间戳
 */
void ChatService::file_chunk_transfer(const TcpConnectionPtr &conn, const chat::MessageWrapper &msg, Timestamp time) {
    try {
        // 解析分片消息
        json js = json::parse(msg.data());
        string session_id = js["session_id"];
        int chunk_seq = js["chunk_seq"];
        string chunk_data = js["chunk_data"];
        bool is_last = js["is_last"];
        
        LOG_DEBUG << "Received chunk " << chunk_seq << " for session " << session_id;
        
        // 验证会话有效性
        FileTransferSession session = file_model_.query_transfer_session(session_id);
        if (session.session_id.empty()) {
            json error_response;
            error_response["msgid"] = FILE_CHUNK_RSP;
            error_response["errno"] = 1;
            error_response["errmsg"] = "无效的传输会话";
            error_response["chunk_seq"] = chunk_seq;
            
            conn->send(error_response.dump());
            LOG_WARN << "Invalid transfer session: " << session_id;
            return;
        }
        
        // 创建分片对象
        FileChunk chunk;
        chunk.file_id = session.file_id;
        chunk.chunk_seq = chunk_seq;
        chunk.total_chunks = session.total_chunks;
        chunk.chunk_data = chunk_data;
        chunk.is_last = is_last;
        
        // 保存分片到临时文件
        bool save_success = file_model_.save_chunk_to_temp_file(session_id, chunk);
        
        // 更新传输进度
        if (save_success) {
            file_model_.update_chunk_status(session_id, chunk_seq);
        }
        
        // 发送分片确认响应
        json response;
        response["msgid"] = FILE_CHUNK_RSP;
        response["chunk_seq"] = chunk_seq;
        response["session_id"] = session_id;
        
        if (save_success) {
            response["errno"] = 0;
            response["errmsg"] = "分片接收成功";
            
            LOG_DEBUG << "Chunk " << chunk_seq << " saved successfully for session " << session_id;
        } else {
            response["errno"] = 2;
            response["errmsg"] = "分片保存失败";
            
            LOG_ERROR << "Failed to save chunk " << chunk_seq << " for session " << session_id;
        }
        
        conn->send(response.dump());
        
        // 检查是否为最后一个分片
        if (is_last && save_success) {
            LOG_INFO << "Last chunk received for session " << session_id << ", starting file merge";
            
            // 触发文件合并流程
            bool merge_success = file_model_.merge_chunks_to_final_file(session_id);
            if (merge_success) {
                // 发送传输完成通知
                notify_file_transfer_complete(session_id);
                LOG_INFO << "File transfer completed successfully for session " << session_id;
            } else {
                LOG_ERROR << "File merge failed for session " << session_id;
            }
        }
        
    } catch (const exception& e) {
        LOG_ERROR << "Error processing file chunk transfer: " << e.what();
        
        json error_response;
        error_response["msgid"] = FILE_CHUNK_RSP;
        error_response["errno"] = 3;
        error_response["errmsg"] = "服务器内部错误";
        
        conn->send(error_response.dump());
    }
}

/**
 * 处理文件下载请求
 * @param conn 客户端连接
 * @param msg 消息包装器
 * @param time 时间戳
 */
void ChatService::file_download_request(const TcpConnectionPtr &conn, const chat::MessageWrapper &msg, Timestamp time) {
    try {
        // 解析下载请求
        json js = json::parse(msg.data());
        int user_id = js["id"];
        string file_id = js["file_id"];
        
        LOG_INFO << "File download request from user " << user_id << " for file " << file_id;
        
        // 查询文件信息
        FileInfo file_info = file_model_.query_file_info(file_id);
        if (file_info.file_id.empty()) {
            json error_response;
            error_response["msgid"] = FILE_DOWNLOAD_RSP;
            error_response["errno"] = 1;
            error_response["errmsg"] = "文件不存在";
            
            conn->send(error_response.dump());
            LOG_WARN << "File not found: " << file_id;
            return;
        }
        
        // 检查权限（简化版本，实际应该检查用户是否有权限下载此文件）
        // TODO: 实现更完善的权限检查逻辑
        
        // 计算文件分片信息
        const size_t DOWNLOAD_CHUNK_SIZE = 64 * 1024; // 64KB
        int total_chunks = (file_info.file_size + DOWNLOAD_CHUNK_SIZE - 1) / DOWNLOAD_CHUNK_SIZE;
        
        // 检查文件是否存在且可读
        ifstream file(file_info.file_path, ios::binary);
        if (!file.is_open()) {
            json error_response;
            error_response["msgid"] = FILE_DOWNLOAD_RSP;
            error_response["errno"] = 2;
            error_response["errmsg"] = "文件读取失败";
            
            conn->send(error_response.dump());
            LOG_ERROR << "Failed to open file: " << file_info.file_path;
            return;
        }
        file.close();
        
        // 构造响应 - 返回文件信息以便分片下载
        json response;
        response["msgid"] = FILE_DOWNLOAD_RSP;
        response["errno"] = 0;
        response["errmsg"] = "文件信息获取成功，请开始分片下载";
        response["file_id"] = file_id;
        response["file_name"] = file_info.file_name;
        response["file_size"] = file_info.file_size;
        response["total_chunks"] = total_chunks;
        response["chunk_size"] = DOWNLOAD_CHUNK_SIZE;
        response["download_type"] = "chunked"; // 标识为分片下载
        
        conn->send(response.dump());
        
        // 更新文件状态
        file_model_.update_file_status(file_id, 3); // 3表示已下载
        
        LOG_INFO << "File download completed for user " << user_id << " file " << file_id;
        
    } catch (const exception& e) {
        LOG_ERROR << "Error processing file download request: " << e.what();
        
        json error_response;
        error_response["msgid"] = FILE_DOWNLOAD_RSP;
        error_response["errno"] = 3;
        error_response["errmsg"] = "服务器内部错误";
        
        conn->send(error_response.dump());
    }
}

/**
 * 处理文件发送通知
 * @param conn 客户端连接
 * @param msg 消息包装器
 * @param time 时间戳
 */
void ChatService::file_send_notify(const TcpConnectionPtr &conn, const chat::MessageWrapper &msg, Timestamp time) {
    // 这个函数主要由服务器主动调用，客户端不会直接发送此类型消息
    LOG_WARN << "Received unexpected FILE_SEND_NOTIFY message from client";
}

/**
 * 处理文件接收确认
 * @param conn 客户端连接
 * @param msg 消息包装器
 * @param time 时间戳
 */
void ChatService::file_receive_confirm(const TcpConnectionPtr &conn, const chat::MessageWrapper &msg, Timestamp time) {
    try {
        json js = json::parse(msg.data());
        int user_id = js["id"];
        string file_id = js["file_id"];
        
        LOG_INFO << "File receive confirmation from user " << user_id << " for file " << file_id;
        
        // 更新文件状态为已确认接收
        file_model_.update_file_status(file_id, 3); // 3表示已下载/已确认
        
        // 可以在这里添加其他逻辑，比如统计下载次数等
        
    } catch (const exception& e) {
        LOG_ERROR << "Error processing file receive confirmation: " << e.what();
    }
}

/**
 * 处理文件分片下载请求
 * @param conn 客户端连接
 * @param msg 消息包装器
 * @param time 时间戳
 */
void ChatService::file_chunk_download_request(const TcpConnectionPtr &conn, const chat::MessageWrapper &msg, Timestamp time) {
    try {
        // 解析分片下载请求
        json js = json::parse(msg.data());
        int user_id = js["id"];
        string file_id = js["file_id"];
        int chunk_seq = js["chunk_seq"];
        
        LOG_INFO << "File chunk download request from user " << user_id 
                 << " for file " << file_id << " chunk " << chunk_seq;
        
        // 查询文件信息
        FileInfo file_info = file_model_.query_file_info(file_id);
        if (file_info.file_id.empty()) {
            json error_response;
            error_response["msgid"] = FILE_CHUNK_DOWNLOAD_RSP;
            error_response["errno"] = 1;
            error_response["errmsg"] = "文件不存在";
            error_response["chunk_seq"] = chunk_seq;
            
            conn->send(error_response.dump());
            LOG_WARN << "File not found for chunk download: " << file_id;
            return;
        }
        
        // 验证分片序号
        const size_t DOWNLOAD_CHUNK_SIZE = 64 * 1024; // 64KB
        int total_chunks = (file_info.file_size + DOWNLOAD_CHUNK_SIZE - 1) / DOWNLOAD_CHUNK_SIZE;
        
        if (chunk_seq < 1 || chunk_seq > total_chunks) {
            json error_response;
            error_response["msgid"] = FILE_CHUNK_DOWNLOAD_RSP;
            error_response["errno"] = 2;
            error_response["errmsg"] = "无效的分片序号";
            error_response["chunk_seq"] = chunk_seq;
            
            conn->send(error_response.dump());
            LOG_WARN << "Invalid chunk sequence: " << chunk_seq << " for file " << file_id;
            return;
        }
        
        // 计算分片范围
        size_t start_pos = (chunk_seq - 1) * DOWNLOAD_CHUNK_SIZE;
        size_t chunk_size = min(DOWNLOAD_CHUNK_SIZE, 
                               static_cast<size_t>(file_info.file_size) - start_pos);
        
        // 读取指定分片
        ifstream file(file_info.file_path, ios::binary);
        if (!file.is_open()) {
            json error_response;
            error_response["msgid"] = FILE_CHUNK_DOWNLOAD_RSP;
            error_response["errno"] = 3;
            error_response["errmsg"] = "文件读取失败";
            error_response["chunk_seq"] = chunk_seq;
            
            conn->send(error_response.dump());
            LOG_ERROR << "Failed to open file for chunk download: " << file_info.file_path;
            return;
        }
        
        // 定位到分片起始位置
        file.seekg(start_pos);
        
        // 读取分片数据
        vector<char> chunk_data(chunk_size);
        file.read(chunk_data.data(), chunk_size);
        size_t bytes_read = file.gcount();
        file.close();
        
        if (bytes_read != chunk_size) {
            json error_response;
            error_response["msgid"] = FILE_CHUNK_DOWNLOAD_RSP;
            error_response["errno"] = 4;
            error_response["errmsg"] = "分片读取不完整";
            error_response["chunk_seq"] = chunk_seq;
            
            conn->send(error_response.dump());
            LOG_ERROR << "Incomplete chunk read. Expected: " << chunk_size 
                      << ", Read: " << bytes_read;
            return;
        }
        
        // Base64编码分片数据
        chunk_data.resize(bytes_read);
        string encoded_chunk = Base64Utils::encode(chunk_data);
        
        // 构造响应
        json response;
        response["msgid"] = FILE_CHUNK_DOWNLOAD_RSP;
        response["errno"] = 0;
        response["errmsg"] = "分片下载成功";
        response["file_id"] = file_id;
        response["chunk_seq"] = chunk_seq;
        response["chunk_data"] = encoded_chunk;
        response["chunk_size"] = bytes_read;
        response["total_chunks"] = total_chunks;
        response["is_last"] = (chunk_seq == total_chunks);
        
        conn->send(response.dump());
        
        LOG_INFO << "File chunk " << chunk_seq << "/" << total_chunks 
                 << " downloaded successfully for user " << user_id 
                 << " (" << bytes_read << " bytes)";
        
        // 如果是最后一个分片，更新文件下载状态
        if (chunk_seq == total_chunks) {
            file_model_.update_file_status(file_id, 3); // 3表示已下载
            LOG_INFO << "File download completed for user " << user_id << " file " << file_id;
        }
        
    } catch (const exception& e) {
        LOG_ERROR << "Error processing file chunk download request: " << e.what();
        
        json error_response;
        error_response["msgid"] = FILE_CHUNK_DOWNLOAD_RSP;
        error_response["errno"] = 5;
        error_response["errmsg"] = "服务器内部错误";
        
        conn->send(error_response.dump());
    }
}

// 集群相关方法实现
bool ChatService::initialize_cluster() {
    try {
        // 初始化集群配置
        cluster_config_ = new ClusterConfig();
        if (!cluster_config_->initialize("./config/cluster.conf")) {
            LOG_ERROR << "Failed to initialize cluster config";
            return false;
        }
        
        // 初始化集群管理器
        cluster_manager_ = new ClusterManager();
        string redis_host = cluster_config_->get_string("redis.host", "127.0.0.1");
        int redis_port = cluster_config_->get_int("redis.port", 6379);
        
        if (!cluster_manager_->initialize(redis_host, redis_port)) {
            LOG_ERROR << "Failed to initialize cluster manager";
            return false;
        }
        
        // 设置集群事件回调
        cluster_manager_->set_server_online_callback(
            bind(&ChatService::handle_cluster_event, this, "server_online", _1, _2));
        cluster_manager_->set_server_offline_callback(
            bind(&ChatService::handle_cluster_event, this, "server_offline", _1, _2));
        cluster_manager_->set_server_failure_callback(
            bind(&ChatService::handle_cluster_event, this, "server_failure", _1, _2));
        
        // 启动健康检查
        cluster_manager_->start_health_check();
        
        // 注册当前服务器到集群
        register_to_cluster();
        
        LOG_INFO << "Cluster initialization completed successfully";
        return true;
        
    } catch (const exception& e) {
        LOG_ERROR << "Exception during cluster initialization: " << e.what();
        return false;
    }
}

void ChatService::shutdown_cluster() {
    if (cluster_manager_) {
        cluster_manager_->stop_health_check();
        cluster_manager_->unregister_server(server_id_);
        delete cluster_manager_;
        cluster_manager_ = nullptr;
    }
    
    if (cluster_config_) {
        delete cluster_config_;
        cluster_config_ = nullptr;
    }
    
    LOG_INFO << "Cluster shutdown completed";
}

bool ChatService::register_to_cluster() {
    if (!cluster_manager_ || !cluster_config_) {
        return false;
    }
    
    ServerInfo server_info = get_current_server_info();
    
    if (!cluster_manager_->register_server(server_info)) {
        LOG_ERROR << "Failed to register server to cluster: " << server_id_;
        return false;
    }
    
    LOG_INFO << "Server registered to cluster successfully: " << server_id_;
    return true;
}

void ChatService::update_server_status() {
    if (!cluster_manager_) return;
    
    ServerInfo server_info = get_current_server_info();
    cluster_manager_->update_server_status(server_id_, server_info);
}

ServerInfo ChatService::get_current_server_info() {
    ServerInfo info;
    info.server_id = server_id_;
    info.host = cluster_config_ ? cluster_config_->get_string("server.host", "127.0.0.1") : "127.0.0.1";
    info.port = cluster_config_ ? cluster_config_->get_int("server.port", 8000) : 8000;
    info.status = "online";
    info.max_connections = cluster_config_ ? cluster_config_->get_int("server.max_connections", 1000) : 1000;
    
    // 获取当前连接数
    {
        lock_guard<mutex> lock(conn_mutex_);
        info.connections = user_connection_map_.size();
    }
    
    // 简单的负载计算（基于连接数比例）
    info.load = (int)((double)info.connections / info.max_connections * 100);
    
    info.last_heartbeat = chrono::system_clock::now();
    
    // 添加一些元数据
    info.metadata["version"] = "1.0.0";
    info.metadata["start_time"] = to_string(chrono::duration_cast<chrono::seconds>(
        chrono::system_clock::now().time_since_epoch()).count());
    
    return info;
}

void ChatService::handle_cluster_event(const string& event, const string& server_id, const ServerInfo& info) {
    LOG_INFO << "Cluster event: " << event << " for server: " << server_id 
             << " [" << info.host << ":" << info.port << "]";
    
    // 可以在这里添加特定的集群事件处理逻辑
}

// ================= 文件清理管理方法实现 =================

bool ChatService::start_file_cleanup_service() {
    if (!file_cleanup_manager_) {
        LOG_ERROR << "文件清理管理器未初始化";
        return false;
    }
    
    return file_cleanup_manager_->start_cleanup_service();
}

void ChatService::stop_file_cleanup_service() {
    if (file_cleanup_manager_) {
        file_cleanup_manager_->stop_cleanup_service();
    }
}

void ChatService::configure_file_cleanup(int interval_minutes, int expire_hours) {
    if (!file_cleanup_manager_) {
        LOG_ERROR << "文件清理管理器未初始化";
        return;
    }
    
    CleanupConfig config;
    config.cleanup_interval_minutes = interval_minutes;
    config.temp_file_expire_hours = expire_hours;
    config.session_expire_hours = expire_hours * 2; // 会话过期时间是文件过期时间的2倍
    config.auto_start = true;
    
    file_cleanup_manager_->set_cleanup_config(config);
    LOG_INFO << "文件清理配置已更新 - 间隔: " << interval_minutes << "分钟, 过期: " << expire_hours << "小时";
}

CleanupStats ChatService::get_file_cleanup_stats() {
    if (!file_cleanup_manager_) {
        LOG_ERROR << "文件清理管理器未初始化";
        return CleanupStats{};
    }
    
    return file_cleanup_manager_->get_cleanup_stats();
}
    if (event == "server_online") {
        // 新服务器上线，可以进行一些初始化操作
        // 例如：同步用户状态、重新平衡负载等
        
    } else if (event == "server_offline" || event == "server_failure") {
        // 服务器下线或故障，需要处理该服务器上的用户
        // 这里可以实现用户迁移逻辑
        handle_server_failure(server_id);
    }
}

void ChatService::handle_server_failure(const string& failed_server_id) {
    // 处理故障服务器上的用户
    // 1. 获取该服务器上的用户列表（从Redis获取）
    // 2. 将这些用户标记为离线
    // 3. 清理相关的连接状态
    
    LOG_WARN << "Handling failure of server: " << failed_server_id;
    
    // 这里可以实现具体的故障处理逻辑
    // 例如：通知其他用户该服务器上的用户已离线
}
