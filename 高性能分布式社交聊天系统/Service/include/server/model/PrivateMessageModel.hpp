#ifndef PRIVATEMESSAGEMODEL_H
#define PRIVATEMESSAGEMODEL_H

#include <string>
#include <vector>
#include "User.hpp"

using namespace std;

// 维护一对一聊天历史消息的结构方法
class PrivateMessageModel
{
public:
    // 存储一对一聊天消息到历史记录
    bool insert_private_message(int from_userid, int to_userid, const string &message, const string &time);

    // 查询两个用户之间的聊天历史记录
    vector<pair<pair<int, string>, pair<string, string>>> query_private_history(int userid1, int userid2, int count, const string &before_time = "");

    // 搜索两个用户之间的聊天消息
    vector<pair<pair<int, string>, pair<string, string>>> search_private_messages(int userid1, int userid2, const string &keyword, int limit = 50);

    // 标记消息为已读
    bool mark_messages_as_read(int to_userid, int from_userid);

    // 查询未读消息数量
    int get_unread_message_count(int userid, int from_userid = 0);

    // 查询用户的所有会话列表（最近聊天的用户）
    vector<pair<User, pair<string, string>>> get_conversation_list(int userid, int limit = 20);

    // 删除两个用户之间的聊天记录
    bool delete_conversation(int userid1, int userid2);
};

#endif
