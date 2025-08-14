#!/bin/bash

# 集群聊天服务器状态监控脚本

set -e

# 颜色输出
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
CYAN='\033[0;36m'
NC='\033[0m' # No Color

# 配置
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SERVICE_DIR="$(dirname "$SCRIPT_DIR")"
LOGS_DIR="$SERVICE_DIR/logs"

# 服务器配置
declare -a SERVER_PORTS=(8000 8001 8002)
declare -a SERVER_IDS=("server-001" "server-002" "server-003")

# 获取进程信息
get_process_info() {
    local pid=$1
    if [ -z "$pid" ] || ! kill -0 $pid 2>/dev/null; then
        echo "N/A"
        return
    fi
    
    local cpu=$(ps -p $pid -o %cpu --no-headers | tr -d ' ')
    local mem=$(ps -p $pid -o %mem --no-headers | tr -d ' ')
    local time=$(ps -p $pid -o etime --no-headers | tr -d ' ')
    
    echo "CPU:${cpu}% MEM:${mem}% 运行时间:${time}"
}

# 检查端口状态
check_port() {
    local port=$1
    if lsof -ti :$port > /dev/null 2>&1; then
        return 0
    else
        return 1
    fi
}

# 获取Redis信息
get_redis_info() {
    if ! command -v redis-cli &> /dev/null; then
        echo "Redis CLI不可用"
        return
    fi
    
    local connected_clients=$(redis-cli info clients 2>/dev/null | grep "connected_clients:" | cut -d: -f2 | tr -d '\r')
    local used_memory=$(redis-cli info memory 2>/dev/null | grep "used_memory_human:" | cut -d: -f2 | tr -d '\r')
    local total_commands=$(redis-cli info stats 2>/dev/null | grep "total_commands_processed:" | cut -d: -f2 | tr -d '\r')
    
    if [ -n "$connected_clients" ] && [ -n "$used_memory" ] && [ -n "$total_commands" ]; then
        echo "客户端:${connected_clients} 内存:${used_memory} 命令数:${total_commands}"
    else
        echo "连接失败"
    fi
}

# 获取MySQL信息
get_mysql_info() {
    local connections=$(mysql -e "SHOW STATUS LIKE 'Threads_connected';" 2>/dev/null | tail -n1 | awk '{print $2}')
    local queries=$(mysql -e "SHOW STATUS LIKE 'Queries';" 2>/dev/null | tail -n1 | awk '{print $2}')
    
    if [ -n "$connections" ] && [ -n "$queries" ]; then
        echo "连接数:${connections} 查询数:${queries}"
    else
        echo "连接失败"
    fi
}

# 获取系统负载
get_system_load() {
    local load=$(uptime | awk -F'load average:' '{print $2}' | awk '{print $1}' | tr -d ',')
    local cpu_count=$(nproc)
    local cpu_usage=$(top -bn1 | grep "Cpu(s)" | awk '{print $2}' | tr -d '%us,')
    local mem_info=$(free -h | grep Mem)
    local mem_used=$(echo $mem_info | awk '{print $3}')
    local mem_total=$(echo $mem_info | awk '{print $2}')
    
    echo "负载:${load}/${cpu_count} CPU:${cpu_usage}% 内存:${mem_used}/${mem_total}"
}

# 显示详细状态
show_detailed_status() {
    clear
    echo -e "${GREEN}========================================${NC}"
    echo -e "${GREEN}  集群聊天服务器状态监控${NC}"
    echo -e "${GREEN}========================================${NC}"
    echo -e "${CYAN}更新时间: $(date '+%Y-%m-%d %H:%M:%S')${NC}"
    echo ""
    
    # 系统状态
    echo -e "${BLUE}🖥️  系统状态${NC}"
    echo -e "   $(get_system_load)"
    echo ""
    
    # Redis状态
    echo -e "${BLUE}📊 Redis状态${NC}"
    if pgrep -x "redis-server" > /dev/null; then
        local redis_pid=$(pgrep -x "redis-server")
        local redis_info=$(get_redis_info)
        local process_info=$(get_process_info $redis_pid)
        echo -e "   ${GREEN}✓ 运行中${NC} (PID: $redis_pid)"
        echo -e "   $process_info"
        echo -e "   $redis_info"
    else
        echo -e "   ${RED}✗ 未运行${NC}"
    fi
    echo ""
    
    # MySQL状态
    echo -e "${BLUE}🗄️  MySQL状态${NC}"
    if pgrep -x "mysqld" > /dev/null || systemctl is-active --quiet mysql 2>/dev/null; then
        local mysql_info=$(get_mysql_info)
        echo -e "   ${GREEN}✓ 运行中${NC}"
        echo -e "   $mysql_info"
    else
        echo -e "   ${RED}✗ 未运行${NC}"
    fi
    echo ""
    
    # Nginx状态
    echo -e "${BLUE}⚖️  Nginx负载均衡器${NC}"
    if pgrep -x "nginx" > /dev/null; then
        local nginx_pid=$(pgrep -x "nginx" | head -n1)
        local process_info=$(get_process_info $nginx_pid)
        echo -e "   ${GREEN}✓ 运行中${NC} (PID: $nginx_pid)"
        echo -e "   $process_info"
        echo -e "   负载均衡器: http://localhost:8080"
        echo -e "   状态页面: http://localhost:8080/status"
    else
        echo -e "   ${RED}✗ 未运行${NC}"
    fi
    echo ""
    
    # 聊天服务器集群状态
    echo -e "${BLUE}💬 聊天服务器集群${NC}"
    local running_servers=0
    local total_connections=0
    
    for i in "${!SERVER_PORTS[@]}"; do
        local port=${SERVER_PORTS[$i]}
        local server_id=${SERVER_IDS[$i]}
        local pid_file="$LOGS_DIR/chatserver_${port}.pid"
        
        echo -e "   ${CYAN}$server_id (端口:$port)${NC}"
        
        if [ -f "$pid_file" ] && kill -0 $(cat "$pid_file") 2>/dev/null; then
            local pid=$(cat "$pid_file")
            local process_info=$(get_process_info $pid)
            echo -e "     ${GREEN}✓ 运行中${NC} (PID: $pid)"
            echo -e "     $process_info"
            
            # 检查端口连接数
            local connections=$(lsof -ti :$port 2>/dev/null | wc -l)
            echo -e "     连接数: $connections"
            total_connections=$((total_connections + connections))
            running_servers=$((running_servers + 1))
        else
            echo -e "     ${RED}✗ 未运行${NC}"
            if check_port $port; then
                echo -e "     ${YELLOW}⚠️ 端口被其他进程占用${NC}"
            fi
        fi
        echo ""
    done
    
    # 集群总结
    echo -e "${BLUE}📊 集群统计${NC}"
    echo -e "   运行中的服务器: ${running_servers}/${#SERVER_PORTS[@]}"
    echo -e "   总连接数: $total_connections"
    echo ""
    
    # 最近的日志
    echo -e "${BLUE}📝 最近日志 (最后5行)${NC}"
    for i in "${!SERVER_PORTS[@]}"; do
        local port=${SERVER_PORTS[$i]}
        local server_id=${SERVER_IDS[$i]}
        local log_file="$LOGS_DIR/chatserver_${port}.log"
        
        if [ -f "$log_file" ]; then
            echo -e "   ${CYAN}$server_id 日志:${NC}"
            tail -n 2 "$log_file" 2>/dev/null | sed 's/^/     /' || echo "     无日志内容"
        fi
    done
}

# 实时监控模式
real_time_monitor() {
    echo -e "${GREEN}启动实时监控模式 (按 Ctrl+C 退出)${NC}"
    
    while true; do
        show_detailed_status
        sleep 5
    done
}

# 简单状态检查
simple_status() {
    echo -e "${BLUE}集群快速状态检查:${NC}"
    
    # Redis
    if pgrep -x "redis-server" > /dev/null; then
        echo -e "${GREEN}✓ Redis${NC}"
    else
        echo -e "${RED}✗ Redis${NC}"
    fi
    
    # MySQL
    if pgrep -x "mysqld" > /dev/null || systemctl is-active --quiet mysql 2>/dev/null; then
        echo -e "${GREEN}✓ MySQL${NC}"
    else
        echo -e "${RED}✗ MySQL${NC}"
    fi
    
    # Nginx
    if pgrep -x "nginx" > /dev/null; then
        echo -e "${GREEN}✓ Nginx${NC}"
    else
        echo -e "${RED}✗ Nginx${NC}"
    fi
    
    # 聊天服务器
    local running=0
    for i in "${!SERVER_PORTS[@]}"; do
        local port=${SERVER_PORTS[$i]}
        local server_id=${SERVER_IDS[$i]}
        local pid_file="$LOGS_DIR/chatserver_${port}.pid"
        
        if [ -f "$pid_file" ] && kill -0 $(cat "$pid_file") 2>/dev/null; then
            echo -e "${GREEN}✓ $server_id${NC}"
            running=$((running + 1))
        else
            echo -e "${RED}✗ $server_id${NC}"
        fi
    done
    
    echo -e "\n${BLUE}集群状态: ${running}/${#SERVER_PORTS[@]} 服务器运行中${NC}"
}

# 显示帮助
show_help() {
    echo -e "${GREEN}集群聊天服务器状态监控脚本${NC}"
    echo ""
    echo "用法:"
    echo "  ./cluster_status.sh [选项]"
    echo ""
    echo "选项:"
    echo "  --watch, -w     实时监控模式 (每5秒刷新)"
    echo "  --simple, -s    简单状态检查"
    echo "  --help, -h      显示此帮助信息"
    echo ""
    echo "默认行为:"
    echo "  显示详细的一次性状态报告"
}

# 主函数
main() {
    case "${1:-}" in
        --watch|-w)
            real_time_monitor
            ;;
        --simple|-s)
            simple_status
            ;;
        --help|-h)
            show_help
            ;;
        "")
            show_detailed_status
            ;;
        *)
            echo -e "${RED}未知选项: $1${NC}"
            show_help
            exit 1
            ;;
    esac
}

# 运行主函数
main "$@"
