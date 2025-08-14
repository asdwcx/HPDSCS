## 群聊过程可视化

### 🎯 **标准群聊交互流程**

```
时间轴    Alice(群主1001)         服务器                     Bob(成员1002)       Charlie(成员1003)    David(成员1004)
 T1       groupchat:10001:        接收群消息                       
          大家好！欢迎加入         |                              
          技术交流群              |                              
          |                      |                              
          |--------------------> | 验证群成员身份                   
          |                      | 广播到群内所有成员               
          |                      |                              
 T2       [已发送]               | --------群消息广播------>   [群消息] Alice: 大家好！  [群消息] Alice: 大家好！ [群消息] Alice: 大家好！
          |                      |                         | 欢迎加入技术交流群        | 欢迎加入技术交流群        | 欢迎加入技术交流群  
          |                      |                         |                        |                        |
 T3       |                      | <------群消息回复-------   groupchat:10001:        |                        |
          |                      | 接收Bob消息              | 谢谢群主！很高兴         |                        |
          |                      | 广播给其他成员            | 能加入这个群            |                        |
          |                      |                         |                        |                        |
 T4       [群消息] Bob: 谢谢群主！ | --------群消息广播------>   [已发送]               [群消息] Bob: 谢谢群主！  [群消息] Bob: 谢谢群主！
          很高兴能加入这个群       |                                                    | 很高兴能加入这个群        | 很高兴能加入这个群
          |                      |                                                    |                        |
 T5       |                      | <------群消息回复-------   |                        groupchat:10001:        |
          |                      | 接收Charlie消息          |                        | 我是Charlie，做前端     |
          |                      | 广播给其他成员            |                        | 开发的，多多指教！       |
          |                      |                         |                        |                        |
 T6       [群消息] Charlie:       | --------群消息广播------>   [群消息] Charlie:       [已发送]               [群消息] Charlie:
          我是Charlie，做前端      |                         | 我是Charlie，做前端     |                        | 我是Charlie，做前端
          开发的，多多指教！       |                         | 开发的，多多指教！       |                        | 开发的，多多指教！
          |                      |                         |                        |                        |
 T7       |                      | <------群消息回复-------   |                        |                        groupchat:10001:
          |                      | 接收David消息            |                        |                        | 我是David，后端工程师
          |                      | 广播给其他成员            |                        |                        | 我们可以组个项目！
          |                      |                         |                        |                        |
 T8       [群消息] David:         | --------群消息广播------>   [群消息] David:         [群消息] David:         [已发送]
          我是David，后端工程师    |                         | 我是David，后端工程师   | 我是David，后端工程师   |
          我们可以组个项目！       |                         | 我们可以组个项目！       | 我们可以组个项目！       |
```

### 📱 **用户界面显示效果（按时间顺序）**

#### **Alice(群主)看到的群聊界面：**
```bash
┌─────────────────────────────────────────────────────┐
│ 群聊: 技术交流群 (4人在线) [群主模式]                 │
├─────────────────────────────────────────────────────┤
│ [14:30:15] Alice: 大家好！欢迎加入技术交流群           │
│ [14:30:28] Bob: 谢谢群主！很高兴能加入这个群           │
│ [14:30:42] Charlie: 我是Charlie，做前端开发的，多多指教！│
│ [14:30:55] David: 我是David，后端工程师，我们可以组个项目！│
│ [14:31:08] ___________________________              │
├─────────────────────────────────────────────────────┤
│ >> groupchat:10001:__________________________      │
│                                                     │
│ 在线成员: Alice👑, Bob, Charlie, David              │
│ 群管理: groupinfo:10001 | 邀请成员: addusertogroup  │
└─────────────────────────────────────────────────────┘
```

#### **Bob(成员)看到的群聊界面：**
```bash
┌─────────────────────────────────────────────────────┐
│ 群聊: 技术交流群 (4人在线)                           │
├─────────────────────────────────────────────────────┤
│ [14:30:15] Alice👑: 大家好！欢迎加入技术交流群        │
│ [14:30:28] Bob(我): 谢谢群主！很高兴能加入这个群      │
│ [14:30:42] Charlie: 我是Charlie，做前端开发的，多多指教！│
│ [14:30:55] David: 我是David，后端工程师，我们可以组个项目！│
│ [14:31:08] ___________________________              │
├─────────────────────────────────────────────────────┤
│ >> groupchat:10001:__________________________      │
│                                                     │
│ 在线成员: Alice👑, Bob(我), Charlie, David          │
│ 退出群聊: quitgroup:10001                           │
└─────────────────────────────────────────────────────┘
```

#### **Charlie(成员)看到的群聊界面：**
```bash
┌─────────────────────────────────────────────────────┐
│ 群聊: 技术交流群 (4人在线)                           │
├─────────────────────────────────────────────────────┤
│ [14:30:15] Alice👑: 大家好！欢迎加入技术交流群        │
│ [14:30:28] Bob: 谢谢群主！很高兴能加入这个群          │
│ [14:30:42] Charlie(我): 我是Charlie，做前端开发的，多多指教！│
│ [14:30:55] David: 我是David，后端工程师，我们可以组个项目！│
│ [14:31:08] ___________________________              │
├─────────────────────────────────────────────────────┤
│ >> groupchat:10001:__________________________      │
│                                                     │
│ 在线成员: Alice👑, Bob, Charlie(我), David          │
│ 退出群聊: quitgroup:10001                           │
└─────────────────────────────────────────────────────┘
```

### 🔄 **消息时序处理机制**

#### **服务器端消息排序逻辑：**
```cpp
// 群消息处理函数
void ChatService::groupChat(const TcpConnectionPtr &conn, ChatMessage &msg, Timestamp time) {
    int groupId = msg.groupid();
    int userId = msg.from();
    
    // 1. 验证用户是否为群成员
    vector<int> groupUsers = _groupModel.queryGroupUsers(userId, groupId);
    if (groupUsers.empty()) {
        // 用户不在群内，拒绝发送
        return;
    }
    
    // 2. 给消息添加服务器时间戳
    msg.set_time(getCurrentTimestamp());
    
    // 3. 获取群内所有用户列表
    vector<int> groupUserList = _groupModel.queryGroupUsers(0, groupId);
    
    // 4. 按用户在线状态分类处理
    for (int id : groupUserList) {
        auto it = _userConnMap.find(id);
        if (it != _userConnMap.end()) {
            // 在线用户：立即发送
            string response = ProtobufHelper::chatMessageToString(msg);
            it->second->send(response);
        } else {
            // 离线用户：存储为离线消息
            _offlineMsgModel.insert(id, ProtobufHelper::chatMessageToString(msg));
        }
    }
}
```

#### **客户端消息显示排序：**
```cpp
// 消息接收处理
void handleGroupMessage(const ChatMessage& msg) {
    // 1. 获取消息时间戳
    string timestamp = msg.time();
    
    // 2. 解析发送者信息  
    int senderId = msg.from();
    string senderName = getUserName(senderId);
    string content = msg.msg();
    
    // 3. 格式化显示
    string displayMsg = formatMessage(timestamp, senderName, content, senderId);
    
    // 4. 按时间顺序插入到消息列表
    insertMessageInOrder(displayMsg, timestamp);
    
    // 5. 刷新界面显示
    refreshChatWindow();
}

// 消息格式化函数
string formatMessage(const string& time, const string& name, 
                    const string& content, int senderId) {
    string roleIcon = "";
    if (isGroupOwner(senderId, currentGroupId)) {
        roleIcon = "👑";  // 群主标识
    } else if (isGroupAdmin(senderId, currentGroupId)) {
        roleIcon = "⭐";  // 管理员标识  
    }
    
    string selfFlag = (senderId == currentUserId) ? "(我)" : "";
    
    return "[" + time + "] " + name + roleIcon + selfFlag + ": " + content;
}
```

### 📊 **消息排序和同步机制**

#### **时间戳处理策略：**
```
1. 客户端发送时间    - 用户发送消息的本地时间
2. 服务器接收时间    - 服务器处理消息的时间 (权威时间)
3. 客户端显示时间    - 基于服务器时间进行显示排序

优先级: 服务器时间 > 客户端时间
```

#### **消息同步流程：**
```
发送者客户端                服务器                    接收者客户端
     |                       |                          |
     | 1. 发送消息             |                          |
     |----------------------->|                          |
     |                       | 2. 添加服务器时间戳        |
     |                       | 3. 验证群成员身份         |
     |                       | 4. 广播给群内成员         |
     |                       |------------------------->| 5. 接收消息
     |                       |                          | 6. 按时间排序显示
     | 7. 接收自己的消息回显    |<-------------------------|
     |<-----------------------|                          |
     | 8. 确认发送成功         |                          |
```

### 📈 **消息历史记录查询**

#### **查看群聊历史命令：**
```bash
>> grouphistory:10001:50
群聊历史记录 (最近50条):

[2025-01-15 14:25:30] Alice👑: 群聊创建成功，欢迎大家！
[2025-01-15 14:26:15] Bob: 谢谢群主邀请
[2025-01-15 14:27:08] Charlie: 大家好，我是新人
[2025-01-15 14:28:22] David: 很高兴认识大家
[2025-01-15 14:30:15] Alice👑: 大家好！欢迎加入技术交流群
[2025-01-15 14:30:28] Bob: 谢谢群主！很高兴能加入这个群
[2025-01-15 14:30:42] Charlie: 我是Charlie，做前端开发的，多多指教！
[2025-01-15 14:30:55] David: 我是David，后端工程师，我们可以组个项目！

共显示 8 条消息，按发送时间升序排列
```

#### **消息搜索功能：**
```bash
>> searchgroup:10001:项目
搜索结果 (关键词: "项目"):

[2025-01-15 14:30:55] David: 我是David，后端工程师，我们可以组个项目！
[2025-01-15 14:35:12] Alice👑: 项目想法不错，我们可以讨论一下技术栈
[2025-01-15 14:40:33] Charlie: 项目的前端部分我可以负责

共找到 3 条相关消息
```

### 🕒 **消息时间显示格式**

#### **不同时间范围的显示策略：**
```bash
# 今天的消息 - 只显示时间
[14:30] Alice: 大家好！

# 昨天的消息 - 显示"昨天"
[昨天 14:30] Bob: 谢谢群主！

# 本周内的消息 - 显示星期
[周三 14:30] Charlie: 我是Charlie

# 更早的消息 - 显示完整日期
[01-10 14:30] David: 我是David

# 跨年的消息 - 显示完整年月日
[2024-12-15 14:30] Eve: 新年快乐！
```

#### **消息状态指示：**
```bash
# 发送中
[14:30] Alice: 大家好！ ⏳

# 发送成功
[14:30] Alice: 大家好！ ✓

# 发送失败
[14:30] Alice: 大家好！ ❌ [点击重发]

# 群内消息回执（可选功能）
[14:30] Alice: 大家好！ ✓✓ (3人已读)
```

### 🔄 **离线消息同步**

#### **用户上线时的消息同步：**
```
用户离线期间              用户重新上线               消息同步显示
     |                         |                          |
     | 群内有新消息               | 连接服务器                |
     | 存储为离线消息             |                          |
     |                         |                          |
     |                         | 请求离线消息               |
     |                         |------------------------->|
     |                         |                          | 按时间顺序显示
     |                         |<-------------------------|
     |                         | 接收历史消息               |
     |                         |                          |
     |                         | 标记消息已读               |
```

#### **离线消息批量加载：**
```bash
>> 用户 Bob(1002) 上线
正在同步离线消息...

[系统] 你有 15 条未读群消息
┌─────────────────────────────────────────────────────┐
│ 离线期间的群消息:                                    │
├─────────────────────────────────────────────────────┤
│ [14:35:12] Alice👑: 项目想法不错，我们可以讨论技术栈   │
│ [14:40:33] Charlie: 前端部分我可以负责                │
│ [14:45:18] David: 后端我来搞定                       │
│ [14:50:05] Alice👑: 那我们建个项目群吧                │
│ ... (还有11条消息)                                   │
├─────────────────────────────────────────────────────┤
│ [查看全部] [标记已读] [跳到最新]                      │
└─────────────────────────────────────────────────────┘

同步完成，现在显示最新消息...
```

### 📱 **多设备消息同步**

#### **同一用户多设备在线：**
```
手机端                    服务器                    电脑端
   |                        |                        |
   | 发送群消息               |                        |
   |----------------------->|                        |
   |                        | 广播给群内所有设备        |
   |                        |----------------------->| 接收消息显示
   |                        |                        |
   | 接收消息回显             |<-----------------------|
   |<-----------------------|                        |
   |                        |                        |
   
所有设备显示相同的消息顺序和时间戳
```

这个群聊系统确保了**消息的时序性**和**一致性**，让所有群成员都能看到按正确时间顺序排列的聊天记录！

---

## 📫 **离线消息处理机制详解**

### 🏢 **一、群聊离线消息处理流程**

#### **📤 群消息发送阶段（有成员离线）**

```
时间轴    Alice(群主1001)         服务器                     Bob(成员1002)       Charlie(成员1003)    David(成员1004)
                在线                |                            在线                     离线                   在线
 T1       groupchat:10001:        接收群消息                       
          欢迎新项目启动！         |                              
          |                      |                              
          |--------------------> | 1. 验证群成员身份                   
          |                      | 2. 存储到GroupMessageHistory    
          |                      | 3. 获取群成员列表               
          |                      | 4. 遍历成员推送消息             
          |                      |                              
 T2       [已发送]               | --------在线成员推送------>   [群消息] Alice: 欢迎新项目启动！    [离线] 消息存储到OfflineMessage  [群消息] Alice: 欢迎新项目启动！
          |                      |                         |                        |                        |  
          |                      | Bob在线：立即推送            |                        | Charlie离线：存储离线消息  |
          |                      | Charlie离线：存储离线消息     |                        |                        |
          |                      | David在线：立即推送          |                        |                        |
```

#### **💾 服务器端群聊离线消息存储逻辑**

```cpp
// 群聊消息处理核心逻辑
void ChatService::group_chat(const TcpConnectionPtr &conn, const chat::MessageWrapper &wrapper, Timestamp time) {
    // 1. 解析群消息
    chat::GroupChatMessage msg;
    msg.ParseFromString(wrapper.data());
    
    int groupid = msg.groupid();
    int userid = msg.id();
    string message = msg.msg();
    string server_time = getCurrentServerTimestamp();
    
    // 2. 存储到群消息历史记录表
    group_model_.insert_group_message_with_time(groupid, userid, message, server_time);
    
    // 3. 获取群内所有成员（除发送者外）
    vector<int> user_id_vec = group_model_.query_group_users(userid, groupid);
    
    // 4. 遍历群成员，按在线状态处理
    lock_guard<mutex> lock(conn_mutex_);
    for (int id : user_id_vec) {
        auto it = user_connection_map_.find(id);
        if (it != user_connection_map_.end()) {
            // ✅ 成员在线 - 立即推送
            string msg_str = updated_wrapper.SerializeAsString();
            it->second->send(msg_str);
        } else {
            // 检查是否在其他服务器节点在线（Redis分布式）
            User user = user_model_.query(id);
            if (user.get_state() == "online") {
                // 在其他节点在线，通过Redis推送
                redis_.publish(id, updated_wrapper.SerializeAsString());
            } else {
                // ❌ 成员离线 - 存储为离线消息
                offline_message_model_.insert(id, updated_wrapper.SerializeAsString());
            }
        }
    }
}
```

#### **📱 群聊离线消息上线同步流程**

```
Charlie重新上线             服务器                    Charlie客户端界面
       |                      |                          |
       | login:1003:password   |                          |
       |--------------------> | 1. 验证登录信息            |
       |                      | 2. 查询离线消息            |
       |                      | 3. 批量推送离线消息         |
       |                      |------------------------->| 📬 离线消息同步中...
       |                      |                          |
       |                      |                          | [系统提示] 你有5条群消息
       |                      |                          | ┌────────────────────────┐
       |                      |                          | │ 离线期间的群消息：       │
       |                      |                          | ├────────────────────────┤
       |                      |                          | │ [14:35] Alice👑: 欢迎新项目启动！│
       |                      |                          | │ [14:40] Bob: 收到，我负责前端 │
       |                      |                          | │ [14:45] David: 后端交给我 │
       |                      |                          | │ [14:50] Alice👑: 很好，开始吧│
       |                      |                          | │ [14:55] Bob: 马上开始编码 │
       |                      |                          | └────────────────────────┘
       |                      | 4. 删除已推送的离线消息     |
       |                      |                          | ✅ 同步完成，现在可以正常聊天
```

### 💬 **二、一对一聊天离线消息处理流程**

#### **📤 私聊消息发送阶段（接收者离线）**

```
时间轴    Alice(1001)            服务器                     Bob(1002)
          在线                    |                          离线
 T1       chat:1002:             接收私聊消息                 
          Bob，项目文档准备好了    |                              
          |                      |                              
          |--------------------> | 1. 验证好友关系                   
          |                      | 2. 存储到PrivateMessageHistory (is_read=FALSE)    
          |                      | 3. 检查Bob在线状态             
          |                      | 4. Bob离线，存储离线消息        
          |                      |                              
 T2       [已发送，对方离线]      | --------离线消息存储------>   [离线] 消息存储到OfflineMessage
          |                      |                         |                        
          | ✓ 消息已存储，对方上线时会收到 |                        |
          |                      |                         |
```

#### **💾 服务器端私聊离线消息存储逻辑**

```cpp
// 一对一聊天消息处理核心逻辑
void ChatService::one_chat(const TcpConnectionPtr &conn, const chat::MessageWrapper &msg, Timestamp time) {
    // 1. 解析私聊消息
    json js = json::parse(msg.data());
    
    int to_id = js["toid"].get<int>();
    int from_id = js["id"].get<int>();
    string message = js["msg"];
    string msg_time = getCurrentServerTimestamp();
    
    // 2. 验证好友关系
    vector<User> friend_list = friend_model_.query(from_id);
    bool is_friend = false;
    for (User &user : friend_list) {
        if (user.get_id() == to_id) {
            is_friend = true;
            break;
        }
    }
    
    if (!is_friend) {
        // 非好友关系，拒绝发送
        json error_response;
        error_response["msgid"] = ONE_CHAT_MSG;
        error_response["errno"] = 1;
        error_response["errmsg"] = "不能向非好友发送消息，请先添加对方为好友";
        conn->send(error_response.dump());
        return;
    }
    
    // 3. 存储到私聊历史记录表（无论对方是否在线都要存储）
    private_message_model_.insert_private_message(from_id, to_id, message, msg_time);
    
    // 4. 检查接收者在线状态
    {
        lock_guard<mutex> lock(conn_mutex_);
        auto it = user_connection_map_.find(to_id);
        if (it != user_connection_map_.end()) {
            // ✅ 接收者在线 - 立即推送
            json forward_msg;
            forward_msg["msgid"] = ONE_CHAT_MSG;
            forward_msg["id"] = from_id;
            forward_msg["name"] = sender_name;
            forward_msg["to"] = to_id;
            forward_msg["msg"] = message;
            forward_msg["time"] = msg_time;
            
            it->second->send(forward_msg.dump());
            
            // 标记消息为已读（因为立即收到了）
            private_message_model_.mark_messages_as_read(to_id, from_id);
            return;
        }
    }
    
    // 5. 检查是否在其他服务器节点在线
    User user = user_model_.query(to_id);
    if (user.get_state() == "online") {
        // 在其他节点在线，通过Redis推送
        redis_.publish(to_id, wrapper.SerializeAsString());
    } else {
        // ❌ 接收者离线 - 存储离线消息
        json offline_msg;
        offline_msg["msgid"] = ONE_CHAT_MSG;
        offline_msg["id"] = from_id;
        offline_msg["name"] = sender_name;
        offline_msg["to"] = to_id;
        offline_msg["msg"] = message;
        offline_msg["time"] = msg_time;
        
        offline_message_model_.insert(to_id, offline_msg.dump());
        
        // 给发送者确认消息已存储
        json confirm;
        confirm["msgid"] = ONE_CHAT_MSG;
        confirm["status"] = "stored";
        confirm["message"] = "消息已存储，对方上线时会收到";
        conn->send(confirm.dump());
    }
}
```

#### **📱 私聊离线消息上线同步流程**

```
Bob重新上线                服务器                    Bob客户端界面
      |                      |                          |
      | login:1002:password   |                          |
      |--------------------> | 1. 验证登录信息            |
      |                      | 2. 查询离线消息            |
      |                      | SELECT message FROM      |
      |                      |   OfflineMessage         |
      |                      |   WHERE userid=1002      |
      |                      | 3. 推送所有离线消息         |
      |                      |------------------------->| 📬 正在同步离线消息...
      |                      |                          |
      |                      |                          | [15:30:25] Alice(1001): Bob，项目文档准备好了
      |                      |                          | [15:35:10] Alice(1001): 请查收邮件附件
      |                      |                          | [15:40:15] Charlie(1003): 明天开会记得参加
      |                      |                          |
      |                      | 4. 删除已推送的离线消息     | ✅ 离线消息同步完成 (3条消息)
      |                      | DELETE FROM OfflineMessage|
      |                      | WHERE userid=1002         |
      |                      |                          | 现在可以正常聊天了
```

### 🔄 **三、Redis分布式离线消息处理**

#### **跨服务器节点的离线消息处理**

```cpp
// Redis订阅回调函数 - 处理来自其他服务器节点的消息
void ChatService::redis_subscribe_message_handler(int userid, string msg) {
    lock_guard<mutex> lock(conn_mutex_);
    auto it = user_connection_map_.find(userid);
    if (it != user_connection_map_.end()) {
        // 用户在当前服务器节点在线，直接推送
        it->second->send(msg);
        return;
    }
    
    // 用户不在当前节点，或者离线，存储为离线消息
    offline_message_model_.insert(userid, msg);
}
```

#### **分布式架构下的消息流转**

```
服务器节点A                Redis中间件               服务器节点B
(Alice发送消息)              |                      (Bob可能在线)
     |                       |                          |
     | publish(bob_id, msg)   |                          |
     |---------------------->|                          |
     |                       | subscribe回调             |
     |                       |------------------------->| 检查Bob是否在本节点
     |                       |                          |
     |                       |                          | if (Bob在线) {
     |                       |                          |     立即推送消息
     |                       |                          | } else {
     |                       |                          |     存储离线消息
     |                       |                          | }
```

### 📊 **四、离线消息数据库设计**

#### **OfflineMessage表结构**
```sql
CREATE TABLE OfflineMessage (
    userid INT NOT NULL,              -- 接收者用户ID
    message TEXT NOT NULL,            -- 消息内容(JSON格式)
    INDEX idx_userid (userid),        -- 用户ID索引，优化查询
    FOREIGN KEY (userid) REFERENCES User(id) ON DELETE CASCADE
);
```

#### **离线消息查询和清理**
```cpp
// 查询用户离线消息
vector<string> OfflineMessageModel::query(int id) {
    char sql[1024] = {0};
    sprintf(sql, "SELECT message FROM OfflineMessage WHERE userid=%d", id);
    
    vector<string> vec;
    MySQL mysql;
    if (mysql.connet()) {
        MYSQL_RES *res = mysql.query(sql);
        if (res != nullptr) {
            MYSQL_ROW row;
            while ((row = mysql_fetch_row(res)) != nullptr) {
                vec.push_back(row[0]);
            }
            mysql_free_result(res);
        }
    }
    return vec;
}

// 删除用户离线消息（用户上线后调用）
bool OfflineMessageModel::remove(int id) {
    char sql[1024] = {0};
    sprintf(sql, "DELETE FROM OfflineMessage WHERE userid=%d", id);
    
    MySQL mysql;
    if (mysql.connet()) {
        return mysql.update(sql);
    }
    return false;
}
```

### 🎯 **五、离线消息处理总结**

#### **✅ 群聊离线消息特点**
- **广播机制**: 一条消息需要发送给多个群成员
- **部分离线**: 群内部分成员在线，部分离线
- **消息完整性**: 确保所有群成员最终都能收到消息
- **时序保证**: 离线成员上线后看到的消息顺序正确

#### **✅ 一对一离线消息特点**  
- **点对点**: 一条消息只需要发送给一个接收者
- **好友验证**: 只有好友之间才能发送私聊消息
- **已读状态**: 支持消息已读/未读状态管理
- **即时反馈**: 发送者能知道消息是否成功发送或存储

#### **🔄 离线消息生命周期**
```
消息发送 → 检查接收者状态 → 在线：立即推送 | 离线：存储到数据库
                                ↓
                           用户上线登录
                                ↓
                         查询并推送离线消息
                                ↓
                          删除已推送的离线消息
                                ↓
                            消息生命周期结束
```

#### **📈 性能优化策略**
- **批量操作**: 用户上线时批量推送所有离线消息
- **索引优化**: 按用户ID建立索引，快速查询离线消息
- **内存缓存**: 在线用户连接信息缓存在内存中
- **分布式负载**: Redis支持多服务器节点的消息分发
- **异步处理**: 离线消息存储不阻塞正常的消息发送流程

这套离线消息系统确保了**消息不丢失**、**时序正确**，并且支持**分布式部署**和**高并发**场景！
