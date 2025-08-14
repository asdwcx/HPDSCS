#ifndef ENHANCED_FILE_CHUNK_MANAGER_HPP
#define ENHANCED_FILE_CHUNK_MANAGER_HPP

#include "FileChunkProtocol.hpp"
#include "FileModel.hpp"
#include "message.pb.h"
#include <muduo/net/TcpConnection.h>
#include <muduo/base/Timestamp.h>
#include <map>
#include <memory>

using namespace muduo;
using namespace muduo::net;

/**
 * 增强的文件分片管理器
 * 整合文件头协议，解决粘包问题
 */
class EnhancedFileChunkManager {
private:
    // 连接缓冲区管理
    std::map<string, std::shared_ptr<FileChunkBuffer>> connection_buffers_;
    
    // 文件模型引用
    FileModel& file_model_;
    
    // 消息处理回调函数类型
    using MessageHandler = std::function<void(const TcpConnectionPtr&, uint32_t, const string&, Timestamp)>;
    std::map<uint32_t, MessageHandler> message_handlers_;
    
public:
    explicit EnhancedFileChunkManager(FileModel& file_model);
    ~EnhancedFileChunkManager();
    
    /**
     * 处理TCP连接接收到的原始数据
     * 自动解析协议头，处理粘包问题
     * @param conn TCP连接
     * @param buffer 接收到的数据缓冲区
     * @param time 时间戳
     */
    void handle_received_data(const TcpConnectionPtr& conn, Buffer* buffer, Timestamp time);
    
    /**
     * 发送带协议头的分片消息
     * @param conn TCP连接
     * @param message_type 消息类型
     * @param protobuf_message Protobuf消息对象
     */
    template<typename T>
    bool send_chunk_message(const TcpConnectionPtr& conn, uint32_t message_type, const T& protobuf_message);
    
    /**
     * 注册消息处理器
     */
    void register_message_handler(uint32_t message_type, MessageHandler handler);
    
    /**
     * 处理连接关闭事件
     */
    void handle_connection_closed(const TcpConnectionPtr& conn);
    
    /**
     * 获取连接的缓冲区统计信息
     */
    struct BufferStats {
        size_t total_connections;
        size_t total_buffer_size;
        size_t pending_messages;
    };
    BufferStats get_buffer_stats() const;
    
private:
    /**
     * 获取或创建连接缓冲区
     */
    std::shared_ptr<FileChunkBuffer> get_connection_buffer(const TcpConnectionPtr& conn);
    
    /**
     * 处理解析出的完整消息
     */
    void process_parsed_message(const TcpConnectionPtr& conn, uint32_t message_type, 
                              const string& protobuf_data, Timestamp time);
};

/**
 * 文件分片传输的具体实现类
 * 基于EnhancedFileChunkManager构建
 */
class FileTransferService {
private:
    EnhancedFileChunkManager& chunk_manager_;
    FileModel& file_model_;
    
    // 统计信息
    struct TransferStats {
        size_t total_uploads = 0;
        size_t total_downloads = 0;
        size_t active_sessions = 0;
        size_t failed_transfers = 0;
        size_t bytes_transferred = 0;
    } stats_;
    
public:
    FileTransferService(EnhancedFileChunkManager& chunk_manager, FileModel& file_model);
    
    /**
     * 初始化服务，注册消息处理器
     */
    void initialize();
    
    /**
     * 处理文件上传请求
     */
    void handle_file_upload_request(const TcpConnectionPtr& conn, uint32_t message_type,
                                   const string& protobuf_data, Timestamp time);
    
    /**
     * 处理文件分片传输
     */
    void handle_file_chunk_transfer(const TcpConnectionPtr& conn, uint32_t message_type,
                                   const string& protobuf_data, Timestamp time);
    
    /**
     * 处理文件下载请求
     */
    void handle_file_download_request(const TcpConnectionPtr& conn, uint32_t message_type,
                                     const string& protobuf_data, Timestamp time);
    
    /**
     * 处理分片下载请求
     */
    void handle_file_chunk_download_request(const TcpConnectionPtr& conn, uint32_t message_type,
                                           const string& protobuf_data, Timestamp time);
    
    /**
     * 获取传输统计信息
     */
    const TransferStats& get_stats() const { return stats_; }
    
    /**
     * 重置统计信息
     */
    void reset_stats() { stats_ = TransferStats{}; }
    
private:
    /**
     * 发送错误响应
     */
    void send_error_response(const TcpConnectionPtr& conn, uint32_t response_type,
                           int error_code, const string& error_message);
    
    /**
     * 发送文件分片错误响应
     */
    void send_chunk_error_response(const TcpConnectionPtr& conn,
                                  const string& upload_id,
                                  uint32_t chunk_index,
                                  int error_code,
                                  const string& error_message);
    
    /**
     * 发送文件分片下载错误响应
     */
    void send_chunk_download_error_response(const TcpConnectionPtr& conn,
                                          const string& download_id,
                                          const string& file_id,
                                          uint32_t chunk_index,
                                          int error_code,
                                          const string& error_message);
    
    /**
     * 计算分片哈希值
     */
    string calculate_chunk_hash(const string& chunk_data);
    
    /**
     * 生成唯一的会话ID
     */
    string generate_session_id();
    
    /**
     * 验证文件传输权限
     */
    bool validate_transfer_permission(int user_id, const string& file_id);
};

// 模板方法实现
template<typename T>
bool EnhancedFileChunkManager::send_chunk_message(const TcpConnectionPtr& conn, 
                                                  uint32_t message_type, 
                                                  const T& protobuf_message) {
    try {
        // 序列化Protobuf消息
        string protobuf_data;
        if (!protobuf_message.SerializeToString(&protobuf_data)) {
            return false;
        }
        
        // 使用协议编码
        vector<char> packet = FileChunkProtocol::encode_message(message_type, protobuf_data);
        
        // 发送数据
        conn->send(packet.data(), packet.size());
        
        return true;
    } catch (const std::exception& e) {
        // 记录错误日志
        return false;
    }
}

#endif // ENHANCED_FILE_CHUNK_MANAGER_HPP
