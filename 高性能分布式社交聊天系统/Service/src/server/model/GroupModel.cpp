#include "GroupModel.hpp"
#include "MySQL.hpp"
#include<iostream>
using namespace std;

//创建群组
bool GroupModel::create_group(Group &group)
{
    //组装SQL语句
    char sql[1024] = {0};
    sprintf(sql, "insert into AllGroup(groupname,groupdesc) values('%s','%s');", group.get_name().c_str(), group.get_desc().c_str());
    //cout << sql << endl;

    MySQL mysql;    return result;
}

}

//创建群组
bool GroupModel::create_group(Group &group)   if (mysql.connet())
    {
        if (mysql.update(sql))
        {
            group.set_id(mysql_insert_id(mysql.get_connection()));
            return true;
        }
    }
    return false;
}

//加入群组
bool GroupModel::add_group(int user_id, int group_id, string role)
{
    //组装SQL语句
    char sql[1024] = {0};
    sprintf(sql, "insert into GroupUser values(%d,%d,'%s');", group_id, user_id, role.c_str());
    cout << sql << endl;

    MySQL mysql;
    if (mysql.connet())
    {
        if (mysql.update(sql))
        {
            return true;
        }
    }
    return false;
}

//查询用户所在群组信息
vector<Group> GroupModel::query_group(int user_id)
{
    //组装SQL语句
    char sql[1024] = {0};
    sprintf(sql, "select  a.id,a.groupname,a.groupdesc from AllGroup a inner join GroupUser b on b.groupid=a.id  where b.userid=%d;", user_id);
    //cout << sql << endl;

    vector<Group> group_vec;
    MySQL mysql;
    if (mysql.connet())
    {
        MYSQL_RES *res = mysql.query(sql);
        if (res != nullptr)
        {
            MYSQL_ROW row;
            while ((row = mysql_fetch_row(res)) != nullptr)
            {
                Group group;
                group.set_id(atoi(row[0]));
                group.set_name(row[1]);
                group.set_desc(row[2]);
                group_vec.push_back(group);
            }
            //释放资源，否则内存不断泄露
            mysql_free_result(res);
        }
    }

    //查询群组所有群员的信息
    for (Group &temp : group_vec)
    {
        sprintf(sql, "select  a.id,a.name,a.state,b.grouprole from User a inner join GroupUser b on b.userid=a.id  where b.groupid=%d;", temp.get_id());

        MYSQL_RES *res = mysql.query(sql);
        if (res != nullptr)
        {
            MYSQL_ROW row;
            while ((row = mysql_fetch_row(res)) != nullptr)
            {
                GroupUser group_user;
                group_user.set_id(atoi(row[0]));
                group_user.set_name(row[1]);
                group_user.set_state(row[2]);
                group_user.set_role(row[3]);
                temp.get_User().push_back(group_user);
            }
            //释放资源，否则内存不断泄露
            mysql_free_result(res);
        }
        return group_vec;
    }
}

//根据指定的groupid查询群组用户id列表，除userid自己，给该群用户群发消息
vector<int> GroupModel::query_group_users(int user_id, int group_id)
{
    //组装SQL语句
    char sql[1024] = {0};
    sprintf(sql, "select userid  from GroupUser where groupid=%d and userid != %d;", group_id, user_id);
    //cout << sql << endl;

    vector<int> id_vec;
    MySQL mysql;
    if (mysql.connet())
    {
        MYSQL_RES *res = mysql.query(sql);
        if (res != nullptr)
        {
            MYSQL_ROW row;
            while ((row = mysql_fetch_row(res)) != nullptr)
            {
                id_vec.push_back(atoi(row[0]));
            }
            //释放资源，否则内存不断泄露
            mysql_free_result(res);
        }
    }
    return id_vec;
}

//根据群组ID查询群组信息
Group GroupModel::query_group(int group_id)
{
    char sql[1024] = {0};
    sprintf(sql, "select id, groupname, groupdesc from AllGroup where id=%d;", group_id);
    
    Group group;
    group.set_id(-1); // 默认设置为-1表示不存在
    
    MySQL mysql;
    if (mysql.connet())
    {
        MYSQL_RES *res = mysql.query(sql);
        if (res != nullptr)
        {
            MYSQL_ROW row = mysql_fetch_row(res);
            if (row != nullptr)
            {
                group.set_id(atoi(row[0]));
                group.set_name(row[1]);
                group.set_desc(row[2]);
            }
            mysql_free_result(res);
        }
    }
    return group;
}

//查询用户所属的群组ID列表
vector<int> GroupModel::query_groups(int user_id)
{
    char sql[1024] = {0};
    sprintf(sql, "select groupid from GroupUser where userid=%d;", user_id);
    
    vector<int> group_ids;
    MySQL mysql;
    if (mysql.connet())
    {
        MYSQL_RES *res = mysql.query(sql);
        if (res != nullptr)
        {
            MYSQL_ROW row;
            while ((row = mysql_fetch_row(res)) != nullptr)
            {
                group_ids.push_back(atoi(row[0]));
            }
            mysql_free_result(res);
        }
    }
    return group_ids;
}

//查询用户在群组中的角色
string GroupModel::query_group_user_role(int user_id, int group_id)
{
    char sql[1024] = {0};
    sprintf(sql, "select grouprole from GroupUser where userid=%d and groupid=%d;", user_id, group_id);
    
    string role = "";
    MySQL mysql;
    if (mysql.connet())
    {
        MYSQL_RES *res = mysql.query(sql);
        if (res != nullptr)
        {
            MYSQL_ROW row = mysql_fetch_row(res);
            if (row != nullptr)
            {
                role = row[0];
            }
            mysql_free_result(res);
        }
    }
    return role;
}

//存储群聊消息到历史记录
bool GroupModel::insert_group_message(int group_id, int user_id, const string &message, const string &time)
{
    char sql[1024] = {0};
    sprintf(sql, "insert into GroupMessageHistory(groupid, userid, message, time) values(%d, %d, '%s', '%s');", 
            group_id, user_id, message.c_str(), time.c_str());
    
    MySQL mysql;
    if (mysql.connet())
    {
        return mysql.update(sql);
    }
    return false;
}

//存储群聊消息到历史记录（使用高精度服务器时间戳）
bool GroupModel::insert_group_message_with_time(int group_id, int user_id, const string &message, 
                                                const string &server_time, const string &client_time)
{
    char sql[2048] = {0};
    
    // 转义消息内容，防止SQL注入
    MySQL mysql;
    if (!mysql.connet()) {
        return false;
    }
    
    string escaped_message = message;
    // 简单的转义处理，实际项目中应使用mysql_real_escape_string
    size_t pos = 0;
    while ((pos = escaped_message.find("'", pos)) != string::npos) {
        escaped_message.replace(pos, 1, "\\'");
        pos += 2;
    }
    
    if (client_time.empty()) {
        sprintf(sql, 
            "INSERT INTO GroupMessageHistory(groupid, userid, message, server_time, time) "
            "VALUES(%d, %d, '%s', '%s', '%s')", 
            group_id, user_id, escaped_message.c_str(), 
            server_time.c_str(), server_time.c_str());
    } else {
        sprintf(sql, 
            "INSERT INTO GroupMessageHistory(groupid, userid, message, server_time, client_time, time) "
            "VALUES(%d, %d, '%s', '%s', '%s', '%s')", 
            group_id, user_id, escaped_message.c_str(), 
            server_time.c_str(), client_time.c_str(), server_time.c_str());
    }
    
    return mysql.update(sql);
}

//查询群聊历史记录
vector<pair<pair<int, string>, pair<string, string>>> GroupModel::query_group_history(int group_id, int count, const string &before_time)
{
    char sql[1024] = {0};
    if (before_time.empty())
    {
        sprintf(sql, "select h.userid, u.name, h.message, h.time from GroupMessageHistory h "
                     "join User u on h.userid = u.id where h.groupid=%d "
                     "order by h.time desc limit %d;", group_id, count);
    }
    else
    {
        sprintf(sql, "select h.userid, u.name, h.message, h.time from GroupMessageHistory h "
                     "join User u on h.userid = u.id where h.groupid=%d and h.time < '%s' "
                     "order by h.time desc limit %d;", group_id, before_time.c_str(), count);
    }
    
    vector<pair<pair<int, string>, pair<string, string>>> history;
    MySQL mysql;
    if (mysql.connet())
    {
        MYSQL_RES *res = mysql.query(sql);
        if (res != nullptr)
        {
            MYSQL_ROW row;
            while ((row = mysql_fetch_row(res)) != nullptr)
            {
                pair<int, string> user_info = {atoi(row[0]), row[1]};
                pair<string, string> msg_info = {row[2], row[3]};
                history.push_back({user_info, msg_info});
            }
            mysql_free_result(res);
        }
    }
    return history;
}

//搜索群消息
vector<pair<pair<int, string>, pair<string, string>>> GroupModel::search_group_messages(int group_id, const string &keyword, int limit)
{
    char sql[1024] = {0};
    sprintf(sql, "select h.userid, u.name, h.message, h.time from GroupMessageHistory h "
                 "join User u on h.userid = u.id where h.groupid=%d and h.message like '%%%s%%' "
                 "order by h.time desc limit %d;", group_id, keyword.c_str(), limit);
    
    vector<pair<pair<int, string>, pair<string, string>>> results;
    MySQL mysql;
    if (mysql.connet())
    {
        MYSQL_RES *res = mysql.query(sql);
        if (res != nullptr)
        {
            MYSQL_ROW row;
            while ((row = mysql_fetch_row(res)) != nullptr)
            {
                pair<int, string> user_info = {atoi(row[0]), row[1]};
                pair<string, string> msg_info = {row[2], row[3]};
                results.push_back({user_info, msg_info});
            }
            mysql_free_result(res);
        }
    }
    return results;
}

//查询群详细信息
Group GroupModel::query_group_detail(int group_id)
{
    char sql[1024] = {0};
    sprintf(sql, "select id, groupname, groupdesc from AllGroup where id=%d;", group_id);
    
    Group group;
    group.set_id(-1); // 默认设置为-1表示不存在
    
    MySQL mysql;
    if (mysql.connet())
    {
        MYSQL_RES *res = mysql.query(sql);
        if (res != nullptr)
        {
            MYSQL_ROW row = mysql_fetch_row(res);
            if (row != nullptr)
            {
                group.set_id(atoi(row[0]));
                group.set_name(row[1]);
                group.set_desc(row[2]);
            }
            mysql_free_result(res);
        }
    }
    return group;
}

//查询群成员详细信息
vector<pair<User, string>> GroupModel::query_group_members(int group_id)
{
    char sql[1024] = {0};
    sprintf(sql, "select u.id, u.name, u.state, gu.grouprole from User u "
                 "join GroupUser gu on u.id = gu.userid where gu.groupid=%d "
                 "order by case gu.grouprole when 'creator' then 1 when 'admin' then 2 else 3 end;", group_id);
    
    vector<pair<User, string>> members;
    MySQL mysql;
    if (mysql.connet())
    {
        MYSQL_RES *res = mysql.query(sql);
        if (res != nullptr)
        {
            MYSQL_ROW row;
            while ((row = mysql_fetch_row(res)) != nullptr)
            {
                User user;
                user.set_id(atoi(row[0]));
                user.set_name(row[1]);
                user.set_state(row[2]);
                string role = row[3];
                members.push_back({user, role});
            }
            mysql_free_result(res);
        }
    }
    return members;
}

//从群组中移除用户
bool GroupModel::remove_from_group(int user_id, int group_id)
{
    char sql[1024] = {0};
    sprintf(sql, "delete from GroupUser where userid=%d and groupid=%d;", user_id, group_id);
    
    MySQL mysql;
    if (mysql.connet())
    {
        return mysql.update(sql);
    }
    return false;
}
