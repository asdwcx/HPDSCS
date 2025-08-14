#include <iostream>
#include <thread>
#include <string>
#include <vector>
#include <chrono>
#include <ctime>
#include <algorithm>  // 添加algorithm头文件支持sort
#include <fstream>
#include <sstream>
#include <iomanip>
#include <filesystem>  // 添加filesystem头文件支持目录操作

#include <unistd.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include <openssl/md5.h>

#include "json.hpp"

#include "Group.hpp"
#include "User.hpp"
#include "public.hpp"
#include "../../include/Base64Utils.hpp"

using namespace std;
using json = nlohmann::json;

#define PASSWOED_LENGTH 50
#define NAME_LENGTH 50
#define BUFFER_SIZE 1024

//记录当前系统登录的用户信息
User g_current_user;
//记录当前登录用户的好友列表信息
vector<User> g_current_friends_list;
//记录当前登录用户的群组列表信息
vector<Group> g_current_group_list;

// 文件下载状态变量
struct DownloadSession {
    string file_id;
    string file_name;
    int file_size;
    int total_chunks;
    int chunk_size;
    vector<vector<char>> chunks;
    vector<bool> received_chunks;
    int received_count;
    bool is_downloading;
    
    DownloadSession() : file_size(0), total_chunks(0), chunk_size(0), 
                       received_count(0), is_downloading(false) {}
};

DownloadSession g_download_session;

//显示当前登录成功用户的基本信息
void ShowCurrentUserData();
//接收用户收到消息的线程
void ReadTaskHandler(int client_fd);
//获取系统时间
string GetCurrentTime();
//主聊天页面程序
void MainMenu(int client_fd);

// "help" command handler
void Help(int fd = 0, string str = "");
// "chat" command handler
void Chat(int, string);
// "addfriend" command handler
void AddFriend(int, string);
// "creategroup" command handler
void CreateGroup(int, string);
// "addgroup" command handler
void AddGroup(int, string);
// "approvejoin" command handler
void ApproveJoin(int, string);
// "groupchat" command handler
void GroupChat(int, string);
// "grouphistory" command handler
void GroupHistory(int, string);
// "searchgroup" command handler
void SearchGroup(int, string);
// "groupinfo" command handler
void GroupInfo(int, string);
// "quitgroup" command handler
void QuitGroup(int, string);
// "chathistory" command handler - 一对一聊天历史查询
void ChatHistory(int, string);
// "chatsearch" command handler - 一对一聊天消息搜索
void ChatSearch(int, string);
// "unreadcount" command handler - 未读消息数量查询
void UnreadCount(int, string);
// "conversations" command handler - 会话列表查询
void Conversations(int, string);
// "updatelocation" command handler - 更新位置
void UpdateLocation(int, string);
// "findnearby" command handler - 查找附近的人
void FindNearby(int, string);
// "setvisibility" command handler - 设置位置可见性
void SetLocationVisibility(int, string);
// "getlocation" command handler - 获取位置信息
void GetLocation(int, string);
// "sendfile" command handler - 发送文件
void SendFile(int, string);
// "downloadfile" command handler - 下载文件
void DownloadFile(int, string);
// "listfiles" command handler - 查看文件列表
void ListFiles(int, string);
// 处理分片下载响应
void HandleChunkDownloadResponse(const json& js, int clientfd);
// "loginout" command handler
void LoginOut(int, string);

//控制主菜单是否继续显示
bool g_is_menu_running = false;
// 系统支持的客户端命令列表
unordered_map<string, string> command_map = {
    {"help", "显示所有支持的命令，格式help"},
    {"chat", "一对一聊天，格式chat:friendid:message"},
    {"addfriend", "添加好友，格式addfriend:friendid"},
    {"creategroup", "创建群组，格式creategroup:groupname:groupdesc"},
    {"addgroup", "加入群组，格式addgroup:groupid"},
    {"approvejoin", "批准加入群组，格式approvejoin:groupid:userid:approve(1同意/0拒绝)"},
    {"groupchat", "群聊，格式groupchat:groupid:message"},
    {"grouphistory", "查看群聊历史，格式grouphistory:groupid:count"},
    {"searchgroup", "搜索群消息，格式searchgroup:groupid:keyword"},
    {"groupinfo", "查看群信息，格式groupinfo:groupid"},
    {"quitgroup", "退出群聊，格式quitgroup:groupid"},
    {"chathistory", "查看一对一聊天历史，格式chathistory:friendid:count"},
    {"chatsearch", "搜索一对一聊天消息，格式chatsearch:friendid:keyword"},
    {"unreadcount", "查看未读消息数量，格式unreadcount或unreadcount:friendid"},
    {"conversations", "查看会话列表，格式conversations"},
    {"updatelocation", "更新位置，格式updatelocation:latitude:longitude:location_name"},
    {"findnearby", "查找附近的人，格式findnearby:latitude:longitude:radius"},
    {"setvisibility", "设置位置可见性，格式setvisibility:visible(1显示/0隐藏)"},
    {"getlocation", "获取位置信息，格式getlocation或getlocation:userid"},
    {"sendfile", "发送文件，格式sendfile:filepath:receiver_id或sendfile:filepath:group:group_id"},
    {"downloadfile", "下载文件，格式downloadfile:file_id"},
    {"listfiles", "查看文件列表，格式listfiles或listfiles:group:group_id"},
    {"loginout", "注销，格式loginout"}};

// 注册系统支持的客户端命令处理
unordered_map<string, function<void(int, string)>> command_handler_map = {
    {"help", Help},
    {"chat", Chat},
    {"addfriend", AddFriend},
    {"creategroup", CreateGroup},
    {"addgroup", AddGroup},
    {"approvejoin", ApproveJoin},
    {"groupchat", GroupChat},
    {"grouphistory", GroupHistory},
    {"searchgroup", SearchGroup},
    {"groupinfo", GroupInfo},
    {"quitgroup", QuitGroup},
    {"chathistory", ChatHistory},
    {"chatsearch", ChatSearch},
    {"unreadcount", UnreadCount},
    {"conversations", Conversations},
    {"updatelocation", UpdateLocation},
    {"findnearby", FindNearby},
    {"setvisibility", SetLocationVisibility},
    {"getlocation", GetLocation},
    {"sendfile", SendFile},
    {"downloadfile", DownloadFile},
    {"listfiles", ListFiles},
    {"loginout", LoginOut}};

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

//main函数主要获取用户的输入，接收线程则是将用户收到的信息打印出来
//集成登录、注册功能
int main(int argc, char **argv)
{
    if (argc < 3)
    {
        cerr << "command invaild example: ./ExeNAME  IpAddress  port" << endl;
        exit(-1);
    }

    //解析IP地址和端口号
    char *ip = argv[1];
    uint16_t port = atoi(argv[2]);

    //创建socket
    int clientfd = socket(AF_INET, SOCK_STREAM, 0);
    if (clientfd == -1)
    {
        cerr << "create socket error" << endl;
        exit(-1);
    }

    //录入连接server服务器信息
    sockaddr_in server;
    memset(&server, 0, sizeof(sockaddr_in));

    server.sin_family = AF_INET;
    server.sin_port = htons(port);
    server.sin_addr.s_addr = inet_addr(ip);

    //server与clientfd连接
    if (-1 == connect(clientfd, (sockaddr *)&server, sizeof(sockaddr_in)))
    {
        cout << "connect error" << endl;
        close(clientfd);
        exit(-1);
    }

    //主业务
    for (;;)
    {
        //显示首页面菜单 登录，注册，退出
        cout << "**********welcome**********" << endl;
        cout << "           1.  login" << endl;
        cout << "           2.  register" << endl;
        cout << "           3.  quit " << endl;
        cout << "please input your choice:";
        int choice = 0;
        cin >> choice;
        //处理读入 缓冲区的回车
        cin.get();
        switch (choice)
        {
        case 1:
        {
            int id = 0;
            char pwd[PASSWOED_LENGTH] = {0};
            cout << "please input id:";
            cin >> id;
            cin.get();

            cout << "please input password:";
            cin.getline(pwd, PASSWOED_LENGTH);

            json js;
            js["msgid"] = LOGIN_MSG;
            js["id"] = id;
            js["password"] = pwd;
            string request = js.dump();

            //发送
            int len = send(clientfd, request.c_str(), strlen(request.c_str()) + 1, 0);
            if (len == -1)
            {
                cerr << "send reg msg error" << endl;
            }
            else
            {
                //接收服务器端反馈
                char buffer[BUFFER_SIZE] = {0};
                len = recv(clientfd, buffer, BUFFER_SIZE, 0);
                if (len == -1)
                {
                    cerr << "recv login response error" << endl;
                }
                else
                {
                    //反序列化
                    json response_js = json::parse(buffer);
                    //登录失败
                    if (response_js["errno"].get<int>() != 0)
                    {
                        cerr << response_js["errmsg"] << endl;
                    }
                    else
                    {
                        //登陆成功记录当前用户信息、好友信息、群组信息、离线消息
                        //记录当前用户信息
                        g_current_user.set_id(response_js["id"].get<int>());
                        g_current_user.set_name(response_js["name"]);

                        //记录当前用户的好友信息
                        if (response_js.contains("friends"))
                        {
                            vector<string> vec = response_js["friends"];
                            for (string &str : vec)
                            {
                                json friend_js = json::parse(str);
                                User user;
                                user.set_id(friend_js["id"].get<int>());
                                user.set_name(friend_js["name"]);
                                user.set_state(friend_js["state"]);
                                g_current_friends_list.push_back(user);
                            }
                        }

                        //群组信息
                        if (response_js.contains("groups"))
                        {
                            vector<string> vec = response_js["groups"];
                            for (string &str : vec)
                            {
                                json group_js = json::parse(str);
                                Group group;
                                group.set_id(group_js["id"].get<int>());
                                group.set_name(group_js["groupname"]);
                                group.set_desc(group_js["groupdesc"]);

                                vector<string> vec2 = group_js["users"];
                                for (string &user_str : vec2)
                                {
                                    GroupUser group_user;
                                    json group_user_js = json::parse(user_str);
                                    group_user.set_id(group_user_js["id"].get<int>());
                                    group_user.set_name(group_user_js["name"]);
                                    group_user.set_state(group_user_js["state"]);
                                    group_user.set_role(group_user_js["role"]);

                                    group.get_User().push_back(group_user);
                                }
                                g_current_group_list.push_back(group);
                            }
                        }

                        //显示登录用户的基本信息
                        ShowCurrentUserData();

                        //显示用户的离线消息
                        if (response_js.contains("offlinemsg"))
                        {
                            vector<string> vec = response_js["offlinemsg"];
                            for (string &str : vec)
                            {
                                json js = json::parse(str);
                                cout << js["time"].get<string>() << "[" << js["id"] << "]" << js["name"].get<string>() << " said: " << js["msg"].get<string>() << endl;
                            }
                        }

                        //登录成功
                        thread read_task(ReadTaskHandler, clientfd);
                        read_task.detach();

                        //进入主界面
                        g_is_menu_running = true;
                        MainMenu(clientfd);
                    }
                }
            }
        }
        break;
        case 2:
        {
            //注册
            char name[NAME_LENGTH] = {0};
            char pwd[PASSWOED_LENGTH] = {0};
            cout << "user name:" << endl;
            cin.getline(name, NAME_LENGTH);

            cout << "password:" << endl;
            cin.getline(pwd, PASSWOED_LENGTH);

            //序列化
            json js;
            js["msgid"] = REG_MSG;
            js["name"] = name;
            js["password"] = pwd;
            string request = js.dump();

            //发送
            int len = send(clientfd, request.c_str(), strlen(request.c_str()) + 1, 0);
            if (len == -1)
            {
                cerr << "send reg msg error" << endl;
            }
            else
            {
                //接收服务器端反馈
                char buffer[BUFFER_SIZE] = {0};
                len = recv(clientfd, buffer, BUFFER_SIZE, 0);
                if (len == -1)
                {
                    cerr << "recv register response error" << endl;
                }
                else
                {
                    cout << "lens: " << len << "buffer :" << buffer << endl;
                    //反序列化
                    json response_js = json::parse(buffer);
                    //注册失败
                    if (response_js["errno"].get<int>() != 0)
                    {
                        cerr << name << " is already exist,register error!" << endl;
                    }
                    else
                    {
                        //注册成功
                        cout << name << "register success , and user id: " << response_js["id"] << " , please remeber it!!!" << endl;
                    }
                }
            }
        }
        break;
        case 3:
        {
            //退出
            close(clientfd);
            exit(0);
        }
        default:
            cerr << "invaild input!" << endl;
            break;
        }
    }
}

//显示当前登录成功用户的基本信息
void ShowCurrentUserData()
{
    cout << "--------------------login user--------------------" << endl;
    cout << "current login uer => id: " << g_current_user.get_id() << " name: " << g_current_user.get_name() << endl;
    cout << "-------------------friend  list-------------------" << endl;
    if (!g_current_friends_list.empty())
    {
        for (User &user : g_current_friends_list)
        {
            cout << user.get_id() << " " << user.get_name() << " " << user.get_state() << endl;
        }
    }
    cout << "--------------------group list--------------------" << endl;
    if (!g_current_group_list.empty())
    {
        for (Group &group : g_current_group_list)
        {
            cout << group.get_id() << " " << group.get_name() << " " << group.get_desc() << endl;

            //打印群员信息
            cout << "========group user========" << endl;
            for (GroupUser &group_user : group.get_User())
            {
                cout << group_user.get_id() << " " << group_user.get_name() << " " << group_user.get_state() << " " << group_user.get_role() << endl;
            }
        }
    }
    cout << "--------------------------------------------------" << endl;
}

//接收用户收到消息的线程
void ReadTaskHandler(int client_fd)
{
    for (;;)
    {
        char buffer[BUFFER_SIZE] = {0};
        int len = recv(client_fd, buffer, BUFFER_SIZE, 0);
        if (len == -1 || len == 0)
        {
            close(client_fd);
            exit(-1);
        }

        json js = json::parse(buffer);
        int msgid = js["msgid"].get<int>();
        
        //一对一聊天
        if (msgid == ONE_CHAT_MSG)
        {
            // 检查是否为错误响应
            if (js.count("errno") > 0)
            {
                int errno_val = js["errno"].get<int>();
                if (errno_val != 0)
                {
                    cout << "❌ 消息发送失败: " << js["errmsg"].get<string>() << endl;
                    continue;
                }
            }
            
            // 正常消息显示
            cout << js["time"].get<string>() << " [" << js["id"] << "] " << js["name"].get<string>() << ": " << js["msg"].get<string>() << endl;
            continue;
        }
        else if (msgid == GROUP_CHAT_MSG)
        {
            cout << "group msg: [" << js["groupid"] << "]";
            cout << js["time"].get<string>() << "[" << js["id"] << "]" << js["name"].get<string>() << " said: " << js["msg"].get<string>();
            
            // 如果消息包含sequence_id，显示序列号（调试用）
            if (js.count("sequence_id")) {
                cout << " [seq:" << js["sequence_id"].get<uint64_t>() << "]";
            }
            cout << endl;
            continue;
        }
        //加入群组通知(给管理员)
        else if (msgid == JOIN_GROUP_NOTIFY)
        {
            cout << "[系统通知] 用户 " << js["user_name"].get<string>() << "(ID:" << js["user_id"] << ") 申请加入群组 " 
                 << js["group_name"].get<string>() << "(ID:" << js["group_id"] << ")" << endl;
            cout << "使用命令 approvejoin:" << js["group_id"] << ":" << js["user_id"] << ":1 同意，或 approvejoin:" 
                 << js["group_id"] << ":" << js["user_id"] << ":0 拒绝" << endl;
            continue;
        }
        //加入群组结果通知(给申请者)
        else if (msgid == JOIN_GROUP_RESULT)
        {
            bool approved = js["approved"].get<bool>();
            string group_name = js["group_name"].get<string>();
            string admin_name = js["admin_name"].get<string>();
            
            if (approved)
            {
                cout << "[系统通知] 您的加入群组 " << group_name << " 的申请已被管理员 " 
                     << admin_name << " 批准！" << endl;
            }
            else
            {
                cout << "[系统通知] 您的加入群组 " << group_name << " 的申请被管理员 " 
                     << admin_name << " 拒绝。" << endl;
            }
            continue;
        }
        //群组通知(有新成员加入)
        else if (msgid == GROUP_NOTIFY)
        {
            cout << "[群组通知] " << js["user_name"].get<string>() << " 加入了群组 " 
                 << js["group_name"].get<string>() << endl;
            continue;
        }
        //群聊历史记录响应
        else if (msgid == GROUP_HISTORY_MSG)
        {
            int errno_val = js["errno"].get<int>();
            if (errno_val != 0)
            {
                cout << "[错误] " << js["errmsg"].get<string>() << endl;
                continue;
            }
            
            cout << "=== 群聊历史记录 ===" << endl;
            auto messages = js["messages"];
            
            // 如果消息包含sequence_id，按sequence_id排序确保正确时序
            vector<json> sorted_messages;
            for (auto &msg : messages) {
                sorted_messages.push_back(msg);
            }
            
            // 按sequence_id升序排序（如果存在），否则按时间排序
            sort(sorted_messages.begin(), sorted_messages.end(), [](const json &a, const json &b) {
                if (a.count("sequence_id") && b.count("sequence_id")) {
                    return a["sequence_id"].get<uint64_t>() < b["sequence_id"].get<uint64_t>();
                } else {
                    return a["time"].get<string>() < b["time"].get<string>();
                }
            });
            
            for (auto &msg : sorted_messages)
            {
                string role_icon = "";
                string role = msg["user_role"].get<string>();
                if (role == "creator") role_icon = "👑";
                else if (role == "admin") role_icon = "⭐";
                
                cout << "[" << msg["time"].get<string>() << "] " 
                     << msg["user_name"].get<string>() << role_icon << ": " 
                     << msg["content"].get<string>();
                
                // 如果有sequence_id，显示序列号（调试用）
                if (msg.count("sequence_id")) {
                    cout << " [seq:" << msg["sequence_id"].get<uint64_t>() << "]";
                }
                cout << endl;
            }
            cout << "===================" << endl;
            continue;
        }
        //群消息搜索响应
        else if (msgid == GROUP_SEARCH_MSG)
        {
            int errno_val = js["errno"].get<int>();
            if (errno_val != 0)
            {
                cout << "[错误] " << js["errmsg"].get<string>() << endl;
                continue;
            }
            
            string keyword = js["keyword"].get<string>();
            cout << "=== 搜索结果 (关键词: \"" << keyword << "\") ===" << endl;
            auto results = js["results"];
            if (results.empty())
            {
                cout << "未找到相关消息" << endl;
            }
            else
            {
                for (auto &msg : results)
                {
                    string role_icon = "";
                    string role = msg["user_role"].get<string>();
                    if (role == "creator") role_icon = "👑";
                    else if (role == "admin") role_icon = "⭐";
                    
                    cout << "[" << msg["time"].get<string>() << "] " 
                         << msg["user_name"].get<string>() << role_icon << ": " 
                         << msg["content"].get<string>() << endl;
                }
                cout << "共找到 " << results.size() << " 条相关消息" << endl;
            }
            cout << "================================" << endl;
            continue;
        }
        //群信息查询响应
        else if (msgid == GROUP_INFO_MSG)
        {
            int errno_val = js["errno"].get<int>();
            if (errno_val != 0)
            {
                cout << "[错误] " << js["errmsg"].get<string>() << endl;
                continue;
            }
            
            auto group_info = js["group_info"];
            auto members = js["members"];
            
            cout << "┌─────────────────────────────────────────────────────┐" << endl;
            cout << "│ 群信息: " << group_info["name"].get<string>() << " (ID:" << group_info["id"].get<int>() << ")" << endl;
            cout << "├─────────────────────────────────────────────────────┤" << endl;
            cout << "│ 群描述: " << group_info["desc"].get<string>() << endl;
            cout << "│ 成员列表 (" << members.size() << "人):" << endl;
            
            for (auto &member : members)
            {
                string role_icon = "";
                string role = member["role"].get<string>();
                if (role == "creator") role_icon = "👑";
                else if (role == "admin") role_icon = "⭐";
                
                string state_icon = member["state"].get<string>() == "online" ? "🟢" : "🔴";
                cout << "│ " << state_icon << " " << member["user_name"].get<string>() << role_icon;
                if (member["user_id"].get<int>() == g_current_user.get_id())
                {
                    cout << " (我)";
                }
                cout << endl;
            }
            cout << "└─────────────────────────────────────────────────────┘" << endl;
            continue;
        }
        //退出群聊响应
        else if (msgid == QUIT_GROUP_MSG)
        {
            int errno_val = js["errno"].get<int>();
            if (errno_val == 0)
            {
                cout << "[系统通知] " << js["errmsg"].get<string>() << endl;
            }
            else
            {
                cout << "[错误] " << js["errmsg"].get<string>() << endl;
            }
            continue;
        }
        //一对一聊天历史响应
        else if (msgid == PRIVATE_HISTORY_MSG)
        {
            int errno_val = js["errno"].get<int>();
            if (errno_val == 0)
            {
                cout << "\n========== 聊天历史记录 ==========" << endl;
                cout << "好友ID: " << js["target_id"].get<int>() << endl;
                cout << "消息数量: " << js["count"].get<int>() << endl;
                cout << "=================================" << endl;
                
                auto messages = js["messages"];
                for (auto& msg : messages)
                {
                    cout << "[" << msg["time"].get<string>() << "] "
                         << msg["from_name"].get<string>() << "(" << msg["from_id"].get<int>() << "): "
                         << msg["message"].get<string>() << endl;
                }
                cout << "=================================" << endl;
            }
            else
            {
                cout << "[错误] " << js["errmsg"].get<string>() << endl;
            }
            continue;
        }
        //一对一聊天搜索响应
        else if (msgid == PRIVATE_SEARCH_MSG)
        {
            int errno_val = js["errno"].get<int>();
            if (errno_val == 0)
            {
                cout << "\n========== 搜索结果 ==========" << endl;
                cout << "好友ID: " << js["target_id"].get<int>() << endl;
                cout << "关键词: " << js["keyword"].get<string>() << endl;
                cout << "结果数量: " << js["count"].get<int>() << endl;
                cout << "=============================" << endl;
                
                auto messages = js["messages"];
                for (auto& msg : messages)
                {
                    cout << "[" << msg["time"].get<string>() << "] "
                         << msg["from_name"].get<string>() << "(" << msg["from_id"].get<int>() << "): "
                         << msg["message"].get<string>() << endl;
                }
                cout << "=============================" << endl;
            }
            else
            {
                cout << "[错误] " << js["errmsg"].get<string>() << endl;
            }
            continue;
        }
        //未读消息数量响应
        else if (msgid == PRIVATE_UNREAD_COUNT_MSG)
        {
            int errno_val = js["errno"].get<int>();
            if (errno_val == 0)
            {
                int unread_count = js["unread_count"].get<int>();
                if (js.contains("from_id"))
                {
                    cout << "[未读消息] 来自好友 " << js["from_id"].get<int>() 
                         << " 的未读消息: " << unread_count << " 条" << endl;
                }
                else
                {
                    cout << "[未读消息] 总未读消息: " << unread_count << " 条" << endl;
                }
            }
            else
            {
                cout << "[错误] " << js["errmsg"].get<string>() << endl;
            }
            continue;
        }
        //会话列表响应
        else if (msgid == CONVERSATION_LIST_MSG)
        {
            int errno_val = js["errno"].get<int>();
            if (errno_val == 0)
            {
                cout << "\n========== 会话列表 ==========" << endl;
                cout << "会话数量: " << js["count"].get<int>() << endl;
                cout << "=============================" << endl;
                
                auto conversations = js["conversations"];
                for (auto& conv : conversations)
                {
                    string state_icon = (conv["user_state"].get<string>() == "online") ? "🟢" : "🔴";
                    int unread = conv["unread_count"].get<int>();
                    string unread_info = (unread > 0) ? " [未读:" + to_string(unread) + "]" : "";
                    
                    cout << state_icon << " " << conv["user_name"].get<string>() 
                         << "(" << conv["user_id"].get<int>() << ")" << unread_info << endl;
                    cout << "   最后消息: " << conv["last_message"].get<string>() << endl;
                    cout << "   时间: " << conv["last_time"].get<string>() << endl;
                    cout << "-----------------------------" << endl;
                }
                cout << "=============================" << endl;
            }
            else
            {
                cout << "[错误] " << js["errmsg"].get<string>() << endl;
            }
            continue;
        }
        //位置更新响应
        else if (msgid == UPDATE_LOCATION_MSG)
        {
            int errno_val = js["errno"].get<int>();
            if (errno_val == 0)
            {
                cout << "✅ " << js["errmsg"].get<string>() << endl;
                cout << "📍 位置: " << js["latitude"].get<double>() << ", " << js["longitude"].get<double>() << endl;
                if (js.count("location_name") && !js["location_name"].get<string>().empty())
                {
                    cout << "📍 地点: " << js["location_name"].get<string>() << endl;
                }
            }
            else
            {
                cout << "❌ " << js["errmsg"].get<string>() << endl;
            }
            continue;
        }
        //查找附近的人响应
        else if (msgid == FIND_NEARBY_MSG)
        {
            int errno_val = js["errno"].get<int>();
            if (errno_val == 0)
            {
                cout << "\n========== 附近的人 ==========" << endl;
                cout << "搜索位置: " << js["search_latitude"].get<double>() << ", " << js["search_longitude"].get<double>() << endl;
                cout << "搜索半径: " << js["search_radius"].get<int>() << "米" << endl;
                cout << "找到 " << js["count"].get<int>() << " 个用户" << endl;
                cout << "==============================" << endl;
                
                auto nearby_users = js["nearby_users"];
                for (auto& user : nearby_users)
                {
                    string state_icon = (user["state"].get<string>() == "online") ? "🟢" : "🔴";
                    cout << state_icon << " " << user["username"].get<string>() 
                         << "(ID:" << user["userid"].get<int>() << ")" << endl;
                    cout << "   距离: " << user["distance_desc"].get<string>() << endl;
                    if (user.count("location_name") && !user["location_name"].get<string>().empty())
                    {
                        cout << "   地点: " << user["location_name"].get<string>() << endl;
                    }
                    cout << "   更新时间: " << user["last_update"].get<string>() << endl;
                    cout << "------------------------------" << endl;
                }
                cout << "==============================" << endl;
            }
            else
            {
                cout << "❌ " << js["errmsg"].get<string>() << endl;
            }
            continue;
        }
        //位置可见性设置响应
        else if (msgid == SET_LOCATION_VISIBILITY_MSG)
        {
            int errno_val = js["errno"].get<int>();
            if (errno_val == 0)
            {
                cout << "✅ " << js["errmsg"].get<string>() << endl;
            }
            else
            {
                cout << "❌ " << js["errmsg"].get<string>() << endl;
            }
            continue;
        }
        //获取位置信息响应
        else if (msgid == GET_LOCATION_MSG)
        {
            int errno_val = js["errno"].get<int>();
            if (errno_val == 0)
            {
                cout << "========== 位置信息 ==========" << endl;
                cout << "用户ID: " << js["userid"].get<int>() << endl;
                cout << "经纬度: " << js["latitude"].get<double>() << ", " << js["longitude"].get<double>() << endl;
                cout << "可见性: " << (js["is_visible"].get<bool>() ? "显示" : "隐藏") << endl;
                if (js.count("location_name") && !js["location_name"].get<string>().empty())
                {
                    cout << "地点名称: " << js["location_name"].get<string>() << endl;
                }
                cout << "更新时间: " << js["last_update"].get<string>() << endl;
                cout << "=============================" << endl;
            }
            else
            {
                cout << "❌ " << js["errmsg"].get<string>() << endl;
            }
            continue;
        }
        //文件上传响应
        else if (msgid == FILE_UPLOAD_RSP)
        {
            int errno_val = js["errno"].get<int>();
            if (errno_val == 0)
            {
                cout << "✅ " << js["errmsg"].get<string>() << endl;
                g_upload_session_id = js["session_id"].get<string>();
                g_upload_file_id = js["file_id"].get<string>();
                cout << "📁 文件ID: " << g_upload_file_id << endl;
                cout << "📁 会话ID: " << g_upload_session_id << endl;
                cout << "📦 分片大小: " << js["chunk_size"].get<int>() << " bytes" << endl;
            }
            else
            {
                cout << "❌ " << js["errmsg"].get<string>() << endl;
            }
            continue;
        }
        //文件分片响应
        else if (msgid == FILE_CHUNK_MSG)
        {
            int errno_val = js["errno"].get<int>();
            if (errno_val == 0)
            {
                cout << "✅ 分片 " << js["chunk_seq"].get<int>() << " 上传成功" << endl;
            }
            else
            {
                cout << "❌ 分片 " << js["chunk_seq"].get<int>() << " 上传失败: " << js["errmsg"].get<string>() << endl;
            }
            continue;
        }
        //文件下载响应
        else if (msgid == FILE_DOWNLOAD_RSP)
        {
            int errno_val = js["errno"].get<int>();
            if (errno_val == 0)
            {
                // 检查是否为分片下载模式
                if (js.contains("download_type") && js["download_type"].get<string>() == "chunked")
                {
                    // 分片下载模式
                    cout << "========== 开始分片下载 ==========" << endl;
                    
                    string file_id = js["file_id"].get<string>();
                    string file_name = js["file_name"].get<string>();
                    int file_size = js["file_size"].get<int>();
                    int total_chunks = js["total_chunks"].get<int>();
                    int chunk_size = js["chunk_size"].get<int>();
                    
                    cout << "📁 文件名: " << file_name << endl;
                    cout << "📏 文件大小: " << file_size << " bytes" << endl;
                    cout << "📦 总分片数: " << total_chunks << endl;
                    cout << "📐 分片大小: " << chunk_size << " bytes" << endl;
                    cout << "=================================" << endl;
                    
                    // 初始化下载会话
                    g_download_session.file_id = file_id;
                    g_download_session.file_name = file_name;
                    g_download_session.file_size = file_size;
                    g_download_session.total_chunks = total_chunks;
                    g_download_session.chunk_size = chunk_size;
                    g_download_session.chunks.resize(total_chunks);
                    g_download_session.received_chunks.resize(total_chunks, false);
                    g_download_session.received_count = 0;
                    g_download_session.is_downloading = true;
                    
                    // 发送第一个分片请求
                    json chunk_req;
                    chunk_req["msgid"] = FILE_CHUNK_DOWNLOAD_REQ;
                    chunk_req["id"] = g_current_user.get_id();
                    chunk_req["file_id"] = file_id;
                    chunk_req["chunk_seq"] = 1;
                    
                    string request = chunk_req.dump();
                    send(clientfd, request.c_str(), strlen(request.c_str()) + 1, 0);
                    
                    cout << "📤 请求第一个分片..." << endl;
                }
                else
                {
                    // 传统下载模式（兼容性）
                    cout << "========== 文件下载成功 ==========" << endl;
                    string file_name = js["file_name"].get<string>();
                    int file_size = js["file_size"].get<int>();
                    string file_data = js["file_data"].get<string>();
                    
                    // 解码Base64数据
                    vector<char> decoded_data = Base64Utils::decode(file_data);
                    
                    // 保存文件到downloads目录
                    string download_path = "./downloads/" + file_name;
                    
                    // 创建downloads目录
                    system("mkdir downloads 2>nul");
                    
                    ofstream outfile(download_path, ios::binary);
                    if (outfile.is_open())
                    {
                        outfile.write(decoded_data.data(), decoded_data.size());
                        outfile.close();
                        
                        cout << "📁 文件名: " << file_name << endl;
                        cout << "📏 文件大小: " << file_size << " bytes" << endl;
                        cout << "💾 保存路径: " << download_path << endl;
                    }
                    else
                    {
                        cout << "❌ 无法保存文件到: " << download_path << endl;
                    }
                    cout << "=================================" << endl;
                }
            }
            else
            {
                cout << "❌ " << js["errmsg"].get<string>() << endl;
            }
            continue;
        }
        //分片下载响应
        else if (msgid == FILE_CHUNK_DOWNLOAD_RSP)
        {
            HandleChunkDownloadResponse(js, clientfd);
            continue;
        }
        //文件发送通知
        else if (msgid == FILE_SEND_NOTIFY)
        {
            cout << "\n📎 收到文件通知" << endl;
            cout << "📁 文件ID: " << js["file_id"].get<string>() << endl;
            cout << "📁 文件名: " << js["file_name"].get<string>() << endl;
            cout << "📏 文件大小: " << js["file_size"].get<int>() << " bytes" << endl;
            cout << "👤 发送者: " << js["sender_name"].get<string>() << " (ID:" << js["sender_id"].get<int>() << ")" << endl;
            cout << "⏰ 上传时间: " << js["upload_time"].get<string>() << endl;
            cout << "💡 使用命令下载: downloadfile:" << js["file_id"].get<string>() << endl;
            continue;
        }
        //文件列表响应
        else if (msgid == FILE_LIST_RSP)
        {
            int errno_val = js["errno"].get<int>();
            if (errno_val == 0)
            {
                cout << "\n========== 文件列表 ==========" << endl;
                auto files = js["files"];
                if (files.empty())
                {
                    cout << "暂无文件" << endl;
                }
                else
                {
                    for (auto& file : files)
                    {
                        cout << "📁 文件ID: " << file["file_id"].get<string>() << endl;
                        cout << "📄 文件名: " << file["file_name"].get<string>() << endl;
                        cout << "📏 大小: " << file["file_size"].get<int>() << " bytes" << endl;
                        cout << "👤 上传者: " << file["uploader_name"].get<string>() << endl;
                        cout << "⏰ 上传时间: " << file["upload_time"].get<string>() << endl;
                        cout << "🔒 哈希: " << file["file_hash"].get<string>() << endl;
                        cout << "------------------------------" << endl;
                    }
                    cout << "共 " << files.size() << " 个文件" << endl;
                }
                cout << "=============================" << endl;
            }
            else
            {
                cout << "❌ " << js["errmsg"].get<string>() << endl;
            }
            continue;
        }
    }
}

//获取系统时间
string GetCurrentTime()
{
    auto tt = chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    struct tm *ptm = localtime(&tt);
    char date[60] = {0};
    sprintf(date, "%d-%02d-%02d %02d:%02d:%02d",
            (int)ptm->tm_year + 1900, (int)ptm->tm_mon + 1, (int)ptm->tm_mday,
            (int)ptm->tm_hour, (int)ptm->tm_min, (int)ptm->tm_sec);
    return string(date);
}

//主聊天页面程序，开闭原则
void MainMenu(int clientfd)
{
    Help();
    char buffer[BUFFER_SIZE] = {0};
    while (g_is_menu_running)
    {
        cin.getline(buffer, BUFFER_SIZE);
        string command_buf(buffer);
        //存储命令
        string command;
        int index = command_buf.find(":");
        if (index == -1)
        {
            //help或者loginout
            command = command_buf;
        }
        else
        {
            //其他命令
            command = command_buf.substr(0, index);
        }

        auto it = command_handler_map.find(command);
        if (it == command_handler_map.end())
        {
            cerr << "invaild input command" << endl;
            continue;
        }

        //调用命令
        it->second(clientfd, command_buf.substr(index + 1, command_buf.size() - index));
    }
}

//打印系统支持的所有命令
void Help(int, string)
{
    cout << "--------command list--------" << endl;
    for (auto &it : command_map)
    {
        cout << it.first << " : " << it.second << endl;
    }
    cout << endl;
}

//一对一聊天
void Chat(int clientfd, string str)
{
    int index = str.find(":");
    if (index == -1)
    {
        cerr << "chat command invaild" << endl;
    }

    int friend_id = atoi(str.substr(0, index).c_str());
    string message = str.substr(index + 1, str.size() - index);

    json js;
    js["msgid"] = ONE_CHAT_MSG;
    js["id"] = g_current_user.get_id();
    js["name"] = g_current_user.get_name();
    js["to"] = friend_id;
    js["msg"] = message;
    js["time"] = GetCurrentTime();

    string request = js.dump();

    int len = send(clientfd, request.c_str(), strlen(request.c_str()) + 1, 0);
    if (len == -1)
    {
        cerr << "send chat msg error" << endl;
    }
}

//添加好友
void AddFriend(int clientfd, string str)
{
    int friend_id = atoi(str.c_str());

    json js;
    js["msgid"] = ADD_FRIEND_MSG;
    js["id"] = g_current_user.get_id();
    js["friendid"] = friend_id;

    string request = js.dump();

    int len = send(clientfd, request.c_str(), strlen(request.c_str()) + 1, 0);
    if (len == -1)
    {
        cerr << "send addfriend msg error" << endl;
    }
}

//创建群聊
void CreateGroup(int clientfd, string str)
{
    int index = str.find(":");
    if (index == -1)
    {
        cerr << "creategroup command invalid!" << endl;
        return;
    }

    string group_name = str.substr(0, index);
    string group_desc = str.substr(index + 1, str.size() - index);

    json js;
    js["msgid"] = CREATE_GROUP_MSG;
    js["id"] = g_current_user.get_id();
    js["groupname"] = group_name;
    js["groupdesc"] = group_desc;

    string request = js.dump();

    int len = send(clientfd, request.c_str(), strlen(request.c_str()) + 1, 0);
    if (len == -1)
    {
        cerr << "send creategroup msg error" << endl;
    }
}

//加入群聊
void AddGroup(int clientfd, string str)
{
    int group_id = atoi(str.c_str());

    json js;
    js["msgid"] = JOIN_GROUP_MSG;
    js["id"] = g_current_user.get_id();
    js["group_id"] = group_id;

    string request = js.dump();

    int len = send(clientfd, request.c_str(), strlen(request.c_str()) + 1, 0);
    if (len == -1)
    {
        cerr << "send joingroup msg error" << endl;
    }
}

//批准加入群组
void ApproveJoin(int clientfd, string str)
{
    int first_index = str.find(":");
    if (first_index == -1)
    {
        cerr << "approvejoin command invalid! Format: approvejoin:groupid:userid:approve" << endl;
        return;
    }
    
    int second_index = str.find(":", first_index + 1);
    if (second_index == -1)
    {
        cerr << "approvejoin command invalid! Format: approvejoin:groupid:userid:approve" << endl;
        return;
    }
    
    int group_id = atoi(str.substr(0, first_index).c_str());
    int user_id = atoi(str.substr(first_index + 1, second_index - first_index - 1).c_str());
    int approve = atoi(str.substr(second_index + 1).c_str());
    
    json js;
    js["msgid"] = APPROVE_JOIN_MSG;
    js["admin_id"] = g_current_user.get_id();
    js["group_id"] = group_id;
    js["user_id"] = user_id;
    js["approve"] = (approve == 1);
    
    string request = js.dump();
    
    int len = send(clientfd, request.c_str(), strlen(request.c_str()) + 1, 0);
    if (len == -1)
    {
        cerr << "send approvejoin msg error" << endl;
    }
}

//群聊消息
void GroupChat(int clientfd, string str)
{
    int index = str.find(":");
    if (index == -1)
    {
        cerr << "creategroup command invalid!" << endl;
        return;
    }

    int group_id = atoi(str.substr(0, index).c_str());
    string message = str.substr(index + 1, str.size() - index);

    json js;
    js["msgid"] = GROUP_CHAT_MSG;
    js["id"] = g_current_user.get_id();
    js["name"] = g_current_user.get_name();
    js["groupid"] = group_id;
    js["msg"] = message;
    js["time"] = GetCurrentTime();

    string request = js.dump();

    int len = send(clientfd, request.c_str(), strlen(request.c_str()) + 1, 0);
    if (len == -1)
    {
        cerr << "send grouochat msg error" << endl;
    }
}

//注销
void LoginOut(int clientfd, string)
{
    json js;
    js["msgid"] = LOGINOUT_MSG;
    js["id"] = g_current_user.get_id();
    string buffer = js.dump();

    string request = js.dump();

    int len = send(clientfd, request.c_str(), strlen(request.c_str()) + 1, 0);
    if (len == -1)
    {
        cerr << "send grouochat msg error" << endl;
    }
    else
    {
        g_is_menu_running = false;
        g_current_friends_list.clear();
        g_current_group_list.clear();
    }
}

//查看群聊历史记录
void GroupHistory(int clientfd, string str)
{
    int index = str.find(":");
    if (index == -1)
    {
        cerr << "grouphistory command invalid! Format: grouphistory:groupid:count" << endl;
        return;
    }
    
    int group_id = atoi(str.substr(0, index).c_str());
    int count = atoi(str.substr(index + 1).c_str());
    
    json js;
    js["msgid"] = GROUP_HISTORY_MSG;
    js["user_id"] = g_current_user.get_id();
    js["group_id"] = group_id;
    js["count"] = count;
    
    string request = js.dump();
    
    int len = send(clientfd, request.c_str(), strlen(request.c_str()) + 1, 0);
    if (len == -1)
    {
        cerr << "send grouphistory msg error" << endl;
    }
}

//搜索群消息
void SearchGroup(int clientfd, string str)
{
    int index = str.find(":");
    if (index == -1)
    {
        cerr << "searchgroup command invalid! Format: searchgroup:groupid:keyword" << endl;
        return;
    }
    
    int group_id = atoi(str.substr(0, index).c_str());
    string keyword = str.substr(index + 1);
    
    json js;
    js["msgid"] = GROUP_SEARCH_MSG;
    js["user_id"] = g_current_user.get_id();
    js["group_id"] = group_id;
    js["keyword"] = keyword;
    js["limit"] = 50;
    
    string request = js.dump();
    
    int len = send(clientfd, request.c_str(), strlen(request.c_str()) + 1, 0);
    if (len == -1)
    {
        cerr << "send searchgroup msg error" << endl;
    }
}

//查看群信息
void GroupInfo(int clientfd, string str)
{
    int group_id = atoi(str.c_str());
    
    json js;
    js["msgid"] = GROUP_INFO_MSG;
    js["user_id"] = g_current_user.get_id();
    js["group_id"] = group_id;
    
    string request = js.dump();
    
    int len = send(clientfd, request.c_str(), strlen(request.c_str()) + 1, 0);
    if (len == -1)
    {
        cerr << "send groupinfo msg error" << endl;
    }
}

//退出群聊
void QuitGroup(int clientfd, string str)
{
    int group_id = atoi(str.c_str());
    
    json js;
    js["msgid"] = QUIT_GROUP_MSG;
    js["user_id"] = g_current_user.get_id();
    js["group_id"] = group_id;
    
    string request = js.dump();
    
    int len = send(clientfd, request.c_str(), strlen(request.c_str()) + 1, 0);
    if (len == -1)
    {
        cerr << "send quitgroup msg error" << endl;
    }
}

//查看一对一聊天历史
void ChatHistory(int clientfd, string str)
{
    int index = str.find(":");
    if (index == -1)
    {
        cerr << "chathistory command invalid! Usage: chathistory:friendid:count" << endl;
        return;
    }

    int friend_id = atoi(str.substr(0, index).c_str());
    int count = 20; // 默认20条
    if (index < str.size() - 1)
    {
        count = atoi(str.substr(index + 1).c_str());
        if (count <= 0) count = 20;
    }

    json js;
    js["msgid"] = PRIVATE_HISTORY_MSG;
    js["id"] = g_current_user.get_id();
    js["target_id"] = friend_id;
    js["count"] = count;

    string request = js.dump();

    int len = send(clientfd, request.c_str(), strlen(request.c_str()) + 1, 0);
    if (len == -1)
    {
        cerr << "send chathistory msg error" << endl;
    }
}

//搜索一对一聊天消息
void ChatSearch(int clientfd, string str)
{
    int index = str.find(":");
    if (index == -1)
    {
        cerr << "chatsearch command invalid! Usage: chatsearch:friendid:keyword" << endl;
        return;
    }

    int friend_id = atoi(str.substr(0, index).c_str());
    string keyword = str.substr(index + 1);

    if (keyword.empty())
    {
        cerr << "search keyword cannot be empty!" << endl;
        return;
    }

    json js;
    js["msgid"] = PRIVATE_SEARCH_MSG;
    js["id"] = g_current_user.get_id();
    js["target_id"] = friend_id;
    js["keyword"] = keyword;

    string request = js.dump();

    int len = send(clientfd, request.c_str(), strlen(request.c_str()) + 1, 0);
    if (len == -1)
    {
        cerr << "send chatsearch msg error" << endl;
    }
}

//查看未读消息数量
void UnreadCount(int clientfd, string str)
{
    json js;
    js["msgid"] = PRIVATE_UNREAD_COUNT_MSG;
    js["id"] = g_current_user.get_id();
    
    if (!str.empty())
    {
        int from_id = atoi(str.c_str());
        js["from_id"] = from_id;
    }

    string request = js.dump();

    int len = send(clientfd, request.c_str(), strlen(request.c_str()) + 1, 0);
    if (len == -1)
    {
        cerr << "send unreadcount msg error" << endl;
    }
}

//查看会话列表
void Conversations(int clientfd, string str)
{
    json js;
    js["msgid"] = CONVERSATION_LIST_MSG;
    js["id"] = g_current_user.get_id();
    
    int limit = 20; // 默认20个会话
    if (!str.empty())
    {
        limit = atoi(str.c_str());
        if (limit <= 0) limit = 20;
    }
    js["limit"] = limit;

    string request = js.dump();

    int len = send(clientfd, request.c_str(), strlen(request.c_str()) + 1, 0);
    if (len == -1)
    {
        cerr << "send conversations msg error" << endl;
    }
}

// 更新位置
void UpdateLocation(int clientfd, string str)
{
    int idx = str.find(":");
    if (idx == -1)
    {
        cerr << "UpdateLocation command invalid! Format: updatelocation:latitude:longitude:location_name" << endl;
        return;
    }
    
    string params = str.substr(idx + 1);
    vector<string> parts;
    stringstream ss(params);
    string item;
    
    while (getline(ss, item, ':'))
    {
        parts.push_back(item);
    }
    
    if (parts.size() < 2)
    {
        cerr << "UpdateLocation command invalid! Format: updatelocation:latitude:longitude:location_name" << endl;
        return;
    }
    
    try
    {
        double latitude = stod(parts[0]);
        double longitude = stod(parts[1]);
        string location_name = parts.size() > 2 ? parts[2] : "";
        
        json js;
        js["msgid"] = UPDATE_LOCATION_MSG;
        js["id"] = g_current_user.get_id();
        js["latitude"] = latitude;
        js["longitude"] = longitude;
        js["is_visible"] = true;
        js["location_name"] = location_name;
        
        string buffer = js.dump();
        
        int len = send(clientfd, buffer.c_str(), strlen(buffer.c_str()) + 1, 0);
        if (len == -1)
        {
            cerr << "send update location msg error -> " << buffer << endl;
        }
        else
        {
            cout << "位置更新请求已发送" << endl;
        }
    }
    catch (const exception& e)
    {
        cerr << "坐标格式错误: " << e.what() << endl;
    }
}

// 查找附近的人
void FindNearby(int clientfd, string str)
{
    int idx = str.find(":");
    if (idx == -1)
    {
        cerr << "FindNearby command invalid! Format: findnearby:latitude:longitude:radius" << endl;
        return;
    }
    
    string params = str.substr(idx + 1);
    vector<string> parts;
    stringstream ss(params);
    string item;
    
    while (getline(ss, item, ':'))
    {
        parts.push_back(item);
    }
    
    if (parts.size() < 2)
    {
        cerr << "FindNearby command invalid! Format: findnearby:latitude:longitude:radius" << endl;
        return;
    }
    
    try
    {
        double latitude = stod(parts[0]);
        double longitude = stod(parts[1]);
        int radius = parts.size() > 2 ? stoi(parts[2]) : 5000; // 默认5000米
        
        json js;
        js["msgid"] = FIND_NEARBY_MSG;
        js["id"] = g_current_user.get_id();
        js["latitude"] = latitude;
        js["longitude"] = longitude;
        js["radius"] = radius;
        js["limit"] = 20;
        
        string buffer = js.dump();
        
        int len = send(clientfd, buffer.c_str(), strlen(buffer.c_str()) + 1, 0);
        if (len == -1)
        {
            cerr << "send find nearby msg error -> " << buffer << endl;
        }
        else
        {
            cout << "查找附近的人请求已发送，搜索半径: " << radius << "米" << endl;
        }
    }
    catch (const exception& e)
    {
        cerr << "参数格式错误: " << e.what() << endl;
    }
}

// 设置位置可见性
void SetLocationVisibility(int clientfd, string str)
{
    int idx = str.find(":");
    if (idx == -1)
    {
        cerr << "SetLocationVisibility command invalid! Format: setvisibility:visible(1显示/0隐藏)" << endl;
        return;
    }
    
    string visible_str = str.substr(idx + 1);
    
    try
    {
        int visible = stoi(visible_str);
        if (visible != 0 && visible != 1)
        {
            cerr << "Visibility value must be 0 or 1!" << endl;
            return;
        }
        
        json js;
        js["msgid"] = SET_LOCATION_VISIBILITY_MSG;
        js["id"] = g_current_user.get_id();
        js["is_visible"] = (visible == 1);
        
        string buffer = js.dump();
        
        int len = send(clientfd, buffer.c_str(), strlen(buffer.c_str()) + 1, 0);
        if (len == -1)
        {
            cerr << "send set location visibility msg error -> " << buffer << endl;
        }
        else
        {
            cout << "位置可见性设置请求已发送: " << (visible ? "显示" : "隐藏") << endl;
        }
    }
    catch (const exception& e)
    {
        cerr << "参数格式错误: " << e.what() << endl;
    }
}

// 获取位置信息
void GetLocation(int clientfd, string str)
{
    int target_id = g_current_user.get_id(); // 默认查询自己
    
    int idx = str.find(":");
    if (idx != -1)
    {
        string target_str = str.substr(idx + 1);
        try
        {
            target_id = stoi(target_str);
        }
        catch (const exception& e)
        {
            cerr << "用户ID格式错误: " << e.what() << endl;
            return;
        }
    }
    
    json js;
    js["msgid"] = GET_LOCATION_MSG;
    js["id"] = g_current_user.get_id();
    js["target_id"] = target_id;
    
    string buffer = js.dump();
    
    int len = send(clientfd, buffer.c_str(), strlen(buffer.c_str()) + 1, 0);
    if (len == -1)
    {
        cerr << "send get location msg error -> " << buffer << endl;
    }
    else
    {
        if (target_id == g_current_user.get_id())
        {
            cout << "查询自己位置信息请求已发送" << endl;
        }
        else
        {
            cout << "查询用户 " << target_id << " 位置信息请求已发送" << endl;
        }
    }
}

// 已移除手写的Base64函数，改用Base64Utils类

// 全局变量存储上传会话信息
string g_upload_session_id = "";
string g_upload_file_id = "";

// 发送文件
void SendFile(int clientfd, string str)
{
    int idx = str.find(":");
    if (idx == -1)
    {
        cerr << "SendFile command invalid! Format: sendfile:filepath:receiver_id或sendfile:filepath:group:group_id" << endl;
        return;
    }
    
    string params = str.substr(idx + 1);
    vector<string> parts;
    stringstream ss(params);
    string item;
    
    while (getline(ss, item, ':'))
    {
        parts.push_back(item);
    }
    
    if (parts.size() < 2)
    {
        cerr << "SendFile command invalid! Need at least filepath and target" << endl;
        return;
    }
    
    string file_path = parts[0];
    string target_type = parts.size() > 2 ? parts[1] : "user";
    string target_id = parts.size() > 2 ? parts[2] : parts[1];
    
    // 检查文件是否存在
    ifstream test_file(file_path);
    if (!test_file.good())
    {
        cerr << "文件不存在: " << file_path << endl;
        return;
    }
    test_file.close();
    
    // 获取文件信息
    ifstream file(file_path, ios::binary | ios::ate);
    size_t file_size = file.tellg();
    file.close();
    
    size_t last_slash = file_path.find_last_of("/\\");
    string file_name = (last_slash != string::npos) ? file_path.substr(last_slash + 1) : file_path;
    
    size_t last_dot = file_name.find_last_of('.');
    string file_type = (last_dot != string::npos) ? file_name.substr(last_dot) : "";
    
    // 检查文件大小限制（100MB）
    if (file_size > 100 * 1024 * 1024)
    {
        cerr << "文件太大，最大支持100MB" << endl;
        return;
    }
    
    // 计算分片数量（每片64KB）
    const size_t CHUNK_SIZE = 64 * 1024;
    int total_chunks = (file_size + CHUNK_SIZE - 1) / CHUNK_SIZE;
    
    try
    {
        // 发送文件上传请求
        json js;
        js["msgid"] = FILE_UPLOAD_REQ;
        js["id"] = g_current_user.get_id();
        js["file_name"] = file_name;
        js["file_size"] = static_cast<int>(file_size);
        js["file_type"] = file_type;
        js["total_chunks"] = total_chunks;
        
        if (target_type == "group")
        {
            js["group_id"] = stoi(target_id);
            js["receiver_id"] = -1;
        }
        else
        {
            js["receiver_id"] = stoi(target_id);
            js["group_id"] = -1;
        }
        
        string request = js.dump();
        
        int len = send(clientfd, request.c_str(), strlen(request.c_str()) + 1, 0);
        if (len == -1)
        {
            cerr << "发送文件上传请求失败" << endl;
            return;
        }
        
        cout << "文件上传请求已发送: " << file_name << " (" << file_size << " bytes, " << total_chunks << " chunks)" << endl;
        
    }
    catch (const exception& e)
    {
        cerr << "发送文件出错: " << e.what() << endl;
    }
}

// 下载文件
void DownloadFile(int clientfd, string str)
{
    int idx = str.find(":");
    if (idx == -1)
    {
        cerr << "DownloadFile command invalid! Format: downloadfile:file_id" << endl;
        return;
    }
    
    string file_id = str.substr(idx + 1);
    
    try
    {
        json js;
        js["msgid"] = FILE_DOWNLOAD_REQ;
        js["id"] = g_current_user.get_id();
        js["file_id"] = file_id;
        
        string request = js.dump();
        
        int len = send(clientfd, request.c_str(), strlen(request.c_str()) + 1, 0);
        if (len == -1)
        {
            cerr << "发送文件下载请求失败" << endl;
            return;
        }
        
        cout << "文件下载请求已发送: " << file_id << endl;
        
    }
    catch (const exception& e)
    {
        cerr << "下载文件请求出错: " << e.what() << endl;
    }
}

// 查看文件列表
void ListFiles(int clientfd, string str)
{
    try
    {
        json js;
        js["msgid"] = 100; // 临时消息ID，需要在协议中定义
        js["id"] = g_current_user.get_id();
        
        if (!str.empty())
        {
            vector<string> parts;
            stringstream ss(str);
            string item;
            
            while (getline(ss, item, ':'))
            {
                parts.push_back(item);
            }
            
            if (parts.size() >= 2 && parts[0] == "group")
            {
                js["group_id"] = stoi(parts[1]);
            }
        }
        
        string request = js.dump();
        
        int len = send(clientfd, request.c_str(), strlen(request.c_str()) + 1, 0);
        if (len == -1)
        {
            cerr << "发送文件列表请求失败" << endl;
            return;
        }
        
        cout << "文件列表请求已发送" << endl;
        
    }
    catch (const exception& e)
    {
        cerr << "查看文件列表出错: " << e.what() << endl;
    }
}

// 处理分片下载响应
void HandleChunkDownloadResponse(const json& js, int clientfd) {
    try {
        int errno_val = js["errno"].get<int>();
        if (errno_val != 0) {
            cout << "❌ 分片下载失败: " << js["errmsg"].get<string>() << endl;
            if (js.contains("chunk_seq")) {
                int chunk_seq = js["chunk_seq"].get<int>();
                cout << "失败的分片: " << chunk_seq << endl;
            }
            // 重置下载会话
            g_download_session = DownloadSession();
            return;
        }
        
        // 解析分片响应
        string file_id = js["file_id"].get<string>();
        int chunk_seq = js["chunk_seq"].get<int>();
        string chunk_data = js["chunk_data"].get<string>();
        int chunk_size = js["chunk_size"].get<int>();
        bool is_last = js["is_last"].get<bool>();
        
        // 验证文件ID
        if (file_id != g_download_session.file_id) {
            cerr << "❌ 文件ID不匹配，忽略分片" << endl;
            return;
        }
        
        // 验证分片序号
        if (chunk_seq < 1 || chunk_seq > g_download_session.total_chunks) {
            cerr << "❌ 无效的分片序号: " << chunk_seq << endl;
            return;
        }
        
        // 解码分片数据
        vector<char> decoded_data = Base64Utils::decode(chunk_data);
        
        // 存储分片
        int chunk_index = chunk_seq - 1;
        if (!g_download_session.received_chunks[chunk_index]) {
            g_download_session.chunks[chunk_index] = decoded_data;
            g_download_session.received_chunks[chunk_index] = true;
            g_download_session.received_count++;
            
            cout << "✅ 分片 " << chunk_seq << "/" << g_download_session.total_chunks 
                 << " 接收成功 (" << decoded_data.size() << " bytes)" << endl;
        }
        
        // 计算并显示进度
        double progress = (double)g_download_session.received_count / g_download_session.total_chunks * 100.0;
        cout << "📊 下载进度: " << fixed << setprecision(1) << progress << "%" << endl;
        
        // 检查是否还有未下载的分片
        if (g_download_session.received_count < g_download_session.total_chunks) {
            // 查找下一个未下载的分片
            for (int i = 0; i < g_download_session.total_chunks; ++i) {
                if (!g_download_session.received_chunks[i]) {
                    // 请求下一个分片
                    json chunk_req;
                    chunk_req["msgid"] = FILE_CHUNK_DOWNLOAD_REQ;
                    chunk_req["id"] = g_current_user.get_id();
                    chunk_req["file_id"] = file_id;
                    chunk_req["chunk_seq"] = i + 1;
                    
                    string request = chunk_req.dump();
                    send(clientfd, request.c_str(), strlen(request.c_str()) + 1, 0);
                    break;
                }
            }
        } else {
            // 所有分片已接收，保存文件
            try {
                // 创建downloads目录
                filesystem::create_directories("./downloads");
                
                string download_path = "./downloads/" + g_download_session.file_name;
                
                ofstream outfile(download_path, ios::binary);
                if (!outfile.is_open()) {
                    cerr << "❌ 无法创建文件: " << download_path << endl;
                    g_download_session = DownloadSession();
                    return;
                }
                
                // 按顺序写入所有分片
                for (int i = 0; i < g_download_session.total_chunks; ++i) {
                    if (g_download_session.received_chunks[i]) {
                        outfile.write(g_download_session.chunks[i].data(), 
                                     g_download_session.chunks[i].size());
                    } else {
                        cerr << "❌ 分片 " << (i + 1) << " 缺失，无法保存文件" << endl;
                        outfile.close();
                        filesystem::remove(download_path);
                        g_download_session = DownloadSession();
                        return;
                    }
                }
                
                outfile.close();
                
                cout << "\n========== 文件下载完成 ==========" << endl;
                cout << "📁 文件名: " << g_download_session.file_name << endl;
                cout << "📏 文件大小: " << g_download_session.file_size << " bytes" << endl;
                cout << "💾 保存路径: " << download_path << endl;
                cout << "📦 总分片数: " << g_download_session.total_chunks << endl;
                cout << "=================================" << endl;
                
                // 重置下载会话
                g_download_session = DownloadSession();
                
            } catch (const exception& e) {
                cerr << "保存文件出错: " << e.what() << endl;
                g_download_session = DownloadSession();
            }
        }
        
    } catch (const exception& e) {
        cerr << "处理分片下载响应出错: " << e.what() << endl;
        g_download_session = DownloadSession();
    }
}
