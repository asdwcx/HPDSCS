-- ================================================================
-- 集群聊天服务器 - 完整数据库表定义
-- 创建日期: 2025年8月9日
-- 说明: 该文件包含聊天系统所需的所有数据库表结构
-- ================================================================

-- ================================================================
-- 1. 用户信息表 (User)
-- 用途: 存储用户基本信息，用于登录验证和用户状态管理
-- ================================================================
CREATE TABLE IF NOT EXISTS User (
    id INT AUTO_INCREMENT PRIMARY KEY,        -- 用户ID (自增主键)
    name VARCHAR(50) NOT NULL UNIQUE,         -- 用户名 (唯一)
    password VARCHAR(50) NOT NULL,            -- 用户密码
    state ENUM('online', 'offline') DEFAULT 'offline',  -- 用户状态
    INDEX idx_name (name),                    -- 用户名索引，优化登录查询
    INDEX idx_state (state)                   -- 状态索引，优化在线用户查询
);

-- ================================================================
-- 2. 好友关系表 (Friend)
-- 用途: 存储用户之间的好友关系，支持一对一聊天的权限验证
-- ================================================================
CREATE TABLE IF NOT EXISTS Friend (
    userid INT NOT NULL,                      -- 用户ID
    friendid INT NOT NULL,                    -- 好友ID
    PRIMARY KEY (userid, friendid),           -- 复合主键，防止重复好友关系
    INDEX idx_userid (userid),                -- 用户ID索引
    INDEX idx_friendid (friendid),            -- 好友ID索引
    FOREIGN KEY (userid) REFERENCES User(id) ON DELETE CASCADE,
    FOREIGN KEY (friendid) REFERENCES User(id) ON DELETE CASCADE
);

-- ================================================================
-- 3. 离线消息表 (OfflineMessage)
-- 用途: 存储用户离线时收到的消息，用户上线时推送
-- ================================================================
CREATE TABLE IF NOT EXISTS OfflineMessage (
    userid INT NOT NULL,                      -- 接收者用户ID
    message TEXT NOT NULL,                    -- 消息内容 (JSON格式)
    INDEX idx_userid (userid),                -- 用户ID索引，优化离线消息查询
    FOREIGN KEY (userid) REFERENCES User(id) ON DELETE CASCADE
);

-- ================================================================
-- 4. 群组信息表 (AllGroup)
-- 用途: 存储群组基本信息，包括群名、群描述等
-- ================================================================
CREATE TABLE IF NOT EXISTS AllGroup (
    id INT AUTO_INCREMENT PRIMARY KEY,        -- 群组ID (自增主键)
    groupname VARCHAR(50) NOT NULL,           -- 群组名称
    groupdesc VARCHAR(200) DEFAULT '',        -- 群组描述
    INDEX idx_groupname (groupname)           -- 群名索引，优化群组查询
);

-- ================================================================
-- 5. 群组成员表 (GroupUser)
-- 用途: 存储群组成员关系和角色信息
-- ================================================================
CREATE TABLE IF NOT EXISTS GroupUser (
    groupid INT NOT NULL,                     -- 群组ID
    userid INT NOT NULL,                      -- 用户ID
    grouprole ENUM('creator', 'admin', 'normal') DEFAULT 'normal',  -- 群组角色
    PRIMARY KEY (groupid, userid),            -- 复合主键，防止重复加群
    INDEX idx_groupid (groupid),              -- 群组ID索引
    INDEX idx_userid (userid),                -- 用户ID索引
    INDEX idx_role (grouprole),               -- 角色索引
    FOREIGN KEY (groupid) REFERENCES AllGroup(id) ON DELETE CASCADE,
    FOREIGN KEY (userid) REFERENCES User(id) ON DELETE CASCADE
);

-- ================================================================
-- 6. 一对一聊天历史记录表 (PrivateMessageHistory)
-- 用途: 存储一对一聊天消息历史，支持消息查询、搜索和分页功能
-- ================================================================
CREATE TABLE IF NOT EXISTS PrivateMessageHistory (
    id INT AUTO_INCREMENT PRIMARY KEY,        -- 消息ID (自增主键)
    from_userid INT NOT NULL,                 -- 发送者用户ID
    to_userid INT NOT NULL,                   -- 接收者用户ID
    message TEXT NOT NULL,                    -- 消息内容
    time DATETIME NOT NULL,                   -- 发送时间
    is_read BOOLEAN DEFAULT FALSE,            -- 是否已读
    INDEX idx_users_time (from_userid, to_userid, time),  -- 复合索引，优化用户间聊天历史查询
    --LEAST(from_userid, to_userid)：取两者中最小的 ID
    --GREATEST(from_userid, to_userid)：取两者中最大的 ID
    --查询 用户 A 和用户 B 的所有聊天记录，不管是 A 发给 B，还是 B 发给 A。
    INDEX idx_conversation (LEAST(from_userid, to_userid), GREATEST(from_userid, to_userid), time),  -- 会话索引，优化双向查询
    INDEX idx_to_user_unread (to_userid, is_read),        -- 未读消息索引
    INDEX idx_time (time),                    -- 时间索引，优化时间范围查询
    FOREIGN KEY (from_userid) REFERENCES User(id) ON DELETE CASCADE,
    FOREIGN KEY (to_userid) REFERENCES User(id) ON DELETE CASCADE
);

-- ================================================================
-- 7. 群聊历史记录表 (GroupMessageHistory) - 支持消息时序优化
-- 用途: 存储群聊消息历史，支持消息查询、搜索和分页功能
-- 新增: 服务器时间戳，确保消息时序的绝对正确性
-- ================================================================
CREATE TABLE IF NOT EXISTS GroupMessageHistory (
    id INT AUTO_INCREMENT PRIMARY KEY,        -- 消息ID (自增主键)
    groupid INT NOT NULL,                     -- 群组ID
    userid INT NOT NULL,                      -- 发送者用户ID
    message TEXT NOT NULL,                    -- 消息内容
    time DATETIME NOT NULL,                   -- 客户端发送时间
    --sequence_id BIGINT UNSIGNED NOT NULL DEFAULT 0 COMMENT '全局消息序列号',
    server_time DATETIME(3) NOT NULL DEFAULT CURRENT_TIMESTAMP(3) COMMENT '服务器时间戳（精确到毫秒）',
    client_time DATETIME NULL COMMENT '客户端时间戳（保留用于兼容）',
    -- 时序优化索引（新增）
    --INDEX idx_sequence (sequence_id),         -- 序列号索引，确保时序查询性能
    --INDEX idx_group_sequence (groupid, sequence_id), -- 群组+序列号复合索引
    INDEX idx_group_server_time (groupid, server_time), -- 群组+服务器时间索引
    -- 传统索引（保持兼容）
    INDEX idx_group_time (groupid, time),     -- 复合索引，优化群组历史查询
    INDEX idx_group_user (groupid, userid),   -- 复合索引，优化按用户查询
    INDEX idx_time (time),                    -- 时间索引，优化时间范围查询
    FOREIGN KEY (groupid) REFERENCES AllGroup(id) ON DELETE CASCADE,
    FOREIGN KEY (userid) REFERENCES User(id) ON DELETE CASCADE
) COMMENT='群聊消息历史表（支持消息时序优化）';

-- 用户位置信息表
CREATE TABLE IF NOT EXISTS UserLocation (
    id INT AUTO_INCREMENT PRIMARY KEY,                    -- 记录ID (自增主键)
    userid INT NOT NULL UNIQUE,                          -- 用户ID (唯一)
    latitude DECIMAL(10, 8) NOT NULL,                    -- 纬度 (精度8位小数，约1米精度)
    longitude DECIMAL(11, 8) NOT NULL,                   -- 经度 (精度8位小数，约1米精度)
    last_update TIMESTAMP DEFAULT CURRENT_TIMESTAMP      -- 最后更新时间
        ON UPDATE CURRENT_TIMESTAMP,
    is_visible BOOLEAN DEFAULT TRUE,                      -- 是否允许被发现
    location_name VARCHAR(200) DEFAULT '',               -- 位置描述 (如"北京市朝阳区")
    INDEX idx_userid (userid),                           -- 用户ID索引
    INDEX idx_location (latitude, longitude),            -- 位置坐标复合索引
    INDEX idx_visible (is_visible),                      -- 可见性索引
    INDEX idx_update_time (last_update),                 -- 更新时间索引
    FOREIGN KEY (userid) REFERENCES User(id) ON DELETE CASCADE
);

-- 优化存储过程：查找附近用户
DELIMITER //     --需要临时修改语句结束符，防止解析错误
CREATE PROCEDURE sp_find_nearby_users_optimized(
    IN input_lat DOUBLE,
    IN input_lng DOUBLE, 
    IN radius_meters DOUBLE,
    IN limit_count INT
)
BEGIN
    SELECT userid, latitude, longitude,
           ST_Distance_Sphere(location_point, POINT(input_lng, input_lat)) as distance
    FROM UserLocation 
    WHERE is_visible = TRUE
      AND ST_Distance_Sphere(location_point, POINT(input_lng, input_lat)) <= radius_meters
    ORDER BY distance
    LIMIT limit_count;
END //
DELIMITER ;
-- ================================================================
-- 可选优化: 消息全文搜索索引
-- 用途: 为消息内容创建全文索引，提高搜索性能
-- 注意: 需要根据数据库配置和性能需求决定是否启用
-- ================================================================
-- ALTER TABLE PrivateMessageHistory ADD FULLTEXT(message);
-- ALTER TABLE GroupMessageHistory ADD FULLTEXT(message);

-- ================================================================
-- 示例数据插入 (可选)
-- ================================================================

-- 插入测试用户
-- INSERT INTO User (name, password, state) VALUES 
--     ('alice', '123456', 'offline'),
--     ('bob', '123456', 'offline'),
--     ('charlie', '123456', 'offline'),
--     ('diana', '123456', 'offline');

-- 插入好友关系
-- INSERT INTO Friend (userid, friendid) VALUES 
--     (1, 2), (2, 1),  -- alice 和 bob 互为好友
--     (1, 3), (3, 1),  -- alice 和 charlie 互为好友
--     (2, 4), (4, 2);  -- bob 和 diana 互为好友

-- 创建测试群组
-- INSERT INTO AllGroup (groupname, groupdesc) VALUES 
--     ('技术交流群', '讨论技术问题的群组'),
--     ('项目讨论组', '项目开发讨论群');

-- 添加群组成员
-- INSERT INTO GroupUser (groupid, userid, grouprole) VALUES 
--     (1, 1, 'creator'),   -- alice 创建技术交流群
--     (1, 2, 'normal'),    -- bob 加入技术交流群
--     (1, 3, 'normal'),    -- charlie 加入技术交流群
--     (2, 1, 'normal'),    -- alice 加入项目讨论组
--     (2, 4, 'creator');   -- diana 创建项目讨论组

-- 插入一对一聊天历史示例
-- INSERT INTO PrivateMessageHistory (from_userid, to_userid, message, time, is_read) VALUES 
--     (1, 2, 'Hi Bob, how are you?', '2025-08-09 10:00:00', TRUE),
--     (2, 1, 'Hello Alice! I am fine, thanks!', '2025-08-09 10:01:00', TRUE),
--     (1, 3, 'Charlie, are you available for a meeting?', '2025-08-09 11:00:00', FALSE),
--     (3, 1, 'Yes, I am free this afternoon.', '2025-08-09 11:30:00', FALSE);

-- 插入群聊历史示例
-- INSERT INTO GroupMessageHistory (groupid, userid, message, time) VALUES 
--     (1, 1, 'Welcome to the tech discussion group!', '2025-08-09 09:00:00'),
--     (1, 2, 'Thanks Alice! Excited to be here.', '2025-08-09 09:05:00'),
--     (1, 3, 'Great to see everyone!', '2025-08-09 09:10:00');

-- ================================================================
-- 表结构说明
-- ================================================================
-- 
-- 📊 **数据库设计特点:**
-- 
-- 1. **User表**: 核心用户表，支持用户注册、登录、状态管理
--    - 自增ID作为用户唯一标识
--    - 用户名唯一约束，防止重复注册
--    - 状态字段支持在线/离线状态管理
-- 
-- 2. **Friend表**: 好友关系表，支持双向好友关系
--    - 复合主键设计，避免重复好友关系
--    - 外键约束保证数据一致性
--    - 支持一对一聊天的权限验证
-- 
-- 3. **OfflineMessage表**: 离线消息存储
--    - 存储JSON格式消息，灵活支持各种消息类型
--    - 按用户ID索引，快速查询用户离线消息
-- 
-- 4. **AllGroup表**: 群组基本信息
--    - 简洁的群组信息设计
--    - 支持群名和群描述
-- 
-- 5. **GroupUser表**: 群组成员关系
--    - 复合主键防止重复加群
--    - 角色字段支持群组权限管理
--    - 外键约束保证群组和用户的一致性
-- 
-- 6. **PrivateMessageHistory表**: 一对一聊天消息历史
--    - 完整的私聊消息历史记录
--    - 支持已读/未读状态管理
--    - 优化的会话索引，支持双向查询
--    - 支持消息搜索和分页查询
-- 
-- 7. **GroupMessageHistory表**: 群聊消息历史
--    - 完整的消息历史记录
--    - 多重索引优化各种查询场景
--    - 支持消息搜索和分页查询
--    - 新增: 消息序列号和服务器时间戳，确保时序正确性
-- 
-- 🔧 **索引优化策略:**
-- - 单列索引: 优化基础查询性能
-- - 复合索引: 优化多条件查询
-- - 外键约束: 保证数据一致性
-- - 可选全文索引: 提升消息搜索性能
-- - 序列号索引: 确保消息时序查询性能
-- 
-- ================================================================

