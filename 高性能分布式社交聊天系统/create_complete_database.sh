#!/bin/bash

# 完整数据库创建脚本 (Linux/Unix版本)
# 使用方式: ./create_complete_database.sh

# 数据库连接配置
DB_HOST="127.0.0.1"
DB_PORT="3306"
DB_USER="ik"
DB_PASSWORD="123456"
DB_NAME="chat"
SQL_FILE="complete_database_tables.sql"

echo "======================================="
echo "   集群聊天服务器 - 完整数据库创建"
echo "======================================="
echo "目标数据库: $DB_HOST:$DB_PORT/$DB_NAME"
echo "创建脚本: $SQL_FILE"
echo ""

# 检查mysql客户端是否可用
if ! command -v mysql &> /dev/null; then
    echo "❌ 错误: 未找到mysql客户端，请确保MySQL已安装并配置到PATH"
    exit 1
fi

# 检查SQL文件是否存在
if [ ! -f "$SQL_FILE" ]; then
    echo "❌ 错误: 数据库脚本文件不存在: $SQL_FILE"
    exit 1
fi

echo "🔄 创建完整的数据库结构（包含消息时序优化）..."
echo ""

# 首先尝试创建数据库（如果不存在）
echo "CREATE DATABASE IF NOT EXISTS $DB_NAME DEFAULT CHARACTER SET utf8mb4 COLLATE utf8mb4_unicode_ci;" | mysql -h$DB_HOST -P$DB_PORT -u$DB_USER -p$DB_PASSWORD

# 执行完整的数据库创建脚本
mysql -h$DB_HOST -P$DB_PORT -u$DB_USER -p$DB_PASSWORD $DB_NAME < $SQL_FILE

if [ $? -eq 0 ]; then
    echo "✅ 数据库创建成功!"
    echo ""
    echo "🔍 验证创建结果："
    echo ""
    
    echo "=== 数据库表列表 ==="
    mysql -h$DB_HOST -P$DB_PORT -u$DB_USER -p$DB_PASSWORD $DB_NAME -e "SHOW TABLES;"
    
    echo ""
    echo "=== GroupMessageHistory表结构验证 ==="
    mysql -h$DB_HOST -P$DB_PORT -u$DB_USER -p$DB_PASSWORD $DB_NAME -e "DESCRIBE GroupMessageHistory;"
    
    echo ""
    echo "=== MessageSequence表状态 ==="
    mysql -h$DB_HOST -P$DB_PORT -u$DB_USER -p$DB_PASSWORD $DB_NAME -e "SELECT * FROM MessageSequence;"
    
    echo ""
    echo "=== 存储过程验证 ==="
    mysql -h$DB_HOST -P$DB_PORT -u$DB_USER -p$DB_PASSWORD $DB_NAME -e "SHOW PROCEDURE STATUS WHERE Name IN ('GetNextMessageSequence', 'GetSequenceBatch');"
    
    echo ""
    echo "✅ 数据库创建完成! 功能特点："
    echo "   • ✅ 完整的聊天系统表结构"
    echo "   • ✅ 消息时序优化（序列号 + 服务器时间戳）"
    echo "   • ✅ 高并发序列号管理存储过程"
    echo "   • ✅ 性能优化索引配置"
    echo "   • ✅ 数据一致性外键约束"
    echo ""
    echo "🎉 数据库已就绪，可以启动聊天服务器！"
    
else
    echo "❌ 数据库创建失败!"
    echo "   请检查MySQL连接配置和权限设置"
    exit 1
fi
