#include "ClientFileChunkHelper.hpp"
#include <iostream>
#include <fstream>
#include <thread>
#include <chrono>

#ifdef _WIN32
    #include <winsock2.h>
    #include <ws2tcpip.h>
    #pragma comment(lib, "ws2_32.lib")
    typedef int socklen_t;
#else
    #include <sys/socket.h>
    #include <netinet/in.h>
    #include <arpa/inet.h>
    #include <unistd.h>
    #define SOCKET int
    #define INVALID_SOCKET -1
    #define SOCKET_ERROR -1
    #define closesocket close
#endif

using namespace std;

// 初始化网络库（Windows需要）
class NetworkInitializer {
public:
    NetworkInitializer() {
#ifdef _WIN32
        WSADATA wsaData;
        if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
            throw runtime_error("WSAStartup failed");
        }
#endif
    }
    
    ~NetworkInitializer() {
#ifdef _WIN32
        WSACleanup();
#endif
    }
};

static NetworkInitializer network_init;

// ClientFileUploader 实现
ClientFileUploader::ClientFileUploader(const string& server_host, int server_port)
    : server_host_(server_host), server_port_(server_port), 
      socket_(INVALID_SOCKET), is_connected_(false) {
}

ClientFileUploader::~ClientFileUploader() {
    disconnect();
}

bool ClientFileUploader::connect() {
    if (is_connected_) {
        return true;
    }
    
    // 创建socket
    socket_ = socket(AF_INET, SOCK_STREAM, 0);
    if (socket_ == INVALID_SOCKET) {
        last_error_ = "Failed to create socket";
        return false;
    }
    
    // 配置服务器地址
    struct sockaddr_in server_addr = {};
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(static_cast<uint16_t>(server_port_));
    
    if (inet_pton(AF_INET, server_host_.c_str(), &server_addr.sin_addr) <= 0) {
        last_error_ = "Invalid server address: " + server_host_;
        closesocket(socket_);
        socket_ = INVALID_SOCKET;
        return false;
    }
    
    // 连接到服务器
    if (::connect(socket_, reinterpret_cast<struct sockaddr*>(&server_addr), 
                  sizeof(server_addr)) == SOCKET_ERROR) {
        last_error_ = "Failed to connect to server";
        closesocket(socket_);
        socket_ = INVALID_SOCKET;
        return false;
    }
    
    is_connected_ = true;
    last_error_.clear();
    return true;
}

void ClientFileUploader::disconnect() {
    if (socket_ != INVALID_SOCKET) {
        closesocket(socket_);
        socket_ = INVALID_SOCKET;
    }
    is_connected_ = false;
}

bool ClientFileUploader::upload_file(const string& file_path, 
                                   const UploadCallback& progress_callback) {
    if (!is_connected_ && !connect()) {
        return false;
    }
    
    // 打开文件
    ifstream file(file_path, ios::binary);
    if (!file.is_open()) {
        last_error_ = "Failed to open file: " + file_path;
        return false;
    }
    
    // 获取文件大小
    file.seekg(0, ios::end);
    size_t file_size = static_cast<size_t>(file.tellg());
    file.seekg(0, ios::beg);
    
    // 提取文件名
    string filename = file_path;
    size_t last_slash = filename.find_last_of("/\\");
    if (last_slash != string::npos) {
        filename = filename.substr(last_slash + 1);
    }
    
    cout << "开始上传文件: " << filename << " (大小: " << file_size << " 字节)" << endl;
    
    // 发送文件头信息（简化版本，实际项目中需要根据具体协议调整）
    Json::Value file_info;
    file_info["filename"] = filename;
    file_info["filesize"] = static_cast<Json::UInt64>(file_size);
    file_info["chunk_size"] = CHUNK_SIZE;
    
    string file_info_str = file_info.toStyledString();
    if (!send_message(FILE_UPLOAD_REQ, file_info_str)) {
        last_error_ = "Failed to send file upload request";
        return false;
    }
    
    // 等待服务器响应
    uint32_t response_type;
    string response_data;
    if (!receive_message(response_type, response_data)) {
        last_error_ = "Failed to receive upload response";
        return false;
    }
    
    if (response_type != FILE_UPLOAD_RSP) {
        last_error_ = "Unexpected response type: " + to_string(response_type);
        return false;
    }
    
    // 分块上传文件
    char buffer[CHUNK_SIZE];
    size_t total_sent = 0;
    int chunk_index = 0;
    
    while (total_sent < file_size) {
        size_t to_read = min(static_cast<size_t>(CHUNK_SIZE), file_size - total_sent);
        file.read(buffer, static_cast<streamsize>(to_read));
        size_t actually_read = static_cast<size_t>(file.gcount());
        
        if (actually_read == 0) {
            break;
        }
        
        // 创建分块消息
        Json::Value chunk_info;
        chunk_info["chunk_index"] = chunk_index++;
        chunk_info["chunk_size"] = static_cast<Json::UInt64>(actually_read);
        chunk_info["file_data"] = Base64Utils::encode(string(buffer, actually_read));
        
        string chunk_data = chunk_info.toStyledString();
        if (!send_message(FILE_CHUNK_REQ, chunk_data)) {
            last_error_ = "Failed to send file chunk";
            return false;
        }
        
        // 等待分块确认
        if (!receive_message(response_type, response_data)) {
            last_error_ = "Failed to receive chunk response";
            return false;
        }
        
        total_sent += actually_read;
        
        // 更新进度
        if (progress_callback) {
            double progress = static_cast<double>(total_sent) / file_size * 100.0;
            progress_callback(total_sent, file_size, progress);
        }
        
        cout << "已上传: " << total_sent << "/" << file_size 
             << " (" << (total_sent * 100 / file_size) << "%)" << endl;
    }
    
    cout << "文件上传完成!" << endl;
    return true;
}

bool ClientFileUploader::download_file(const string& filename, 
                                     const string& save_path,
                                     const DownloadCallback& progress_callback) {
    if (!is_connected_ && !connect()) {
        return false;
    }
    
    // 发送下载请求
    Json::Value download_req;
    download_req["filename"] = filename;
    
    string request_data = download_req.toStyledString();
    if (!send_message(FILE_DOWNLOAD_REQ, request_data)) {
        last_error_ = "Failed to send download request";
        return false;
    }
    
    // 接收下载响应
    uint32_t response_type;
    string response_data;
    if (!receive_message(response_type, response_data)) {
        last_error_ = "Failed to receive download response";
        return false;
    }
    
    if (response_type != FILE_DOWNLOAD_RSP) {
        last_error_ = "Unexpected response type: " + to_string(response_type);
        return false;
    }
    
    // 解析文件信息
    Json::Reader reader;
    Json::Value file_info;
    if (!reader.parse(response_data, file_info)) {
        last_error_ = "Failed to parse file info";
        return false;
    }
    
    size_t file_size = file_info["filesize"].asUInt64();
    int total_chunks = file_info["total_chunks"].asInt();
    
    cout << "开始下载文件: " << filename << " (大小: " << file_size << " 字节)" << endl;
    
    // 创建输出文件
    ofstream output_file(save_path, ios::binary);
    if (!output_file.is_open()) {
        last_error_ = "Failed to create output file: " + save_path;
        return false;
    }
    
    // 接收文件分块
    size_t total_received = 0;
    for (int i = 0; i < total_chunks; ++i) {
        if (!receive_message(response_type, response_data)) {
            last_error_ = "Failed to receive file chunk";
            return false;
        }
        
        if (response_type != FILE_CHUNK_RSP) {
            last_error_ = "Unexpected chunk response type";
            return false;
        }
        
        // 解析分块数据
        Json::Value chunk_info;
        if (!reader.parse(response_data, chunk_info)) {
            last_error_ = "Failed to parse chunk data";
            return false;
        }
        
        string encoded_data = chunk_info["file_data"].asString();
        string chunk_data = Base64Utils::decode(encoded_data);
        
        output_file.write(chunk_data.data(), chunk_data.size());
        total_received += chunk_data.size();
        
        // 更新进度
        if (progress_callback) {
            double progress = static_cast<double>(total_received) / file_size * 100.0;
            progress_callback(total_received, file_size, progress);
        }
        
        cout << "已下载: " << total_received << "/" << file_size 
             << " (" << (total_received * 100 / file_size) << "%)" << endl;
    }
    
    output_file.close();
    cout << "文件下载完成!" << endl;
    return true;
}

bool ClientFileUploader::send_message(uint32_t message_type, const string& data) {
    vector<char> packet = FileChunkProtocol::encode_message(message_type, data);
    
    size_t total_sent = 0;
    while (total_sent < packet.size()) {
        int sent = send(socket_, packet.data() + total_sent, 
                       static_cast<int>(packet.size() - total_sent), 0);
        if (sent == SOCKET_ERROR) {
            return false;
        }
        total_sent += static_cast<size_t>(sent);
    }
    
    return true;
}

bool ClientFileUploader::receive_message(uint32_t& message_type, string& data) {
    vector<char> buffer(8192);
    
    // 先读取协议头
    if (!receive_exact(buffer.data(), FileChunkProtocol::PROTOCOL_HEADER_SIZE)) {
        return false;
    }
    
    // 解析协议头获取消息长度
    FileChunkProtocol::FileChunkHeader header;
    memcpy(&header, buffer.data(), sizeof(header));
    FileChunkProtocol::header_to_host_order(header);
    
    if (!FileChunkProtocol::validate_header(header)) {
        return false;
    }
    
    size_t data_length = header.length - FileChunkProtocol::PROTOCOL_HEADER_SIZE;
    if (data_length > buffer.size()) {
        buffer.resize(data_length);
    }
    
    // 读取消息数据
    if (!receive_exact(buffer.data(), data_length)) {
        return false;
    }
    
    // 解码消息
    vector<char> full_message(FileChunkProtocol::PROTOCOL_HEADER_SIZE + data_length);
    memcpy(full_message.data(), &header, FileChunkProtocol::PROTOCOL_HEADER_SIZE);
    memcpy(full_message.data() + FileChunkProtocol::PROTOCOL_HEADER_SIZE, 
           buffer.data(), data_length);
    
    FileChunkProtocol::ParseResult result = FileChunkProtocol::decode_message(
        full_message.data(), full_message.size(), message_type, data);
    
    return result == FileChunkProtocol::SUCCESS;
}

bool ClientFileUploader::receive_exact(char* buffer, size_t length) {
    size_t total_received = 0;
    while (total_received < length) {
        int received = recv(socket_, buffer + total_received, 
                          static_cast<int>(length - total_received), 0);
        if (received <= 0) {
            return false;
        }
        total_received += static_cast<size_t>(received);
    }
    return true;
}

string ClientFileUploader::get_last_error() const {
    return last_error_;
}

// ClientFileDownloader 实现（继承自 ClientFileUploader）
ClientFileDownloader::ClientFileDownloader(const string& server_host, int server_port)
    : ClientFileUploader(server_host, server_port) {
}
