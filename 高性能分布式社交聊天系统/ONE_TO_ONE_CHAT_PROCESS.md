# 一对一聊天过程详解

## 📱 **一对一聊天完整流程**

### 🔐 **前提条件：好友关系验证**

在进行一对一聊天之前，系统会验证两个用户之间是否存在好友关系：

```sql
-- 验证好友关系的SQL查询
SELECT * FROM Friend 
WHERE (userid = ? AND friendid = ?) OR (userid = ? AND friendid = ?)
```

只有互为好友的用户才能进行一对一聊天。

---

## 🎯 **一对一聊天时序图**

```
时间轴    Alice(1001)              服务器                     Bob(1002)
 T1       chat:1002:你好，Bob！      接收一对一聊天消息           
          |                       |                          
          |--------------------->| 1. 验证用户登录状态         
          |                      | 2. 验证好友关系             
          |                      | 3. 检查目标用户是否存在     
          |                      |                          
 T2       [消息发送中...]          | 4. 添加服务器时间戳         
          |                      | 5. 判断Bob在线状态          
          |                      |                          
 T3       [发送成功确认]           |--------消息转发-------->  [收到消息] Alice: 你好，Bob！
          |                      |                         |
          |                      |                         | 显示消息内容
          |                      |                         | 更新聊天界面
          |                      |                         |
 T4       |                      |<------回复消息----------  chat:1001:你好，Alice！
          |                      | 接收Bob回复              | 很高兴收到你的消息
          |                      | 验证好友关系             |
          |                      |                         |
 T5       [收到消息] Bob:          |--------消息转发-------->  [已发送]
          你好，Alice！很高兴       |                         |
          收到你的消息             |                         |
```

---

## 💬 **消息发送流程详解**

### **1. 客户端发起聊天**
```bash
# 用户在客户端输入命令
>> chat:1002:你好，Bob！今天天气不错呢！

# 命令格式解析
命令类型: chat
目标用户ID: 1002  
消息内容: "你好，Bob！今天天气不错呢！"
```

### **2. 客户端消息封装**
```cpp
// 客户端代码片段 (main.cpp - Chat函数)
void Chat(int clientfd, string str) {
    int index = str.find(":");
    int friendid = atoi(str.substr(0, index).c_str());
    string message = str.substr(index + 1);

    json js;
    js["msgid"] = ONE_CHAT_MSG;        // 消息类型：一对一聊天
    js["id"] = g_current_user.get_id(); // 发送者ID
    js["name"] = g_current_user.get_name(); // 发送者昵称
    js["toid"] = friendid;             // 接收者ID
    js["msg"] = message;               // 消息内容
    js["time"] = GetCurrentTime();     // 发送时间

    string request = js.dump();
    send(clientfd, request.c_str(), strlen(request.c_str()) + 1, 0);
}
```

### **3. 服务器端处理逻辑**
```cpp
// 服务器代码片段 (ChatService.cpp - one_chat函数)
void ChatService::one_chat(const TcpConnectionPtr &conn, json &js, Timestamp time) {
    int toid = js["toid"].get<int>();
    int id = js["id"].get<int>();
    
    // 🔍 步骤1: 验证好友关系
    vector<User> friend_list = friend_model_.query(id);
    bool is_friend = false;
    for (const User& user : friend_list) {
        if (user.get_id() == toid) {
            is_friend = true;
            break;
        }
    }
    
    if (!is_friend) {
        // ❌ 非好友关系，拒绝发送
        json error_response;
        error_response["msgid"] = ONE_CHAT_MSG;
        error_response["errno"] = 1;
        error_response["errmsg"] = "不能向非好友发送消息";
        conn->send(error_response.dump());
        return;
    }
    
    // 🔍 步骤2: 检查目标用户是否存在
    User target_user = user_model_.query(toid);
    if (target_user.get_id() == -1) {
        json error_response;
        error_response["msgid"] = ONE_CHAT_MSG;
        error_response["errno"] = 2;
        error_response["errmsg"] = "目标用户不存在";
        conn->send(error_response.dump());
        return;
    }
    
    // ✅ 步骤3: 验证通过，处理消息转发
    lock_guard<mutex> lock(conn_mutex_);
    auto it = user_connection_map_.find(toid);
    
    if (it != user_connection_map_.end()) {
        // 🟢 目标用户在线 - 立即转发
        it->second->send(js.dump());
    } else {
        // 🔴 目标用户离线 - 存储离线消息
        offline_message_model_.insert(toid, js.dump());
    }
}
```

---

## 🖥️ **用户界面显示效果**

### **Alice 的聊天界面：**
```bash
┌─────────────────────────────────────────────────────┐
│ 与 Bob 的聊天                                       │
├─────────────────────────────────────────────────────┤
│ [14:30:15] Alice(我): 你好，Bob！今天天气不错呢！     │
│ [14:30:28] Bob: 你好，Alice！是的，很适合出去走走    │
│ [14:30:45] Alice(我): 要不要一起去公园？             │
│ [14:30:52] Bob: 好啊，几点见面？                     │
│ [14:31:05] ___________________________              │
├─────────────────────────────────────────────────────┤
│ >> chat:1002:________________________________      │
│                                                     │
│ Bob 当前状态: 🟢在线                                │
│ 好友关系: ✅已确认                                  │
└─────────────────────────────────────────────────────┘
```

### **Bob 的聊天界面：**
```bash
┌─────────────────────────────────────────────────────┐
│ 与 Alice 的聊天                                     │
├─────────────────────────────────────────────────────┤
│ [14:30:15] Alice: 你好，Bob！今天天气不错呢！        │
│ [14:30:28] Bob(我): 你好，Alice！是的，很适合出去走走 │
│ [14:30:45] Alice: 要不要一起去公园？                 │
│ [14:30:52] Bob(我): 好啊，几点见面？                 │
│ [14:31:05] ___________________________              │
├─────────────────────────────────────────────────────┤
│ >> chat:1001:________________________________      │
│                                                     │
│ Alice 当前状态: 🟢在线                              │
│ 好友关系: ✅已确认                                  │
└─────────────────────────────────────────────────────┘
```

---

## 🛡️ **安全验证机制**

### **1. 好友关系验证**
```cpp
// FriendModel::query() 查询好友列表
vector<User> FriendModel::query(int userid) {
    char sql[1024] = {0};
    sprintf(sql, "SELECT a.id, a.name, a.state FROM User a "
                 "INNER JOIN Friend b ON b.friendid = a.id "
                 "WHERE b.userid = %d", userid);
    // 执行查询，返回好友列表
}
```

### **2. 权限检查流程**
```
发送消息请求
      ↓
验证发送者是否登录
      ↓
查询发送者的好友列表
      ↓
检查目标用户是否在好友列表中
      ↓
验证目标用户是否存在
      ↓
✅ 验证通过 → 转发消息
❌ 验证失败 → 返回错误
```

---

## 📡 **离线消息处理**

### **离线消息存储**
```cpp
// 当目标用户离线时
if (target_user.get_state() == "offline") {
    // 存储为离线消息
    offline_message_model_.insert(toid, js.dump());
    
    // 发送确认给发送者
    json confirm;
    confirm["msgid"] = ONE_CHAT_MSG;
    confirm["status"] = "stored";
    confirm["message"] = "消息已存储，对方上线时会收到";
    conn->send(confirm.dump());
}
```

### **用户上线时的消息同步**
```cpp
// 用户登录成功后
void ChatService::login(/* 参数 */) {
    // ... 登录验证逻辑 ...
    
    // 查询并发送离线消息
    vector<string> offline_msgs = offline_message_model_.query(id);
    for (string &msg : offline_msgs) {
        conn->send(msg);
    }
    
    // 清除已发送的离线消息
    offline_message_model_.remove(id);
}
```

---

## 🔄 **消息状态追踪**

### **消息发送状态**
```bash
# 发送中状态
[14:30:15] Alice(我): 你好，Bob！ ⏳

# 发送成功状态  
[14:30:15] Alice(我): 你好，Bob！ ✓

# 发送失败状态
[14:30:15] Alice(我): 你好，Bob！ ❌ [非好友关系]

# 离线存储状态
[14:30:15] Alice(我): 你好，Bob！ 📫 [已存储]
```

---

## 🚫 **错误处理机制**

### **常见错误情况**

#### **1. 非好友尝试聊天**
```bash
>> chat:1003:你好
❌ 错误: 不能向非好友发送消息，请先添加对方为好友
```

#### **2. 目标用户不存在**
```bash
>> chat:9999:你好  
❌ 错误: 目标用户不存在
```

#### **3. 自己给自己发消息**
```bash
>> chat:1001:测试
❌ 错误: 不能给自己发送消息
```

#### **4. 消息内容为空**
```bash
>> chat:1002:
❌ 错误: 消息内容不能为空
```

---

## 📊 **数据库相关表结构**

### **Friend 好友关系表**
```sql
CREATE TABLE Friend (
    userid INT NOT NULL,     -- 用户ID
    friendid INT NOT NULL,   -- 好友ID
    PRIMARY KEY (userid, friendid),
    FOREIGN KEY (userid) REFERENCES User(id),
    FOREIGN KEY (friendid) REFERENCES User(id)
);
```

### **OfflineMessage 离线消息表**
```sql
CREATE TABLE OfflineMessage (
    userid INT NOT NULL,     -- 接收者ID
    message TEXT NOT NULL,   -- 消息内容(JSON格式)
    FOREIGN KEY (userid) REFERENCES User(id)
);
```

---

## 🎮 **实际使用示例**

### **完整的聊天会话**
```bash
# Alice 开始聊天
>> chat:1002:Hi Bob, 今天工作怎么样？
✓ 消息已发送

# Bob 收到消息并回复
收到消息: [14:30:15] Alice: Hi Bob, 今天工作怎么样？
>> chat:1001:还不错！刚完成了一个项目，你呢？
✓ 消息已发送

# Alice 继续对话
收到消息: [14:30:28] Bob: 还不错！刚完成了一个项目，你呢？
>> chat:1002:我也在忙一个新功能，想请教你一些技术问题
✓ 消息已发送

# Bob 响应
收到消息: [14:30:45] Alice: 我也在忙一个新功能，想请教你一些技术问题
>> chat:1001:当然可以！有什么问题尽管问
✓ 消息已发送
```

---

## ✨ **关键特性总结**

### **🔒 安全特性**
- ✅ 强制好友关系验证
- ✅ 用户存在性检查
- ✅ 登录状态验证
- ✅ 消息内容安全过滤

### **📱 用户体验**
- ✅ 实时消息推送
- ✅ 离线消息存储
- ✅ 消息状态反馈
- ✅ 清晰的界面显示

### **⚡ 性能优化**
- ✅ 高效的好友关系查询
- ✅ 在线用户连接缓存
- ✅ 批量离线消息处理
- ✅ 异步消息转发

### **🛠️ 技术实现**
- ✅ JSON消息格式
- ✅ TCP长连接
- ✅ 多线程并发处理
- ✅ 数据库持久化

这个一对一聊天系统确保了只有好友之间才能进行聊天，提供了完整的消息发送、接收、存储和同步功能！
