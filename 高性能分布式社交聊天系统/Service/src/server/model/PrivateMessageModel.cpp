#include "PrivateMessageModel.hpp"
#include "MySQL.hpp"
#include <iostream>

using namespace std;

// 存储一对一聊天消息到历史记录
bool PrivateMessageModel::insert_private_message(int from_userid, int to_userid, const string &message, const string &time)
{
    char sql[2048] = {0};
    sprintf(sql, "insert into PrivateMessageHistory(from_userid, to_userid, message, time, is_read) values(%d, %d, '%s', '%s', FALSE);", 
            from_userid, to_userid, message.c_str(), time.c_str());
    
    MySQL mysql;
    if (mysql.connet())
    {
        return mysql.update(sql);
    }
    return false;
}

// 查询两个用户之间的聊天历史记录
vector<pair<pair<int, string>, pair<string, string>>> PrivateMessageModel::query_private_history(int userid1, int userid2, int count, const string &before_time)
{
    char sql[2048] = {0};
    if (before_time.empty())
    {
        sprintf(sql, "select h.from_userid, u1.name, h.message, h.time from PrivateMessageHistory h "
                     "join User u1 on h.from_userid = u1.id "
                     "where ((h.from_userid=%d and h.to_userid=%d) or (h.from_userid=%d and h.to_userid=%d)) "
                     "order by h.time desc limit %d;", 
                     userid1, userid2, userid2, userid1, count);
    }
    else
    {
        sprintf(sql, "select h.from_userid, u1.name, h.message, h.time from PrivateMessageHistory h "
                     "join User u1 on h.from_userid = u1.id "
                     "where ((h.from_userid=%d and h.to_userid=%d) or (h.from_userid=%d and h.to_userid=%d)) "
                     "and h.time < '%s' order by h.time desc limit %d;", 
                     userid1, userid2, userid2, userid1, before_time.c_str(), count);
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

// 搜索两个用户之间的聊天消息
vector<pair<pair<int, string>, pair<string, string>>> PrivateMessageModel::search_private_messages(int userid1, int userid2, const string &keyword, int limit)
{
    char sql[2048] = {0};
    sprintf(sql, "select h.from_userid, u1.name, h.message, h.time from PrivateMessageHistory h "
                 "join User u1 on h.from_userid = u1.id "
                 "where ((h.from_userid=%d and h.to_userid=%d) or (h.from_userid=%d and h.to_userid=%d)) "
                 "and h.message like '%%%s%%' order by h.time desc limit %d;", 
                 userid1, userid2, userid2, userid1, keyword.c_str(), limit);
    
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

// 标记消息为已读
bool PrivateMessageModel::mark_messages_as_read(int to_userid, int from_userid)
{
    char sql[1024] = {0};
    sprintf(sql, "update PrivateMessageHistory set is_read=TRUE where to_userid=%d and from_userid=%d and is_read=FALSE;", 
            to_userid, from_userid);
    
    MySQL mysql;
    if (mysql.connet())
    {
        return mysql.update(sql);
    }
    return false;
}

// 查询未读消息数量
int PrivateMessageModel::get_unread_message_count(int userid, int from_userid)
{
    char sql[1024] = {0};
    if (from_userid == 0)
    {
        // 查询所有未读消息数量
        sprintf(sql, "select count(*) from PrivateMessageHistory where to_userid=%d and is_read=FALSE;", userid);
    }
    else
    {
        // 查询来自特定用户的未读消息数量
        sprintf(sql, "select count(*) from PrivateMessageHistory where to_userid=%d and from_userid=%d and is_read=FALSE;", 
                userid, from_userid);
    }
    
    MySQL mysql;
    if (mysql.connet())
    {
        MYSQL_RES *res = mysql.query(sql);
        if (res != nullptr)
        {
            MYSQL_ROW row = mysql_fetch_row(res);
            if (row != nullptr)
            {
                int count = atoi(row[0]);
                mysql_free_result(res);
                return count;
            }
            mysql_free_result(res);
        }
    }
    return 0;
}

// 查询用户的所有会话列表（最近聊天的用户）
vector<pair<User, pair<string, string>>> PrivateMessageModel::get_conversation_list(int userid, int limit)
{
    char sql[2048] = {0};
    sprintf(sql, "select u.id, u.name, u.state, h.message, h.time from User u "
                 "join (select case when from_userid=%d then to_userid else from_userid end as other_userid, "
                 "message, time, row_number() over (partition by case when from_userid=%d then to_userid else from_userid end "
                 "order by time desc) as rn from PrivateMessageHistory "
                 "where from_userid=%d or to_userid=%d) h on u.id = h.other_userid "
                 "where h.rn = 1 order by h.time desc limit %d;", 
                 userid, userid, userid, userid, limit);
    
    vector<pair<User, pair<string, string>>> conversations;
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
                
                pair<string, string> last_msg = {row[3], row[4]};
                conversations.push_back({user, last_msg});
            }
            mysql_free_result(res);
        }
    }
    return conversations;
}

// 删除两个用户之间的聊天记录
bool PrivateMessageModel::delete_conversation(int userid1, int userid2)
{
    char sql[1024] = {0};
    sprintf(sql, "delete from PrivateMessageHistory where (from_userid=%d and to_userid=%d) or (from_userid=%d and to_userid=%d);", 
            userid1, userid2, userid2, userid1);
    
    MySQL mysql;
    if (mysql.connet())
    {
        return mysql.update(sql);
    }
    return false;
}
