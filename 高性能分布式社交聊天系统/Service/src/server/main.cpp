#include "DistributedServiceCoordinator.hpp"
#include <iostream>
#include <csignal>
#include <thread>
#include <chrono>
#include <fstream>

using namespace std;

// 全局服务协调器实例
unique_ptr<DistributedServiceCoordinator> g_coordinator;

// 信号处理函数
void signal_handler(int signal) {
    if (g_coordinator) {
        g_coordinator->shutdown();
    }
    exit(signal);
}

// 显示帮助信息
void show_help() {
    cout << "分布式聊天服务启动器\n"
         << "用法: ChatServer [选项]\n\n"
         << "选项:\n"
         << "  --config FILE        配置文件路径 (默认: ./config/distributed_services.json)\n"
         << "  --service NAME       启动指定服务 (如果不指定则启动协调器)\n"
         << "  --daemon             以守护进程模式运行\n"
         << "  --help, -h           显示此帮助信息\n\n"
         << "可用服务:\n"
         << "  AuthService          用户认证服务\n"
         << "  PrivateChatService   私聊服务\n"
         << "  GroupChatService     群聊服务\n"
         << "  FriendService        好友管理服务\n"
         << "  FileService          文件服务\n"
         << "  LocationService      位置服务\n"
         << "  NotificationService  通知服务\n\n"
         << "示例:\n"
         << "  ChatServer                                    # 启动服务协调器\n"
         << "  ChatServer --service AuthService              # 仅启动认证服务\n"
         << "  ChatServer --config /path/to/config.json     # 使用指定配置文件\n";
}

// 解析命令行参数
struct CommandLineArgs {
    string config_file = "./config/distributed_services.json";
    string service_name = "";
    bool daemon_mode = false;
    bool show_help = false;
};

CommandLineArgs parse_args(int argc, char* argv[]) {
    CommandLineArgs args;
    
    for (int i = 1; i < argc; ++i) {
        string arg = argv[i];
        
        if (arg == "--help" || arg == "-h") {
            args.show_help = true;
            break;
        } else if (arg == "--config" && i + 1 < argc) {
            args.config_file = argv[++i];
        } else if (arg == "--service" && i + 1 < argc) {
            args.service_name = argv[++i];
        } else if (arg == "--daemon") {
            args.daemon_mode = true;
        } else {
            cerr << "未知参数: " << arg << endl;
            args.show_help = true;
            break;
        }
    }
    
    return args;
}

// 验证配置文件
bool validate_config_file(const string& config_file) {
    ifstream file(config_file);
    if (!file.is_open()) {
        cerr << "错误: 无法打开配置文件: " << config_file << endl;
        return false;
    }
    
    // 简单的JSON格式验证
    string content((istreambuf_iterator<char>(file)), istreambuf_iterator<char>());
    if (content.empty() || content.find('{') == string::npos) {
        cerr << "错误: 配置文件格式无效" << endl;
        return false;
    }
    
    return true;
}

// 运行单个服务
int run_single_service(const string& service_name, const string& config_file) {
    try {
        g_coordinator = make_unique<DistributedServiceCoordinator>(config_file);
        
        cout << "启动服务: " << service_name << endl;
        
        if (!g_coordinator->start_service(service_name)) {
            cerr << "错误: 服务 " << service_name << " 启动失败" << endl;
            return 1;
        }
        
        cout << "服务 " << service_name << " 启动成功" << endl;
        
        // 等待服务运行
        while (g_coordinator->is_service_running(service_name)) {
            this_thread::sleep_for(chrono::seconds(5));
        }
        
        cout << "服务 " << service_name << " 已停止" << endl;
        return 0;
        
    } catch (const exception& e) {
        cerr << "错误: " << e.what() << endl;
        return 1;
    }
}

// 运行服务协调器
int run_coordinator(const string& config_file, bool daemon_mode) {
    try {
        g_coordinator = make_unique<DistributedServiceCoordinator>(config_file);
        
        cout << "启动分布式服务协调器..." << endl;
        
        if (!g_coordinator->start_all_services()) {
            cerr << "错误: 服务协调器启动失败" << endl;
            return 1;
        }
        
        cout << "所有服务启动成功，协调器正在运行..." << endl;
        
        if (daemon_mode) {
            cout << "以守护进程模式运行，按 Ctrl+C 停止" << endl;
        } else {
            cout << "按 Enter 键停止所有服务..." << endl;
            cin.get();
        }
        
        return 0;
        
    } catch (const exception& e) {
        cerr << "错误: " << e.what() << endl;
        return 1;
    }
}

// 设置守护进程模式
void setup_daemon_mode() {
#ifdef __linux__
    // Linux下的守护进程设置
    if (daemon(0, 0) != 0) {
        cerr << "错误: 无法设置守护进程模式" << endl;
        exit(1);
    }
#endif
}

// 设置信号处理
void setup_signal_handlers() {
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
#ifdef SIGQUIT
    signal(SIGQUIT, signal_handler);
#endif
}

// 创建PID文件
void create_pid_file() {
    ofstream pid_file("./pids/coordinator.pid");
    if (pid_file.is_open()) {
#ifdef _WIN32
        pid_file << GetCurrentProcessId() << endl;
#else
        pid_file << getpid() << endl;
#endif
        pid_file.close();
    }
}

int main(int argc, char* argv[]) {
    // 解析命令行参数
    CommandLineArgs args = parse_args(argc, argv);
    
    if (args.show_help) {
        show_help();
        return 0;
    }
    
    // 验证配置文件
    if (!validate_config_file(args.config_file)) {
        return 1;
    }
    
    // 设置信号处理
    setup_signal_handlers();
    
    try {
        // 如果指定了服务名，则运行单个服务
        if (!args.service_name.empty()) {
            return run_single_service(args.service_name, args.config_file);
        }
        
        // 否则运行服务协调器
        if (args.daemon_mode) {
            setup_daemon_mode();
            create_pid_file();
        }
        
        return run_coordinator(args.config_file, args.daemon_mode);
        
    } catch (const exception& e) {
        cerr << "致命错误: " << e.what() << endl;
        return 1;
    }
}