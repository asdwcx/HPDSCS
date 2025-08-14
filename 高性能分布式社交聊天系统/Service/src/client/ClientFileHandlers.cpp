// 客户端文件传输功能实现

// 标准库头文件
#include <iostream>
#include <fstream>
#include <filesystem>
#include <vector>
#include <string>
#include <sstream>
#include <thread>
#include <chrono>
#include <exception>
#include <algorithm>
#include <cctype>
#include <cstring>
#include <iomanip>

// 网络相关头文件
#ifdef _WIN32
    #include <winsock2.h>
    #include <ws2tcpip.h>
    #pragma comment(lib, "ws2_32.lib")
#else
    #include <sys/socket.h>
    #include <sys/types.h>
    #include <netinet/in.h>
    #include <arpa/inet.h>
    #include <unistd.h>
    #include <netdb.h>
#endif

// OpenSSL Base64 头文件
#include <openssl/bio.h>
#include <openssl/evp.h>
#include <openssl/buffer.h>

// JSON库头文件
#include <json/json.h>

// 项目相关头文件
#include "public.hpp"
#include "User.hpp"
#include "../../include/Base64Utils.hpp"

using namespace std;
using namespace std::filesystem;

// 全局变量声明
extern User g_current_user;
extern bool g_is_menu_running;

// 声明在main.cpp中定义的下载会话结构
struct DownloadSession {
    string file_id;
    string file_name;
    int file_size;
    int total_chunks;
    int chunk_size;
    vector<vector<char>> chunks;
    vector<bool> received_chunks;
    int received_count;
    bool is_downloading;
    
    DownloadSession() : file_size(0), total_chunks(0), chunk_size(0), 
                       received_count(0), is_downloading(false) {}
};

extern DownloadSession g_download_session;

// 文件传输消息类型已统一定义在public.hpp中，无需重复定义

// 发送文件
void SendFile(int clientfd, string str)
{
    int idx = str.find(":");
    if (idx == -1)
    {
        cerr << "SendFile command invalid! Format: sendfile:filepath:receiver_id或sendfile:filepath:group:group_id" << endl;
        return;
    }
    
    string params = str.substr(idx + 1);
    vector<string> parts;
    stringstream ss(params);
    string item;
    
    while (getline(ss, item, ':'))
    {
        parts.push_back(item);
    }
    
    if (parts.size() < 2)
    {
        cerr << "SendFile command invalid! Need at least filepath and target" << endl;
        return;
    }
    
    string file_path = parts[0];
    string target_type = parts.size() > 2 ? parts[1] : "user";
    string target_id = parts.size() > 2 ? parts[2] : parts[1];
    
    // 检查文件是否存在
    if (!exists(file_path))
    {
        cerr << "文件不存在: " << file_path << endl;
        return;
    }
    
    // 获取文件信息
    path file_path_obj(file_path);
    string file_name = file_path_obj.filename().string();
    size_t file_size = file_size(file_path_obj);
    string file_type = file_path_obj.extension().string();
    
    // 检查文件大小限制（100MB）
    if (file_size > 100 * 1024 * 1024)
    {
        cerr << "文件太大，最大支持100MB" << endl;
        return;
    }
    
    // 计算分片数量（每片64KB）
    const size_t CHUNK_SIZE = 64 * 1024;
    int total_chunks = (file_size + CHUNK_SIZE - 1) / CHUNK_SIZE;
    
    try
    {
        // 发送文件上传请求
        json js;
        js["msgid"] = FILE_UPLOAD_REQ;
        js["id"] = g_current_user.get_id();
        js["file_name"] = file_name;
        js["file_size"] = static_cast<int>(file_size);
        js["file_type"] = file_type;
        js["total_chunks"] = total_chunks;
        
        if (target_type == "group")
        {
            js["group_id"] = stoi(target_id);
            js["receiver_id"] = -1;
        }
        else
        {
            js["receiver_id"] = stoi(target_id);
            js["group_id"] = -1;
        }
        
        string request = js.dump();
        
        int len = send(clientfd, request.c_str(), strlen(request.c_str()) + 1, 0);
        if (len == -1)
        {
            cerr << "发送文件上传请求失败" << endl;
            return;
        }
        
        cout << "文件上传请求已发送: " << file_name << " (" << file_size << " bytes, " << total_chunks << " chunks)" << endl;
        
        // 等待服务器响应后，开始发送文件分片（这里简化处理，实际应该在响应处理中进行）
        this_thread::sleep_for(chrono::milliseconds(100));
        
        // 读取文件并分片发送
        ifstream file(file_path, ios::binary);
        if (!file.is_open())
        {
            cerr << "无法打开文件: " << file_path << endl;
            return;
        }
        
        vector<char> buffer(CHUNK_SIZE);
        int chunk_seq = 1;
        
        while (file.good() && chunk_seq <= total_chunks)
        {
            //这是一个 ​二进制读取，适合读取任意文件类型（比如图片、视频、压缩包等）
            //调用了 ifstream::read()方法，从文件中读取最多 CHUNK_SIZE个字节的数据，存入 buffer.data()
            file.read(buffer.data(), CHUNK_SIZE);
            //file.gcount()是 ifstream的一个成员函数，用于返回上一次 read() 操作实际读取到的字节数。
            size_t bytes_read = file.gcount();
            
            if (bytes_read > 0)
            {
                // 调整buffer大小
                buffer.resize(bytes_read);
                
                // Base64编码
                string encoded_data = Base64Utils::encode(buffer);
                
                // 创建分片消息
                json chunk_msg;
                chunk_msg["msgid"] = FILE_CHUNK_MSG;
                chunk_msg["session_id"] = ""; // 需要从上传响应中获取
                chunk_msg["chunk_seq"] = chunk_seq;
                chunk_msg["chunk_data"] = encoded_data;
                chunk_msg["is_last"] = (chunk_seq == total_chunks);
                
                string chunk_request = chunk_msg.dump();
                
                len = send(clientfd, chunk_request.c_str(), strlen(chunk_request.c_str()) + 1, 0);
                if (len == -1)
                {
                    cerr << "发送文件分片失败: chunk " << chunk_seq << endl;
                    break;
                }
                
                cout << "发送分片 " << chunk_seq << "/" << total_chunks << " (" << bytes_read << " bytes)" << endl;
                
                chunk_seq++;
                buffer.resize(CHUNK_SIZE);
                
                // 添加小延迟避免过快发送
                this_thread::sleep_for(chrono::milliseconds(10));
            }
        }
        
        file.close();
        cout << "文件发送完成: " << file_name << endl;
        
    }
    catch (const exception& e)
    {
        cerr << "发送文件出错: " << e.what() << endl;
    }
}

// 下载文件
void DownloadFile(int clientfd, string str)
{
    int idx = str.find(":");
    if (idx == -1)
    {
        cerr << "DownloadFile command invalid! Format: downloadfile:file_id" << endl;
        return;
    }
    
    string file_id = str.substr(idx + 1);
    
    // 检查是否已有下载进行中
    if (g_download_session.is_downloading) {
        cout << "❌ 已有文件下载进行中，请等待完成后再下载其他文件" << endl;
        return;
    }
    
    try
    {
        json js;
        js["msgid"] = FILE_DOWNLOAD_REQ;
        js["id"] = g_current_user.get_id();
        js["file_id"] = file_id;
        
        string request = js.dump();
        
        int len = send(clientfd, request.c_str(), strlen(request.c_str()) + 1, 0);
        if (len == -1)
        {
            cerr << "发送文件下载请求失败" << endl;
            return;
        }
        
        cout << "文件下载请求已发送: " << file_id << endl;
        cout << "等待服务器响应..." << endl;
        
    }
    catch (const exception& e)
    {
        cerr << "下载文件请求出错: " << e.what() << endl;
    }
}

// 查看文件列表
void ListFiles(int clientfd, string str)
{
    try
    {
        json js;
        js["msgid"] = 100; // 临时消息ID，需要在协议中定义
        js["id"] = g_current_user.get_id();
        
        if (!str.empty())
        {
            vector<string> parts;
            stringstream ss(str);
            string item;
            
            while (getline(ss, item, ':'))
            {
                parts.push_back(item);
            }
            
            if (parts.size() >= 2 && parts[0] == "group")
            {
                js["group_id"] = stoi(parts[1]);
            }
        }
        
        string request = js.dump();
        
        int len = send(clientfd, request.c_str(), strlen(request.c_str()) + 1, 0);
        if (len == -1)
        {
            cerr << "发送文件列表请求失败" << endl;
            return;
        }
        
        cout << "文件列表请求已发送" << endl;
        
    }
    catch (const exception& e)
    {
        cerr << "查看文件列表出错: " << e.what() << endl;
    }
}
