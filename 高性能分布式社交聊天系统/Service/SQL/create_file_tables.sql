-- 创建文件信息表-一个文件一条记录
CREATE TABLE FileInfo (
    file_id VARCHAR(100) PRIMARY KEY,
    file_name VARCHAR(255) NOT NULL,
    file_path VARCHAR(500) NOT NULL,
    file_size INT NOT NULL,
    file_type VARCHAR(50) NOT NULL,
    file_hash VARCHAR(64) NOT NULL, -- 整个文件的哈希值
    sender_id INT NOT NULL,
    receiver_id INT DEFAULT -1,
    group_id INT DEFAULT -1,
    upload_time TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    status INT DEFAULT 0 COMMENT '0-上传中，1-上传完成，2-下载中，3-已下载',
    
    INDEX idx_sender_id (sender_id),
    INDEX idx_receiver_id (receiver_id),
    INDEX idx_group_id (group_id),
    INDEX idx_upload_time (upload_time),
    INDEX idx_status (status),
    
    FOREIGN KEY (sender_id) REFERENCES User(id) ON DELETE CASCADE
);

-- 创建文件传输会话表
CREATE TABLE FileTransferSession (
    session_id VARCHAR(100) PRIMARY KEY,
    file_id VARCHAR(100) NOT NULL,
    sender_id INT NOT NULL,
    receiver_id INT DEFAULT -1,
    group_id INT DEFAULT -1,
    total_chunks INT NOT NULL,
    received_chunks INT DEFAULT 0,  -- 已接收多少片
    temp_file_path VARCHAR(500) NOT NULL,
    start_time TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    transfer_status INT DEFAULT 0 COMMENT '0-进行中，1-完成，2-失败，3-取消',
    
    INDEX idx_file_id (file_id),
    INDEX idx_sender_id (sender_id),
    INDEX idx_receiver_id (receiver_id),
    INDEX idx_group_id (group_id),
    INDEX idx_start_time (start_time),
    INDEX idx_transfer_status (transfer_status),
    
    FOREIGN KEY (file_id) REFERENCES FileInfo(file_id) ON DELETE CASCADE,
    FOREIGN KEY (sender_id) REFERENCES User(id) ON DELETE CASCADE
);

-- 创建文件下载记录表（可选）
CREATE TABLE FileDownloadRecord (
    id INT AUTO_INCREMENT PRIMARY KEY,
    file_id VARCHAR(100) NOT NULL,
    user_id INT NOT NULL,
    download_time TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    download_status INT DEFAULT 0 COMMENT '0-下载中，1-完成，2-失败',
    
    INDEX idx_file_id (file_id),
    INDEX idx_user_id (user_id),
    INDEX idx_download_time (download_time),
    
    FOREIGN KEY (file_id) REFERENCES FileInfo(file_id) ON DELETE CASCADE,
    FOREIGN KEY (user_id) REFERENCES User(id) ON DELETE CASCADE
);

-- ===================================================================================
-- 📁 文件传输完成后的数据存储说明
-- ===================================================================================

/*
🗂️ 数据库存储的是文件元信息，实际文件数据存储在服务器文件系统中：

1. 📍 文件存储路径结构：
   服务器根目录/
   ├── uploads/                    -- 上传文件根目录
   │   ├── private/               -- 私聊文件目录
   │   │   ├── 2025/             -- 按年份分类
   │   │   │   ├── 01/           -- 按月份分类
   │   │   │   │   ├── user_1001_to_1002_20250115_143022_document.pdf
   │   │   │   │   └── user_1003_to_1004_20250115_150830_image.jpg
   │   │   │   └── 02/
   │   │   └── 2024/
   │   ├── group/                 -- 群聊文件目录
   │   │   ├── group_10001/       -- 按群组ID分类
   │   │   │   ├── 2025/
   │   │   │   │   ├── 01/
   │   │   │   │   │   ├── user_1001_group_10001_20250115_143022_report.docx
   │   │   │   │   │   └── user_1002_group_10001_20250115_151045_screenshot.png
   │   │   │   │   └── 02/
   │   │   │   └── 2024/
   │   │   └── group_10002/
   │   └── temp/                  -- 临时文件目录（传输过程中）
   │       ├── session_abc123_chunk_1.tmp
   │       ├── session_abc123_chunk_2.tmp
   │       └── session_def456_chunk_1.tmp
   └── downloads/                 -- 用户下载的文件副本（可选）

2. 📄 FileInfo.file_path字段示例：
   - 私聊文件: "/uploads/private/2025/01/user_1001_to_1002_20250115_143022_document.pdf"
   - 群聊文件: "/uploads/group/group_10001/2025/01/user_1001_group_10001_20250115_143022_report.docx"

3. 🔄 文件传输完成后的处理流程：
   ① 接收所有文件分片 → temp_file_path (临时文件)
   ② 合并分片完成 → 移动到正式存储路径 file_path
   ③ 更新FileInfo表状态: status = 1 (上传完成)
   ④ 删除临时文件和传输会话记录
   ⑤ 通知相关用户文件上传完成

4. 🛡️ 文件安全策略：
   - 文件哈希校验: 防止文件损坏
   - 访问权限控制: 只有相关用户才能下载
   - 文件扫描: 可集成病毒扫描功能
   - 存储配额: 限制用户上传文件总大小

5. 🗃️ 文件管理功能：
   - 自动清理: 定期清理过期临时文件
   - 备份策略: 重要文件可以备份到其他服务器
   - 压缩存储: 对大文件进行压缩存储
   - 分片存储: 大文件分片存储，支持断点续传
*/

-- 文件存储配置表（可选）
CREATE TABLE FileStorageConfig (
    id INT AUTO_INCREMENT PRIMARY KEY,
    config_key VARCHAR(100) NOT NULL UNIQUE,
    config_value VARCHAR(500) NOT NULL,
    description VARCHAR(200),
    update_time TIMESTAMP DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP
);

-- 初始化文件存储配置
INSERT INTO FileStorageConfig (config_key, config_value, description) VALUES
('upload_root_path', '/var/chatserver/uploads', '文件上传根目录'),
('max_file_size', '104857600', '单个文件最大大小(100MB)'),
('allowed_file_types', 'jpg,jpeg,png,gif,pdf,doc,docx,txt,zip,rar', '允许的文件类型'),
('temp_file_retention_hours', '24', '临时文件保留时间(小时)'),
('enable_file_compression', 'true', '是否启用文件压缩'),
('max_user_storage_mb', '1024', '每用户最大存储空间(MB)');

-- 用户存储使用统计表
CREATE TABLE UserStorageUsage (
    user_id INT PRIMARY KEY,
    used_storage_bytes BIGINT DEFAULT 0,
    file_count INT DEFAULT 0,
    last_update TIMESTAMP DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
    
    FOREIGN KEY (user_id) REFERENCES User(id) ON DELETE CASCADE
);

/*
文件传输的正确流程
阶段1：客户端A → 服务器 (分片上传)
客户端A (发送方)                    服务器                          客户端B (接收方)
      │                            │                                    │
      │ 1. 发起文件传输请求          │                                    │
      ├─────────────────────────────→│                                    │
      │                            │ 创建传输会话                        │
      │                            │ 创建FileInfo记录                    │
      │                            │ 创建FileTransferSession记录         │
      │                            │                                    │
      │ 2. 发送分片1                │                                    │
      ├─────────────────────────────→│ 保存到temp/session_xxx_chunk_1.tmp │
      │ 3. 发送分片2                │                                    │
      ├─────────────────────────────→│ 保存到temp/session_xxx_chunk_2.tmp │
      │ 4. 发送分片3                │                                    │
      ├─────────────────────────────→│ 保存到temp/session_xxx_chunk_3.tmp │
      │ ...                         │ ...                                │
      │ N. 发送最后分片              │                                    │
      ├─────────────────────────────→│ 保存到temp/session_xxx_chunk_N.tmp │
      │                            │                                    │

阶段2：服务器端处理 (合并分片)
// 服务器端的处理逻辑
void ChatService::file_chunk_transfer(...) {
    // 1. 接收客户端A发送的分片
    save_chunk_to_temp_file(session_id, chunk_seq, chunk_data);
    
    // 2. 更新接收计数
    update_chunk_status(session_id, chunk_seq);
    
    // 3. 检查是否所有分片都已接收
    if (session.received_chunks >= session.total_chunks) {
        // 4. 在服务器端合并所有分片
        merge_chunks_to_final_file(session_id);
        
        // 5. 更新文件状态为上传完成
        update_file_status(file_id, 1);
        
        // 6. 删除临时分片文件
        cleanup_temp_chunks(session_id);
        
        // 7. 通知客户端B有新文件
        notify_file_ready(receiver_id, file_info);
    }
}

阶段3：客户端B接收通知 (不是接收分片)
服务器                              客户端B (接收方)
   │                                     │
   │ 发送文件就绪通知                    │
   ├─────────────────────────────────────→│
   │ {                                   │ "您收到一个文件: document.pdf"
   │   "msgid": "FILE_READY_NOTIFY",     │ "发送者: 张三"  
   │   "file_id": "file_xxx",            │ "文件大小: 5MB"
   │   "file_name": "document.pdf",      │ [下载] [忽略] 按钮
   │   "sender_name": "张三",             │
   │   "file_size": 5242880              │
   │ }                                   │
   │                                     │

阶段4：客户端B下载文件 (按需下载)
客户端B                              服务器
   │                                     │
   │ 用户点击下载按钮                    │
   │ 发送下载请求                        │
   ├─────────────────────────────────────→│
   │                                     │ 验证下载权限
   │                                     │ 读取完整文件
   │ 接收完整文件数据                    │
   ←─────────────────────────────────────┤
   │                                     │
   │ 保存到本地下载目录                  │
   │                                     │

具体的服务器端存储过程
临时文件存储 (传输过程中)
# 服务器文件系统中的临时文件
/uploads/temp/
├── session_1723456789_chunk_1.tmp    # 客户端A发送的第1片
├── session_1723456789_chunk_2.tmp    # 客户端A发送的第2片
├── session_1723456789_chunk_3.tmp    # 客户端A发送的第3片
├── ...
└── session_1723456789_chunk_80.tmp   # 客户端A发送的第80片

合并后的最终文件
# 服务器文件系统中的最终文件
/uploads/private/2025/08/13/
└── user_1001_to_1002_20250813_143022_document.pdf  # 合并后的完整文件

为什么是这样的设计？
优势分析：
1. 服务器端集中存储
✅ 优势:
- 文件统一管理，便于备份和维护
- 多个接收者可以下载同一文件（群文件场景）
- 支持离线传输（接收者不在线也能传输）
- 节省网络带宽（一次上传，多次下载）

2. 客户端按需下载
✅ 优势:
- 接收者可以选择是否下载
- 节省客户端存储空间
- 支持大文件传输（不占用客户端内存）
- 可以随时重新下载

对比错误的设计（直接发给客户端B）：
❌ 如果直接发送给客户端B的问题:
1. 接收者必须在线才能接收
2. 网络中断后难以恢复传输  
3. 群文件需要给每个成员都发送一遍
4. 无法进行文件管理和权限控制
5. 大文件会占用大量客户端内存
*/
/*为什么要合并分片而不是保持分片状态？
1. 分片是传输手段，不是存储目的
分片的目的是解决网络传输问题，而不是为了文件存储：
分片的作用：
✅ 解决TCP粘包问题
✅ 支持断点续传
✅ 提高传输稳定性
✅ 支持并发传输

分片不是为了：
❌ 长期存储管理
❌ 文件系统优化
❌ 用户体验

2. 文件完整性和一致性
保持分片状态会带来很多问题：
// ❌ 如果保持分片状态的问题
/uploads/files/
├── file_1723456789_chunk_1.tmp
├── file_1723456789_chunk_2.tmp
├── file_1723456789_chunk_3.tmp
├── ...
└── file_1723456789_chunk_80.tmp

问题：
1. 如何确保80个分片都存在？
2. 分片顺序错误怎么办？
3. 某个分片损坏如何处理？
4. 文件哈希验证如何进行？
5. 用户下载时需要重新分片80次？

而合并后的完整文件：
// ✅ 合并后的优势
/uploads/files/document.pdf  // 一个完整文件

优势：
1. 文件完整性可验证 (一次哈希计算)
2. 标准文件格式，兼容所有软件
3. 备份和迁移简单
4. 用户体验好 (下载就是完整文件)

# ✅ 合并后的清晰结构
ls /uploads/files/2025/08/13/
document.pdf          # 5MB，用户可直接打开
presentation.pptx     # 10MB，标准格式
video.mp4            # 50MB，可直接播放

优势：
- 文件数量可控
- 目录结构清晰
- 标准文件格式
- 可以直接操作 (复制、移动、查看)

分片方案的性能问题：需要对每一个分片进行打开关闭操作，频繁的系统调用性能较低

现代文件系统 (ext4, NTFS) 针对大文件优化：
✅ 大文件顺序读取性能极高
✅ 预读机制减少磁盘寻道
✅ 缓存命中率高

而小文件存储的问题：
❌ 磁盘碎片化严重
❌ inode浪费 (每个分片都占用一个inode)
❌ 元数据开销大
❌ 随机读取性能差

合并文件的用户体验：
✅ 下载后可以直接打开
✅ 可以用任何软件处理
✅ 文件完整性有保障
✅ 支持文件预览

分片文件的用户体验：
❌ 下载后需要客户端软件合并
❌ 无法直接查看文件内容
❌ 分片丢失导致文件损坏
❌ 不兼容其他软件