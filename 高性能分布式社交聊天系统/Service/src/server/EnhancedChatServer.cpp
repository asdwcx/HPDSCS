#include "EnhancedChatServer.hpp"
#include "ChatService.hpp"
#include "FileChunkProtocol.hpp"
#include <muduo/base/Logging.h>

EnhancedChatServer::EnhancedChatServer(EventLoop* loop, const InetAddress& listenAddr, const string& nameArg)
    : ChatServer(loop, listenAddr, nameArg) {
    
    // 初始化文件传输组件
    initialize_file_transfer_components();
    
    LOG_INFO << "EnhancedChatServer initialized with dual protocol support";
}

EnhancedChatServer::~EnhancedChatServer() {
    // 智能指针会自动清理
}

void EnhancedChatServer::initialize_file_transfer_components() {
    // 创建文件模型
    file_model_ = std::make_unique<FileModel>();
    
    // 创建分片管理器
    chunk_manager_ = std::make_unique<EnhancedFileChunkManager>(*file_model_);
    
    // 创建文件传输服务
    file_service_ = std::make_unique<FileTransferService>(*chunk_manager_, *file_model_);
    file_service_->initialize();
}

void EnhancedChatServer::on_connection(const TcpConnectionPtr &conn) {
    if (!conn->connected()) {
        // 处理连接断开
        ChatService::instance()->client_close_exception(conn);
        
        // 清理文件传输相关资源
        if (chunk_manager_) {
            chunk_manager_->handle_connection_closed(conn);
        }
        
        conn->shutdown();
    } else {
        LOG_INFO << "New connection established: " << conn->name();
    }
}

void EnhancedChatServer::on_message(const TcpConnectionPtr &conn, Buffer *buffer, Timestamp time) {
    // 检查是否为新协议消息（带文件头）
    if (buffer->readableBytes() >= PROTOCOL_HEADER_SIZE) {
        const char* peek_data = buffer->peek();
        
        if (has_protocol_header(peek_data, buffer->readableBytes())) {
            // 使用新协议处理
            handle_enhanced_message(conn, buffer, time);
            return;
        }
    }
    
    // 使用传统协议处理
    handle_legacy_message(conn, buffer, time);
}

bool EnhancedChatServer::has_protocol_header(const char* data, size_t length) {
    if (length < 4) {
        return false;
    }
    
    // 检查魔数（网络字节序）
    uint32_t magic_number;
    memcpy(&magic_number, data, 4);
    magic_number = ntohl(magic_number);
    
    return magic_number == PROTOCOL_MAGIC_NUMBER;
}

void EnhancedChatServer::handle_legacy_message(const TcpConnectionPtr &conn, Buffer *buffer, Timestamp time) {
    // 使用原始的消息处理逻辑
    string buf = buffer->retrieveAllAsString();
    cout << "Legacy message: " << buf << endl;
    
    // 数据反序列化 - 使用protobuf
    chat::MessageWrapper msg;
    if (!msg.ParseFromString(buf)) {
        cout << "protobuf parse error!" << endl;
        return;
    }
    
    // 解耦网络和业务模块的代码
    auto msg_handler = ChatService::instance()->get_handler(static_cast<int>(msg.msgid()));
    msg_handler(conn, msg, time);
}

void EnhancedChatServer::handle_enhanced_message(const TcpConnectionPtr &conn, Buffer *buffer, Timestamp time) {
    // 使用增强的文件传输协议处理
    cout << "Enhanced protocol message received" << endl;
    
    if (chunk_manager_) {
        chunk_manager_->handle_received_data(conn, buffer, time);
    } else {
        LOG_ERROR << "Chunk manager not initialized";
        buffer->retrieveAll(); // 清空缓冲区
    }
}

FileTransferService::TransferStats EnhancedChatServer::get_file_transfer_stats() const {
    if (file_service_) {
        return file_service_->get_stats();
    }
    return FileTransferService::TransferStats{};
}

EnhancedFileChunkManager::BufferStats EnhancedChatServer::get_buffer_stats() const {
    if (chunk_manager_) {
        return chunk_manager_->get_buffer_stats();
    }
    return EnhancedFileChunkManager::BufferStats{};
}
