#ifndef CLIENT_FILE_CHUNK_HELPER_HPP
#define CLIENT_FILE_CHUNK_HELPER_HPP

#include "FileChunkProtocol.hpp"
#include "ProtobufMessageHelper.hpp"
#include <string>
#include <vector>
#include <fstream>

using namespace std;

/**
 * 客户端文件分片传输助手
 * 提供简化的API用于客户端文件传输
 */
class ClientFileChunkHelper {
private:
    FileChunkBuffer buffer_;
    string server_host_;
    int server_port_;
    int user_id_;
    int socket_;  // 更改为int类型，适配Windows和Linux
    bool connected_;
    
    // 上传状态
    struct UploadState {
        string upload_id;
        string file_id;
        uint32_t total_chunks = 0;
        uint32_t sent_chunks = 0;
        bool is_uploading = false;
    } upload_state_;
    
    // 下载状态
    struct DownloadState {
        string download_id;
        string file_id;
        string file_name;
        uint64_t file_size = 0;
        uint32_t total_chunks = 0;
        uint32_t received_chunks = 0;
        vector<vector<char>> chunks;
        vector<bool> received_status;
        bool is_downloading = false;
    } download_state_;
    
public:
    ClientFileChunkHelper(const string& server_host, int server_port, int user_id);
    ~ClientFileChunkHelper();
    
    /**
     * 连接到服务器
     */
    bool connect_to_server(const string& host, int port);
    
    /**
     * 断开连接
     */
    void disconnect();
    
    /**
     * 发送文件上传请求
     */
    bool request_file_upload(int user_id, const string& file_path, 
                           int receiver_id = -1, int group_id = -1);
    
    /**
     * 发送文件分片
     */
    bool send_file_chunk(const vector<char>& chunk_data, int chunk_seq, bool is_last);
    
    /**
     * 发送完整文件（自动分片）
     */
    bool upload_file(int user_id, const string& file_path, 
                    int receiver_id = -1, int group_id = -1);
    
    /**
     * 请求文件下载
     */
    bool request_file_download(int user_id, const string& file_id);
    
    /**
     * 请求特定分片下载
     */
    bool request_chunk_download(const string& file_id, int chunk_seq);
    
    /**
     * 下载完整文件（自动分片接收）
     */
    bool download_file(int user_id, const string& file_id, const string& save_path);
    
    /**
     * 处理服务器响应
     */
    bool process_server_response();
    
    /**
     * 获取上传进度
     */
    double get_upload_progress() const;
    
    /**
     * 获取下载进度
     */
    double get_download_progress() const;
    
    /**
     * 取消当前传输
     */
    void cancel_transfer();
    
private:
    /**
     * 发送Protobuf协议消息
     */
    template<typename T>
    bool send_protocol_message(uint32_t message_type, const T& protobuf_message);
    
    /**
     * 接收Protobuf协议消息
     */
    template<typename T>
    bool receive_protocol_message(uint32_t& message_type, T& protobuf_message);
    
    /**
     * 处理上传响应
     */
    void handle_upload_response(const FileUploadResponse& response);
    
    /**
     * 处理分片响应
     */
    void handle_chunk_response(const FileChunkResponse& response);
    
    /**
     * 处理下载响应
     */
    void handle_download_response(const FileDownloadResponse& response);
    
    /**
     * 处理分片下载响应
     */
    void handle_chunk_download_response(const FileChunkDownloadResponse& response);
    
    /**
     * 读取文件到分片
     */
    vector<vector<char>> split_file_to_chunks(const string& file_path, size_t chunk_size = 64 * 1024);
    
    /**
     * 合并分片到文件
     */
    bool merge_chunks_to_file(const string& file_path);
    
    /**
     * Base64编码
     */
    string encode_base64(const vector<char>& data);
    
    /**
     * Base64解码
     */
    vector<char> decode_base64(const string& encoded);
    
    /**
     * 计算文件哈希值
     */
    string calculate_file_hash(const string& file_path);
    
    /**
     * 计算分片哈希值
     */
    string calculate_chunk_hash(const string& chunk_data);
    
    /**
     * 发送原始数据
     */
    bool send_data(const char* data, size_t length);
    
    /**
     * 接收原始数据
     */
    bool receive_data(char* buffer, size_t length);
};

// 模板方法实现
template<typename T>
bool ClientFileChunkHelper::send_protocol_message(uint32_t message_type, const T& protobuf_message) {
    try {
        // 序列化Protobuf消息
        string protobuf_data = ProtobufMessageHelper::serialize_message(protobuf_message);
        if (protobuf_data.empty()) {
            return false;
        }
        
        // 使用协议编码
        vector<char> packet = FileChunkProtocol::encode_message(message_type, protobuf_data);
        
        // 发送数据
        return send_data(packet.data(), packet.size());
    } catch (const std::exception& e) {
        return false;
    }
}

template<typename T>
bool ClientFileChunkHelper::receive_protocol_message(uint32_t& message_type, T& protobuf_message) {
    try {
        // 接收协议头
        char header_buffer[16];
        if (!receive_data(header_buffer, 16)) {
            return false;
        }
        
        // 添加到缓冲区
        buffer_.append_data(header_buffer, 16);
        
        // 尝试解析消息
        string protobuf_data;
        FileChunkProtocol::ParseResult result = buffer_.try_parse_message(message_type, protobuf_data);
        
        if (result == FileChunkProtocol::NEED_MORE_DATA) {
            // 需要接收更多数据
            // 这里简化处理，实际应该根据协议头中的长度来接收
            char additional_buffer[65536];
            if (!receive_data(additional_buffer, sizeof(additional_buffer))) {
                return false;
            }
            buffer_.append_data(additional_buffer, sizeof(additional_buffer));
            result = buffer_.try_parse_message(message_type, protobuf_data);
        }
        
        if (result == FileChunkProtocol::SUCCESS) {
            return ProtobufMessageHelper::deserialize_message(protobuf_data, protobuf_message);
        }
        
        return false;
    } catch (const std::exception& e) {
        return false;
    }
}

#endif // CLIENT_FILE_CHUNK_HELPER_HPP
