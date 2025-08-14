#ifndef GROUPMODEL_H
#define GROUPMODEL_H

#include "Group.hpp"
#include <vector>
#include <string>

using namespace std;

class GroupModel
{
public:
    //创建群组
    bool create_group(Group &group);

    //加入群组
    bool add_group(int user_id, int group_id, string role);

    //查询用户所在群组信息
    vector<Group> query_group(int user_id);

    //根据指定的groupid查询群组用户id列表，除userid自己，给该群用户群发消息
    vector<int> query_group_users(int user_id, int group_id);

    //根据群组ID查询群组信息
    Group query_group(int group_id);

    //查询用户所属的群组ID列表
    vector<int> query_groups(int user_id);

    //查询用户在群组中的角色
    string query_group_user_role(int user_id, int group_id);

    //存储群聊消息到历史记录
    bool insert_group_message(int group_id, int user_id, const string &message, const string &time);

    //存储群聊消息到历史记录（使用高精度服务器时间戳）
    bool insert_group_message_with_time(int group_id, int user_id, const string &message, 
                                        const string &server_time, const string &client_time = "");

    //查询群聊历史记录
    vector<pair<pair<int, string>, pair<string, string>>> query_group_history(int group_id, int count, const string &before_time = "");

    //搜索群消息
    vector<pair<pair<int, string>, pair<string, string>>> search_group_messages(int group_id, const string &keyword, int limit = 50);

    //查询群详细信息
    Group query_group_detail(int group_id);

    //查询群成员详细信息
    vector<pair<User, string>> query_group_members(int group_id);

    //从群组中移除用户
    bool remove_from_group(int user_id, int group_id);
};

#endif