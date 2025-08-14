#include "ClientFileChunkHelper.hpp"
#include <iostream>
#include <fstream>
#include <thread>
#include <chrono>
#include <sstream>

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
            throw runtime_error("Failed to initialize Winsock");
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

ClientFileChunkHelper::ClientFileChunkHelper(const string& server_host, int server_port, int user_id)
    : buffer_(8192), server_host_(server_host), server_port_(server_port), 
      user_id_(user_id), socket_(INVALID_SOCKET), connected_(false) {
}

ClientFileChunkHelper::~ClientFileChunkHelper() {
    disconnect();
}

bool ClientFileChunkHelper::connect() {
    if (connected_) {
        return true;
    }
    
    socket_ = socket(AF_INET, SOCK_STREAM, 0);
    if (socket_ == INVALID_SOCKET) {
        return false;
    }
    
    sockaddr_in server_addr{};
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(server_port_);
    
    if (inet_pton(AF_INET, server_host_.c_str(), &server_addr.sin_addr) <= 0) {
        closesocket(socket_);
        socket_ = INVALID_SOCKET;
        return false;
    }
    
    if (::connect(socket_, (sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        closesocket(socket_);
        socket_ = INVALID_SOCKET;
        return false;
    }
    
    connected_ = true;
    return true;
}

void ClientFileChunkHelper::disconnect() {
    if (socket_ != INVALID_SOCKET) {
        closesocket(socket_);
        socket_ = INVALID_SOCKET;
    }
    connected_ = false;
}

bool ClientFileChunkHelper::upload_file(const string& file_path, 
                                       const std::function<void(float)>& progress_callback,
                                       const std::function<void(bool, const string&)>& completion_callback) {
    if (!connected_) {
        if (completion_callback) {
            completion_callback(false, "未连接到服务器");
        }
        return false;
    }
    
    // 检查文件是否存在
    ifstream file(file_path, ios::binary | ios::ate);
    if (!file) {
        if (completion_callback) {
            completion_callback(false, "无法打开文件");
        }
        return false;
    }
    
    // 获取文件信息
    size_t file_size = file.tellg();
    file.seekg(0, ios::beg);
    
    string filename = file_path.substr(file_path.find_last_of("/\\") + 1);
    string file_hash = calculate_file_hash(file_path);
    
    // 创建文件上传请求
    FileUploadRequest upload_request = FileTransferMessageBuilder::create_upload_request(
        user_id_, filename, file_size, file_hash, "", 64 * 1024);
    
    // 发送上传请求
    if (!send_protocol_message(FILE_UPLOAD_MSG, upload_request)) {
        if (completion_callback) {
            completion_callback(false, "发送上传请求失败");
        }
        return false;
    }
    
    // 接收上传响应
    uint32_t response_type;
    FileUploadResponse upload_response;
    if (!receive_protocol_message(response_type, upload_response) || 
        response_type != FILE_UPLOAD_RSP) {
        if (completion_callback) {
            completion_callback(false, "接收上传响应失败");
        }
        return false;
    }
    
    // 处理上传响应
    if (upload_response.errno() != 0) {
        if (completion_callback) {
            completion_callback(false, upload_response.errmsg());
        }
        return false;
    }
    
    string upload_id = upload_response.upload_id();
    string file_id = upload_response.file_id();
    uint32_t chunk_size = 64 * 1024;
    uint32_t total_chunks = (file_size + chunk_size - 1) / chunk_size;
    
    // 开始分片上传
    vector<char> chunk_data(chunk_size);
    for (uint32_t chunk_index = 0; chunk_index < total_chunks; ++chunk_index) {
        size_t actually_read = 0;
        file.read(chunk_data.data(), chunk_size);
        actually_read = file.gcount();
        
        if (actually_read == 0) {
            break;
        }
        
        bool is_last_chunk = (chunk_index == total_chunks - 1);
        string chunk_data_str(chunk_data.data(), actually_read);
        string chunk_hash = calculate_chunk_hash(chunk_data_str);
        
        // 创建分片消息
        FileChunkMessage chunk_msg = FileTransferMessageBuilder::create_chunk_message(
            upload_id, file_id, chunk_index, chunk_data_str, chunk_hash, is_last_chunk, total_chunks);
        
        // 发送分片
        if (!send_protocol_message(FILE_CHUNK_MSG, chunk_msg)) {
            if (completion_callback) {
                completion_callback(false, "发送分片失败");
            }
            return false;
        }
        
        // 接收分片响应
        uint32_t chunk_response_type;
        FileChunkResponse chunk_response;
        if (!receive_protocol_message(chunk_response_type, chunk_response) || 
            chunk_response_type != FILE_CHUNK_RSP) {
            if (completion_callback) {
                completion_callback(false, "接收分片响应失败");
            }
            return false;
        }
        
        // 检查分片响应
        if (chunk_response.errno() != 0) {
            if (completion_callback) {
                completion_callback(false, chunk_response.errmsg());
            }
            return false;
        }
        
        // 更新进度
        float progress = chunk_response.upload_progress();
        if (progress_callback) {
            progress_callback(progress);
        }
        
        // 检查是否完成
        if (chunk_response.upload_complete()) {
            if (completion_callback) {
                completion_callback(true, "文件上传成功");
            }
            return true;
        }
    }
    
    if (completion_callback) {
        completion_callback(false, "文件上传未完成");
    }
    return false;
}

bool ClientFileChunkHelper::download_file(const string& file_id,
                                         const string& save_path,
                                         const std::function<void(float)>& progress_callback,
                                         const std::function<void(bool, const string&)>& completion_callback) {
    if (!connected_) {
        if (completion_callback) {
            completion_callback(false, "未连接到服务器");
        }
        return false;
    }
    
    // 创建文件下载请求
    FileDownloadRequest download_request = FileTransferMessageBuilder::create_download_request(
        user_id_, file_id, "", 64 * 1024);
    
    // 发送下载请求
    if (!send_protocol_message(FILE_DOWNLOAD_MSG, download_request)) {
        if (completion_callback) {
            completion_callback(false, "发送下载请求失败");
        }
        return false;
    }
    
    // 接收下载响应
    uint32_t response_type;
    FileDownloadResponse download_response;
    if (!receive_protocol_message(response_type, download_response) || 
        response_type != FILE_DOWNLOAD_RSP) {
        if (completion_callback) {
            completion_callback(false, "接收下载响应失败");
        }
        return false;
    }
    
    // 处理下载响应
    if (download_response.errno() != 0) {
        if (completion_callback) {
            completion_callback(false, download_response.errmsg());
        }
        return false;
    }
    
    string download_id = download_response.download_id();
    uint32_t total_chunks = download_response.total_chunks();
    uint64_t file_size = download_response.filesize();
    
    // 创建输出文件
    ofstream output_file(save_path, ios::binary);
    if (!output_file) {
        if (completion_callback) {
            completion_callback(false, "无法创建输出文件");
        }
        return false;
    }
    
    // 逐个下载分片
    for (uint32_t chunk_index = 0; chunk_index < total_chunks; ++chunk_index) {
        // 创建分片下载请求
        FileChunkDownloadRequest chunk_request = FileTransferMessageBuilder::create_chunk_download_request(
            download_id, file_id, chunk_index);
        
        // 发送分片下载请求
        if (!send_protocol_message(FILE_CHUNK_DOWN_MSG, chunk_request)) {
            if (completion_callback) {
                completion_callback(false, "发送分片下载请求失败");
            }
            return false;
        }
        
        // 接收分片下载响应
        uint32_t chunk_response_type;
        FileChunkDownloadResponse chunk_response;
        if (!receive_protocol_message(chunk_response_type, chunk_response) || 
            chunk_response_type != FILE_CHUNK_DOWN_RSP) {
            if (completion_callback) {
                completion_callback(false, "接收分片下载响应失败");
            }
            return false;
        }
        
        // 检查分片响应
        if (chunk_response.errno() != 0) {
            if (completion_callback) {
                completion_callback(false, chunk_response.errmsg());
            }
            return false;
        }
        
        // 写入分片数据
        const string& chunk_data = chunk_response.chunk_data();
        output_file.write(chunk_data.data(), chunk_data.size());
        
        // 更新进度
        float progress = chunk_response.download_progress();
        if (progress_callback) {
            progress_callback(progress);
        }
        
        // 检查是否为最后一个分片
        if (chunk_response.is_last_chunk()) {
            if (completion_callback) {
                completion_callback(true, "文件下载成功");
            }
            return true;
        }
    }
    
    if (completion_callback) {
        completion_callback(false, "文件下载未完成");
    }
    return false;
}

// 处理响应的辅助方法
void ClientFileChunkHelper::handle_upload_response(const FileUploadResponse& response) {
    cout << "Upload response: errno=" << response.errno() 
         << ", errmsg=" << response.errmsg()
         << ", file_id=" << response.file_id()
         << ", upload_id=" << response.upload_id() << endl;
}

void ClientFileChunkHelper::handle_chunk_response(const FileChunkResponse& response) {
    cout << "Chunk response: errno=" << response.errno()
         << ", errmsg=" << response.errmsg()
         << ", upload_id=" << response.upload_id()
         << ", progress=" << response.upload_progress() << endl;
}

void ClientFileChunkHelper::handle_download_response(const FileDownloadResponse& response) {
    cout << "Download response: errno=" << response.errno()
         << ", errmsg=" << response.errmsg()
         << ", file_id=" << response.file_id()
         << ", filename=" << response.filename()
         << ", filesize=" << response.filesize() << endl;
}

void ClientFileChunkHelper::handle_chunk_download_response(const FileChunkDownloadResponse& response) {
    cout << "Chunk download response: errno=" << response.errno()
         << ", errmsg=" << response.errmsg()
         << ", download_id=" << response.download_id()
         << ", chunk_index=" << response.chunk_index()
         << ", progress=" << response.download_progress() << endl;
}

// 工具方法
vector<vector<char>> ClientFileChunkHelper::split_file_to_chunks(const string& file_path, size_t chunk_size) {
    vector<vector<char>> chunks;
    ifstream file(file_path, ios::binary);
    
    if (!file) {
        return chunks;
    }
    
    vector<char> chunk(chunk_size);
    while (file.read(chunk.data(), chunk_size) || file.gcount() > 0) {
        size_t actually_read = file.gcount();
        chunks.emplace_back(chunk.begin(), chunk.begin() + actually_read);
    }
    
    return chunks;
}

bool ClientFileChunkHelper::merge_chunks_to_file(const string& file_path) {
    // TODO: 实现分片合并逻辑
    return true;
}

string ClientFileChunkHelper::encode_base64(const vector<char>& data) {
    // TODO: 实现Base64编码
    return "";
}

vector<char> ClientFileChunkHelper::decode_base64(const string& encoded) {
    // TODO: 实现Base64解码
    return {};
}

string ClientFileChunkHelper::calculate_file_hash(const string& file_path) {
    // TODO: 实现文件哈希计算（MD5或SHA256）
    // 这里暂时返回简单的文件大小作为占位符
    ifstream file(file_path, ios::binary | ios::ate);
    if (!file) {
        return "";
    }
    
    size_t file_size = file.tellg();
    return "hash_" + std::to_string(file_size);
}

string ClientFileChunkHelper::calculate_chunk_hash(const string& chunk_data) {
    // TODO: 实现分片哈希计算
    // 这里暂时返回简单的长度字符串作为占位符
    return "chunk_" + std::to_string(chunk_data.size());
}

// 网络I/O辅助方法（需要根据实际的网络库实现）
bool ClientFileChunkHelper::send_data(const char* data, size_t length) {
    if (socket_ == INVALID_SOCKET) {
        return false;
    }
    
    size_t total_sent = 0;
    while (total_sent < length) {
        int sent = send(socket_, data + total_sent, length - total_sent, 0);
        if (sent <= 0) {
            return false;
        }
        total_sent += sent;
    }
    
    return true;
}

bool ClientFileChunkHelper::receive_data(char* buffer, size_t length) {
    if (socket_ == INVALID_SOCKET) {
        return false;
    }
    
    size_t total_received = 0;
    while (total_received < length) {
        int received = recv(socket_, buffer + total_received, length - total_received, 0);
        if (received <= 0) {
            return false;
        }
        total_received += received;
    }
    
    return true;
}
