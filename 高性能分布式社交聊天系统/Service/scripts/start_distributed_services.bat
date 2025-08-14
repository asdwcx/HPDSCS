@echo off
REM 分布式聊天服务启动脚本 (Windows版本)
REM Distributed Chat Services Startup Script for Windows

setlocal enabledelayedexpansion

REM 配置参数
set CONFIG_FILE=.\config\distributed_services.json
set LOG_DIR=.\logs
set PID_DIR=.\pids

REM 创建必要目录
if not exist "%LOG_DIR%" mkdir "%LOG_DIR%"
if not exist "%PID_DIR%" mkdir "%PID_DIR%"

REM 日志函数
:log_info
echo [INFO] %date% %time% - %~1
goto :eof

:log_warn
echo [WARN] %date% %time% - %~1
goto :eof

:log_error
echo [ERROR] %date% %time% - %~1
goto :eof

REM 检查依赖
:check_dependencies
call :log_info "检查系统依赖..."

REM 检查配置文件
if not exist "%CONFIG_FILE%" (
    call :log_error "配置文件不存在: %CONFIG_FILE%"
    exit /b 1
)

REM 检查可执行文件
if not exist ".\bin\ChatServer.exe" (
    call :log_error "ChatServer.exe可执行文件不存在，请先编译项目"
    exit /b 1
)

call :log_info "依赖检查完成"
goto :eof

REM 启动服务
:start_service
set service_name=%~1
set port=%~2

call :log_info "启动服务: %service_name% (端口: %port%)"

REM 检查端口是否被占用
netstat -an | findstr ":%port% " >nul 2>&1
if !errorlevel! equ 0 (
    call :log_warn "端口 %port% 已被占用，跳过服务 %service_name%"
    goto :eof
)

REM 启动服务
start /b "ChatService_%service_name%" cmd /c ".\bin\ChatServer.exe --service=%service_name% --config=%CONFIG_FILE% > %LOG_DIR%\%service_name%.log 2>&1"

REM 获取进程ID (Windows下比较复杂，这里简化处理)
timeout /t 2 >nul

call :log_info "服务 %service_name% 启动完成"
goto :eof

REM 启动所有服务
:start_all_services
call :log_info "启动所有分布式服务..."

call :start_service "AuthService" "50001"
timeout /t 1 >nul
call :start_service "PrivateChatService" "50002"
timeout /t 1 >nul
call :start_service "GroupChatService" "50003"
timeout /t 1 >nul
call :start_service "FriendService" "50004"
timeout /t 1 >nul
call :start_service "FileService" "50005"
timeout /t 1 >nul
call :start_service "LocationService" "50006"
timeout /t 1 >nul
call :start_service "NotificationService" "50007"

call :log_info "所有服务启动完成"
goto :eof

REM 停止服务
:stop_service
set service_name=%~1
call :log_info "停止服务: %service_name%"

REM 通过窗口标题杀死进程
taskkill /f /fi "WINDOWTITLE eq ChatService_%service_name%*" >nul 2>&1

call :log_info "服务 %service_name% 已停止"
goto :eof

REM 停止所有服务
:stop_all_services
call :log_info "停止所有分布式服务..."

call :stop_service "AuthService"
call :stop_service "PrivateChatService"
call :stop_service "GroupChatService"
call :stop_service "FriendService"
call :stop_service "FileService"
call :stop_service "LocationService"
call :stop_service "NotificationService"

REM 强制停止所有ChatServer进程
taskkill /f /im ChatServer.exe >nul 2>&1

call :log_info "所有服务已停止"
goto :eof

REM 查看服务状态
:show_status
call :log_info "服务状态:"
echo 服务名称                状态      端口
echo --------------------------------------------

set services=AuthService:50001 PrivateChatService:50002 GroupChatService:50003 FriendService:50004 FileService:50005 LocationService:50006 NotificationService:50007

for %%s in (!services!) do (
    for /f "tokens=1,2 delims=:" %%a in ("%%s") do (
        set service_name=%%a
        set port=%%b
        
        REM 检查端口是否被占用
        netstat -an | findstr ":!port! " >nul 2>&1
        if !errorlevel! equ 0 (
            echo !service_name!                运行中    !port!
        ) else (
            echo !service_name!                停止      !port!
        )
    )
)
goto :eof

REM 显示帮助信息
:show_help
echo 分布式聊天服务管理脚本 (Windows版本)
echo.
echo 用法: %~nx0 [命令] [选项]
echo.
echo 命令:
echo   start                启动所有服务
echo   stop                 停止所有服务
echo   restart              重启所有服务
echo   status               查看服务状态
echo   stop-service NAME    停止指定服务
echo   help                 显示此帮助信息
echo.
echo 服务名称:
echo   AuthService          用户认证服务
echo   PrivateChatService   私聊服务
echo   GroupChatService     群聊服务
echo   FriendService        好友管理服务
echo   FileService          文件服务
echo   LocationService      位置服务
echo   NotificationService  通知服务
echo.
echo 示例:
echo   %~nx0 start                        # 启动所有服务
echo   %~nx0 stop-service AuthService     # 停止认证服务
echo   %~nx0 status                       # 查看服务状态
goto :eof

REM 主函数
:main
if "%1"=="start" (
    call :check_dependencies
    call :start_all_services
) else if "%1"=="stop" (
    call :stop_all_services
) else if "%1"=="restart" (
    call :stop_all_services
    timeout /t 3 >nul
    call :check_dependencies
    call :start_all_services
) else if "%1"=="status" (
    call :show_status
) else if "%1"=="stop-service" (
    if "%2"=="" (
        call :log_error "请指定服务名称"
        exit /b 1
    )
    call :stop_service "%2"
) else if "%1"=="help" (
    call :show_help
) else if "%1"=="--help" (
    call :show_help
) else if "%1"=="-h" (
    call :show_help
) else if "%1"=="" (
    call :show_help
) else (
    call :log_error "未知命令: %1"
    call :show_help
    exit /b 1
)

goto :eof

REM 调用主函数
call :main %*
