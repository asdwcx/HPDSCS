#ifndef FILE_TRANSFER_REDIS_MODEL_HPP
#define FILE_TRANSFER_REDIS_MODEL_HPP

#include <string>
#include <vector>
#include <map>
#include <chrono>
#include <memory>
#include <hiredis/hiredis.h>

using namespace std;

// 前向声明，避免循环依赖
struct FileTransferSession;

// 基于Redis的文件传输状态管理模型
class FileTransferRedisModel {
public:
    FileTransferRedisModel();
    ~FileTransferRedisModel();
    
    // 初始化Redis连接
    bool init_redis_connection();
    
    // ================= 替换MySQL的传输会话管理 =================
    
    // 创建传输会话 - 替换MySQL的create_transfer_session
    bool create_transfer_session(const FileTransferSession& session);
    
    // 查询传输会话 - 替换MySQL的query_transfer_session
    FileTransferSession query_transfer_session(const string& session_id);
    
    // 更新分片状态 - 替换MySQL的update_chunk_status
    bool update_chunk_status(const string& session_id, int chunk_seq);
    
    // 更新传输状态 - 替换MySQL的update_transfer_status
    bool update_transfer_status(const string& session_id, int status);
    
    // 删除传输会话 - 替换MySQL的delete_transfer_session
    bool delete_transfer_session(const string& session_id);
    
    // ================= 断点续传功能增强 =================
    
    // 获取缺失的分片列表
    vector<int> get_missing_chunks(const string& session_id);
    
    // 检查传输是否可恢复
    bool is_transfer_resumable(const string& session_id);
    
    // 恢复传输会话
    bool resume_transfer_session(const string& session_id);
    
    // 获取传输进度百分比
    double get_transfer_progress(const string& session_id);
    
    // 验证已接收分片的完整性
    bool verify_received_chunks(const string& session_id);
    
    // ================= 用户传输管理 =================
    
    // 获取用户的活跃传输
    vector<FileTransferSession> get_active_transfers(int user_id);
    
    // 获取用户的失败传输
    vector<FileTransferSession> get_failed_transfers(int user_id);
    
    // 清理过期的传输会话
    bool cleanup_expired_sessions(int hours = 24);
    
    // ================= 工具函数 =================
    
    // 生成传输会话ID
    string generate_session_id();
    
    // 检查分片是否已接收
    bool is_chunk_received(const string& session_id, int chunk_seq);
    
    // 获取会话统计信息
    map<string, int> get_session_stats();
    
    // 批量更新分片状态（用于并发优化）
    bool batch_update_chunks(const string& session_id, const vector<int>& chunk_sequences);

private:
    Redis redis_;
    bool redis_connected_;
    
    // Redis键名生成
    string get_session_key(const string& session_id);
    string get_bitmap_key(const string& session_id);
    string get_user_transfers_key(int user_id);
    string get_global_sessions_key();
    
    // 内部辅助函数
    bool session_exists(const string& session_id);
    void update_last_activity(const string& session_id);
    bool add_to_user_transfers(int user_id, const string& session_id);
    bool remove_from_user_transfers(int user_id, const string& session_id);
    
    // 数据序列化和反序列化
    map<string, string> session_to_hash(const FileTransferSession& session);
    FileTransferSession hash_to_session(const string& session_id, const map<string, string>& hash_data);
    
    // 错误处理和日志
    void log_redis_error(const string& operation, const string& error);
    bool handle_redis_error(const string& operation);
};

#endif // FILE_TRANSFER_REDIS_MODEL_HPP
