#include "EnhancedFileChunkManager.hpp"
#include "public.hpp"
#include "ProtobufMessageHelper.hpp"
#include <muduo/base/Logging.h>
#include <chrono>
#include <sstream>
#include <iomanip>

using namespace muduo;

// EnhancedFileChunkManager 实现
EnhancedFileChunkManager::EnhancedFileChunkManager(FileModel& file_model) 
    : file_model_(file_model) {
}

EnhancedFileChunkManager::~EnhancedFileChunkManager() {
    // 清理所有连接缓冲区
    connection_buffers_.clear();
}

void EnhancedFileChunkManager::handle_received_data(const TcpConnectionPtr& conn, 
                                                   Buffer* buffer, 
                                                   Timestamp time) {
    // 获取连接对应的缓冲区
    auto chunk_buffer = get_connection_buffer(conn);
    
    // 将接收到的数据追加到缓冲区
    string received_data = buffer->retrieveAllAsString();
    chunk_buffer->append(received_data.data(), received_data.length());
    
    // 尝试解析消息
    uint32_t message_type;
    string protobuf_data;
    
    // 可能一次接收到多个完整消息，循环处理
    while (true) {
        FileChunkProtocol::ParseResult result = 
            chunk_buffer->try_parse_message(message_type, protobuf_data);
        
        if (result == FileChunkProtocol::SUCCESS) {
            // 处理解析出的完整消息
            process_parsed_message(conn, message_type, protobuf_data, time);
        } else if (result == FileChunkProtocol::NEED_MORE_DATA) {
            // 需要更多数据，等待下次接收
            break;
        } else {
            // 协议错误，记录日志并断开连接
            LOG_ERROR << "Protocol error for connection " << conn->name() 
                      << ", error code: " << result;
            conn->shutdown();
            break;
        }
    }
    
    // 定期压缩缓冲区
    if (chunk_buffer->readable_size() == 0) {
        chunk_buffer->compact();
    }
}

std::shared_ptr<FileChunkBuffer> EnhancedFileChunkManager::get_connection_buffer(const TcpConnectionPtr& conn) {
    string conn_name = conn->name();
    
    auto it = connection_buffers_.find(conn_name);
    if (it == connection_buffers_.end()) {
        // 创建新的缓冲区
        auto buffer = std::make_shared<FileChunkBuffer>(8192);
        connection_buffers_[conn_name] = buffer;
        return buffer;
    }
    
    return it->second;
}

void EnhancedFileChunkManager::process_parsed_message(const TcpConnectionPtr& conn, 
                                                     uint32_t message_type,
                                                     const string& protobuf_data, 
                                                     Timestamp time) {
    // 查找对应的消息处理器
    auto it = message_handlers_.find(message_type);
    if (it != message_handlers_.end()) {
        try {
            it->second(conn, message_type, protobuf_data, time);
        } catch (const std::exception& e) {
            LOG_ERROR << "Error processing message type " << message_type 
                      << " for connection " << conn->name() << ": " << e.what();
        }
    } else {
        LOG_WARN << "No handler registered for message type " << message_type;
    }
}

void EnhancedFileChunkManager::register_message_handler(uint32_t message_type, MessageHandler handler) {
    message_handlers_[message_type] = handler;
}

void EnhancedFileChunkManager::handle_connection_closed(const TcpConnectionPtr& conn) {
    string conn_name = conn->name();
    connection_buffers_.erase(conn_name);
    LOG_INFO << "Cleaned up buffer for connection " << conn_name;
}

EnhancedFileChunkManager::BufferStats EnhancedFileChunkManager::get_buffer_stats() const {
    BufferStats stats;
    stats.total_connections = connection_buffers_.size();
    stats.total_buffer_size = 0;
    stats.pending_messages = 0;
    
    for (const auto& pair : connection_buffers_) {
        stats.total_buffer_size += pair.second->readable_size();
        // 这里可以添加更详细的统计逻辑
    }
    
    return stats;
}

// FileTransferService 实现
FileTransferService::FileTransferService(EnhancedFileChunkManager& chunk_manager, FileModel& file_model)
    : chunk_manager_(chunk_manager), file_model_(file_model) {
}

void FileTransferService::initialize() {
    // 注册文件传输相关的消息处理器
    using namespace std::placeholders;
    
    chunk_manager_.register_message_handler(FILE_UPLOAD_REQ, 
        std::bind(&FileTransferService::handle_file_upload_request, this, _1, _2, _3, _4));
    
    chunk_manager_.register_message_handler(FILE_CHUNK_MSG,
        std::bind(&FileTransferService::handle_file_chunk_transfer, this, _1, _2, _3, _4));
    
    chunk_manager_.register_message_handler(FILE_DOWNLOAD_REQ,
        std::bind(&FileTransferService::handle_file_download_request, this, _1, _2, _3, _4));
    
    chunk_manager_.register_message_handler(FILE_CHUNK_DOWNLOAD_REQ,
        std::bind(&FileTransferService::handle_file_chunk_download_request, this, _1, _2, _3, _4));
    
    LOG_INFO << "FileTransferService initialized with protocol support";
}

void FileTransferService::handle_file_upload_request(const TcpConnectionPtr& conn, 
                                                    uint32_t message_type,
                                                    const string& protobuf_data, 
                                                    Timestamp time) {
    try {
        // 解析Protobuf消息
        FileUploadRequest request;
        if (!ProtobufMessageHelper::deserialize_message(protobuf_data, request)) {
            LOG_ERROR << "Failed to parse FileUploadRequest from protobuf data";
            send_error_response(conn, FILE_UPLOAD_RSP, 1, "消息解析失败");
            return;
        }
        
        // 验证消息
        if (!ProtobufMessageHelper::validate_message(request)) {
            LOG_ERROR << "Invalid FileUploadRequest message";
            send_error_response(conn, FILE_UPLOAD_RSP, 2, "请求参数无效");
            return;
        }
        
        int user_id = request.user_id();
        string file_name = request.filename();
        uint64_t file_size = request.filesize();
        string file_hash = request.file_hash();
        string file_type = request.file_type();
        uint32_t chunk_size = request.chunk_size();
        string upload_id = request.upload_id();
        
        // 计算总分片数
        uint32_t total_chunks = (file_size + chunk_size - 1) / chunk_size;
        
        // 生成会话ID和文件ID（如果没有提供upload_id）
        if (upload_id.empty()) {
            upload_id = generate_session_id();
        }
        string file_id = "file_" + std::to_string(
            std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::system_clock::now().time_since_epoch()).count());
        
        // 创建传输会话
        FileTransferSession session;
        session.session_id = upload_id;
        session.file_id = file_id;
        session.sender_id = user_id;
        session.receiver_id = -1;  // 文件传输暂不支持指定接收者
        session.group_id = -1;     // 文件传输暂不支持群组
        session.total_chunks = total_chunks;
        session.received_chunks = 0;
        session.file_name = file_name;
        session.file_size = file_size;
        session.file_hash = file_hash;
        
        bool success = file_model_.create_transfer_session(session);
        
        // 构造Protobuf响应
        FileUploadResponse response;
        if (success) {
            response = FileTransferMessageBuilder::create_upload_response(
                0, "传输会话创建成功", file_id, upload_id, 0);
            stats_.total_uploads++;
            stats_.active_sessions++;
        } else {
            response = FileTransferMessageBuilder::create_upload_response(
                3, "创建传输会话失败");
            stats_.failed_transfers++;
        }
        
        // 发送响应
        string response_data = ProtobufMessageHelper::serialize_message(response);
        vector<char> packet = FileChunkProtocol::encode_message(FILE_UPLOAD_RSP, response_data);
        conn->send(packet.data(), packet.size());
        
        LOG_INFO << "File upload request processed for user " << user_id 
                 << ", upload_id: " << upload_id;
        
    } catch (const std::exception& e) {
        LOG_ERROR << "Error processing file upload request: " << e.what();
        send_error_response(conn, FILE_UPLOAD_RSP, 2, "请求格式错误");
    }
}

void FileTransferService::handle_file_chunk_transfer(const TcpConnectionPtr& conn,
                                                    uint32_t message_type,
                                                    const string& protobuf_data, 
                                                    Timestamp time) {
    try {
        // 解析Protobuf消息
        FileChunkMessage chunk_msg;
        if (!ProtobufMessageHelper::deserialize_message(protobuf_data, chunk_msg)) {
            LOG_ERROR << "Failed to parse FileChunkMessage from protobuf data";
            send_error_response(conn, FILE_CHUNK_RSP, 1, "消息解析失败");
            return;
        }
        
        // 验证消息
        if (!ProtobufMessageHelper::validate_message(chunk_msg)) {
            LOG_ERROR << "Invalid FileChunkMessage";
            send_error_response(conn, FILE_CHUNK_RSP, 2, "分片数据无效");
            return;
        }
        
        string upload_id = chunk_msg.upload_id();
        string file_id = chunk_msg.file_id();
        uint32_t chunk_index = chunk_msg.chunk_index();
        string chunk_data = chunk_msg.chunk_data();
        string chunk_hash = chunk_msg.chunk_hash();
        bool is_last_chunk = chunk_msg.is_last_chunk();
        
        string session_id = js["session_id"];
        int chunk_seq = js["chunk_seq"];
        string chunk_data = js["chunk_data"];
        bool is_last = js["is_last"];
        
        LOG_DEBUG << "Received chunk " << chunk_seq << " for session " << session_id;
        
        // 验证会话有效性
        FileTransferSession session = file_model_.query_transfer_session(session_id);
        if (session.session_id.empty()) {
            send_error_response(conn, FILE_CHUNK_RSP, 1, "无效的传输会话");
            return;
        }
        
        // 从数据库获取传输会话
        FileTransferSession session;
        if (!file_model_.get_transfer_session(upload_id, session)) {
            LOG_ERROR << "Transfer session not found: " << upload_id;
            send_chunk_error_response(conn, upload_id, chunk_index, 3, "传输会话不存在");
            return;
        }
        
        // 验证分片序号
        if (chunk_index >= session.total_chunks) {
            LOG_ERROR << "Invalid chunk index " << chunk_index << " for session " << upload_id;
            send_chunk_error_response(conn, upload_id, chunk_index, 4, "分片序号无效");
            return;
        }
        
        // 创建分片对象
        FileChunk chunk;
        chunk.file_id = session.file_id;
        chunk.chunk_seq = chunk_index;
        chunk.total_chunks = session.total_chunks;
        chunk.chunk_data = chunk_data;
        chunk.is_last = is_last_chunk;
        
        // 保存分片到临时文件
        bool save_success = file_model_.save_chunk_to_temp_file(upload_id, chunk);
        
        // 计算上传进度
        float progress = 0.0f;
        bool upload_complete = false;
        if (save_success) {
            file_model_.update_chunk_status(upload_id, chunk_index);
            session.received_chunks++;
            progress = (float)session.received_chunks / session.total_chunks;
            upload_complete = (session.received_chunks >= session.total_chunks);
            
            stats_.bytes_transferred += chunk_data.length();
            
            LOG_DEBUG << "Chunk " << chunk_index << " saved successfully for session " << upload_id;
        }
        
        // 构造Protobuf响应
        FileChunkResponse response;
        if (save_success) {
            response = FileTransferMessageBuilder::create_chunk_response(
                0, "分片接收成功", upload_id, chunk_index, chunk_index + 1, progress, upload_complete);
        } else {
            response = FileTransferMessageBuilder::create_chunk_response(
                2, "分片保存失败", upload_id, chunk_index, chunk_index, progress, false);
            stats_.failed_transfers++;
            
            LOG_ERROR << "Failed to save chunk " << chunk_index << " for session " << upload_id;
        }
        
        // 发送响应
        string response_data = ProtobufMessageHelper::serialize_message(response);
        vector<char> packet = FileChunkProtocol::encode_message(FILE_CHUNK_RSP, response_data);
        conn->send(packet.data(), packet.size());
        
        // 检查是否为最后一个分片或已完成
        if (upload_complete && save_success) {
            LOG_INFO << "File upload completed for session " << upload_id << ", starting file merge";
            
            // 触发文件合并流程
            bool merge_success = file_model_.merge_chunks_to_final_file(upload_id);
            if (merge_success) {
                LOG_INFO << "File transfer completed successfully for session " << upload_id;
                stats_.active_sessions--;
            } else {
                LOG_ERROR << "File merge failed for session " << session_id;
                stats_.failed_transfers++;
            }
        }
        
    } catch (const std::exception& e) {
        LOG_ERROR << "Error processing file chunk transfer: " << e.what();
        send_error_response(conn, FILE_CHUNK_RSP, 3, "服务器内部错误");
    }
}

void FileTransferService::handle_file_download_request(const TcpConnectionPtr& conn,
                                                      uint32_t message_type,
                                                      const string& protobuf_data, 
                                                      Timestamp time) {
    try {
        // 解析Protobuf消息
        FileDownloadRequest request;
        if (!ProtobufMessageHelper::deserialize_message(protobuf_data, request)) {
            LOG_ERROR << "Failed to parse FileDownloadRequest from protobuf data";
            send_error_response(conn, FILE_DOWNLOAD_RSP, 1, "消息解析失败");
            return;
        }
        
        // 验证消息
        if (!ProtobufMessageHelper::validate_message(request)) {
            LOG_ERROR << "Invalid FileDownloadRequest";
            send_error_response(conn, FILE_DOWNLOAD_RSP, 2, "请求参数无效");
            return;
        }
        
        int user_id = request.user_id();
        string file_id = request.file_id();
        string filename = request.filename();
        uint32_t chunk_size = request.chunk_size();
        uint32_t start_chunk = request.start_chunk();
        string download_id = request.download_id();
        
        // 验证下载权限
        if (!validate_transfer_permission(user_id, file_id)) {
            send_error_response(conn, FILE_DOWNLOAD_RSP, 3, "没有下载权限");
            return;
        }
        
        // 查询文件信息
        FileInfo file_info = file_model_.query_file_info(file_id);
        if (file_info.file_id.empty()) {
            send_error_response(conn, FILE_DOWNLOAD_RSP, 4, "文件不存在");
            return;
        }
        
        // 设置默认分片大小
        if (chunk_size == 0) {
            chunk_size = 64 * 1024;  // 64KB
        }
        
        // 计算分片信息
        uint32_t total_chunks = (file_info.file_size + chunk_size - 1) / chunk_size;
        
        // 生成下载会话ID
        if (download_id.empty()) {
            download_id = generate_session_id();
        }
        
        // 构造Protobuf响应
        FileDownloadResponse response = FileTransferMessageBuilder::create_download_response(
            0, "下载准备就绪", 
            file_id, file_info.file_name, file_info.file_size, file_info.file_hash, file_info.file_type,
            total_chunks, chunk_size, download_id, start_chunk);
        
        // 发送响应
        string response_data = ProtobufMessageHelper::serialize_message(response);
        vector<char> packet = FileChunkProtocol::encode_message(FILE_DOWNLOAD_RSP, response_data);
        conn->send(packet.data(), packet.size());
        
        stats_.total_downloads++;
        
        LOG_INFO << "File download request processed for user " << user_id << " file " << file_id;
        
    } catch (const std::exception& e) {
        LOG_ERROR << "Error processing file download request: " << e.what();
        send_error_response(conn, FILE_DOWNLOAD_RSP, 5, "服务器内部错误");
    }
}

void FileTransferService::handle_file_chunk_download_request(const TcpConnectionPtr& conn,
                                                            uint32_t message_type,
                                                            const string& protobuf_data, 
                                                            Timestamp time) {
    try {
        // 解析Protobuf消息
        FileChunkDownloadRequest request;
        if (!ProtobufMessageHelper::deserialize_message(protobuf_data, request)) {
            LOG_ERROR << "Failed to parse FileChunkDownloadRequest from protobuf data";
            send_chunk_download_error_response(conn, "", "", 0, 1, "消息解析失败");
            return;
        }
        
        string download_id = request.download_id();
        string file_id = request.file_id();
        uint32_t chunk_index = request.chunk_index();
        
        // 验证下载会话
        if (download_id.empty()) {
            send_chunk_download_error_response(conn, download_id, file_id, chunk_index, 1, "下载会话ID为空");
            return;
        }
        
        // 验证文件ID
        if (file_id.empty()) {
            send_chunk_download_error_response(conn, download_id, file_id, chunk_index, 1, "文件ID为空");
            return;
        }
        
        // 读取文件分片
        string chunk_data;
        bool success = file_model_.read_file_chunk(file_id, chunk_index, chunk_data);
        
        if (success) {
            // 计算分片哈希
            string chunk_hash = calculate_chunk_hash(chunk_data);
            
            // 获取文件总分片数并判断是否为最后一个分片
            uint32_t total_chunks = 0;
            bool is_last_chunk = false;
            float progress = 0.0f;
            
            if (file_model_.get_file_total_chunks(file_id, total_chunks)) {
                is_last_chunk = (chunk_index == total_chunks - 1); // chunk_index 从0开始
                progress = static_cast<float>(chunk_index + 1) / static_cast<float>(total_chunks);
            } else {
                LOG_ERROR << "Failed to get total chunks for file: " << file_id;
                // 如果无法获取总分片数，假设不是最后一个分片
                is_last_chunk = false;
                progress = 0.0f;
            }
            
            // 构造成功响应
            FileChunkDownloadResponse response = FileTransferMessageBuilder::create_chunk_download_response(
                0, "分片下载成功", download_id, file_id, chunk_index, 
                chunk_data, chunk_hash, is_last_chunk, progress);
            
            string response_data = ProtobufMessageHelper::serialize_message(response);
            vector<char> packet = FileChunkProtocol::encode_message(FILE_CHUNK_DOWN_RSP, response_data);
            conn->send(packet.data(), packet.size());
            
            stats_.bytes_transferred += chunk_data.size();
        } else {
            send_chunk_download_error_response(conn, download_id, file_id, chunk_index, 2, "分片读取失败");
        }
        
    } catch (const std::exception& e) {
        LOG_ERROR << "Error processing file chunk download request: " << e.what();
        send_chunk_download_error_response(conn, "", "", 0, 3, "服务器内部错误");
    }
}

// 辅助方法实现
void FileTransferService::send_error_response(const TcpConnectionPtr& conn, 
                                             uint32_t response_type,
                                             int error_code, 
                                             const string& error_message) {
    // 根据响应类型创建对应的错误响应
    switch (response_type) {
        case FILE_UPLOAD_RSP: {
            FileUploadResponse response = FileTransferMessageBuilder::create_upload_response(
                error_code, error_message);
            string response_data = ProtobufMessageHelper::serialize_message(response);
            vector<char> packet = FileChunkProtocol::encode_message(response_type, response_data);
            conn->send(packet.data(), packet.size());
            break;
        }
        case FILE_DOWNLOAD_RSP: {
            FileDownloadResponse response = FileTransferMessageBuilder::create_download_response(
                error_code, error_message);
            string response_data = ProtobufMessageHelper::serialize_message(response);
            vector<char> packet = FileChunkProtocol::encode_message(response_type, response_data);
            conn->send(packet.data(), packet.size());
            break;
        }
        default: {
            LOG_WARN << "Unknown response type for error response: " << response_type;
            break;
        }
    }
}

void FileTransferService::send_chunk_error_response(const TcpConnectionPtr& conn,
                                                   const string& upload_id,
                                                   uint32_t chunk_index,
                                                   int error_code,
                                                   const string& error_message) {
    FileChunkResponse response = FileTransferMessageBuilder::create_chunk_response(
        error_code, error_message, upload_id, chunk_index, chunk_index, 0.0f, false);
    
    string response_data = ProtobufMessageHelper::serialize_message(response);
    vector<char> packet = FileChunkProtocol::encode_message(FILE_CHUNK_RSP, response_data);
    conn->send(packet.data(), packet.size());
}

void FileTransferService::send_chunk_download_error_response(const TcpConnectionPtr& conn,
                                                           const string& download_id,
                                                           const string& file_id,
                                                           uint32_t chunk_index,
                                                           int error_code,
                                                           const string& error_message) {
    FileChunkDownloadResponse response = FileTransferMessageBuilder::create_chunk_download_response(
        error_code, error_message, download_id, file_id, chunk_index, "", "", false, 0.0f);
    
    string response_data = ProtobufMessageHelper::serialize_message(response);
    vector<char> packet = FileChunkProtocol::encode_message(FILE_CHUNK_DOWN_RSP, response_data);
    conn->send(packet.data(), packet.size());
}

string FileTransferService::calculate_chunk_hash(const string& chunk_data) {
    // TODO: 实现实际的哈希计算（MD5或SHA256）
    // 这里暂时返回简单的长度字符串作为占位符
    return std::to_string(chunk_data.size());
}

string FileTransferService::generate_session_id() {
    auto now = std::chrono::system_clock::now();
    auto timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
    
    std::stringstream ss;
    ss << "session_" << timestamp << "_" << (rand() % 10000);
    return ss.str();
}

bool FileTransferService::validate_transfer_permission(int user_id, const string& file_id) {
    // 实现权限验证逻辑
    // 这里可以检查用户是否有权限访问该文件
    return true; // 简化实现
}
