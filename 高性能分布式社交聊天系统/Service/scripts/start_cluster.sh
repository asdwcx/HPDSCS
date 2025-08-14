#!/bin/bash

# 集群聊天服务器启动脚本
# 用于启动多个服务器实例以形成集群

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
BUILD_DIR="$SERVICE_DIR/build"
CONFIG_DIR="$SERVICE_DIR/config"
NGINX_DIR="$SERVICE_DIR/nginx"
LOGS_DIR="$SERVICE_DIR/logs"

# 服务器配置
declare -a SERVER_PORTS=(8000 8001 8002)
declare -a SERVER_IDS=("server-001" "server-002" "server-003")

# 创建必要的目录
create_directories() {
    echo -e "${BLUE}创建必要的目录...${NC}"
    mkdir -p "$LOGS_DIR"
    mkdir -p "$SERVICE_DIR/uploads"
    mkdir -p "/tmp/nginx"
}

# 检查依赖
check_dependencies() {
    echo -e "${BLUE}检查依赖项...${NC}"
    
    # 检查Redis
    if ! command -v redis-server &> /dev/null; then
        echo -e "${RED}错误: Redis未安装${NC}"
        exit 1
    fi
    
    # 检查MySQL
    if ! command -v mysql &> /dev/null; then
        echo -e "${RED}错误: MySQL未安装${NC}"
        exit 1
    fi
    
    # 检查Nginx
    if ! command -v nginx &> /dev/null; then
        echo -e "${RED}错误: Nginx未安装${NC}"
        exit 1
    fi
    
    # 检查可执行文件
    if [ ! -f "$BUILD_DIR/src/server/ChatServer" ]; then
        echo -e "${RED}错误: ChatServer可执行文件不存在，请先编译${NC}"
        exit 1
    fi
    
    echo -e "${GREEN}依赖检查完成${NC}"
}

# 启动Redis
start_redis() {
    echo -e "${BLUE}启动Redis服务...${NC}"
    
    if pgrep -x "redis-server" > /dev/null; then
        echo -e "${YELLOW}Redis已经在运行${NC}"
    else
        redis-server --daemonize yes --logfile "$LOGS_DIR/redis.log"
        sleep 2
        if pgrep -x "redis-server" > /dev/null; then
            echo -e "${GREEN}Redis启动成功${NC}"
        else
            echo -e "${RED}Redis启动失败${NC}"
            exit 1
        fi
    fi
}

# 启动MySQL (检查是否运行)
check_mysql() {
    echo -e "${BLUE}检查MySQL服务...${NC}"
    
    if pgrep -x "mysqld" > /dev/null || systemctl is-active --quiet mysql; then
        echo -e "${GREEN}MySQL正在运行${NC}"
    else
        echo -e "${RED}MySQL未运行，请启动MySQL服务${NC}"
        echo -e "${YELLOW}运行: sudo systemctl start mysql${NC}"
        exit 1
    fi
}

# 生成服务器配置文件
generate_server_configs() {
    echo -e "${BLUE}生成服务器配置文件...${NC}"
    
    for i in "${!SERVER_PORTS[@]}"; do
        local port=${SERVER_PORTS[$i]}
        local server_id=${SERVER_IDS[$i]}
        local config_file="$CONFIG_DIR/cluster_${port}.conf"
        
        # 复制基础配置
        cp "$CONFIG_DIR/cluster.conf" "$config_file"
        
        # 修改端口和服务器ID
        sed -i "s/server.port=8000/server.port=$port/" "$config_file"
        sed -i "s/cluster.server_id=/cluster.server_id=$server_id/" "$config_file"
        
        echo -e "${GREEN}生成配置文件: $config_file${NC}"
    done
}

# 启动聊天服务器
start_chat_servers() {
    echo -e "${BLUE}启动聊天服务器集群...${NC}"
    
    for i in "${!SERVER_PORTS[@]}"; do
        local port=${SERVER_PORTS[$i]}
        local server_id=${SERVER_IDS[$i]}
        local config_file="$CONFIG_DIR/cluster_${port}.conf"
        local log_file="$LOGS_DIR/chatserver_${port}.log"
        local pid_file="$LOGS_DIR/chatserver_${port}.pid"
        
        # 检查服务器是否已经在运行
        if [ -f "$pid_file" ] && kill -0 $(cat "$pid_file") 2>/dev/null; then
            echo -e "${YELLOW}服务器 $server_id (端口:$port) 已在运行${NC}"
            continue
        fi
        
        # 启动服务器
        echo -e "${BLUE}启动服务器 $server_id (端口:$port)...${NC}"
        cd "$BUILD_DIR"
        nohup ./src/server/ChatServer "$config_file" > "$log_file" 2>&1 &
        local server_pid=$!
        echo $server_pid > "$pid_file"
        
        # 等待服务器启动
        sleep 2
        if kill -0 $server_pid 2>/dev/null; then
            echo -e "${GREEN}服务器 $server_id 启动成功 (PID: $server_pid)${NC}"
        else
            echo -e "${RED}服务器 $server_id 启动失败${NC}"
            cat "$log_file"
            exit 1
        fi
    done
}

# 启动Nginx负载均衡器
start_nginx() {
    echo -e "${BLUE}启动Nginx负载均衡器...${NC}"
    
    # 检查Nginx配置
    if ! nginx -t -c "$NGINX_DIR/nginx.conf"; then
        echo -e "${RED}Nginx配置文件有错误${NC}"
        exit 1
    fi
    
    # 启动Nginx
    if pgrep -x "nginx" > /dev/null; then
        echo -e "${YELLOW}重新加载Nginx配置...${NC}"
        nginx -s reload -c "$NGINX_DIR/nginx.conf"
    else
        echo -e "${BLUE}启动Nginx...${NC}"
        nginx -c "$NGINX_DIR/nginx.conf"
    fi
    
    if pgrep -x "nginx" > /dev/null; then
        echo -e "${GREEN}Nginx启动成功${NC}"
    else
        echo -e "${RED}Nginx启动失败${NC}"
        exit 1
    fi
}

# 显示集群状态
show_cluster_status() {
    echo -e "\n${BLUE}========== 集群状态 ==========${NC}"
    
    # Redis状态
    if pgrep -x "redis-server" > /dev/null; then
        echo -e "${GREEN}✓ Redis: 运行中${NC}"
    else
        echo -e "${RED}✗ Redis: 未运行${NC}"
    fi
    
    # MySQL状态
    if pgrep -x "mysqld" > /dev/null || systemctl is-active --quiet mysql; then
        echo -e "${GREEN}✓ MySQL: 运行中${NC}"
    else
        echo -e "${RED}✗ MySQL: 未运行${NC}"
    fi
    
    # Nginx状态
    if pgrep -x "nginx" > /dev/null; then
        echo -e "${GREEN}✓ Nginx: 运行中${NC}"
        echo -e "  负载均衡器地址: http://localhost:8080"
        echo -e "  管理接口: http://localhost:8080/status"
    else
        echo -e "${RED}✗ Nginx: 未运行${NC}"
    fi
    
    # 聊天服务器状态
    echo -e "\n${BLUE}聊天服务器状态:${NC}"
    for i in "${!SERVER_PORTS[@]}"; do
        local port=${SERVER_PORTS[$i]}
        local server_id=${SERVER_IDS[$i]}
        local pid_file="$LOGS_DIR/chatserver_${port}.pid"
        
        if [ -f "$pid_file" ] && kill -0 $(cat "$pid_file") 2>/dev/null; then
            local pid=$(cat "$pid_file")
            echo -e "${GREEN}✓ $server_id (端口:$port, PID:$pid): 运行中${NC}"
        else
            echo -e "${RED}✗ $server_id (端口:$port): 未运行${NC}"
        fi
    done
    
    echo -e "\n${BLUE}日志文件位置: $LOGS_DIR${NC}"
    echo -e "${BLUE}配置文件位置: $CONFIG_DIR${NC}"
}

# 主函数
main() {
    echo -e "${GREEN}========================================${NC}"
    echo -e "${GREEN}  集群聊天服务器启动脚本${NC}"
    echo -e "${GREEN}========================================${NC}"
    
    create_directories
    check_dependencies
    start_redis
    check_mysql
    generate_server_configs
    start_chat_servers
    start_nginx
    show_cluster_status
    
    echo -e "\n${GREEN}🎉 集群启动完成！${NC}"
    echo -e "${BLUE}使用以下命令查看日志:${NC}"
    echo -e "  tail -f $LOGS_DIR/chatserver_8000.log"
    echo -e "\n${BLUE}使用以下命令停止集群:${NC}"
    echo -e "  ./scripts/stop_cluster.sh"
}

# 运行主函数
main "$@"
