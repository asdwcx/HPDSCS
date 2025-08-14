#!/bin/bash

# 文件清理服务管理脚本
# File Cleanup Service Management Script

# 颜色定义
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# 配置
CLEANUP_LOG="./logs/file_cleanup.log"
CONFIG_FILE="./config/file_cleanup.conf"
PID_FILE="./pids/file_cleanup.pid"

# 创建必要的目录
create_directories() {
    mkdir -p ./logs
    mkdir -p ./pids
    mkdir -p ./temp
    mkdir -p ./uploads/temp
}

# 显示使用帮助
show_help() {
    echo -e "${BLUE}文件清理服务管理脚本${NC}"
    echo "用法: $0 {start|stop|status|manual|config|stats|help}"
    echo ""
    echo "命令说明："
    echo "  start   - 启动自动清理服务"
    echo "  stop    - 停止自动清理服务"
    echo "  status  - 查看服务状态"
    echo "  manual  - 执行手动清理"
    echo "  config  - 查看/编辑配置"
    echo "  stats   - 查看清理统计信息"
    echo "  help    - 显示此帮助信息"
}

# 启动清理服务
start_cleanup_service() {
    echo -e "${BLUE}启动文件清理服务...${NC}"
    
    if [ -f "$PID_FILE" ]; then
        local pid=$(cat "$PID_FILE")
        if ps -p "$pid" > /dev/null 2>&1; then
            echo -e "${YELLOW}清理服务已在运行中 (PID: $pid)${NC}"
            return 1
        else
            rm -f "$PID_FILE"
        fi
    fi
    
    create_directories
    
    # 这里应该调用你的聊天服务器程序，并启用文件清理功能
    # 示例：./ChatServer --enable-file-cleanup &
    echo -e "${GREEN}✅ 文件清理服务已启动${NC}"
    echo "$(date '+%Y-%m-%d %H:%M:%S') - 文件清理服务启动" >> "$CLEANUP_LOG"
}

# 停止清理服务
stop_cleanup_service() {
    echo -e "${BLUE}停止文件清理服务...${NC}"
    
    if [ -f "$PID_FILE" ]; then
        local pid=$(cat "$PID_FILE")
        if ps -p "$pid" > /dev/null 2>&1; then
            kill "$pid"
            rm -f "$PID_FILE"
            echo -e "${GREEN}✅ 文件清理服务已停止${NC}"
            echo "$(date '+%Y-%m-%d %H:%M:%S') - 文件清理服务停止" >> "$CLEANUP_LOG"
        else
            echo -e "${YELLOW}清理服务未运行${NC}"
            rm -f "$PID_FILE"
        fi
    else
        echo -e "${YELLOW}清理服务未运行${NC}"
    fi
}

# 查看服务状态
check_status() {
    echo -e "${BLUE}查看文件清理服务状态...${NC}"
    
    if [ -f "$PID_FILE" ]; then
        local pid=$(cat "$PID_FILE")
        if ps -p "$pid" > /dev/null 2>&1; then
            echo -e "${GREEN}🟢 文件清理服务正在运行 (PID: $pid)${NC}"
            
            # 显示最近的清理活动
            if [ -f "$CLEANUP_LOG" ]; then
                echo ""
                echo "最近的清理活动："
                tail -5 "$CLEANUP_LOG"
            fi
        else
            echo -e "${RED}🔴 文件清理服务已停止${NC}"
            rm -f "$PID_FILE"
        fi
    else
        echo -e "${RED}🔴 文件清理服务未运行${NC}"
    fi
    
    # 显示临时目录使用情况
    echo ""
    echo "临时目录使用情况："
    show_temp_dir_usage
}

# 显示临时目录使用情况
show_temp_dir_usage() {
    local dirs=("./temp" "./uploads/temp" "/tmp/file_transfer")
    
    for dir in "${dirs[@]}"; do
        if [ -d "$dir" ]; then
            local size=$(du -sh "$dir" 2>/dev/null | cut -f1)
            local files=$(find "$dir" -type f 2>/dev/null | wc -l)
            echo "  📁 $dir: $size ($files 个文件)"
        else
            echo "  📁 $dir: 不存在"
        fi
    done
}

# 执行手动清理
manual_cleanup() {
    echo -e "${BLUE}执行手动文件清理...${NC}"
    
    local total_cleaned=0
    local dirs=("./temp" "./uploads/temp" "/tmp/file_transfer")
    
    for dir in "${dirs[@]}"; do
        if [ -d "$dir" ]; then
            echo "清理目录: $dir"
            
            # 删除超过24小时的文件
            local cleaned=$(find "$dir" -type f -mtime +1 -delete -print 2>/dev/null | wc -l)
            echo "  删除了 $cleaned 个过期文件"
            total_cleaned=$((total_cleaned + cleaned))
            
            # 删除空目录
            find "$dir" -type d -empty -delete 2>/dev/null
        fi
    done
    
    echo -e "${GREEN}✅ 手动清理完成，总共清理了 $total_cleaned 个文件${NC}"
    echo "$(date '+%Y-%m-%d %H:%M:%S') - 手动清理完成，清理文件: $total_cleaned 个" >> "$CLEANUP_LOG"
}

# 查看/编辑配置
manage_config() {
    echo -e "${BLUE}文件清理配置管理${NC}"
    
    if [ -f "$CONFIG_FILE" ]; then
        echo "当前配置："
        cat "$CONFIG_FILE"
        echo ""
        echo "是否编辑配置文件? (y/n)"
        read -r answer
        if [ "$answer" = "y" ] || [ "$answer" = "Y" ]; then
            ${EDITOR:-nano} "$CONFIG_FILE"
        fi
    else
        echo -e "${YELLOW}配置文件不存在: $CONFIG_FILE${NC}"
        echo "是否创建默认配置文件? (y/n)"
        read -r answer
        if [ "$answer" = "y" ] || [ "$answer" = "Y" ]; then
            create_default_config
        fi
    fi
}

# 创建默认配置文件
create_default_config() {
    mkdir -p "$(dirname "$CONFIG_FILE")"
    
    cat > "$CONFIG_FILE" << EOF
# 文件清理配置
[cleanup]
interval_minutes = 30
temp_file_expire_hours = 24
session_expire_hours = 48
max_temp_dir_size = 1073741824
enable_aggressive_cleanup = false
auto_start = true

[directories]
temp_dir_1 = ./temp/
temp_dir_2 = ./uploads/temp/

[logging]
log_file = ./logs/file_cleanup.log
verbose_logging = true
EOF
    
    echo -e "${GREEN}✅ 默认配置文件已创建: $CONFIG_FILE${NC}"
}

# 查看清理统计信息
show_stats() {
    echo -e "${BLUE}文件清理统计信息${NC}"
    
    if [ -f "$CLEANUP_LOG" ]; then
        echo "清理日志统计："
        
        # 总清理次数
        local total_cleanups=$(grep -c "清理任务完成" "$CLEANUP_LOG" 2>/dev/null || echo "0")
        echo "  总清理次数: $total_cleanups"
        
        # 最近清理时间
        local last_cleanup=$(grep "清理任务完成" "$CLEANUP_LOG" 2>/dev/null | tail -1 | cut -d' ' -f1-2)
        echo "  最后清理时间: ${last_cleanup:-"无记录"}"
        
        # 显示最近的日志
        echo ""
        echo "最近的清理记录："
        tail -10 "$CLEANUP_LOG" 2>/dev/null || echo "  无日志记录"
    else
        echo -e "${YELLOW}暂无清理日志记录${NC}"
    fi
    
    echo ""
    show_temp_dir_usage
}

# 主函数
main() {
    case "$1" in
        start)
            start_cleanup_service
            ;;
        stop)
            stop_cleanup_service
            ;;
        status)
            check_status
            ;;
        manual)
            manual_cleanup
            ;;
        config)
            manage_config
            ;;
        stats)
            show_stats
            ;;
        help|--help|-h)
            show_help
            ;;
        *)
            echo -e "${RED}错误: 未知命令 '$1'${NC}"
            echo ""
            show_help
            exit 1
            ;;
    esac
}

# 检查参数
if [ $# -eq 0 ]; then
    show_help
    exit 1
fi

# 执行主函数
main "$@"
