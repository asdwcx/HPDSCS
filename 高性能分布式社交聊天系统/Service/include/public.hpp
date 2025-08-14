#ifndef PUBLIC_H
#define PUBLIC_H

#include "message.pb.h"

/*
* service和client的公共文件
*/
enum EnMsgType
{
    LOGIN_MSG = 0,          //登录消息，绑定login
    LOGIN_MSG_ACK = 1,      //登录响应消息
    REG_MSG = 2,            //注册消息，绑定regist
    REG_MSG_ACK = 3,        //注册响应消息
    ONE_CHAT_MSG = 4,       //一对一聊天消息
    ADD_FRIEND_MSG = 5,     //添加好友请求
    ADD_FRIEND_NOTIFY = 6,  //好友请求通知
    ADD_FRIEND_RESPONSE = 7,//好友请求回复
    ADD_FRIEND_RESULT = 8,  //添加好友最终结果
    CREATE_GROUP_MSG = 9,   //创建群聊
    ADD_GROUP_MSG = 10,     //加入群聊
    GROUP_CHAT_MSG = 11,    //群聊消息
    LOGINOUT_MSG = 12,      //注销消息
    GROUP_INVITE_NOTIFY = 13, //群邀请通知
    JOIN_GROUP_MSG = 14,    //申请加入群聊
    JOIN_GROUP_NOTIFY = 15, //加群申请通知
    APPROVE_JOIN_MSG = 16,  //审核加群申请
    JOIN_GROUP_RESULT = 17, //加群结果通知
    GROUP_NOTIFY = 18,      //群通知
    GROUP_HISTORY_MSG = 19, //群聊历史记录查询
    GROUP_SEARCH_MSG = 20,  //群消息搜索
    GROUP_INFO_MSG = 21,    //群信息查询
    QUIT_GROUP_MSG = 22,    //退出群聊
    PRIVATE_HISTORY_MSG = 23, //一对一聊天历史查询
    PRIVATE_SEARCH_MSG = 24,  //一对一聊天消息搜索
    PRIVATE_UNREAD_COUNT_MSG = 25, //未读消息数量查询
    CONVERSATION_LIST_MSG = 26,    //会话列表查询
    UPDATE_LOCATION_MSG = 27,      //更新用户位置
    FIND_NEARBY_MSG = 28,          //查找附近的人
    SET_LOCATION_VISIBILITY_MSG = 29, //设置位置可见性
    GET_LOCATION_MSG = 30,         //获取用户位置信息
    // 文件传输消息类型
    FILE_UPLOAD_REQ = 31,          //文件上传请求
    FILE_UPLOAD_RSP = 32,          //文件上传响应
    FILE_CHUNK_MSG = 33,           //文件分片传输
    FILE_CHUNK_RSP = 34,           //文件分片响应
    FILE_SEND_NOTIFY = 35,         //文件发送通知
    FILE_RECEIVE_CONFIRM = 36,     //文件接收确认
    FILE_DOWNLOAD_REQ = 37,        //文件下载请求
    FILE_DOWNLOAD_RSP = 38,        //文件下载响应
    FILE_CHUNK_DOWNLOAD_REQ = 39,  //分片下载请求
    FILE_CHUNK_DOWNLOAD_RSP = 40,  //分片下载响应
    FILE_DOWNLOAD_COMPLETE = 41,   //下载完成确认
    FILE_LIST_REQ = 42,            //文件列表请求
    FILE_LIST_RSP = 43,            //文件列表响应
    FILE_RESUME_REQ = 44,          //断点续传请求
    FILE_RESUME_RSP = 45           //断点续传响应
};

#endif