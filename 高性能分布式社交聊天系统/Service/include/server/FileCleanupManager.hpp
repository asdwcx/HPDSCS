#ifndef FILE_CLEANUP_MANAGER_HPP
#define FILE_CLEANUP_MANAGER_HPP

#include <thread>
#include <mutex>
#include <atomic>
#include <chrono>
#include <string>
#include <vector>
#include <functional>
#include <memory>
#include "FileModel.hpp"
#include "FileTransferRedisModel.hpp"

using namespace std;

/**
 * 文件清理统计信息
 */
struct CleanupStats {
    int total_temp_files_cleaned = 0;      // 清理的临时文件总数
    int expired_sessions_cleaned = 0;       // 清理的过期会话数
    int failed_cleanup_attempts = 0;       // 清理失败次数
    int64_t freed_disk_space = 0;          // 释放的磁盘空间(字节)
    string last_cleanup_time;              // 最后清理时间
    string next_cleanup_time;              // 下次清理时间
};

/**
 * 文件清理配置
 */
struct CleanupConfig {
    int cleanup_interval_minutes = 30;     // 清理间隔(分钟)
    int temp_file_expire_hours = 24;       // 临时文件过期时间(小时)
    int session_expire_hours = 48;         // 会话过期时间(小时)
    int64_t max_temp_dir_size = 1024*1024*1024; // 临时目录最大大小(1GB)
    bool enable_aggressive_cleanup = false; // 启用积极清理模式
    bool auto_start = true;                 // 自动启动清理服务
};

/**
 * 文件清理管理器
 * 负责定时清理过期的临时文件和传输会话
 */
class FileCleanupManager {
public:
    FileCleanupManager(FileModel& file_model, FileTransferRedisModel& redis_model);
    ~FileCleanupManager();
    
    // =================== 主要接口 ===================
    
    /**
     * 启动定时清理服务
     */
    bool start_cleanup_service();
    
    /**
     * 停止定时清理服务
     */
    void stop_cleanup_service();
    
    /**
     * 手动执行一次清理
     */
    bool manual_cleanup();
    
    /**
     * 设置清理配置
     */
    void set_cleanup_config(const CleanupConfig& config);
    
    /**
     * 获取清理统计信息
     */
    CleanupStats get_cleanup_stats() const;
    
    /**
     * 检查临时目录大小是否超限
     */
    bool is_temp_dir_size_exceeded();
    
    /**
     * 强制清理指定会话的临时文件
     */
    bool force_cleanup_session(const string& session_id);
    
    // =================== 高级功能 ===================
    
    /**
     * 设置清理完成回调
     */
    void set_cleanup_callback(function<void(const CleanupStats&)> callback);
    
    /**
     * 获取临时目录使用情况
     */
    struct TempDirInfo {
        int64_t total_size;
        int total_files;
        int active_sessions;
        int orphaned_files;
    };
    TempDirInfo get_temp_dir_info() const;
    
    /**
     * 清理孤儿临时文件（没有对应会话的文件）
     */
    int cleanup_orphaned_files();
    
private:
    // =================== 内部方法 ===================
    
    /**
     * 清理工作线程
     */
    void cleanup_worker_thread();
    
    /**
     * 执行定时清理任务
     */
    bool perform_cleanup_task();
    
    /**
     * 清理过期的临时文件
     */
    int cleanup_expired_temp_files();
    
    /**
     * 清理过期的传输会话
     */
    int cleanup_expired_sessions();
    
    /**
     * 清理特定目录下的过期文件
     */
    int cleanup_directory(const string& dir_path, int expire_hours);
    
    /**
     * 获取文件年龄（小时）
     */
    int get_file_age_hours(const string& file_path);
    
    /**
     * 计算目录大小
     */
    int64_t calculate_directory_size(const string& dir_path);
    
    /**
     * 生成清理报告
     */
    void generate_cleanup_report(const CleanupStats& stats);
    
    /**
     * 记录清理日志
     */
    void log_cleanup_activity(const string& activity, const string& details = "");
    
    /**
     * 获取当前时间字符串
     */
    string get_current_time_string();
    
    /**
     * 计算下次清理时间
     */
    string calculate_next_cleanup_time();
    
    // =================== 成员变量 ===================
    
    FileModel& file_model_;
    FileTransferRedisModel& redis_model_;
    
    // 清理服务状态
    atomic<bool> is_running_;
    thread cleanup_thread_;
    mutable mutex stats_mutex_;
    
    // 配置和统计
    CleanupConfig config_;
    CleanupStats stats_;
    
    // 回调函数
    function<void(const CleanupStats&)> cleanup_callback_;
    
    // 临时目录路径
    vector<string> temp_directories_;
    
    // 常量配置
    static const string TEMP_BASE_DIR;
    static const string CLEANUP_LOG_FILE;
    static const int MIN_CLEANUP_INTERVAL_MINUTES = 5;
    static const int MAX_CLEANUP_INTERVAL_MINUTES = 1440; // 24小时
};

#endif // FILE_CLEANUP_MANAGER_HPP
