#ifndef PROTOBUF_HELPER_H
#define PROTOBUF_HELPER_H

#include "message.pb.h"
#include "model/User.hpp"
#include "model/Group.hpp"
#include "model/GroupUser.hpp"
#include <string>
#include <vector>

class ProtobufHelper
{
public:
    // 将字符串解析为具体的protobuf消息
    template<typename T>
    static bool ParseMessage(const std::string& data, T& message)
    {
        return message.ParseFromString(data);
    }

    // 将protobuf消息序列化为字符串
    template<typename T>
    static std::string SerializeMessage(const T& message)
    {
        return message.SerializeAsString();
    }

    // 创建MessageWrapper
    static chat::MessageWrapper CreateWrapper(chat::MsgType msgtype, const std::string& data)
    {
        chat::MessageWrapper wrapper;
        wrapper.set_msgid(msgtype);
        wrapper.set_data(data);
        return wrapper;
    }

    // User对象转换为protobuf User
    static chat::User UserToProto(const User& user)
    {
        chat::User proto_user;
        proto_user.set_id(user.get_id());
        proto_user.set_name(user.get_name());
        proto_user.set_password(user.get_password());
        proto_user.set_state(user.get_state());
        return proto_user;
    }

    // protobuf User转换为User对象
    static User ProtoToUser(const chat::User& proto_user)
    {
        User user;
        user.set_id(proto_user.id());
        user.set_name(proto_user.name());
        user.set_password(proto_user.password());
        user.set_state(proto_user.state());
        return user;
    }

    // Friend对象转换为protobuf Friend
    static chat::Friend UserToFriend(const User& user)
    {
        chat::Friend proto_friend;
        proto_friend.set_id(user.get_id());
        proto_friend.set_name(user.get_name());
        proto_friend.set_state(user.get_state());
        return proto_friend;
    }

    // Group对象转换为protobuf Group
    static chat::Group GroupToProto(const Group& group)
    {
        chat::Group proto_group;
        proto_group.set_id(group.get_id());
        proto_group.set_name(group.get_name());
        proto_group.set_desc(group.get_desc());
        
        for (const GroupUser& user : group.get_User())
        {
            chat::User* proto_user = proto_group.add_users();
            proto_user->set_id(user.get_id());
            proto_user->set_name(user.get_name());
            proto_user->set_state(user.get_state());
        }
        
        return proto_group;
    }

    // 创建离线消息
    static chat::OfflineMessage CreateOfflineMessage(int from_id, const std::string& message, const std::string& time)
    {
        chat::OfflineMessage offline_msg;
        offline_msg.set_from_id(from_id);
        offline_msg.set_message(message);
        offline_msg.set_time(time);
        return offline_msg;
    }
};

#endif
