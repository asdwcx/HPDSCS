#include <iostream>
#include <thread>
#include <string>
#include <vector>
#include <chrono>
#include <ctime>
#include <atomic>

#include <unistd.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "message.pb.h"
#include "public.hpp"

using namespace std;

#define BUFFER_SIZE 1024

// 全局变量
atomic<bool> isMainMenuRunning(false);
int g_clientfd = -1;
int g_current_userid = -1;
string g_current_username;

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

// 接收消息线程函数
void ReadTaskHandler(int clientfd)
{
    while (isMainMenuRunning)
    {
        char buffer[BUFFER_SIZE] = {0};
        int len = recv(clientfd, buffer, BUFFER_SIZE, 0);
        if (len == -1 || len == 0)
        {
            close(clientfd);
            exit(-1);
        }

        // 解析接收到的消息
        chat::MessageWrapper wrapper;
        if (wrapper.ParseFromString(string(buffer, len)))
        {
            // 根据消息类型处理
            if (wrapper.msgid() == chat::ONE_CHAT_MSG)
            {
                chat::OneChatMessage msg;
                if (msg.ParseFromString(wrapper.data()))
                {
                    cout << endl << "[" << msg.time() << "] " 
                         << msg.name() << "(" << msg.id() << ") says: " 
                         << msg.msg() << endl;
                    cout << ">> ";  // 重新显示输入提示符
                    cout.flush();
                }
            }
            else if (wrapper.msgid() == chat::GROUP_CHAT_MSG)
            {
                chat::GroupChatMessage msg;
                if (msg.ParseFromString(wrapper.data()))
                {
                    cout << endl << "[" << msg.time() << "] Group[" 
                         << msg.groupid() << "] " << msg.name() 
                         << "(" << msg.id() << ") says: " << msg.msg() << endl;
                    cout << ">> ";
                    cout.flush();
                }
            }
            else if (wrapper.msgid() == chat::ADD_FRIEND_NOTIFY)
            {
                // 处理好友请求通知
                chat::AddFriendNotify notify;
                if (notify.ParseFromString(wrapper.data()))
                {
                    cout << endl << "==================================" << endl;
                    cout << "[好友请求] " << notify.from_name() << "(" 
                         << notify.from_id() << ") 想要添加你为好友" << endl;
                    cout << "消息: " << notify.message() << endl;
                    cout << "请回复: accept:" << notify.from_id() 
                         << ":是否同意(true/false):回复消息" << endl;
                    cout << "示例: accept:" << notify.from_id() 
                         << ":true:很高兴认识你" << endl;
                    cout << "==================================" << endl;
                    cout << ">> ";
                    cout.flush();
                }
            }
            else if (wrapper.msgid() == chat::ADD_FRIEND_RESULT)
            {
                // 处理添加好友最终结果
                chat::AddFriendResult result;
                if (result.ParseFromString(wrapper.data()))
                {
                    cout << endl << "==================================" << endl;
                    cout << "[系统通知] 添加好友结果：";
                    
                    if (result.errno() == 0)
                    {
                        cout << "好友添加成功！" << endl;
                        cout << "新好友: " << result.friend_name() 
                             << "(" << result.friend_id() << ")" << endl;
                        if (!result.reply_msg().empty())
                        {
                            cout << "回复: " << result.reply_msg() << endl;
                        }
                        cout << "现在可以开始聊天了。" << endl;
                    }
                    else if (result.errno() == 2)
                    {
                        cout << "对方拒绝了你的好友请求" << endl;
                        cout << "用户: " << result.friend_name() 
                             << "(" << result.friend_id() << ")" << endl;
                        if (!result.reply_msg().empty())
                        {
                            cout << "回复: " << result.reply_msg() << endl;
                        }
                    }
                    else if (result.errno() == 3)
                    {
                        cout << "目标用户不存在 (ID: " << result.friend_id() << ")" << endl;
                    }
                    else if (result.errno() == 4)
                    {
                        cout << "你们已经是好友了" << endl;
                        cout << "好友: " << result.friend_name() 
                             << "(" << result.friend_id() << ")" << endl;
                    }
                    else
                    {
                        cout << "添加好友失败: " << result.errmsg() << endl;
                    }
                    
                    cout << "==================================" << endl;
                    cout << ">> ";
                    cout.flush();
                }
            }
            else if (wrapper.msgid() == chat::CREATE_GROUP_MSG)
            {
                chat::CreateGroupResponse response;
                if (response.ParseFromString(wrapper.data()))
                {
                    cout << endl << "==================================" << endl;
                    cout << "[系统通知] 创建群组结果：";
                    
                    if (response.errno() == 0)
                    {
                        cout << "群聊创建成功！" << endl;
                        cout << "群ID: " << response.group_id() << endl;
                        cout << "群名: " << response.group_name() << endl;
                        cout << "群主: " << g_current_username << "(" << g_current_userid << ")" << endl;
                        cout << "成员列表:" << endl;
                        
                        for (int i = 0; i < response.members_size(); ++i)
                        {
                            const auto& member = response.members(i);
                            string role = (member.id() == g_current_userid) ? " [群主]" : " [成员]";
                            cout << "- " << member.name() << "(" << member.id() << ")" << role << endl;
                        }
                        cout << "现在可以开始群聊了！" << endl;
                        cout << "使用命令: groupchat:" << response.group_id() << ":消息内容" << endl;
                    }
                    else if (response.errno() == 1)
                    {
                        cout << "群名称重复，请换一个名称" << endl;
                    }
                    else if (response.errno() == 2)
                    {
                        cout << "部分成员ID无效" << endl;
                        cout << "错误: " << response.errmsg() << endl;
                    }
                    else
                    {
                        cout << "创建群组失败: " << response.errmsg() << endl;
                    }
                    
                    cout << "==================================" << endl;
                    cout << ">> ";
                    cout.flush();
                }
            }
            else if (wrapper.msgid() == chat::GROUP_INVITE_NOTIFY)
            {
                // 处理群邀请通知
                chat::GroupInviteNotify notify;
                if (notify.ParseFromString(wrapper.data()))
                {
                    cout << endl << "==================================" << endl;
                    cout << "[群邀请] " << notify.inviter_name() << "(" 
                         << notify.inviter_id() << ") 邀请你加入群聊" << endl;
                    cout << "群名: " << notify.group_name() << endl;
                    cout << "群ID: " << notify.group_id() << endl;
                    if (!notify.group_desc().empty())
                    {
                        cout << "群描述: " << notify.group_desc() << endl;
                    }
                    cout << "你已被自动添加到该群聊中" << endl;
                    cout << "现在可以使用 groupchat:" << notify.group_id() 
                         << ":消息内容 来发送群消息" << endl;
                    cout << "==================================" << endl;
                    cout << ">> ";
                    cout.flush();
                }
            }
            else if (wrapper.msgid() == chat::ADD_GROUP_MSG)
            {
                chat::AddGroupResponse response;
                if (response.ParseFromString(wrapper.data()))
                {
                    cout << endl << "加入群组响应: ";
                    if (response.errno() == 0)
                    {
                        cout << "加入群组成功！" << endl;
                    }
                    else
                    {
                        cout << "加入群组失败: " << response.errmsg() << endl;
                    }
                    cout << ">> ";
                    cout.flush();
                }
            }
            else if (wrapper.msgid() == chat::LOGINOUT_MSG)
            {
                chat::LoginoutResponse response;
                if (response.ParseFromString(wrapper.data()))
                {
                    cout << endl << "注销响应: ";
                    if (response.errno() == 0)
                    {
                        cout << "注销成功！" << endl;
                    }
                    else
                    {
                        cout << "注销失败: " << response.errmsg() << endl;
                    }
                    isMainMenuRunning = false;
                }
            }
        }
    }
}

// 一对一聊天功能
void OneToOneChat(int clientfd, const string& input)
{
    // 解析输入格式: chat:friendid:message
    size_t pos1 = input.find(':');
    if (pos1 == string::npos)
    {
        cout << "格式错误！正确格式: chat:好友ID:消息内容" << endl;
        return;
    }

    size_t pos2 = input.find(':', pos1 + 1);
    if (pos2 == string::npos)
    {
        cout << "格式错误！正确格式: chat:好友ID:消息内容" << endl;
        return;
    }

    string friend_id_str = input.substr(pos1 + 1, pos2 - pos1 - 1);
    string message = input.substr(pos2 + 1);

    if (friend_id_str.empty() || message.empty())
    {
        cout << "好友ID和消息内容不能为空！" << endl;
        return;
    }

    int friend_id = atoi(friend_id_str.c_str());
    if (friend_id <= 0)
    {
        cout << "好友ID必须是正整数！" << endl;
        return;
    }

    // 创建一对一聊天消息
    chat::OneChatMessage chatMsg;
    chatMsg.set_msgid(static_cast<chat::MsgType>(ONE_CHAT_MSG));
    chatMsg.set_id(g_current_userid);
    chatMsg.set_name(g_current_username);
    chatMsg.set_to(friend_id);
    chatMsg.set_msg(message);
    chatMsg.set_time(GetCurrentTime());

    // 创建消息包装器
    chat::MessageWrapper wrapper;
    wrapper.set_msgid(static_cast<chat::MsgType>(ONE_CHAT_MSG));
    wrapper.set_data(chatMsg.SerializeAsString());

    string request_str = wrapper.SerializeAsString();

    // 发送消息
    int len = send(clientfd, request_str.c_str(), request_str.length(), 0);
    if (len == -1)
    {
        cout << "发送消息失败！" << endl;
    }
    else
    {
        cout << "消息已发送给好友 " << friend_id << endl;
    }
}

// 添加好友功能
void AddFriend(int clientfd, const string& input)
{
    // 解析输入格式: addfriend:friendid:message
    size_t pos1 = input.find(':');
    if (pos1 == string::npos)
    {
        cout << "格式错误！正确格式: addfriend:好友ID:请求消息" << endl;
        return;
    }

    size_t pos2 = input.find(':', pos1 + 1);
    if (pos2 == string::npos)
    {
        cout << "格式错误！正确格式: addfriend:好友ID:请求消息" << endl;
        cout << "示例: addfriend:1002:我是Alice，想和你成为好友" << endl;
        return;
    }

    string friend_id_str = input.substr(pos1 + 1, pos2 - pos1 - 1);
    string message = input.substr(pos2 + 1);
    
    if (friend_id_str.empty())
    {
        cout << "好友ID不能为空！" << endl;
        return;
    }

    if (message.empty())
    {
        cout << "请求消息不能为空！" << endl;
        return;
    }

    int friend_id = atoi(friend_id_str.c_str());
    if (friend_id <= 0)
    {
        cout << "好友ID必须是正整数！" << endl;
        return;
    }

    if (friend_id == g_current_userid)
    {
        cout << "不能添加自己为好友！" << endl;
        return;
    }

    // 创建添加好友请求
    chat::AddFriendRequest request;
    request.set_msgid(static_cast<chat::MsgType>(ADD_FRIEND_MSG));
    request.set_id(g_current_userid);
    request.set_friendid(friend_id);
    request.set_message(message);

    // 创建消息包装器
    chat::MessageWrapper wrapper;
    wrapper.set_msgid(static_cast<chat::MsgType>(ADD_FRIEND_MSG));
    wrapper.set_data(request.SerializeAsString());

    string request_str = wrapper.SerializeAsString();

    // 发送请求
    int len = send(clientfd, request_str.c_str(), request_str.length(), 0);
    if (len == -1)
    {
        cout << "发送添加好友请求失败！" << endl;
    }
    else
    {
        cout << "添加好友请求已发送给用户 " << friend_id << "，等待对方回复..." << endl;
    }
}

// 回复好友请求功能
void AcceptFriend(int clientfd, const string& input)
{
    // 解析输入格式: accept:from_id:true/false:reply_message
    size_t pos1 = input.find(':');
    if (pos1 == string::npos)
    {
        cout << "格式错误！正确格式: accept:发起人ID:是否同意(true/false):回复消息" << endl;
        return;
    }

    size_t pos2 = input.find(':', pos1 + 1);
    if (pos2 == string::npos)
    {
        cout << "格式错误！正确格式: accept:发起人ID:是否同意(true/false):回复消息" << endl;
        return;
    }

    size_t pos3 = input.find(':', pos2 + 1);
    if (pos3 == string::npos)
    {
        cout << "格式错误！正确格式: accept:发起人ID:是否同意(true/false):回复消息" << endl;
        cout << "示例: accept:1001:true:很高兴认识你" << endl;
        return;
    }

    string from_id_str = input.substr(pos1 + 1, pos2 - pos1 - 1);
    string accept_str = input.substr(pos2 + 1, pos3 - pos2 - 1);
    string reply_message = input.substr(pos3 + 1);

    if (from_id_str.empty() || accept_str.empty() || reply_message.empty())
    {
        cout << "参数不能为空！" << endl;
        return;
    }

    int from_id = atoi(from_id_str.c_str());
    if (from_id <= 0)
    {
        cout << "发起人ID必须是正整数！" << endl;
        return;
    }

    bool accept = false;
    if (accept_str == "true")
    {
        accept = true;
    }
    else if (accept_str == "false")
    {
        accept = false;
    }
    else
    {
        cout << "是否同意参数错误！请输入 true 或 false" << endl;
        return;
    }

    // 创建好友请求回复
    chat::AddFriendReply reply;
    reply.set_msgid(static_cast<chat::MsgType>(ADD_FRIEND_RESPONSE));
    reply.set_from_id(from_id);
    reply.set_to_id(g_current_userid);
    reply.set_accept(accept);
    reply.set_message(reply_message);

    // 创建消息包装器
    chat::MessageWrapper wrapper;
    wrapper.set_msgid(static_cast<chat::MsgType>(ADD_FRIEND_RESPONSE));
    wrapper.set_data(reply.SerializeAsString());

    string request_str = wrapper.SerializeAsString();

    // 发送回复
    int len = send(clientfd, request_str.c_str(), request_str.length(), 0);
    if (len == -1)
    {
        cout << "发送好友请求回复失败！" << endl;
    }
    else
    {
        if (accept)
        {
            cout << "已同意用户 " << from_id << " 的好友请求" << endl;
        }
        else
        {
            cout << "已拒绝用户 " << from_id << " 的好友请求" << endl;
        }
    }
}

// 创建群组功能
void CreateGroup(int clientfd, const string& input)
{
    // 解析输入格式: creategroup:groupname:member_ids
    size_t pos1 = input.find(':');
    if (pos1 == string::npos)
    {
        cout << "格式错误！正确格式: creategroup:群名:成员ID列表" << endl;
        cout << "示例: creategroup:技术交流群:1002,1003,1004" << endl;
        return;
    }

    size_t pos2 = input.find(':', pos1 + 1);
    if (pos2 == string::npos)
    {
        cout << "格式错误！正确格式: creategroup:群名:成员ID列表" << endl;
        cout << "示例: creategroup:技术交流群:1002,1003,1004" << endl;
        return;
    }

    string group_name = input.substr(pos1 + 1, pos2 - pos1 - 1);
    string member_ids_str = input.substr(pos2 + 1);

    if (group_name.empty())
    {
        cout << "群名不能为空！" << endl;
        return;
    }

    // 解析成员ID列表
    vector<int> member_ids;
    if (!member_ids_str.empty())
    {
        stringstream ss(member_ids_str);
        string id_str;
        
        while (getline(ss, id_str, ','))
        {
            // 去除空格
            id_str.erase(0, id_str.find_first_not_of(" \t"));
            id_str.erase(id_str.find_last_not_of(" \t") + 1);
            
            if (!id_str.empty())
            {
                int member_id = atoi(id_str.c_str());
                if (member_id > 0 && member_id != g_current_userid)
                {
                    member_ids.push_back(member_id);
                }
                else if (member_id == g_current_userid)
                {
                    cout << "警告：不需要添加自己到群成员列表中" << endl;
                }
                else
                {
                    cout << "无效的成员ID: " << id_str << endl;
                    return;
                }
            }
        }
    }

    // 创建群组请求
    chat::CreateGroupRequest request;
    request.set_msgid(static_cast<chat::MsgType>(CREATE_GROUP_MSG));
    request.set_id(g_current_userid);
    request.set_groupname(group_name);
    request.set_groupdesc(""); // 暂时不设置描述，可以后续扩展
    
    // 添加成员ID列表
    for (int member_id : member_ids)
    {
        request.add_member_ids(member_id);
    }

    // 创建消息包装器
    chat::MessageWrapper wrapper;
    wrapper.set_msgid(static_cast<chat::MsgType>(CREATE_GROUP_MSG));
    wrapper.set_data(request.SerializeAsString());

    string request_str = wrapper.SerializeAsString();

    // 发送请求
    int len = send(clientfd, request_str.c_str(), request_str.length(), 0);
    if (len == -1)
    {
        cout << "发送创建群组请求失败！" << endl;
    }
    else
    {
        cout << "创建群组请求已发送..." << endl;
        cout << "群名: " << group_name << endl;
        if (!member_ids.empty())
        {
            cout << "邀请成员: ";
            for (size_t i = 0; i < member_ids.size(); ++i)
            {
                cout << member_ids[i];
                if (i < member_ids.size() - 1) cout << ", ";
            }
            cout << endl;
        }
        cout << "等待服务器响应..." << endl;
    }
}
    }
    else
    {
        cout << "创建群组请求已发送，等待服务器响应..." << endl;
    }
}

// 加入群组功能
void JoinGroup(int clientfd, const string& input)
{
    // 解析输入格式: joingroup:groupid
    size_t pos = input.find(':');
    if (pos == string::npos)
    {
        cout << "格式错误！正确格式: joingroup:群ID" << endl;
        return;
    }

    string group_id_str = input.substr(pos + 1);
    if (group_id_str.empty())
    {
        cout << "群ID不能为空！" << endl;
        return;
    }

    int group_id = atoi(group_id_str.c_str());
    if (group_id <= 0)
    {
        cout << "群ID必须是正整数！" << endl;
        return;
    }

    // 创建加入群组请求
    chat::AddGroupRequest request;
    request.set_msgid(static_cast<chat::MsgType>(ADD_GROUP_MSG));
    request.set_id(g_current_userid);
    request.set_groupid(group_id);

    // 创建消息包装器
    chat::MessageWrapper wrapper;
    wrapper.set_msgid(static_cast<chat::MsgType>(ADD_GROUP_MSG));
    wrapper.set_data(request.SerializeAsString());

    string request_str = wrapper.SerializeAsString();

    // 发送请求
    int len = send(clientfd, request_str.c_str(), request_str.length(), 0);
    if (len == -1)
    {
        cout << "发送加入群组请求失败！" << endl;
    }
    else
    {
        cout << "加入群组请求已发送，等待服务器响应..." << endl;
    }
}

// 群聊功能
void GroupChat(int clientfd, const string& input)
{
    // 解析输入格式: groupchat:groupid:message
    size_t pos1 = input.find(':');
    if (pos1 == string::npos)
    {
        cout << "格式错误！正确格式: groupchat:群ID:消息内容" << endl;
        return;
    }

    size_t pos2 = input.find(':', pos1 + 1);
    if (pos2 == string::npos)
    {
        cout << "格式错误！正确格式: groupchat:群ID:消息内容" << endl;
        return;
    }

    string group_id_str = input.substr(pos1 + 1, pos2 - pos1 - 1);
    string message = input.substr(pos2 + 1);

    if (group_id_str.empty() || message.empty())
    {
        cout << "群ID和消息内容不能为空！" << endl;
        return;
    }

    int group_id = atoi(group_id_str.c_str());
    if (group_id <= 0)
    {
        cout << "群ID必须是正整数！" << endl;
        return;
    }

    // 创建群聊消息
    chat::GroupChatMessage chatMsg;
    chatMsg.set_msgid(static_cast<chat::MsgType>(GROUP_CHAT_MSG));
    chatMsg.set_id(g_current_userid);
    chatMsg.set_name(g_current_username);
    chatMsg.set_groupid(group_id);
    chatMsg.set_msg(message);
    chatMsg.set_time(GetCurrentTime());

    // 创建消息包装器
    chat::MessageWrapper wrapper;
    wrapper.set_msgid(static_cast<chat::MsgType>(GROUP_CHAT_MSG));
    wrapper.set_data(chatMsg.SerializeAsString());

    string request_str = wrapper.SerializeAsString();

    // 发送消息
    int len = send(clientfd, request_str.c_str(), request_str.length(), 0);
    if (len == -1)
    {
        cout << "发送群聊消息失败！" << endl;
    }
    else
    {
        cout << "群聊消息已发送到群 " << group_id << endl;
    }
}

// 注销功能
void LoginOut(int clientfd)
{
    // 创建注销请求
    chat::LoginoutRequest request;
    request.set_msgid(static_cast<chat::MsgType>(LOGINOUT_MSG));
    request.set_id(g_current_userid);

    // 创建消息包装器
    chat::MessageWrapper wrapper;
    wrapper.set_msgid(static_cast<chat::MsgType>(LOGINOUT_MSG));
    wrapper.set_data(request.SerializeAsString());

    string request_str = wrapper.SerializeAsString();

    // 发送请求
    int len = send(clientfd, request_str.c_str(), request_str.length(), 0);
    if (len == -1)
    {
        cout << "发送注销请求失败！" << endl;
    }
    else
    {
        cout << "注销请求已发送，正在退出..." << endl;
        isMainMenuRunning = false;
    }
}

// 显示帮助信息
void ShowHelp()
{
    cout << "===================支持的命令===================" << endl;
    cout << "chat:好友ID:消息内容                    - 发送一对一聊天消息" << endl;
    cout << "addfriend:好友ID:请求消息               - 添加好友" << endl;
    cout << "accept:发起人ID:是否同意:回复消息       - 回复好友请求" << endl;
    cout << "creategroup:群名:成员ID列表             - 创建群组" << endl;
    cout << "joingroup:群ID                         - 加入群组" << endl;
    cout << "groupchat:群ID:消息内容                 - 发送群聊消息" << endl;
    cout << "loginout                                - 注销登录" << endl;
    cout << "help                                    - 显示帮助信息" << endl;
    cout << "quit                                    - 退出程序" << endl;
    cout << "===============================================" << endl;
    cout << "示例:" << endl;
    cout << "  chat:123:你好！                       - 向用户ID为123的好友发送'你好！'" << endl;
    cout << "  addfriend:456:我是张三，想加你为好友   - 向用户456发送好友请求" << endl;
    cout << "  accept:456:true:很高兴认识你           - 同意用户456的好友请求" << endl;
    cout << "  accept:456:false:抱歉，暂时不方便      - 拒绝用户456的好友请求" << endl;
    cout << "  creategroup:技术交流群:1002,1003      - 创建群聊并邀请用户1002和1003" << endl;
    cout << "  groupchat:10001:大家好！               - 向群ID为10001的群发送消息" << endl;
    cout << "  joingroup:789            - 加入群ID为789的群组" << endl;
    cout << "  groupchat:789:大家好！   - 在群789中发送'大家好！'" << endl;
    cout << "===============================================" << endl;
}

// 主菜单处理
void MainMenu(int clientfd)
{
    // 启动接收消息的子线程
    thread readTask(ReadTaskHandler, clientfd);
    readTask.detach();

    // 显示欢迎信息
    cout << "===================欢迎进入聊天室===================" << endl;
    cout << "当前用户: " << g_current_username << " (ID: " << g_current_userid << ")" << endl;
    ShowHelp();

    string input;
    while (isMainMenuRunning)
    {
        cout << ">> ";
        getline(cin, input);

        if (input == "quit")
        {
            isMainMenuRunning = false;
            break;
        }
        else if (input == "help")
        {
            ShowHelp();
        }
        else if (input == "loginout")
        {
            LoginOut(clientfd);
        }
        else if (input.substr(0, 5) == "chat:")
        {
            OneToOneChat(clientfd, input);
        }
        else if (input.substr(0, 10) == "addfriend:")
        {
            AddFriend(clientfd, input);
        }
        else if (input.substr(0, 7) == "accept:")
        {
            AcceptFriend(clientfd, input);
        }
        else if (input.substr(0, 12) == "creategroup:")
        {
            CreateGroup(clientfd, input);
        }
        else if (input.substr(0, 10) == "joingroup:")
        {
            JoinGroup(clientfd, input);
        }
        else if (input.substr(0, 10) == "groupchat:")
        {
            GroupChat(clientfd, input);
        }
        else if (!input.empty())
        {
            cout << "未知命令: " << input << "，输入 help 查看帮助" << endl;
        }
    }
}

int main(int argc, char **argv)
{
    if (argc < 3)
    {
        cerr << "使用方法: " << argv[0] << " <服务器IP> <端口>" << endl;
        exit(-1);
    }

    // 解析IP地址和端口号
    char *ip = argv[1];
    uint16_t port = atoi(argv[2]);

    // 创建socket
    int clientfd = socket(AF_INET, SOCK_STREAM, 0);
    if (clientfd == -1)
    {
        cerr << "创建socket失败" << endl;
        exit(-1);
    }

    g_clientfd = clientfd;

    // 填写sockaddr_in结构体变量
    sockaddr_in server;
    bzero(&server, sizeof(server));
    server.sin_family = AF_INET;
    server.sin_port = htons(port);
    server.sin_addr.s_addr = inet_addr(ip);

    // 连接服务器
    if (connect(clientfd, (sockaddr *)&server, sizeof(server)) == -1)
    {
        cerr << "连接服务器失败" << endl;
        close(clientfd);
        exit(-1);
    }

    cout << "==========欢迎来到聊天室========" << endl;
    cout << "1.登录" << endl;
    cout << "2.注册" << endl;
    cout << "请选择:";

    int choice = 0;
    cin >> choice;
    cin.get(); // 读掉缓冲区残留的回车

    switch (choice)
    {
    case 1: // 登录
        {
            int id;
            char pwd[50] = {0};
            cout << "用户ID:";
            cin >> id;
            cin.get();

            cout << "密码:";
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
                cerr << "发送登录消息失败" << endl;
            }
            else
            {
                // 接收服务器端反馈
                char buffer[BUFFER_SIZE] = {0};
                len = recv(clientfd, buffer, BUFFER_SIZE, 0);
                if (len == -1)
                {
                    cerr << "接收登录响应失败" << endl;
                }
                else
                {
                    // 反序列化
                    chat::LoginResponse response;
                    if (response.ParseFromString(string(buffer, len)))
                    {
                        if (response.errno() != 0)
                        {
                            cerr << "登录失败: " << response.errmsg() << endl;
                        }
                        else
                        {
                            cout << "登录成功！" << endl;
                            g_current_userid = response.user().id();
                            g_current_username = response.user().name();
                            
                            cout << "欢迎, " << g_current_username << "!" << endl;
                            
                            // 显示好友列表
                            if (response.friends_size() > 0)
                            {
                                cout << "----------好友列表----------" << endl;
                                for (int i = 0; i < response.friends_size(); ++i)
                                {
                                    const chat::Friend& friend_info = response.friends(i);
                                    cout << "ID:" << friend_info.id() << " 昵称:" << friend_info.name() 
                                         << " 状态:" << friend_info.state() << endl;
                                }
                            }

                            // 显示群组列表
                            if (response.groups_size() > 0)
                            {
                                cout << "----------群组列表----------" << endl;
                                for (int i = 0; i < response.groups_size(); ++i)
                                {
                                    const chat::Group& group_info = response.groups(i);
                                    cout << "群ID:" << group_info.id() << " 群名:" << group_info.name() 
                                         << " 描述:" << group_info.desc() << endl;
                                }
                            }

                            // 显示离线消息
                            if (response.offline_msgs_size() > 0)
                            {
                                cout << "----------离线消息----------" << endl;
                                for (int i = 0; i < response.offline_msgs_size(); ++i)
                                {
                                    const chat::OfflineMessage& offline_msg = response.offline_msgs(i);
                                    cout << "来自:" << offline_msg.from_id() 
                                         << " 消息:" << offline_msg.message() << endl;
                                }
                            }

                            // 进入主菜单
                            isMainMenuRunning = true;
                            MainMenu(clientfd);
                        }
                    }
                    else
                    {
                        cerr << "登录响应解析失败" << endl;
                    }
                }
            }
        }
        break;
    case 2: // 注册
        {
            char name[50] = {0};
            char pwd[50] = {0};
            cout << "用户名:";
            cin.getline(name, 50);

            cout << "密码:";
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
                cerr << "发送注册消息失败" << endl;
            }
            else
            {
                // 接收服务器端反馈
                char buffer[BUFFER_SIZE] = {0};
                len = recv(clientfd, buffer, BUFFER_SIZE, 0);
                if (len == -1)
                {
                    cerr << "接收注册响应失败" << endl;
                }
                else
                {
                    // 反序列化
                    chat::RegisterResponse response;
                    if (response.ParseFromString(string(buffer, len)))
                    {
                        if (response.errno() != 0)
                        {
                            cerr << "注册失败: " << response.errmsg() << endl;
                        }
                        else
                        {
                            cout << "注册成功！您的用户ID是: " << response.id() << endl;
                            cout << "请记住您的ID，用于登录" << endl;
                        }
                    }
                    else
                    {
                        cerr << "注册响应解析失败" << endl;
                    }
                }
            }
        }
        break;
    default:
        cerr << "无效的选择" << endl;
        break;
    }

    close(clientfd);
    return 0;
}
