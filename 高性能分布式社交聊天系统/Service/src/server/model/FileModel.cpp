#include "FileModel.hpp"
#include "db/MySQL.hpp"
#include <iostream>
#include <sstream>
#include <iomanip>
#include <random>
#include <chrono>
#include <fstream>
#include <filesystem>
#include <openssl/md5.h>

using namespace std;
using namespace std::filesystem;

FileModel::FileModel() {
    init_database();
    
    // 初始化Redis文件传输模型
    redis_model_ = make_unique<FileTransferRedisModel>();
    if (!redis_model_->init()) {
        cout << "Warning: Redis文件传输模型初始化失败，将降级到MySQL模式" << endl;
        redis_model_.reset();
    }
}

FileModel::~FileModel() {
}

void FileModel::init_database() {
    // 数据库初始化将使用现有的MySQL连接
}

bool FileModel::insert_file_info(const FileInfo& file_info) {
    MySQL mysql;
    if (!mysql.connect()) {
        cout << "FileModel insert_file_info MySQL connect error!" << endl;
        return false;
    }

    char sql[2048] = {0};
    sprintf(sql, "INSERT INTO FileInfo (file_id, file_name, file_path, file_size, file_type, file_hash, sender_id, receiver_id, group_id, upload_time, status) VALUES ('%s', '%s', '%s', %d, '%s', '%s', %d, %d, %d, '%s', %d)",
            file_info.file_id.c_str(),
            file_info.file_name.c_str(),
            file_info.file_path.c_str(),
            file_info.file_size,
            file_info.file_type.c_str(),
            file_info.file_hash.c_str(),
            file_info.sender_id,
            file_info.receiver_id,
            file_info.group_id,
            file_info.upload_time.c_str(),
            file_info.status);

    if (mysql.update(sql)) {
        return true;
    }
    return false;
}

FileInfo FileModel::query_file_info(const string& file_id) {
    FileInfo file_info;
    MySQL mysql;
    if (!mysql.connect()) {
        cout << "FileModel query_file_info MySQL connect error!" << endl;
        return file_info;
    }

    char sql[1024] = {0};
    sprintf(sql, "SELECT file_id, file_name, file_path, file_size, file_type, file_hash, sender_id, receiver_id, group_id, upload_time, status FROM FileInfo WHERE file_id = '%s'", file_id.c_str());

    MYSQL_RES* res = mysql.query(sql);
    if (res != nullptr) {
        MYSQL_ROW row = mysql_fetch_row(res);
        if (row != nullptr) {
            file_info.file_id = row[0];
            file_info.file_name = row[1];
            file_info.file_path = row[2];
            file_info.file_size = atoi(row[3]);
            file_info.file_type = row[4];
            file_info.file_hash = row[5];
            file_info.sender_id = atoi(row[6]);
            file_info.receiver_id = atoi(row[7]);
            file_info.group_id = atoi(row[8]);
            file_info.upload_time = row[9];
            file_info.status = atoi(row[10]);
        }
        mysql_free_result(res);
    }
    return file_info;
}

bool FileModel::update_file_status(const string& file_id, int status) {
    MySQL mysql;
    if (!mysql.connect()) {
        cout << "FileModel update_file_status MySQL connect error!" << endl;
        return false;
    }

    char sql[512] = {0};
    sprintf(sql, "UPDATE FileInfo SET status = %d WHERE file_id = '%s'", status, file_id.c_str());

    return mysql.update(sql);
}

vector<FileInfo> FileModel::query_user_files(int user_id, int limit) {
    vector<FileInfo> files;
    MySQL mysql;
    if (!mysql.connect()) {
        cout << "FileModel query_user_files MySQL connect error!" << endl;
        return files;
    }

    char sql[1024] = {0};
    sprintf(sql, "SELECT file_id, file_name, file_path, file_size, file_type, file_hash, sender_id, receiver_id, group_id, upload_time, status FROM FileInfo WHERE sender_id = %d OR receiver_id = %d ORDER BY upload_time DESC LIMIT %d", 
            user_id, user_id, limit);

    MYSQL_RES* res = mysql.query(sql);
    if (res != nullptr) {
        MYSQL_ROW row;
        while ((row = mysql_fetch_row(res)) != nullptr) {
            FileInfo file_info;
            file_info.file_id = row[0];
            file_info.file_name = row[1];
            file_info.file_path = row[2];
            file_info.file_size = atoi(row[3]);
            file_info.file_type = row[4];
            file_info.file_hash = row[5];
            file_info.sender_id = atoi(row[6]);
            file_info.receiver_id = atoi(row[7]);
            file_info.group_id = atoi(row[8]);
            file_info.upload_time = row[9];
            file_info.status = atoi(row[10]);
            files.push_back(file_info);
        }
        mysql_free_result(res);
    }
    return files;
}

vector<FileInfo> FileModel::query_group_files(int group_id, int limit) {
    vector<FileInfo> files;
    MySQL mysql;
    if (!mysql.connect()) {
        cout << "FileModel query_group_files MySQL connect error!" << endl;
        return files;
    }

    char sql[1024] = {0};
    sprintf(sql, "SELECT file_id, file_name, file_path, file_size, file_type, file_hash, sender_id, receiver_id, group_id, upload_time, status FROM FileInfo WHERE group_id = %d ORDER BY upload_time DESC LIMIT %d", 
            group_id, limit);

    MYSQL_RES* res = mysql.query(sql);
    if (res != nullptr) {
        MYSQL_ROW row;
        while ((row = mysql_fetch_row(res)) != nullptr) {
            FileInfo file_info;
            file_info.file_id = row[0];
            file_info.file_name = row[1];
            file_info.file_path = row[2];
            file_info.file_size = atoi(row[3]);
            file_info.file_type = row[4];
            file_info.file_hash = row[5];
            file_info.sender_id = atoi(row[6]);
            file_info.receiver_id = atoi(row[7]);
            file_info.group_id = atoi(row[8]);
            file_info.upload_time = row[9];
            file_info.status = atoi(row[10]);
            files.push_back(file_info);
        }
        mysql_free_result(res);
    }
    return files;
}

bool FileModel::delete_file_info(const string& file_id) {
    MySQL mysql;
    if (!mysql.connect()) {
        cout << "FileModel delete_file_info MySQL connect error!" << endl;
        return false;
    }

    char sql[512] = {0};
    sprintf(sql, "DELETE FROM FileInfo WHERE file_id = '%s'", file_id.c_str());

    return mysql.update(sql);
}

// 传输会话管理实现（使用Redis后端）
bool FileModel::create_transfer_session(const FileTransferSession& session) {
    // 优先使用Redis，如果Redis不可用则回退到MySQL
    if (redis_model_) {
        return redis_model_->create_transfer_session(session);
    }
    
    // MySQL后备方案
    MySQL mysql;
    if (!mysql.connect()) {
        cout << "FileModel create_transfer_session MySQL connect error!" << endl;
        return false;
    }

    char sql[1024] = {0};
    sprintf(sql, "INSERT INTO FileTransferSession (session_id, file_id, sender_id, receiver_id, group_id, total_chunks, received_chunks, temp_file_path, start_time, transfer_status) VALUES ('%s', '%s', %d, %d, %d, %d, %d, '%s', '%s', %d)",
            session.session_id.c_str(),
            session.file_id.c_str(),
            session.sender_id,
            session.receiver_id,
            session.group_id,
            session.total_chunks,
            session.received_chunks,
            session.temp_file_path.c_str(),
            session.start_time.c_str(),
            session.transfer_status);

    return mysql.update(sql);
}

FileTransferSession FileModel::query_transfer_session(const string& session_id) {
    // 优先使用Redis
    if (redis_model_) {
        return redis_model_->query_transfer_session(session_id);
    }
    
    // MySQL后备方案
    FileTransferSession session;
    MySQL mysql;
    if (!mysql.connect()) {
        cout << "FileModel query_transfer_session MySQL connect error!" << endl;
        return session;
    }

    char sql[1024] = {0};
    sprintf(sql, "SELECT session_id, file_id, sender_id, receiver_id, group_id, total_chunks, received_chunks, temp_file_path, start_time, transfer_status FROM FileTransferSession WHERE session_id = '%s'", session_id.c_str());

    MYSQL_RES* res = mysql.query(sql);
    if (res != nullptr) {
        MYSQL_ROW row = mysql_fetch_row(res);
        if (row != nullptr) {
            session.session_id = row[0];
            session.file_id = row[1];
            session.sender_id = atoi(row[2]);
            session.receiver_id = atoi(row[3]);
            session.group_id = atoi(row[4]);
            session.total_chunks = atoi(row[5]);
            session.received_chunks = atoi(row[6]);
            session.temp_file_path = row[7];
            session.start_time = row[8];
            session.transfer_status = atoi(row[9]);
        }
        mysql_free_result(res);
    }
    return session;
}

bool FileModel::update_chunk_status(const string& session_id, int chunk_seq) {
    // 优先使用Redis
    if (redis_model_) {
        return redis_model_->update_chunk_status(session_id, chunk_seq);
    }
    
    // MySQL后备方案
    MySQL mysql;
    if (!mysql.connect()) {
        cout << "FileModel update_chunk_status MySQL connect error!" << endl;
        return false;
    }

    char sql[512] = {0};
    sprintf(sql, "UPDATE FileTransferSession SET received_chunks = received_chunks + 1 WHERE session_id = '%s'", session_id.c_str());

    return mysql.update(sql);
}

bool FileModel::update_transfer_status(const string& session_id, int status) {
    // 优先使用Redis
    if (redis_model_) {
        return redis_model_->update_transfer_status(session_id, status);
    }
    
    // MySQL后备方案
    MySQL mysql;
    if (!mysql.connect()) {
        cout << "FileModel update_transfer_status MySQL connect error!" << endl;
        return false;
    }

    char sql[512] = {0};
    sprintf(sql, "UPDATE FileTransferSession SET transfer_status = %d WHERE session_id = '%s'", status, session_id.c_str());

    return mysql.update(sql);
}

bool FileModel::delete_transfer_session(const string& session_id) {
    // 优先使用Redis
    if (redis_model_) {
        return redis_model_->delete_transfer_session(session_id);
    }
    
    // MySQL后备方案
    MySQL mysql;
    if (!mysql.connect()) {
        cout << "FileModel delete_transfer_session MySQL connect error!" << endl;
        return false;
    }

    char sql[512] = {0};
    sprintf(sql, "DELETE FROM FileTransferSession WHERE session_id = '%s'", session_id.c_str());

    return mysql.update(sql);
}

// 工具函数实现
string FileModel::generate_file_id() {
    auto now = chrono::system_clock::now();
    auto timestamp = chrono::duration_cast<chrono::milliseconds>(now.time_since_epoch()).count();
    
    random_device rd;
    mt19937 gen(rd());
    uniform_int_distribution<> dis(1000, 9999);
    
    stringstream ss;
    ss << "file_" << timestamp << "_" << dis(gen);
    return ss.str();
}

string FileModel::generate_session_id() {
    auto now = chrono::system_clock::now();
    auto timestamp = chrono::duration_cast<chrono::milliseconds>(now.time_since_epoch()).count();
    
    random_device rd;
    mt19937 gen(rd());
    uniform_int_distribution<> dis(1000, 9999);
    
    stringstream ss;
    ss << "session_" << timestamp << "_" << dis(gen);
    return ss.str();
}

string FileModel::get_file_storage_path(const string& file_id, const string& file_name) {
    // 创建存储目录结构: /files/YYYY/MM/DD/
    auto now = chrono::system_clock::now();
    auto time_t = chrono::system_clock::to_time_t(now);
    auto tm = *localtime(&time_t);
    
    stringstream path_stream;
    path_stream << "./files/" << (tm.tm_year + 1900) << "/" 
                << setfill('0') << setw(2) << (tm.tm_mon + 1) << "/"
                << setfill('0') << setw(2) << tm.tm_mday << "/";
    
    string dir_path = path_stream.str();
    
    // 创建目录
    try {
        create_directories(dir_path);
    } catch (const exception& e) {
        cout << "Create directory failed: " << e.what() << endl;
        return "./files/" + file_id + "_" + file_name;
    }
    
    return dir_path + file_id + "_" + file_name;
}

string FileModel::get_temp_file_path(const string& session_id) {
    string temp_dir = "./temp/";
    try {
        create_directories(temp_dir);
    } catch (const exception& e) {
        cout << "Create temp directory failed: " << e.what() << endl;
    }
    return temp_dir + session_id;
}

bool FileModel::is_valid_file_type(const string& file_name) {
    // 允许的文件类型
    vector<string> allowed_types = {
        ".txt", ".doc", ".docx", ".pdf", ".xls", ".xlsx", ".ppt", ".pptx",
        ".jpg", ".jpeg", ".png", ".gif", ".bmp", ".webp",
        ".mp3", ".wav", ".flac", ".aac",
        ".mp4", ".avi", ".mkv", ".mov", ".wmv",
        ".zip", ".rar", ".7z", ".tar", ".gz"
    };
    
    // 获取文件扩展名
    size_t dot_pos = file_name.find_last_of('.');
    if (dot_pos == string::npos) {
        return false; // 没有扩展名
    }
    
    string extension = file_name.substr(dot_pos);
    transform(extension.begin(), extension.end(), extension.begin(), ::tolower);
    
    return find(allowed_types.begin(), allowed_types.end(), extension) != allowed_types.end();
}

bool FileModel::is_valid_file_size(int file_size) {
    const int MAX_FILE_SIZE = 100 * 1024 * 1024; // 100MB
    return file_size > 0 && file_size <= MAX_FILE_SIZE;
}

string FileModel::calculate_file_hash(const string& file_path) {
    ifstream file(file_path, ios::binary);
    if (!file.is_open()) {
        return "";
    }
    
    MD5_CTX md5_ctx;
    MD5_Init(&md5_ctx);
    
    char buffer[8192];
    while (file.read(buffer, sizeof(buffer)) || file.gcount() > 0) {
        MD5_Update(&md5_ctx, buffer, file.gcount());
    }
    
    unsigned char result[MD5_DIGEST_LENGTH];
    MD5_Final(result, &md5_ctx);
    
    stringstream ss;
    for (int i = 0; i < MD5_DIGEST_LENGTH; i++) {
        ss << hex << setw(2) << setfill('0') << static_cast<unsigned int>(result[i]);
    }
    
    return ss.str();
}

// ==================== 断点续传核心功能实现 ====================

/**
 * 获取传输会话中缺失的分片列表
 * @param session_id 传输会话ID
 * @return 缺失分片的序号列表
 */
vector<int> FileModel::get_missing_chunks(const string& session_id) {
    vector<int> missing_chunks;
    
    // 1. 查询传输会话信息
    FileTransferSession session = query_transfer_session(session_id);
    if (session.session_id.empty()) {
        return missing_chunks; // 会话不存在
    }
    
    // 2. 扫描已存在的分片文件
    vector<int> existing_chunks = scan_existing_chunks(session.temp_file_path);
    
    // 3. 计算缺失的分片
    for (int i = 1; i <= session.total_chunks; i++) {
        if (find(existing_chunks.begin(), existing_chunks.end(), i) == existing_chunks.end()) {
            missing_chunks.push_back(i);
        }
    }
    
    cout << "Session " << session_id << " missing chunks: " << missing_chunks.size() 
         << " out of " << session.total_chunks << endl;
    
    return missing_chunks;
}

/**
 * 检查传输是否可以恢复
 * @param session_id 传输会话ID
 * @return 是否可恢复
 */
bool FileModel::is_transfer_resumable(const string& session_id) {
    FileTransferSession session = query_transfer_session(session_id);
    
    // 检查会话是否存在且状态为进行中
    if (session.session_id.empty() || session.transfer_status != 0) {
        cout << "Session " << session_id << " not resumable: invalid status" << endl;
        return false;
    }
    
    // 检查临时文件目录是否存在
    string temp_dir = "/uploads/temp/";
    if (!exists(temp_dir)) {
        cout << "Temp directory does not exist: " << temp_dir << endl;
        return false;
    }
    
    // 检查是否有已接收的分片
    vector<int> existing_chunks = scan_existing_chunks(session.temp_file_path);
    bool resumable = !existing_chunks.empty();
    
    cout << "Session " << session_id << " resumable: " << (resumable ? "YES" : "NO") 
         << " (existing chunks: " << existing_chunks.size() << ")" << endl;
    
    return resumable;
}

/**
 * 恢复传输会话
 * @param session_id 传输会话ID
 * @return 是否成功恢复
 */
bool FileModel::resume_transfer_session(const string& session_id) {
    if (!is_transfer_resumable(session_id)) {
        return false;
    }
    
    // 更新会话状态和最后活动时间
    char sql[512] = {0};
    sprintf(sql, "UPDATE FileTransferSession SET start_time = NOW() WHERE session_id = '%s'", 
            session_id.c_str());
    
    MySQL mysql;
    if (mysql.connect()) {
        bool result = mysql.update(sql);
        cout << "Resume session " << session_id << ": " << (result ? "SUCCESS" : "FAILED") << endl;
        return result;
    }
    
    return false;
}

/**
 * 获取传输进度百分比
 * @param session_id 传输会话ID
 * @return 进度百分比 (0.0-100.0)
 */
double FileModel::get_transfer_progress(const string& session_id) {
    FileTransferSession session = query_transfer_session(session_id);
    if (session.session_id.empty() || session.total_chunks == 0) {
        return 0.0;
    }
    
    // 实时扫描已接收分片数
    vector<int> existing_chunks = scan_existing_chunks(session.temp_file_path);
    double progress = (double)existing_chunks.size() / session.total_chunks * 100.0;
    
    cout << "Session " << session_id << " progress: " << fixed << setprecision(1) 
         << progress << "% (" << existing_chunks.size() << "/" << session.total_chunks << ")" << endl;
    
    return progress;
}

/**
 * 验证已接收分片的完整性
 * @param session_id 传输会话ID
 * @return 验证是否通过
 */
bool FileModel::verify_received_chunks(const string& session_id) {
    FileTransferSession session = query_transfer_session(session_id);
    if (session.session_id.empty()) {
        return false;
    }
    
    vector<int> existing_chunks = scan_existing_chunks(session.temp_file_path);
    int valid_chunks = 0;
    
    // 检查分片完整性
    for (int chunk_seq : existing_chunks) {
        string chunk_file = get_temp_chunk_file_path(session_id, chunk_seq);
        
        // 检查文件是否存在且大小合理
        if (exists(chunk_file)) {
            auto file_size = file_size(chunk_file);
            if (file_size > 0) {
                valid_chunks++;
            } else {
                // 删除空的分片文件
                remove(chunk_file);
                cout << "Removed empty chunk file: " << chunk_file << endl;
            }
        }
    }
    
    cout << "Session " << session_id << " verified chunks: " << valid_chunks 
         << " out of " << existing_chunks.size() << endl;
    
    return valid_chunks > 0;
}

// ==================== 断点续传内部实现函数 ====================

/**
 * 保存分片数据到临时文件
 * @param session_id 传输会话ID
 * @param chunk 分片数据
 * @return 是否保存成功
 */
bool FileModel::save_chunk_to_temp_file(const string& session_id, const FileChunk& chunk) {
    try {
        // 1. 生成临时分片文件路径
        string chunk_file_path = get_temp_chunk_file_path(session_id, chunk.chunk_seq);
        
        // 2. 确保临时目录存在
        string temp_dir = "/uploads/temp/";
        if (!exists(temp_dir)) {
            create_directories(temp_dir);
        }
        
        // 3. 解码Base64数据
        string decoded_data = decode_base64(chunk.chunk_data);
        
        // 4. 写入分片文件
        ofstream file(chunk_file_path, ios::binary);
        if (!file.is_open()) {
            cout << "Failed to open chunk file for writing: " << chunk_file_path << endl;
            return false;
        }
        
        file.write(decoded_data.c_str(), decoded_data.size());
        file.close();
        
        // 5. 验证写入的文件大小
        auto actual_size = file_size(chunk_file_path);
        if (actual_size != decoded_data.size()) {
            remove(chunk_file_path); // 删除损坏的文件
            cout << "Chunk file size mismatch: expected " << decoded_data.size() 
                 << ", actual " << actual_size << endl;
            return false;
        }
        
        cout << "Saved chunk " << chunk.chunk_seq << " for session " << session_id 
             << " (" << actual_size << " bytes)" << endl;
        
        return true;
        
    } catch (const exception& e) {
        cout << "Error saving chunk: " << e.what() << endl;
        return false;
    }
}

/**
 * 合并所有分片为最终文件
 * @param session_id 传输会话ID
 * @return 是否合并成功
 */
bool FileModel::merge_chunks_to_final_file(const string& session_id) {
    try {
        // 1. 查询传输会话信息
        FileTransferSession session = query_transfer_session(session_id);
        if (session.session_id.empty()) {
            cout << "Session not found: " << session_id << endl;
            return false;
        }
        
        // 2. 查询文件信息
        FileInfo file_info = query_file_info(session.file_id);
        if (file_info.file_id.empty()) {
            cout << "File info not found: " << session.file_id << endl;
            return false;
        }
        
        // 3. 验证所有分片都已接收（使用完整性检查）
        if (!verify_chunks_integrity(session_id)) {
            cout << "Chunks integrity check failed for session " << session_id << endl;
            return false;
        }
        
        // 4. 创建最终文件目录
        path final_path(file_info.file_path);
        create_directories(final_path.parent_path());
        
        // 5. 按顺序合并分片
        ofstream final_file(file_info.file_path, ios::binary);
        if (!final_file.is_open()) {
            cout << "Failed to create final file: " << file_info.file_path << endl;
            return false;
        }
        
        size_t total_written = 0;
        for (int i = 1; i <= session.total_chunks; i++) {
            string chunk_file = get_temp_chunk_file_path(session_id, i);
            
            ifstream chunk_stream(chunk_file, ios::binary);
            if (!chunk_stream.is_open()) {
                cout << "Failed to open chunk file: " << chunk_file << endl;
                final_file.close();
                remove(file_info.file_path);
                return false;
            }
            
            // 复制分片数据到最终文件
            final_file << chunk_stream.rdbuf();
            total_written += file_size(chunk_file);
            chunk_stream.close();
        }
        
        final_file.close();
        
        // 6. 验证最终文件大小
        auto final_size = file_size(file_info.file_path);
        if (final_size != file_info.file_size) {
            cout << "Final file size mismatch: expected " << file_info.file_size 
                 << ", actual " << final_size << endl;
            remove(file_info.file_path);
            return false;
        }
        
        // 7. 验证文件哈希值
        string actual_hash = calculate_file_hash(file_info.file_path);
        if (actual_hash != file_info.file_hash) {
            cout << "File hash mismatch: expected " << file_info.file_hash 
                 << ", actual " << actual_hash << endl;
            remove(file_info.file_path);
            return false;
        }
        
        // 8. 更新文件状态为上传完成
        update_file_status(session.file_id, 1);
        
        // 9. 更新传输会话状态为完成
        update_transfer_status(session_id, 1);
        
        // 10. 清理临时文件
        cleanup_temp_chunks(session_id);
        
        cout << "Successfully merged file: " << file_info.file_path 
             << " (" << final_size << " bytes)" << endl;
        
        return true;
        
    } catch (const exception& e) {
        cout << "Error merging chunks: " << e.what() << endl;
        return false;
    }
}

/**
 * 扫描已存在的分片文件
 * @param temp_file_path 临时文件路径前缀
 * @return 已存在分片的序号列表
 */
vector<int> FileModel::scan_existing_chunks(const string& temp_file_path) {
    vector<int> existing_chunks;
    
    try {
        // 从temp_file_path提取session_id (假设格式为 /uploads/temp/session_id.tmp)
        path temp_path(temp_file_path);
        string session_id = temp_path.stem(); // 提取文件名（不含扩展名）
        string temp_dir = "/uploads/temp/";
        
        // 扫描临时目录中的分片文件
        if (exists(temp_dir)) {
            for (const auto& entry : directory_iterator(temp_dir)) {
                if (entry.is_regular_file()) {
                    string filename = entry.path().filename();
                    
                    // 匹配分片文件格式: session_id_chunk_N.tmp
                    string prefix = session_id + "_chunk_";
                    string suffix = ".tmp";
                    
                    if (filename.find(prefix) == 0 && filename.find(suffix) != string::npos) {
                        // 提取分片序号
                        size_t start = prefix.length();
                        size_t end = filename.find(suffix);
                        if (end != string::npos && end > start) {
                            string chunk_num_str = filename.substr(start, end - start);
                            try {
                                int chunk_seq = stoi(chunk_num_str);
                                
                                // 验证文件大小不为0
                                if (file_size(entry.path()) > 0) {
                                    existing_chunks.push_back(chunk_seq);
                                }
                            } catch (const exception& e) {
                                // 忽略无效的分片文件名
                                continue;
                            }
                        }
                    }
                }
            }
        }
        
        // 排序分片序号
        sort(existing_chunks.begin(), existing_chunks.end());
        
        cout << "Found " << existing_chunks.size() << " existing chunks for session " 
             << session_id << endl;
        
    } catch (const exception& e) {
        cout << "Error scanning existing chunks: " << e.what() << endl;
    }
    
    return existing_chunks;
}

// ==================== 辅助函数实现 ====================

/**
 * 生成临时分片文件路径
 */
string FileModel::get_temp_chunk_file_path(const string& session_id, int chunk_seq) {
    return "/uploads/temp/" + session_id + "_chunk_" + to_string(chunk_seq) + ".tmp";
}

/**
 * 清理传输会话的所有临时文件
 */
bool FileModel::cleanup_temp_chunks(const string& session_id) {
    try {
        string temp_dir = "/uploads/temp/";
        int deleted_count = 0;
        
        if (exists(temp_dir)) {
            for (const auto& entry : directory_iterator(temp_dir)) {
                if (entry.is_regular_file()) {
                    string filename = entry.path().filename();
                    
                    // 删除属于该会话的所有临时文件
                    if (filename.find(session_id) == 0) {
                        remove(entry.path());
                        deleted_count++;
                    }
                }
            }
        }
        
        cout << "Cleaned up " << deleted_count << " temp files for session " << session_id << endl;
        return true;
        
    } catch (const exception& e) {
        cout << "Error cleaning up temp chunks: " << e.what() << endl;
        return false;
    }
}

/**
 * 简单的Base64解码实现
 */
string FileModel::decode_base64(const string& encoded_data) {
    // 简单的Base64解码实现
    static const string chars = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    
    string decoded;
    vector<int> T(128, -1);
    for (int i = 0; i < 64; i++) T[chars[i]] = i;
    
    int val = 0, valb = -8;
    for (unsigned char c : encoded_data) {
        if (T[c] == -1) break;
        val = (val << 6) + T[c];
        valb += 6;
        if (valb >= 0) {
            decoded.push_back(char((val >> valb) & 0xFF));
            valb -= 8;
        }
    }
    
    return decoded;
}

/**
 * 验证已接收分片的完整性
 * @param session_id 传输会话ID
 * @return true 如果所有分片完整
 */
bool FileModel::verify_chunks_integrity(const string& session_id) {
    try {
        // 1. 获取会话信息
        FileTransferSession session = get_transfer_session(session_id);
        if (session.session_id.empty()) {
            cout << "Invalid session: " << session_id << endl;
            return false;
        }
        
        // 2. 扫描临时目录中的分片文件
        string temp_dir = "/uploads/temp/";
        vector<int> existing_chunks = scan_existing_chunks(temp_dir);
        
        // 3. 检查分片完整性
        for (int i = 1; i <= session.total_chunks; ++i) {
            string chunk_file = get_temp_chunk_file_path(session_id, i);
            
            if (!exists(chunk_file)) {
                cout << "Missing chunk " << i << " for session " << session_id << endl;
                return false;
            }
            
            // 检查分片文件大小
            try {
                auto file_size = file_size(chunk_file);
                if (file_size == 0) {
                    cout << "Empty chunk file " << i << " for session " << session_id << endl;
                    // 删除空的分片文件
                    remove(chunk_file);
                    return false;
                }
            } catch (const filesystem_error& e) {
                cout << "Error checking chunk file " << i << ": " << e.what() << endl;
                return false;
            }
        }
        
        cout << "All " << session.total_chunks << " chunks verified for session " << session_id << endl;
        return true;
        
    } catch (const exception& e) {
        cout << "Error verifying chunks for session " << session_id << ": " << e.what() << endl;
        return false;
    }
}

// 读取文件分片
bool FileModel::read_file_chunk(const string& file_id, uint32_t chunk_index, string& chunk_data) {
    try {
        // 1. 获取文件信息
        FileInfo file_info = query_file_info(file_id);
        if (file_info.file_id.empty()) {
            cout << "File not found: " << file_id << endl;
            return false;
        }
        
        // 2. 检查文件是否存在
        if (!exists(file_info.file_path)) {
            cout << "File does not exist: " << file_info.file_path << endl;
            return false;
        }
        
        // 3. 计算分片大小和偏移量
        const size_t chunk_size = 1024 * 1024; // 1MB 分片大小
        size_t file_size = static_cast<size_t>(file_info.file_size);
        uint32_t total_chunks = static_cast<uint32_t>((file_size + chunk_size - 1) / chunk_size);
        
        // 4. 验证分片索引
        if (chunk_index >= total_chunks) {
            cout << "Invalid chunk index " << chunk_index << " for file " << file_id 
                 << " (total chunks: " << total_chunks << ")" << endl;
            return false;
        }
        
        // 5. 计算当前分片的位置和大小
        size_t offset = static_cast<size_t>(chunk_index) * chunk_size;
        size_t current_chunk_size = min(chunk_size, file_size - offset);
        
        // 6. 读取文件分片
        ifstream file(file_info.file_path, ios::binary);
        if (!file.is_open()) {
            cout << "Failed to open file: " << file_info.file_path << endl;
            return false;
        }
        
        // 定位到分片位置
        file.seekg(offset);
        if (file.fail()) {
            cout << "Failed to seek to position " << offset << " in file " << file_info.file_path << endl;
            file.close();
            return false;
        }
        
        // 读取数据
        chunk_data.resize(current_chunk_size);
        file.read(&chunk_data[0], current_chunk_size);
        
        if (file.gcount() != static_cast<streamsize>(current_chunk_size)) {
            cout << "Failed to read chunk " << chunk_index << " from file " << file_info.file_path 
                 << " (expected: " << current_chunk_size << ", read: " << file.gcount() << ")" << endl;
            file.close();
            return false;
        }
        
        file.close();
        
        cout << "Successfully read chunk " << chunk_index << " (" << current_chunk_size 
             << " bytes) from file " << file_id << endl;
        return true;
        
    } catch (const exception& e) {
        cout << "Error reading chunk " << chunk_index << " from file " << file_id 
             << ": " << e.what() << endl;
        return false;
    }
}

// 获取文件总分片数
bool FileModel::get_file_total_chunks(const string& file_id, uint32_t& total_chunks) {
    try {
        // 1. 获取文件信息
        FileInfo file_info = query_file_info(file_id);
        if (file_info.file_id.empty()) {
            cout << "File not found: " << file_id << endl;
            return false;
        }
        
        // 2. 计算总分片数
        const size_t chunk_size = 1024 * 1024; // 1MB 分片大小
        size_t file_size = static_cast<size_t>(file_info.file_size);
        total_chunks = static_cast<uint32_t>((file_size + chunk_size - 1) / chunk_size);
        
        cout << "File " << file_id << " has " << total_chunks << " chunks (file size: " 
             << file_size << " bytes)" << endl;
        return true;
        
    } catch (const exception& e) {
        cout << "Error getting total chunks for file " << file_id << ": " << e.what() << endl;
        return false;
    }
}
