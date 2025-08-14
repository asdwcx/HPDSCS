#ifndef ENHANCED_CHAT_SERVER_HPP
#define ENHANCED_CHAT_SERVER_HPP

#include "ChatServer.hpp"
#include "EnhancedFileChunkManager.hpp"
#include "FileModel.hpp"
#include <memory>

/**
 * 增强的聊天服务器
 * 集成了文件传输协议支持，解决粘包问题
 */
class EnhancedChatServer : public ChatServer {
private:
    // 文件传输组件
    std::unique_ptr<FileModel> file_model_;
    std::unique_ptr<EnhancedFileChunkManager> chunk_manager_;
    std::unique_ptr<FileTransferService> file_service_;
    
    // 协议检测
    bool is_file_transfer_message(const string& data);
    bool has_protocol_header(const char* data, size_t length);
    
public:
    EnhancedChatServer(EventLoop* loop, const InetAddress& listenAddr, const string& nameArg);
    virtual ~EnhancedChatServer();
    
    /**
     * 重写连接回调，增加文件传输支持
     */
    virtual void on_connection(const TcpConnectionPtr &conn) override;
    
    /**
     * 重写消息处理回调，支持双协议
     */
    virtual void on_message(const TcpConnectionPtr &conn, Buffer *buffer, Timestamp time) override;
    
    /**
     * 获取文件传输统计信息
     */
    FileTransferService::TransferStats get_file_transfer_stats() const;
    
    /**
     * 获取缓冲区统计信息
     */
    EnhancedFileChunkManager::BufferStats get_buffer_stats() const;
    
private:
    /**
     * 初始化文件传输组件
     */
    void initialize_file_transfer_components();
    
    /**
     * 处理传统协议消息（Protobuf包装的JSON）
     */
    void handle_legacy_message(const TcpConnectionPtr &conn, Buffer *buffer, Timestamp time);
    
    /**
     * 处理新协议消息（带文件头的分片传输）
     */
    void handle_enhanced_message(const TcpConnectionPtr &conn, Buffer *buffer, Timestamp time);
};

#endif // ENHANCED_CHAT_SERVER_HPP
