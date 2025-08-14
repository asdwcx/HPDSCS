#include <iostream>
#include <thread>
#include <string>
#include <vector>
#include <chrono>
#include <ctime>

#include <unistd.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "message.pb.h"
#include "public.hpp"

using namespace std;

#define BUFFER_SIZE 1024

// 获取系统时间
string GetCurrentTime()
{
    auto tt = chrono::system_clock::to_time_t(chrono::system_clock::now());
    struct tm *ptm = localtime(&tt);
    char date[60] = {0};
    sprintf(date, "%d-%02d-%02d %02d:%02d:%02d",
            (int)ptm->tm_year + 1900, (int)ptm->tm_mon + 1, (int)ptm->tm_mday,
            (int)ptm->tm_hour, (int)ptm->tm_min, (int)ptm->tm_sec);
    return string(date);
}

int main(int argc, char **argv)
{
    if (argc < 3)
    {
        cerr << "command invalid example: ./ExeNAME IpAddress port" << endl;
        exit(-1);
    }

    // 解析IP地址和端口号
    char *ip = argv[1];
    uint16_t port = atoi(argv[2]);

    // 创建socket
    int clientfd = socket(AF_INET, SOCK_STREAM, 0);
    if (clientfd == -1)
    {
        cerr << "create socket error" << endl;
        exit(-1);
    }

    // 填写sockaddr_in结构体变量
    sockaddr_in server;
    bzero(&server, sizeof(server));

    server.sin_family = AF_INET;
    server.sin_port = htons(port);
    server.sin_addr.s_addr = inet_addr(ip);

    // 连接服务器
    if (connect(clientfd, (sockaddr *)&server, sizeof(server)) == -1)
    {
        cerr << "connect server error" << endl;
        close(clientfd);
        exit(-1);
    }

    cout << "==========欢迎来到聊天室========" << endl;
    cout << "1.登录" << endl;
    cout << "2.注册" << endl;
    cout << "choice:";

    int choice = 0;
    cin >> choice;
    cin.get(); // 读掉缓冲区残留的回车

    switch (choice)
    {
    case 1: // 登录
        {
            int id;
            char pwd[50] = {0};
            cout << "userid:";
            cin >> id;
            cin.get();

            cout << "userpassword:";
            cin.getline(pwd, 50);

            // 创建登录请求
            chat::LoginRequest request;
            request.set_msgid(static_cast<chat::MsgType>(LOGIN_MSG));
            request.set_id(id);
            request.set_password(pwd);

            // 创建消息包装器
            chat::MessageWrapper wrapper;
            wrapper.set_msgid(static_cast<chat::MsgType>(LOGIN_MSG));
            wrapper.set_data(request.SerializeAsString());

            string request_str = wrapper.SerializeAsString();

            // 发送
            int len = send(clientfd, request_str.c_str(), request_str.length(), 0);
            if (len == -1)
            {
                cerr << "send login msg error" << endl;
            }
            else
            {
                // 接收服务器端反馈
                char buffer[BUFFER_SIZE] = {0};
                len = recv(clientfd, buffer, BUFFER_SIZE, 0);
                if (len == -1)
                {
                    cerr << "recv login response error" << endl;
                }
                else
                {
                    // 反序列化
                    chat::LoginResponse response;
                    if (response.ParseFromString(string(buffer, len)))
                    {
                        if (response.errno() != 0)
                        {
                            cerr << "login error: " << response.errmsg() << endl;
                        }
                        else
                        {
                            cout << "login success!" << endl;
                            cout << "welcome: " << response.user().name() << endl;
                            
                            // 显示好友列表
                            cout << "----------friend list----------" << endl;
                            for (int i = 0; i < response.friends_size(); ++i)
                            {
                                const chat::Friend& friend_info = response.friends(i);
                                cout << friend_info.id() << " " << friend_info.name() 
                                     << " " << friend_info.state() << endl;
                            }

                            // 显示群组列表
                            cout << "----------group list----------" << endl;
                            for (int i = 0; i < response.groups_size(); ++i)
                            {
                                const chat::Group& group_info = response.groups(i);
                                cout << group_info.id() << " " << group_info.name() 
                                     << " " << group_info.desc() << endl;
                            }

                            // 显示离线消息
                            if (response.offline_msgs_size() > 0)
                            {
                                cout << "----------offline message----------" << endl;
                                for (int i = 0; i < response.offline_msgs_size(); ++i)
                                {
                                    const chat::OfflineMessage& offline_msg = response.offline_msgs(i);
                                    cout << "from: " << offline_msg.from_id() 
                                         << " message: " << offline_msg.message() << endl;
                                }
                            }
                        }
                    }
                    else
                    {
                        cerr << "login response parse error" << endl;
                    }
                }
            }
        }
        break;
    case 2: // 注册
        {
            char name[50] = {0};
            char pwd[50] = {0};
            cout << "username:";
            cin.getline(name, 50);

            cout << "userpassword:";
            cin.getline(pwd, 50);

            // 创建注册请求
            chat::RegisterRequest request;
            request.set_msgid(static_cast<chat::MsgType>(REG_MSG));
            request.set_name(name);
            request.set_password(pwd);

            // 创建消息包装器
            chat::MessageWrapper wrapper;
            wrapper.set_msgid(static_cast<chat::MsgType>(REG_MSG));
            wrapper.set_data(request.SerializeAsString());

            string request_str = wrapper.SerializeAsString();

            // 发送
            int len = send(clientfd, request_str.c_str(), request_str.length(), 0);
            if (len == -1)
            {
                cerr << "send register msg error" << endl;
            }
            else
            {
                // 接收服务器端反馈
                char buffer[BUFFER_SIZE] = {0};
                len = recv(clientfd, buffer, BUFFER_SIZE, 0);
                if (len == -1)
                {
                    cerr << "recv register response error" << endl;
                }
                else
                {
                    // 反序列化
                    chat::RegisterResponse response;
                    if (response.ParseFromString(string(buffer, len)))
                    {
                        if (response.errno() != 0)
                        {
                            cerr << "register error: " << response.errmsg() << endl;
                        }
                        else
                        {
                            cout << "register success! your id is: " << response.id() << endl;
                        }
                    }
                    else
                    {
                        cerr << "register response parse error" << endl;
                    }
                }
            }
        }
        break;
    default:
        cerr << "invalid input" << endl;
        break;
    }

    close(clientfd);
    return 0;
}
