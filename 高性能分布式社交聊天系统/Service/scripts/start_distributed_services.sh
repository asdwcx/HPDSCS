#!/bin/bash

# 分布式聊天服务启动脚本
# Distributed Chat Services Startup Script

set -e

# 配置参数
CONFIG_FILE="./config/distributed_services.json"
LOG_DIR="./logs"
PID_DIR="./pids"

# 颜色输出
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

# 日志函数
log_info() {
    echo -e "${GREEN}[INFO]${NC} $(date '+%Y-%m-%d %H:%M:%S') - $1"
}

log_warn() {
    echo -e "${YELLOW}[WARN]${NC} $(date '+%Y-%m-%d %H:%M:%S') - $1"
}

log_error() {
    echo -e "${RED}[ERROR]${NC} $(date '+%Y-%m-%d %H:%M:%S') - $1"
}

# 创建必要目录
create_directories() {
    log_info "创建必要目录..."
    mkdir -p $LOG_DIR
    mkdir -p $PID_DIR
    log_info "目录创建完成"
}

# 检查依赖
check_dependencies() {
    log_info "检查系统依赖..."
    
    # 检查ZooKeeper
    if ! command -v zkServer.sh &> /dev/null; then
        log_warn "ZooKeeper服务未找到，请确保ZooKeeper已安装并在PATH中"
    fi
    
    # 检查配置文件
    if [ ! -f "$CONFIG_FILE" ]; then
        log_error "配置文件不存在: $CONFIG_FILE"
        exit 1
    fi
    
    # 检查可执行文件
    if [ ! -f "./bin/ChatServer" ]; then
        log_error "ChatServer可执行文件不存在，请先编译项目"
        exit 1
    fi
    
    log_info "依赖检查完成"
}

# 启动ZooKeeper
start_zookeeper() {
    log_info "检查ZooKeeper状态..."
    
    # 检查ZooKeeper是否已经运行
    if pgrep -f "org.apache.zookeeper" > /dev/null; then
        log_info "ZooKeeper已在运行"
        return
    fi
    
    log_info "启动ZooKeeper..."
    if command -v zkServer.sh &> /dev/null; then
        zkServer.sh start
        sleep 5
        
        if pgrep -f "org.apache.zookeeper" > /dev/null; then
            log_info "ZooKeeper启动成功"
        else
            log_error "ZooKeeper启动失败"
            exit 1
        fi
    else
        log_warn "无法自动启动ZooKeeper，请手动启动"
    fi
}

# 启动服务
start_service() {
    local service_name=$1
    local port=$2
    
    log_info "启动服务: $service_name (端口: $port)"
    
    # 检查端口是否被占用
    if netstat -tuln | grep ":$port " > /dev/null 2>&1; then
        log_warn "端口 $port 已被占用，跳过服务 $service_name"
        return
    fi
    
    # 启动服务
    nohup ./bin/ChatServer --service=$service_name --config=$CONFIG_FILE \
        > $LOG_DIR/$service_name.log 2>&1 &
    
    local pid=$!
    echo $pid > $PID_DIR/$service_name.pid
    
    # 等待服务启动
    sleep 2
    
    if kill -0 $pid 2>/dev/null; then
        log_info "服务 $service_name 启动成功 (PID: $pid)"
    else
        log_error "服务 $service_name 启动失败"
        rm -f $PID_DIR/$service_name.pid
    fi
}

# 启动所有服务
start_all_services() {
    log_info "启动所有分布式服务..."
    
    # 从配置文件读取启用的服务
    local services=(
        "AuthService:50001"
        "PrivateChatService:50002"
        "GroupChatService:50003"
        "FriendService:50004"
        "FileService:50005"
        "LocationService:50006"
        "NotificationService:50007"
    )
    
    for service_info in "${services[@]}"; do
        IFS=':' read -r service_name port <<< "$service_info"
        start_service "$service_name" "$port"
        sleep 1
    done
    
    log_info "所有服务启动完成"
}

# 停止服务
stop_service() {
    local service_name=$1
    local pid_file="$PID_DIR/$service_name.pid"
    
    if [ -f "$pid_file" ]; then
        local pid=$(cat "$pid_file")
        if kill -0 $pid 2>/dev/null; then
            log_info "停止服务: $service_name (PID: $pid)"
            kill $pid
            
            # 等待进程结束
            local count=0
            while kill -0 $pid 2>/dev/null && [ $count -lt 10 ]; do
                sleep 1
                ((count++))
            done
            
            if kill -0 $pid 2>/dev/null; then
                log_warn "强制停止服务: $service_name"
                kill -9 $pid
            fi
            
            log_info "服务 $service_name 已停止"
        else
            log_warn "服务 $service_name 进程不存在"
        fi
        rm -f "$pid_file"
    else
        log_warn "服务 $service_name 的PID文件不存在"
    fi
}

# 停止所有服务
stop_all_services() {
    log_info "停止所有分布式服务..."
    
    local services=(
        "AuthService"
        "PrivateChatService"
        "GroupChatService"
        "FriendService"
        "FileService"
        "LocationService"
        "NotificationService"
    )
    
    for service_name in "${services[@]}"; do
        stop_service "$service_name"
    done
    
    log_info "所有服务已停止"
}

# 查看服务状态
show_status() {
    log_info "服务状态:"
    printf "%-20s %-8s %-10s %-8s\n" "服务名称" "状态" "PID" "端口"
    printf "%s\n" "----------------------------------------------------"
    
    local services=(
        "AuthService:50001"
        "PrivateChatService:50002"
        "GroupChatService:50003"
        "FriendService:50004"
        "FileService:50005"
        "LocationService:50006"
        "NotificationService:50007"
    )
    
    for service_info in "${services[@]}"; do
        IFS=':' read -r service_name port <<< "$service_info"
        local pid_file="$PID_DIR/$service_name.pid"
        local status="停止"
        local pid="N/A"
        
        if [ -f "$pid_file" ]; then
            local service_pid=$(cat "$pid_file")
            if kill -0 $service_pid 2>/dev/null; then
                status="运行中"
                pid="$service_pid"
            else
                rm -f "$pid_file"
            fi
        fi
        
        printf "%-20s %-8s %-10s %-8s\n" "$service_name" "$status" "$pid" "$port"
    done
}

# 重启服务
restart_service() {
    local service_name=$1
    log_info "重启服务: $service_name"
    stop_service "$service_name"
    sleep 2
    
    # 根据服务名获取端口
    case $service_name in
        "AuthService") start_service "$service_name" "50001" ;;
        "PrivateChatService") start_service "$service_name" "50002" ;;
        "GroupChatService") start_service "$service_name" "50003" ;;
        "FriendService") start_service "$service_name" "50004" ;;
        "FileService") start_service "$service_name" "50005" ;;
        "LocationService") start_service "$service_name" "50006" ;;
        "NotificationService") start_service "$service_name" "50007" ;;
        *) log_error "未知服务: $service_name" ;;
    esac
}

# 显示帮助信息
show_help() {
    echo "分布式聊天服务管理脚本"
    echo ""
    echo "用法: $0 [命令] [选项]"
    echo ""
    echo "命令:"
    echo "  start                启动所有服务"
    echo "  stop                 停止所有服务"
    echo "  restart              重启所有服务"
    echo "  status               查看服务状态"
    echo "  start-service NAME   启动指定服务"
    echo "  stop-service NAME    停止指定服务"
    echo "  restart-service NAME 重启指定服务"
    echo "  help                 显示此帮助信息"
    echo ""
    echo "服务名称:"
    echo "  AuthService          用户认证服务"
    echo "  PrivateChatService   私聊服务"
    echo "  GroupChatService     群聊服务"
    echo "  FriendService        好友管理服务"
    echo "  FileService          文件服务"
    echo "  LocationService      位置服务"
    echo "  NotificationService  通知服务"
    echo ""
    echo "示例:"
    echo "  $0 start                    # 启动所有服务"
    echo "  $0 restart-service AuthService  # 重启认证服务"
    echo "  $0 status                   # 查看服务状态"
}

# 主函数
main() {
    case ${1:-""} in
        "start")
            create_directories
            check_dependencies
            start_zookeeper
            start_all_services
            ;;
        "stop")
            stop_all_services
            ;;
        "restart")
            stop_all_services
            sleep 3
            create_directories
            check_dependencies
            start_zookeeper
            start_all_services
            ;;
        "status")
            show_status
            ;;
        "start-service")
            if [ -z "$2" ]; then
                log_error "请指定服务名称"
                exit 1
            fi
            create_directories
            check_dependencies
            restart_service "$2"
            ;;
        "stop-service")
            if [ -z "$2" ]; then
                log_error "请指定服务名称"
                exit 1
            fi
            stop_service "$2"
            ;;
        "restart-service")
            if [ -z "$2" ]; then
                log_error "请指定服务名称"
                exit 1
            fi
            restart_service "$2"
            ;;
        "help"|"--help"|"-h")
            show_help
            ;;
        "")
            show_help
            ;;
        *)
            log_error "未知命令: $1"
            show_help
            exit 1
            ;;
    esac
}

# 运行主函数
main "$@"
