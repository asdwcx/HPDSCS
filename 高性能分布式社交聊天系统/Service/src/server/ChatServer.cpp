#include <functional>
#include <string>
#include <iostream>
#include "ChatServer.hpp"
#include "ChatService.hpp"
#include "message.pb.h"

using namespace std;
using namespace placeholders;

//初始化聊天服务器对象
ChatServer::ChatServer(EventLoop *loop, const InetAddress &listenAddr, const string &nameArg)
    : server_(loop, listenAddr, nameArg),
      loop_(loop)
{
    //注册连接回调
    server_.setConnectionCallback(bind(&ChatServer::on_connection, this, _1));

    //注册消息回调
    server_.setMessageCallback(bind(&ChatServer::on_message, this, _1, _2, _3));

    //设置线程数量
    server_.setThreadNum(4);
}

//启动服务
void ChatServer::start()
{
    server_.start();
}

//上报连接相关信息的回调函数
void ChatServer::on_connection(const TcpConnectionPtr &conn)
{
    //如果用户断开连接
    if (!conn->connected())
    {
        //处理异常断开
        ChatService::instance()->client_close_exception(conn);
        conn->shutdown();
    }
}

//上报读写时间相关信息的回调函数
void ChatServer::on_message(const TcpConnectionPtr &conn, Buffer *buffer, Timestamp time)
{
    string buf = buffer->retrieveAllAsString();
    cout<<"execute: "<<buf<<endl;
    
    // 数据反序列化 - 使用protobuf
    chat::MessageWrapper msg;
    if (!msg.ParseFromString(buf))
    {
        cout << "protobuf parse error!" << endl;
        return;
    }
    
    //解耦网络和业务模块的代码
    //通过msg里面的msgid，绑定msgid的回调函数，获取业务处理器handler
    auto msg_handler = ChatService::instance()->get_handler(static_cast<int>(msg.msgid()));

    //调用对应的业务处理函数
    msg_handler(conn, msg, time);
}
