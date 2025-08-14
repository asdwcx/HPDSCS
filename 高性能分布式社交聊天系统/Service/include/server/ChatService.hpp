#ifndef CHATSERVICE_H
#define CHATSERVICE_H

#include <unordered_map>
#include <functional>
#include <mutex>
#include <atomic>
#include <chrono>
#include <muduo/net/TcpConnection.h>
#include "message.pb.h"
#include "UserModel.hpp"
#include "OfflineMessageModel.hpp"
#include "FriendModel.hpp"
#include "GroupModel.hpp"
#include "PrivateMessageModel.hpp"
#include "UserLocationModel.hpp"
#include "FileModel.hpp"
#include "FileTransferRedisModel.hpp"
#include "FileCleanupManager.hpp"
#include "Redis.hpp"

// 前向声明
class ClusterManager;
class ClusterConfig;

using namespace std;
using namespace muduo;
using namespace muduo::net;
using namespace placeholders;

using MsgHandler = function<void(const TcpConnectionPtr &conn, const chat::MessageWrapper &msg, Timestamp time)>;

//聊天服务器业务类，单例模式设计，因为一个就够了。
//映射事件回调用

class ChatService
{
public:
    //获取单例对象
    static ChatService *instance();

    //登录
    void login(const TcpConnectionPtr &conn, const chat::MessageWrapper &msg, Timestamp time);

    //注册
    void regist(const TcpConnectionPtr &conn, const chat::MessageWrapper &msg, Timestamp time);

    //一对一聊天业务
    void one_chat(const TcpConnectionPtr &conn, const chat::MessageWrapper &msg, Timestamp time);

    //添加好友请求业务（用户A发起请求）
    void add_friend_request(const TcpConnectionPtr &conn, const chat::MessageWrapper &msg, Timestamp time);

    //添加好友回复业务（用户B回复）
    void add_friend_reply(const TcpConnectionPtr &conn, const chat::MessageWrapper &msg, Timestamp time);

    //创建 群组
    void create_group(const TcpConnectionPtr &conn, const chat::MessageWrapper &msg, Timestamp time);

    //加入群组
    bool add_group(const TcpConnectionPtr &conn, const chat::MessageWrapper &msg, Timestamp time);

    //群聊业务
    void group_chat(const TcpConnectionPtr &conn, const chat::MessageWrapper &msg, Timestamp time);

    //申请加入群组
    void join_group_request(const TcpConnectionPtr &conn, const chat::MessageWrapper &msg, Timestamp time);

    //审核加群申请
    void approve_join_request(const TcpConnectionPtr &conn, const chat::MessageWrapper &msg, Timestamp time);

    //查询群聊历史记录
    void group_history(const TcpConnectionPtr &conn, const chat::MessageWrapper &msg, Timestamp time);

    //搜索群消息
    void group_search(const TcpConnectionPtr &conn, const chat::MessageWrapper &msg, Timestamp time);

    //查询群信息
    void group_info(const TcpConnectionPtr &conn, const chat::MessageWrapper &msg, Timestamp time);

    //退出群聊
    void quit_group(const TcpConnectionPtr &conn, const chat::MessageWrapper &msg, Timestamp time);

    //查询一对一聊天历史记录
    void private_history(const TcpConnectionPtr &conn, const chat::MessageWrapper &msg, Timestamp time);

    //搜索一对一聊天消息
    void private_search(const TcpConnectionPtr &conn, const chat::MessageWrapper &msg, Timestamp time);

    //查询未读消息数量
    void private_unread_count(const TcpConnectionPtr &conn, const chat::MessageWrapper &msg, Timestamp time);

    //查询会话列表
    void conversation_list(const TcpConnectionPtr &conn, const chat::MessageWrapper &msg, Timestamp time);

    //更新用户位置
    void update_location(const TcpConnectionPtr &conn, const chat::MessageWrapper &msg, Timestamp time);

    //查找附近的人
    void find_nearby(const TcpConnectionPtr &conn, const chat::MessageWrapper &msg, Timestamp time);

    //设置位置可见性
    void set_location_visibility(const TcpConnectionPtr &conn, const chat::MessageWrapper &msg, Timestamp time);

    //获取用户位置信息
    void get_location(const TcpConnectionPtr &conn, const chat::MessageWrapper &msg, Timestamp time);

    // 文件传输相关功能
    //文件上传请求
    void file_upload_request(const TcpConnectionPtr &conn, const chat::MessageWrapper &msg, Timestamp time);
    
    //文件分片传输
    void file_chunk_transfer(const TcpConnectionPtr &conn, const chat::MessageWrapper &msg, Timestamp time);
    
    //文件下载请求
    void file_download_request(const TcpConnectionPtr &conn, const chat::MessageWrapper &msg, Timestamp time);
    
    //分片下载请求
    void file_chunk_download_request(const TcpConnectionPtr &conn, const chat::MessageWrapper &msg, Timestamp time);
    
    //文件发送通知（一对一和群聊）
    void file_send_notify(const TcpConnectionPtr &conn, const chat::MessageWrapper &msg, Timestamp time);
    
    //文件接收确认
    void file_receive_confirm(const TcpConnectionPtr &conn, const chat::MessageWrapper &msg, Timestamp time);

private:
    // 文件传输辅助函数
    void send_file_notification(const FileInfo& file_info);
    void notify_file_transfer_complete(const string& session_id);
    string getCurrentTime();

public:
    //注销业务
    void loginout(const TcpConnectionPtr &conn, const chat::MessageWrapper &msg, Timestamp time);

    //redis订阅消息触发的回调函数
    void redis_subscribe_message_handler(int channel, string message);

    //获取消息对应的处理器
    MsgHandler get_handler(int msgid);

    //处理客户端异常退出
    void client_close_exception(const TcpConnectionPtr &conn);

    //服务器异常，业务重置
    void reset();

    // 文件清理管理相关
    bool start_file_cleanup_service();
    void stop_file_cleanup_service();
    void configure_file_cleanup(int interval_minutes = 30, int expire_hours = 24);
    CleanupStats get_file_cleanup_stats();

private:
    //注册消息以及对应的回调操作
    ChatService();

    // 时间戳管理
    string getCurrentServerTimestamp();

private:
    //存储事件触发的回调函数
    unordered_map<int, MsgHandler> msg_handler_map_;

    //存储在线用户的连接情况，便于服务器给用户发消息，注意线程安全
    unordered_map<int, TcpConnectionPtr> user_connection_map_;
    mutex conn_mutex_;

    // 服务器ID标识（用于分布式环境）
    static string server_id_;

    //redis操作对象
    Redis redis_;

    UserModel user_model_;
    OfflineMessageModel offline_message_model_;
    FriendModel friend_model_;
    GroupModel group_model_;
    PrivateMessageModel private_message_model_;
    UserLocationModel location_model_;
    FileModel file_model_;
    FileTransferRedisModel file_transfer_redis_;
    
    // 文件清理管理器
    unique_ptr<FileCleanupManager> file_cleanup_manager_;
    
    // 集群管理
    ClusterManager* cluster_manager_;
    ClusterConfig* cluster_config_;
    
public:
    // 集群相关方法
    bool initialize_cluster();
    void shutdown_cluster();
    bool register_to_cluster();
    void update_server_status();
    ServerInfo get_current_server_info();
    void handle_cluster_event(const string& event, const string& server_id, const ServerInfo& info);
};

#endif