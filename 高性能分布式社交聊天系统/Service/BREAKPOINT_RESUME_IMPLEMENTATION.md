# 📂 断点续传功能使用指南

## 🎯 **函数实现完成情况**

**✅ 所有断点续传核心函数已完整实现！**

以下是已实现的断点续传功能函数：

### 🔧 **核心功能函数**

| 函数名 | 功能描述 | 实现状态 |
|--------|----------|----------|
| `get_missing_chunks()` | 获取缺失分片列表 | ✅ 已实现 |
| `is_transfer_resumable()` | 检查传输是否可恢复 | ✅ 已实现 |
| `resume_transfer_session()` | 恢复传输会话 | ✅ 已实现 |
| `get_transfer_progress()` | 获取传输进度 | ✅ 已实现 |
| `verify_received_chunks()` | 验证接收分片完整性 | ✅ 已实现 |

### 🛠️ **内部实现函数**

| 函数名 | 功能描述 | 实现状态 |
|--------|----------|----------|
| `save_chunk_to_temp_file()` | 保存分片到临时文件 | ✅ 已实现 |
| `merge_chunks_to_final_file()` | 合并分片为最终文件 | ✅ 已实现 |
| `scan_existing_chunks()` | 扫描已存在分片文件 | ✅ 已实现 |

### 🔨 **辅助函数**

| 函数名 | 功能描述 | 实现状态 |
|--------|----------|----------|
| `get_temp_chunk_file_path()` | 生成临时分片文件路径 | ✅ 已实现 |
| `cleanup_temp_chunks()` | 清理临时文件 | ✅ 已实现 |
| `decode_base64()` | Base64解码 | ✅ 已实现 |

---

##
传输会话其实就是一张记录文件传输过程各种信息的一张数据库表包括：
唯一身份标识 (session_id)
状态跟踪机制 (分片计数、进度监控)
断点续传支持 (分片状态记录)
并发管理能力 (多会话隔离)
异常处理机制 (超时清理、状态恢复)等

## 📋 **使用示例**

### **1️⃣ 文件上传断点续传流程**

```cpp
// 服务器端处理文件分片上传
class FileUploadHandler {
public:
    bool handle_chunk_upload(const string& session_id, const FileChunk& chunk) {
        FileModel file_model;
        
        // 1. 保存分片到临时文件
        if (!file_model.save_chunk_to_temp_file(session_id, chunk)) {
            cout << "Failed to save chunk " << chunk.chunk_seq << endl;
            return false;
        }
        
        // 2. 更新分片接收状态
        file_model.update_chunk_status(session_id, chunk.chunk_seq);
        
        // 3. 检查是否所有分片都已接收
        if (chunk.is_last || is_all_chunks_received(session_id)) {
            // 4. 合并分片为最终文件
            if (file_model.merge_chunks_to_final_file(session_id)) {
                cout << "File upload completed successfully!" << endl;
                return true;
            } else {
                cout << "Failed to merge chunks" << endl;
                return false;
            }
        }
        
        return true;
    }
    
private:
    bool is_all_chunks_received(const string& session_id) {
        FileModel file_model;
        vector<int> missing = file_model.get_missing_chunks(session_id);
        return missing.empty();
    }
};
```

### **2️⃣ 客户端断点续传恢复**

```cpp
// 客户端断点续传处理
class FileUploadClient {
public:
    bool resume_upload(const string& session_id) {
        FileModel file_model;
        
        // 1. 检查传输是否可以恢复
        if (!file_model.is_transfer_resumable(session_id)) {
            cout << "Transfer cannot be resumed" << endl;
            return false;
        }
        
        // 2. 恢复传输会话
        if (!file_model.resume_transfer_session(session_id)) {
            cout << "Failed to resume transfer session" << endl;
            return false;
        }
        
        // 3. 获取缺失的分片列表
        vector<int> missing_chunks = file_model.get_missing_chunks(session_id);
        cout << "Need to retransmit " << missing_chunks.size() << " chunks" << endl;
        
        // 4. 重传缺失的分片
        for (int chunk_seq : missing_chunks) {
            FileChunk chunk = read_file_chunk(chunk_seq);
            if (!send_chunk_to_server(chunk)) {
                cout << "Failed to send chunk " << chunk_seq << endl;
                return false;
            }
            
            // 显示进度
            double progress = file_model.get_transfer_progress(session_id);
            cout << "Upload progress: " << fixed << setprecision(1) 
                 << progress << "%" << endl;
        }
        
        return true;
    }
    
private:
    FileChunk read_file_chunk(int chunk_seq) {
        // 从本地文件读取指定分片
        // 实现省略...
        return FileChunk();
    }
    
    bool send_chunk_to_server(const FileChunk& chunk) {
        // 发送分片到服务器
        // 实现省略...
        return true;
    }
};
```

### **3️⃣ 传输状态监控**

```cpp
// 传输状态监控
class TransferMonitor {
public:
    void monitor_transfer(const string& session_id) {
        FileModel file_model;
        
        while (true) {
            // 1. 获取传输进度
            double progress = file_model.get_transfer_progress(session_id);
            
            // 2. 获取缺失分片
            vector<int> missing = file_model.get_missing_chunks(session_id);
            
            // 3. 验证已接收分片
            bool chunks_valid = file_model.verify_received_chunks(session_id);
            
            cout << "============ Transfer Status ============" << endl;
            cout << "Session ID: " << session_id << endl;
            cout << "Progress: " << fixed << setprecision(1) << progress << "%" << endl;
            cout << "Missing chunks: " << missing.size() << endl;
            cout << "Chunks valid: " << (chunks_valid ? "YES" : "NO") << endl;
            cout << "=======================================" << endl;
            
            if (progress >= 100.0) {
                cout << "Transfer completed!" << endl;
                break;
            }
            
            this_thread::sleep_for(chrono::seconds(5)); // 5秒监控一次
        }
    }
};
```

---

## � **Session ID 详解**

### **📋 Session ID 的作用和用途**

`session_id` 是文件传输系统中的**核心标识符**，它有以下重要作用：

#### **🎯 主要用途**

1. **文件传输会话标识**: 每次文件传输都会创建一个唯一的session_id
2. **断点续传关键**: 通过session_id可以恢复中断的文件传输
3. **临时文件管理**: 所有分片临时文件都以session_id命名
4. **状态跟踪**: 数据库中通过session_id跟踪传输进度
5. **多文件并发**: 区分不同的并发传输任务

#### **🚫 不是用于会话通信**

**注意**: `session_id` **不是**用于客户端与服务器之间的会话通信！它专门用于**文件传输管理**。

- ✅ **文件传输专用**: 管理单次文件传输的整个生命周期
- ❌ **不是通信会话**: 不是TCP连接会话或用户登录会话

---

### **⚙️ Session ID 生成机制**

```cpp
// FileModel.cpp 中的实现
string FileModel::generate_session_id() {
    // 1. 获取当前时间戳(毫秒级)
    auto now = chrono::system_clock::now();
    auto timestamp = chrono::duration_cast<chrono::milliseconds>(now.time_since_epoch()).count();
    
    // 2. 生成随机数(1000-9999)
    random_device rd;
    mt19937 gen(rd());
    uniform_int_distribution<> dis(1000, 9999);
    
    // 3. 组合生成唯一ID
    stringstream ss;
    ss << "session_" << timestamp << "_" << dis(gen);
    return ss.str();
}
```

#### **🔍 生成规则详解**

```
格式: session_{timestamp}_{random}

示例: session_1642234567890_7325

组成部分:
├── session_           # 固定前缀，标识这是传输会话ID
├── 1642234567890     # 毫秒级时间戳，确保时间唯一性
├── _                 # 分隔符
└── 7325              # 4位随机数，防止同一毫秒内的冲突
```

#### **🛡️ 唯一性保证**

1. **时间唯一性**: 毫秒级时间戳确保不同时间生成的ID不同
2. **随机性**: 4位随机数防止同一毫秒内的冲突
3. **前缀标识**: "session_" 前缀便于识别和管理
4. **碰撞概率**: 同一毫秒内冲突概率仅为 1/9000

---

### **🔄 Session ID 生命周期**

```
文件传输开始                传输过程               传输结束
      |                        |                      |
      ↓                        ↓                      ↓
  生成session_id        分片上传/状态更新         清理session
      |                        |                      |
  创建传输会话            更新进度状态              删除临时文件
      |                        |                      |
  初始化临时目录          断点续传支持              数据库清理
```

#### **📊 详细的生命周期阶段**

```cpp
// 1. 传输开始 - 生成Session ID
string session_id = file_model.generate_session_id();
// 输出: "session_1642234567890_7325"

FileTransferSession session;
session.session_id = session_id;
session.file_id = file_id;
session.total_chunks = total_chunks;
// 存储到数据库

// 2. 传输过程 - 使用Session ID管理
for (int i = 1; i <= total_chunks; i++) {
    // 保存分片: /uploads/temp/session_1642234567890_7325_chunk_1.tmp
    string chunk_file = get_temp_chunk_file_path(session_id, i);
    save_chunk_to_temp_file(session_id, chunk);
    
    // 更新进度
    update_chunk_status(session_id, i);
}

// 3. 传输完成 - 清理Session
merge_chunks_to_final_file(session_id);  // 合并文件
cleanup_temp_chunks(session_id);         // 清理临时文件
delete_transfer_session(session_id);     // 删除会话记录
```

---

### **💾 Session ID 在数据库中的存储**

#### **FileTransferSession 表结构**

```sql
CREATE TABLE FileTransferSession (
    session_id VARCHAR(100) PRIMARY KEY,    -- 🔑 传输会话唯一标识
    file_id VARCHAR(100) NOT NULL,          -- 关联的文件ID
    sender_id INT NOT NULL,                 -- 发送者用户ID
    receiver_id INT DEFAULT -1,             -- 接收者用户ID
    group_id INT DEFAULT -1,                -- 群组ID
    total_chunks INT NOT NULL,              -- 总分片数
    received_chunks INT DEFAULT 0,          -- 已接收分片数
    temp_file_path VARCHAR(500) NOT NULL,   -- 临时文件路径前缀
    start_time TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    transfer_status INT DEFAULT 0           -- 传输状态
);
```

#### **数据库操作示例**

```sql
-- 创建传输会话
INSERT INTO FileTransferSession (
    session_id, file_id, sender_id, receiver_id, 
    total_chunks, temp_file_path
) VALUES (
    'session_1642234567890_7325',
    'file_1642234567890_1234', 
    1001, 1002, 
    100, 
    '/uploads/temp/session_1642234567890_7325.tmp'
);

-- 查询传输状态
SELECT session_id, received_chunks, total_chunks, transfer_status 
FROM FileTransferSession 
WHERE session_id = 'session_1642234567890_7325';

-- 更新分片接收状态
UPDATE FileTransferSession 
SET received_chunks = received_chunks + 1 
WHERE session_id = 'session_1642234567890_7325';
```

---

### **📁 临时文件命名规则**

#### **文件名格式**

```
临时分片文件格式: {session_id}_chunk_{chunk_seq}.tmp

实际示例:
├── session_1642234567890_7325_chunk_1.tmp    # 第1个分片
├── session_1642234567890_7325_chunk_2.tmp    # 第2个分片
├── session_1642234567890_7325_chunk_3.tmp    # 第3个分片
└── session_1642234567890_7325_chunk_100.tmp  # 第100个分片
```

#### **临时文件管理代码**

```cpp
// 生成临时分片文件路径
string FileModel::get_temp_chunk_file_path(const string& session_id, int chunk_seq) {
    return "/uploads/temp/" + session_id + "_chunk_" + to_string(chunk_seq) + ".tmp";
}

// 扫描会话的所有分片文件
vector<int> FileModel::scan_existing_chunks(const string& temp_file_path) {
    // 从路径提取session_id
    string session_id = extract_session_id_from_path(temp_file_path);
    
    // 扫描目录: /uploads/temp/session_1642234567890_7325_chunk_*.tmp
    string pattern = session_id + "_chunk_";
    
    // 返回已存在的分片序号列表
    return existing_chunk_numbers;
}

// 清理会话的所有临时文件
bool FileModel::cleanup_temp_chunks(const string& session_id) {
    // 删除所有匹配的临时文件
    // rm /uploads/temp/session_1642234567890_7325_chunk_*.tmp
    return true;
}
```

---

### **🌐 Session ID 在网络通信中的使用**

#### **客户端发送分片**

```json
// 客户端发送文件分片请求
{
    "msgid": "FILE_CHUNK_UPLOAD",
    "session_id": "session_1642234567890_7325",
    "file_id": "file_1642234567890_1234",
    "chunk_seq": 1,
    "total_chunks": 100,
    "chunk_size": 65536,
    "chunk_data": "base64_encoded_data...",
    "is_last": false
}
```

#### **服务器响应**

```json
// 服务器响应分片接收状态
{
    "msgid": "FILE_CHUNK_RESPONSE", 
    "session_id": "session_1642234567890_7325",
    "chunk_seq": 1,
    "status": "success",
    "received_chunks": 1,
    "total_chunks": 100,
    "progress": 1.0
}
```

#### **断点续传请求**

```json
// 客户端请求断点续传
{
    "msgid": "FILE_RESUME_REQUEST",
    "session_id": "session_1642234567890_7325"
}

// 服务器返回缺失分片
{
    "msgid": "FILE_RESUME_RESPONSE",
    "session_id": "session_1642234567890_7325",
    "missing_chunks": [15, 23, 67, 89],
    "resumable": true,
    "progress": 96.0
}
```

---

### **🔍 Session ID vs 其他ID的区别**

| ID类型 | 用途 | 生命周期 | 格式示例 |
|--------|------|----------|----------|
| **session_id** | 文件传输会话 | 单次传输 | `session_1642234567890_7325` |
| **file_id** | 文件唯一标识 | 文件永久存在 | `file_1642234567890_1234` |
| **user_id** | 用户标识 | 用户账号生命周期 | `1001` |
| **connection_id** | TCP连接标识 | 单次TCP连接 | `conn_192.168.1.100_8080` |

#### **📋 使用场景对比**

```cpp
// ❌ 错误用法 - 不要混淆不同类型的ID
void handle_message(int user_id, string session_id) {
    // 错误: 用session_id查询用户信息
    User user = user_model.query(session_id);  // ❌
    
    // 错误: 用user_id查询传输会话
    FileTransferSession session = file_model.query_transfer_session(to_string(user_id));  // ❌
}

// ✅ 正确用法 - 各司其职
void handle_file_chunk(int user_id, string session_id, FileChunk chunk) {
    // 正确: 用user_id验证用户权限
    if (!auth_service.verify_user(user_id)) return;
    
    // 正确: 用session_id管理文件传输
    file_model.save_chunk_to_temp_file(session_id, chunk);
    
    // 正确: 查询传输会话状态
    FileTransferSession session = file_model.query_transfer_session(session_id);
}
```

---

### **🎯 总结**

#### **✅ Session ID 的核心特点**

1. **专用性**: 专门用于文件传输管理，不是通用会话ID
2. **唯一性**: 时间戳+随机数确保全局唯一
3. **临时性**: 传输完成后会被清理
4. **可追溯**: 通过session_id可以完整追踪传输过程
5. **断点续传**: 是实现断点续传的关键标识

#### **🚀 设计优势**

- **无冲突**: 多用户并发传输不会产生ID冲突
- **易识别**: 前缀标识便于调试和日志分析  
- **时间有序**: 包含时间戳便于按时间排序
- **状态隔离**: 每个传输独立管理，互不影响

**Session ID 是文件传输系统的"身份证"，确保每次传输都能被准确识别和管理！** 🎉

## �🔧 **API接口示例**

### **HTTP RESTful API**

```bash
# 1. 获取缺失分片列表
GET /api/transfer/{session_id}/missing-chunks
Response: {
    "session_id": "abc123",
    "missing_chunks": [3, 7, 12, 18],
    "total_chunks": 100
}

# 2. 检查是否可恢复传输
GET /api/transfer/{session_id}/resumable
Response: {
    "session_id": "abc123",
    "resumable": true,
    "existing_chunks": 85,
    "total_chunks": 100
}

# 3. 恢复传输会话
POST /api/transfer/{session_id}/resume
Response: {
    "session_id": "abc123",
    "status": "resumed",
    "message": "Transfer session resumed successfully"
}

# 4. 获取传输进度
GET /api/transfer/{session_id}/progress
Response: {
    "session_id": "abc123",
    "progress": 85.6,
    "received_chunks": 85,
    "total_chunks": 100,
    "status": "in_progress"
}
```

### **JSON-RPC接口**

```json
// 获取缺失分片
{
    "jsonrpc": "2.0",
    "method": "file.get_missing_chunks",
    "params": {
        "session_id": "abc123"
    },
    "id": 1
}

// 响应
{
    "jsonrpc": "2.0",
    "result": {
        "missing_chunks": [3, 7, 12, 18],
        "count": 4
    },
    "id": 1
}
```

---

## 🗂️ **文件结构说明**

### **临时文件组织**

```
/uploads/temp/
├── session_abc123_chunk_1.tmp      # 会话abc123的第1个分片
├── session_abc123_chunk_2.tmp      # 会话abc123的第2个分片
├── session_abc123_chunk_4.tmp      # 会话abc123的第4个分片 (第3个丢失)
├── session_def456_chunk_1.tmp      # 会话def456的第1个分片
└── session_def456_chunk_2.tmp      # 会话def456的第2个分片
```

### **最终文件存储**

```
/uploads/
├── private/                        # 私聊文件
│   └── 2025/01/
│       ├── user_1001_to_1002_20250115_143022_document.pdf
│       └── user_1003_to_1004_20250115_150830_image.jpg
└── group/                          # 群聊文件
    └── group_10001/
        └── 2025/01/
            ├── user_1001_group_10001_20250115_143022_report.docx
            └── user_1002_group_10001_20250115_151045_screenshot.png
```

---

## 🛡️ **错误处理和日志**

### **错误处理策略**

```cpp
// 完整的错误处理示例
bool FileModel::safe_merge_chunks(const string& session_id) {
    try {
        // 1. 预检查
        if (!is_transfer_resumable(session_id)) {
            throw runtime_error("Transfer not resumable");
        }
        
        // 2. 验证分片完整性
        if (!verify_received_chunks(session_id)) {
            throw runtime_error("Chunk verification failed");
        }
        
        // 3. 执行合并
        bool result = merge_chunks_to_final_file(session_id);
        if (!result) {
            throw runtime_error("Merge operation failed");
        }
        
        return true;
        
    } catch (const exception& e) {
        // 记录错误日志
        cout << "Error in safe_merge_chunks: " << e.what() << endl;
        
        // 更新传输状态为失败
        update_transfer_status(session_id, 2); // 2 = 失败
        
        return false;
    }
}
```

### **日志输出示例**

```
[2025-01-15 14:30:22] Session abc123 missing chunks: 4 out of 100
[2025-01-15 14:30:23] Session abc123 resumable: YES (existing chunks: 96)
[2025-01-15 14:30:24] Resume session abc123: SUCCESS
[2025-01-15 14:30:25] Session abc123 progress: 96.0% (96/100)
[2025-01-15 14:30:26] Saved chunk 97 for session abc123 (65536 bytes)
[2025-01-15 14:30:27] Found 100 existing chunks for session abc123
[2025-01-15 14:30:28] Successfully merged file: /uploads/private/2025/01/large_file.zip (104857600 bytes)
[2025-01-15 14:30:29] Cleaned up 100 temp files for session abc123
```

---

## 🎯 **总结**

### ✅ **实现完成情况**

所有断点续传核心函数已完整实现，包括：

1. **分片管理**: 保存、扫描、验证分片文件
2. **状态追踪**: 获取缺失分片、计算传输进度  
3. **会话恢复**: 检查可恢复性、恢复传输会话
4. **文件合并**: 按序合并分片、验证完整性
5. **错误处理**: 完整的异常处理和日志记录

### 🚀 **功能特性**

- **完整的断点续传**: 支持任意时间点恢复传输
- **数据完整性保证**: 多层校验确保文件正确性
- **高效的存储管理**: 智能的临时文件组织和清理
- **详细的状态监控**: 实时进度跟踪和状态查询
- **健壮的错误处理**: 异常恢复和失败重试机制

**现在您的文件传输系统拥有了完整的断点续传功能！** 🎉
