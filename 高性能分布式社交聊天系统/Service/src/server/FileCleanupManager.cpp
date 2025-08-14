#include "FileCleanupManager.hpp"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <iomanip>
#include <ctime>

using namespace std;
using namespace std::filesystem;
using namespace std::chrono;

// 静态常量定义
const string FileCleanupManager::TEMP_BASE_DIR = "./temp/";
const string FileCleanupManager::CLEANUP_LOG_FILE = "./logs/file_cleanup.log";

FileCleanupManager::FileCleanupManager(FileModel& file_model, FileTransferRedisModel& redis_model)
    : file_model_(file_model), redis_model_(redis_model), is_running_(false) {
    
    // 初始化临时目录列表
    temp_directories_ = {
        "./temp/",
        "./uploads/temp/",
        "/tmp/file_transfer/"
    };
    
    // 初始化统计信息
    stats_.last_cleanup_time = "从未执行";
    stats_.next_cleanup_time = calculate_next_cleanup_time();
    
    // 确保日志目录存在
    create_directories("./logs/");
    
    log_cleanup_activity("FileCleanupManager", "文件清理管理器初始化完成");
}

FileCleanupManager::~FileCleanupManager() {
    stop_cleanup_service();
    log_cleanup_activity("FileCleanupManager", "文件清理管理器已销毁");
}

bool FileCleanupManager::start_cleanup_service() {
    if (is_running_.load()) {
        cout << "清理服务已在运行中" << endl;
        return true;
    }
    
    if (!config_.auto_start) {
        cout << "自动启动已禁用，请手动调用 manual_cleanup()" << endl;
        return false;
    }
    
    is_running_.store(true);
    cleanup_thread_ = thread(&FileCleanupManager::cleanup_worker_thread, this);
    
    log_cleanup_activity("StartService", "定时清理服务已启动，间隔: " + to_string(config_.cleanup_interval_minutes) + "分钟");
    cout << "✅ 文件清理服务已启动" << endl;
    return true;
}

void FileCleanupManager::stop_cleanup_service() {
    if (!is_running_.load()) {
        return;
    }
    
    is_running_.store(false);
    
    if (cleanup_thread_.joinable()) {
        cleanup_thread_.join();
    }
    
    log_cleanup_activity("StopService", "定时清理服务已停止");
    cout << "⏹️ 文件清理服务已停止" << endl;
}

bool FileCleanupManager::manual_cleanup() {
    log_cleanup_activity("ManualCleanup", "开始手动清理");
    cout << "🧹 开始手动清理临时文件..." << endl;
    
    return perform_cleanup_task();
}

void FileCleanupManager::set_cleanup_config(const CleanupConfig& config) {
    config_ = config;
    
    // 验证配置参数
    if (config_.cleanup_interval_minutes < MIN_CLEANUP_INTERVAL_MINUTES) {
        config_.cleanup_interval_minutes = MIN_CLEANUP_INTERVAL_MINUTES;
    }
    if (config_.cleanup_interval_minutes > MAX_CLEANUP_INTERVAL_MINUTES) {
        config_.cleanup_interval_minutes = MAX_CLEANUP_INTERVAL_MINUTES;
    }
    
    {
        lock_guard<mutex> lock(stats_mutex_);
        stats_.next_cleanup_time = calculate_next_cleanup_time();
    }
    
    log_cleanup_activity("ConfigUpdate", "清理配置已更新");
}

CleanupStats FileCleanupManager::get_cleanup_stats() const {
    lock_guard<mutex> lock(stats_mutex_);
    return stats_;
}

bool FileCleanupManager::is_temp_dir_size_exceeded() {
    int64_t total_size = 0;
    for (const string& dir : temp_directories_) {
        if (exists(dir)) {
            total_size += calculate_directory_size(dir);
        }
    }
    
    return total_size > config_.max_temp_dir_size;
}

bool FileCleanupManager::force_cleanup_session(const string& session_id) {
    log_cleanup_activity("ForceCleanup", "强制清理会话: " + session_id);
    
    try {
        // 1. 清理临时文件
        bool files_cleaned = file_model_.cleanup_temp_chunks(session_id);
        
        // 2. 清理Redis会话数据
        bool session_cleaned = redis_model_.delete_transfer_session(session_id);
        
        if (files_cleaned && session_cleaned) {
            {
                lock_guard<mutex> lock(stats_mutex_);
                stats_.expired_sessions_cleaned++;
            }
            log_cleanup_activity("ForceCleanupSuccess", "会话清理成功: " + session_id);
            return true;
        } else {
            {
                lock_guard<mutex> lock(stats_mutex_);
                stats_.failed_cleanup_attempts++;
            }
            log_cleanup_activity("ForceCleanupFailed", "会话清理失败: " + session_id);
            return false;
        }
    } catch (const exception& e) {
        log_cleanup_activity("ForceCleanupError", "会话清理异常: " + session_id + ", 错误: " + e.what());
        return false;
    }
}

void FileCleanupManager::set_cleanup_callback(function<void(const CleanupStats&)> callback) {
    cleanup_callback_ = callback;
}

FileCleanupManager::TempDirInfo FileCleanupManager::get_temp_dir_info() const {
    TempDirInfo info = {0, 0, 0, 0};
    
    for (const string& dir : temp_directories_) {
        if (!exists(dir)) continue;
        
        try {
            for (const auto& entry : recursive_directory_iterator(dir)) {
                if (entry.is_regular_file()) {
                    info.total_files++;
                    info.total_size += entry.file_size();
                }
            }
        } catch (const exception& e) {
            log_cleanup_activity("TempDirInfoError", "获取目录信息失败: " + dir + ", 错误: " + e.what());
        }
    }
    
    return info;
}

int FileCleanupManager::cleanup_orphaned_files() {
    int orphaned_count = 0;
    
    for (const string& dir : temp_directories_) {
        if (!exists(dir)) continue;
        
        try {
            for (const auto& entry : directory_iterator(dir)) {
                if (entry.is_directory()) {
                    string session_id = entry.path().filename().string();
                    
                    // 检查会话是否仍然存在
                    auto session = redis_model_.query_transfer_session(session_id);
                    if (session.session_id.empty()) {
                        // 会话不存在，删除孤儿目录
                        remove_all(entry.path());
                        orphaned_count++;
                        log_cleanup_activity("OrphanedCleanup", "删除孤儿目录: " + session_id);
                    }
                }
            }
        } catch (const exception& e) {
            log_cleanup_activity("OrphanedCleanupError", "清理孤儿文件失败: " + dir + ", 错误: " + e.what());
        }
    }
    
    return orphaned_count;
}

// =================== 私有方法实现 ===================

void FileCleanupManager::cleanup_worker_thread() {
    log_cleanup_activity("WorkerThread", "清理工作线程已启动");
    
    while (is_running_.load()) {
        try {
            // 等待清理间隔
            auto interval = minutes(config_.cleanup_interval_minutes);
            auto start_time = steady_clock::now();
            
            while (is_running_.load() && (steady_clock::now() - start_time) < interval) {
                this_thread::sleep_for(seconds(30)); // 每30秒检查一次停止标志
            }
            
            if (!is_running_.load()) break;
            
            // 执行清理任务
            perform_cleanup_task();
            
        } catch (const exception& e) {
            log_cleanup_activity("WorkerThreadError", "清理线程异常: " + string(e.what()));
            {
                lock_guard<mutex> lock(stats_mutex_);
                stats_.failed_cleanup_attempts++;
            }
        }
    }
    
    log_cleanup_activity("WorkerThread", "清理工作线程已停止");
}

bool FileCleanupManager::perform_cleanup_task() {
    auto start_time = steady_clock::now();
    log_cleanup_activity("CleanupTask", "开始执行清理任务");
    
    CleanupStats current_stats = {0, 0, 0, 0, "", ""};
    
    try {
        // 1. 检查临时目录大小
        if (is_temp_dir_size_exceeded()) {
            log_cleanup_activity("SizeExceeded", "临时目录大小超限，启用积极清理模式");
            config_.enable_aggressive_cleanup = true;
        }
        
        // 2. 清理过期临时文件
        current_stats.total_temp_files_cleaned = cleanup_expired_temp_files();
        
        // 3. 清理过期传输会话
        current_stats.expired_sessions_cleaned = cleanup_expired_sessions();
        
        // 4. 清理孤儿文件
        int orphaned_files = cleanup_orphaned_files();
        current_stats.total_temp_files_cleaned += orphaned_files;
        
        // 5. 计算释放的磁盘空间
        auto temp_info = get_temp_dir_info();
        
        // 6. 更新统计信息
        {
            lock_guard<mutex> lock(stats_mutex_);
            stats_.total_temp_files_cleaned += current_stats.total_temp_files_cleaned;
            stats_.expired_sessions_cleaned += current_stats.expired_sessions_cleaned;
            stats_.last_cleanup_time = get_current_time_string();
            stats_.next_cleanup_time = calculate_next_cleanup_time();
            current_stats = stats_;
        }
        
        // 7. 生成清理报告
        generate_cleanup_report(current_stats);
        
        // 8. 调用回调函数
        if (cleanup_callback_) {
            cleanup_callback_(current_stats);
        }
        
        auto end_time = steady_clock::now();
        auto duration = duration_cast<milliseconds>(end_time - start_time);
        
        log_cleanup_activity("CleanupTaskComplete", 
                           "清理任务完成，耗时: " + to_string(duration.count()) + "ms, " +
                           "清理文件: " + to_string(current_stats.total_temp_files_cleaned) + "个, " +
                           "清理会话: " + to_string(current_stats.expired_sessions_cleaned) + "个");
        
        return true;
        
    } catch (const exception& e) {
        {
            lock_guard<mutex> lock(stats_mutex_);
            stats_.failed_cleanup_attempts++;
        }
        log_cleanup_activity("CleanupTaskError", "清理任务失败: " + string(e.what()));
        return false;
    }
}

int FileCleanupManager::cleanup_expired_temp_files() {
    int cleaned_count = 0;
    
    for (const string& dir : temp_directories_) {
        cleaned_count += cleanup_directory(dir, config_.temp_file_expire_hours);
    }
    
    return cleaned_count;
}

int FileCleanupManager::cleanup_expired_sessions() {
    int cleaned_count = 0;
    
    try {
        // 使用Redis模型清理过期会话
        if (redis_model_.cleanup_expired_sessions()) {
            cleaned_count = 1; // Redis模型的返回值是bool，这里设为1表示执行了清理
        }
        
    } catch (const exception& e) {
        log_cleanup_activity("SessionCleanupError", "清理过期会话失败: " + string(e.what()));
    }
    
    return cleaned_count;
}

int FileCleanupManager::cleanup_directory(const string& dir_path, int expire_hours) {
    int cleaned_count = 0;
    
    if (!exists(dir_path)) {
        return 0;
    }
    
    try {
        for (const auto& entry : directory_iterator(dir_path)) {
            if (entry.is_regular_file() || entry.is_directory()) {
                int age_hours = get_file_age_hours(entry.path().string());
                
                if (age_hours > expire_hours || 
                    (config_.enable_aggressive_cleanup && age_hours > expire_hours / 2)) {
                    
                    if (entry.is_directory()) {
                        remove_all(entry.path());
                    } else {
                        remove(entry.path());
                    }
                    cleaned_count++;
                    
                    log_cleanup_activity("FileCleanup", 
                                       "删除过期文件: " + entry.path().string() + 
                                       ", 年龄: " + to_string(age_hours) + "小时");
                }
            }
        }
    } catch (const exception& e) {
        log_cleanup_activity("DirectoryCleanupError", 
                           "清理目录失败: " + dir_path + ", 错误: " + e.what());
    }
    
    return cleaned_count;
}

int FileCleanupManager::get_file_age_hours(const string& file_path) {
    try {
        auto file_time = last_write_time(path(file_path));
        auto now = file_time_type::clock::now();
        auto duration = now - file_time;
        
        return duration_cast<hours>(duration).count();
    } catch (const exception& e) {
        log_cleanup_activity("FileAgeError", "获取文件年龄失败: " + file_path);
        return 0;
    }
}

int64_t FileCleanupManager::calculate_directory_size(const string& dir_path) {
    int64_t size = 0;
    
    try {
        for (const auto& entry : recursive_directory_iterator(dir_path)) {
            if (entry.is_regular_file()) {
                size += entry.file_size();
            }
        }
    } catch (const exception& e) {
        log_cleanup_activity("DirSizeError", "计算目录大小失败: " + dir_path);
    }
    
    return size;
}

void FileCleanupManager::generate_cleanup_report(const CleanupStats& stats) {
    stringstream report;
    report << "\n=== 文件清理报告 ===\n";
    report << "清理时间: " << stats.last_cleanup_time << "\n";
    report << "清理的临时文件: " << stats.total_temp_files_cleaned << " 个\n";
    report << "清理的过期会话: " << stats.expired_sessions_cleaned << " 个\n";
    report << "失败的清理尝试: " << stats.failed_cleanup_attempts << " 次\n";
    report << "释放的磁盘空间: " << (stats.freed_disk_space / 1024 / 1024) << " MB\n";
    report << "下次清理时间: " << stats.next_cleanup_time << "\n";
    report << "==================\n";
    
    cout << report.str();
    log_cleanup_activity("CleanupReport", report.str());
}

void FileCleanupManager::log_cleanup_activity(const string& activity, const string& details) {
    try {
        ofstream log_file(CLEANUP_LOG_FILE, ios::app);
        if (log_file.is_open()) {
            log_file << "[" << get_current_time_string() << "] " 
                     << activity << ": " << details << endl;
            log_file.close();
        }
    } catch (const exception& e) {
        cerr << "写入清理日志失败: " << e.what() << endl;
    }
}

string FileCleanupManager::get_current_time_string() {
    auto now = system_clock::now();
    auto time_t = system_clock::to_time_t(now);
    auto tm = *localtime(&time_t);
    
    stringstream ss;
    ss << put_time(&tm, "%Y-%m-%d %H:%M:%S");
    return ss.str();
}

string FileCleanupManager::calculate_next_cleanup_time() {
    auto now = system_clock::now();
    auto next = now + minutes(config_.cleanup_interval_minutes);
    auto time_t = system_clock::to_time_t(next);
    auto tm = *localtime(&time_t);
    
    stringstream ss;
    ss << put_time(&tm, "%Y-%m-%d %H:%M:%S");
    return ss.str();
}
