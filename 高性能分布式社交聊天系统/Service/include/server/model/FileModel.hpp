#ifndef FILE_MODEL_HPP
#define FILE_MODEL_HPP

#include <string>
#include <vector>
#include <memory>
#include "User.hpp"
#include "FileTransferRedisModel.hpp"

using namespace std;

// 文件信息结构体
struct FileInfo {
    string file_id;         // 文件唯一ID
    string file_name;       // 文件名
    string file_path;       // 文件存储路径
    int file_size;          // 文件大小（字节）
    string file_type;       // 文件类型/扩展名
    string file_hash;       // 文件MD5校验值
    int sender_id;          // 发送者ID
    int receiver_id;        // 接收者ID（个人聊天）
    int group_id;           // 群组ID（群聊，-1表示个人聊天）
    string upload_time;     // 上传时间
    int status;             // 文件状态：0-上传中，1-上传完成，2-下载中，3-已下载
    
    FileInfo() : receiver_id(-1), group_id(-1), file_size(0), status(0) {}
};

// 文件分片信息
struct FileChunk {
    string file_id;         // 文件ID
    int chunk_seq;          // 分片序号（从1开始）
    int total_chunks;       // 总分片数
    int chunk_size;         // 当前分片大小
    string chunk_data;      // 分片数据（Base64编码）
    bool is_last;           // 是否为最后一个分片
    
    FileChunk() : chunk_seq(0), total_chunks(0), chunk_size(0), is_last(false) {}
};

// 文件传输会话
struct FileTransferSession {
    string session_id;      // 传输会话ID
    string file_id;         // 文件ID
    int sender_id;          // 发送者ID
    int receiver_id;        // 接收者ID
    int group_id;           // 群组ID
    int total_chunks;       // 总分片数
    int received_chunks;    // 已接收分片数
    string temp_file_path;  // 临时文件路径
    vector<bool> chunk_received; // 分片接收状态
    string start_time;      // 传输开始时间
    int transfer_status;    // 传输状态：0-进行中，1-完成，2-失败，3-取消
    
    FileTransferSession() : sender_id(0), receiver_id(0), group_id(-1), 
                           total_chunks(0), received_chunks(0), transfer_status(0) {}
};

class FileModel {
public:
    FileModel();
    ~FileModel();
    
    // 文件信息管理
    bool insert_file_info(const FileInfo& file_info);
    FileInfo query_file_info(const string& file_id);
    bool update_file_status(const string& file_id, int status);
    vector<FileInfo> query_user_files(int user_id, int limit = 50);
    vector<FileInfo> query_group_files(int group_id, int limit = 50);
    bool delete_file_info(const string& file_id);
    
    // 文件传输会话管理（使用Redis后端）
    bool create_transfer_session(const FileTransferSession& session);
    FileTransferSession query_transfer_session(const string& session_id);
    bool update_chunk_status(const string& session_id, int chunk_seq);
    bool update_transfer_status(const string& session_id, int status);
    bool delete_transfer_session(const string& session_id);
    
    // 文件存储路径管理
    string generate_file_id();
    string generate_session_id();
    string get_file_storage_path(const string& file_id, const string& file_name);
    string get_temp_file_path(const string& session_id);
    
    // 文件安全检查
    bool is_valid_file_type(const string& file_name);
    bool is_valid_file_size(int file_size);
    string calculate_file_hash(const string& file_path);
    
    // 断点续传功能
    vector<int> get_missing_chunks(const string& session_id);
    bool is_transfer_resumable(const string& session_id);
    bool resume_transfer_session(const string& session_id);
    double get_transfer_progress(const string& session_id);
    bool verify_received_chunks(const string& session_id);
    
    // 文件传输状态查询
    vector<FileTransferSession> get_active_transfers(int user_id);
    vector<FileTransferSession> get_failed_transfers(int user_id);
    bool cleanup_expired_sessions(int hours = 24);
    
    // 文件分片读取
    bool read_file_chunk(const string& file_id, uint32_t chunk_index, string& chunk_data);
    bool get_file_total_chunks(const string& file_id, uint32_t& total_chunks);

private:
    // Redis文件传输管理器
    unique_ptr<FileTransferRedisModel> redis_model_;
    
    // 数据库连接将在cpp文件中实现
    void init_database();
    
    // 断点续传内部实现
    bool save_chunk_to_temp_file(const string& session_id, const FileChunk& chunk);
    bool merge_chunks_to_final_file(const string& session_id);
    bool verify_chunks_integrity(const string& session_id);
    vector<int> scan_existing_chunks(const string& temp_file_path);
    
    // 辅助函数
    string get_temp_chunk_file_path(const string& session_id, int chunk_seq);
    bool cleanup_temp_chunks(const string& session_id);
    string decode_base64(const string& encoded_data);
};

#endif // FILE_MODEL_HPP
