#!/bin/bash

# 集群聊天服务器停止脚本

set -e

# 颜色输出
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# 配置
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SERVICE_DIR="$(dirname "$SCRIPT_DIR")"
LOGS_DIR="$SERVICE_DIR/logs"

# 服务器配置
declare -a SERVER_PORTS=(8000 8001 8002)
declare -a SERVER_IDS=("server-001" "server-002" "server-003")

# 停止聊天服务器
stop_chat_servers() {
    echo -e "${BLUE}停止聊天服务器集群...${NC}"
    
    for i in "${!SERVER_PORTS[@]}"; do
        local port=${SERVER_PORTS[$i]}
        local server_id=${SERVER_IDS[$i]}
        local pid_file="$LOGS_DIR/chatserver_${port}.pid"
        
        if [ -f "$pid_file" ]; then
            local pid=$(cat "$pid_file")
            if kill -0 $pid 2>/dev/null; then
                echo -e "${BLUE}停止服务器 $server_id (PID: $pid)...${NC}"
                kill -TERM $pid
                
                # 等待进程结束
                local count=0
                while kill -0 $pid 2>/dev/null && [ $count -lt 10 ]; do
                    sleep 1
                    count=$((count + 1))
                done
                
                # 如果仍在运行，强制杀死
                if kill -0 $pid 2>/dev/null; then
                    echo -e "${YELLOW}强制停止服务器 $server_id${NC}"
                    kill -KILL $pid
                fi
                
                echo -e "${GREEN}服务器 $server_id 已停止${NC}"
            else
                echo -e "${YELLOW}服务器 $server_id 进程不存在${NC}"
            fi
            rm -f "$pid_file"
        else
            echo -e "${YELLOW}未找到服务器 $server_id 的PID文件${NC}"
        fi
    done
}

# 停止Nginx
stop_nginx() {
    echo -e "${BLUE}停止Nginx负载均衡器...${NC}"
    
    if pgrep -x "nginx" > /dev/null; then
        nginx -s quit
        sleep 2
        
        if pgrep -x "nginx" > /dev/null; then
            echo -e "${YELLOW}Nginx仍在运行，强制停止...${NC}"
            pkill -KILL nginx
        fi
        
        echo -e "${GREEN}Nginx已停止${NC}"
    else
        echo -e "${YELLOW}Nginx未运行${NC}"
    fi
}

# 停止Redis (可选)
stop_redis() {
    if [ "$1" = "--with-redis" ]; then
        echo -e "${BLUE}停止Redis服务...${NC}"
        
        if pgrep -x "redis-server" > /dev/null; then
            redis-cli shutdown
            sleep 2
            
            if pgrep -x "redis-server" > /dev/null; then
                echo -e "${YELLOW}Redis仍在运行，强制停止...${NC}"
                pkill -KILL redis-server
            fi
            
            echo -e "${GREEN}Redis已停止${NC}"
        else
            echo -e "${YELLOW}Redis未运行${NC}"
        fi
    fi
}

# 清理临时文件
cleanup() {
    echo -e "${BLUE}清理临时文件...${NC}"
    
    # 清理PID文件
    rm -f "$LOGS_DIR"/chatserver_*.pid
    
    # 清理Nginx临时文件
    rm -rf /tmp/nginx/*
    
    echo -e "${GREEN}清理完成${NC}"
}

# 显示停止状态
show_stop_status() {
    echo -e "\n${BLUE}========== 停止状态 ==========${NC}"
    
    # 检查聊天服务器
    local all_stopped=true
    for i in "${!SERVER_PORTS[@]}"; do
        local port=${SERVER_PORTS[$i]}
        local server_id=${SERVER_IDS[$i]}
        
        if lsof -ti :$port > /dev/null 2>&1; then
            echo -e "${RED}✗ $server_id (端口:$port): 仍在运行${NC}"
            all_stopped=false
        else
            echo -e "${GREEN}✓ $server_id (端口:$port): 已停止${NC}"
        fi
    done
    
    # 检查Nginx
    if pgrep -x "nginx" > /dev/null; then
        echo -e "${RED}✗ Nginx: 仍在运行${NC}"
        all_stopped=false
    else
        echo -e "${GREEN}✓ Nginx: 已停止${NC}"
    fi
    
    # 检查Redis (如果要求停止)
    if [ "$1" = "--with-redis" ]; then
        if pgrep -x "redis-server" > /dev/null; then
            echo -e "${RED}✗ Redis: 仍在运行${NC}"
            all_stopped=false
        else
            echo -e "${GREEN}✓ Redis: 已停止${NC}"
        fi
    fi
    
    if [ "$all_stopped" = true ]; then
        echo -e "\n${GREEN}🎉 集群已完全停止！${NC}"
    else
        echo -e "\n${YELLOW}⚠️ 某些服务仍在运行${NC}"
    fi
}

# 显示帮助信息
show_help() {
    echo -e "${GREEN}集群聊天服务器停止脚本${NC}"
    echo ""
    echo "用法:"
    echo "  ./stop_cluster.sh [选项]"
    echo ""
    echo "选项:"
    echo "  --with-redis    同时停止Redis服务"
    echo "  --help, -h      显示此帮助信息"
    echo ""
    echo "示例:"
    echo "  ./stop_cluster.sh              # 停止聊天服务器和Nginx"
    echo "  ./stop_cluster.sh --with-redis # 停止所有服务包括Redis"
}

# 主函数
main() {
    # 解析命令行参数
    local with_redis=false
    
    for arg in "$@"; do
        case $arg in
            --with-redis)
                with_redis=true
                ;;
            --help|-h)
                show_help
                exit 0
                ;;
            *)
                echo -e "${RED}未知参数: $arg${NC}"
                show_help
                exit 1
                ;;
        esac
    done
    
    echo -e "${GREEN}========================================${NC}"
    echo -e "${GREEN}  集群聊天服务器停止脚本${NC}"
    echo -e "${GREEN}========================================${NC}"
    
    stop_chat_servers
    stop_nginx
    
    if [ "$with_redis" = true ]; then
        stop_redis --with-redis
    fi
    
    cleanup
    show_stop_status "$@"
}

# 运行主函数
main "$@"
