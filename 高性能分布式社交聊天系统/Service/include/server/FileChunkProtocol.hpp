#ifndef FILE_CHUNK_PROTOCOL_HPP
#define FILE_CHUNK_PROTOCOL_HPP

#include <cstring>
#include <string>
#include <vector>
#include <iostream>

#ifdef _WIN32
    #include <winsock2.h>
    #pragma comment(lib, "ws2_32.lib")
#else
    #include <arpa/inet.h>
#endif

using namespace std;

/**
 * 文件分片传输协议
 * 解决TCP粘包问题的文件传输协议实现
 * 
 * 协议格式：
 * [协议头(16字节)] + [Protobuf消息数据(变长)]
 * 
 * 协议头结构：
 * - magic_number(4字节): 协议标识 0x46434850 ('FCHP' - File Chunk Protocol)
 * - message_length(4字节): 消息总长度(不包含协议头)
 * - message_type(4字节): 消息类型
 * - checksum(4字节): CRC32校验和
 */

// 协议常量定义
const uint32_t PROTOCOL_MAGIC_NUMBER = 0x46434850;  // 'FCHP'
const uint32_t PROTOCOL_HEADER_SIZE = 16;           // 协议头大小：16字节
const uint32_t MAX_MESSAGE_SIZE = 128 * 1024;       // 最大消息大小：128KB

// 文件分片协议头结构
struct FileChunkHeader {
    uint32_t magic_number;    // 协议标识：0x46434850
    uint32_t message_length;  // 消息长度（不包含头部）
    uint32_t message_type;    // 消息类型
    uint32_t checksum;        // CRC32校验和
    
    FileChunkHeader() : magic_number(PROTOCOL_MAGIC_NUMBER), 
                       message_length(0), message_type(0), checksum(0) {}
};

// 文件分片协议类
class FileChunkProtocol {
public:
    /**
     * 将Protobuf消息封装为带协议头的数据包
     * @param message_type 消息类型
     * @param protobuf_data Protobuf序列化后的数据
     * @return 完整的数据包（协议头+消息体）
     */
    static vector<char> encode_message(uint32_t message_type, const string& protobuf_data);
    
    /**
     * 从TCP字节流中解析完整消息
     * @param buffer 接收缓冲区
     * @param buffer_size 缓冲区大小
     * @param bytes_consumed 输出参数：已消费的字节数
     * @param message_type 输出参数：消息类型
     * @param protobuf_data 输出参数：解析出的Protobuf数据
     * @return 解析结果：SUCCESS, NEED_MORE_DATA, INVALID_HEADER, CHECKSUM_ERROR
     */
    enum ParseResult {
        SUCCESS,           // 解析成功
        NEED_MORE_DATA,    // 需要更多数据
        INVALID_HEADER,    // 无效的协议头
        CHECKSUM_ERROR,    // 校验和错误
        MESSAGE_TOO_LARGE  // 消息过大
    };
    
    static ParseResult decode_message(const char* buffer, size_t buffer_size,
                                    size_t& bytes_consumed,
                                    uint32_t& message_type,
                                    string& protobuf_data);
    
    /**
     * 计算CRC32校验和
     * @param data 数据指针
     * @param length 数据长度
     * @return CRC32校验和
     */
    static uint32_t calculate_crc32(const char* data, size_t length);
    
    /**
     * 验证协议头的完整性
     * @param header 协议头
     * @return 是否有效
     */
    static bool validate_header(const FileChunkHeader& header);
    
    /**
     * 将协议头转换为网络字节序
     */
    static void header_to_network_order(FileChunkHeader& header);
    
    /**
     * 将协议头从网络字节序转换为主机字节序
     */
    static void header_to_host_order(FileChunkHeader& header);
};

// 文件分片缓冲区管理器
class FileChunkBuffer {
private:
    vector<char> buffer_;
    size_t write_pos_;
    size_t read_pos_;
    
public:
    FileChunkBuffer(size_t initial_size = 8192);
    
    /**
     * 向缓冲区追加数据
     */
    void append(const char* data, size_t length);
    
    /**
     * 尝试解析一个完整的消息
     */
    FileChunkProtocol::ParseResult try_parse_message(uint32_t& message_type, string& protobuf_data);
    
    /**
     * 获取可读数据大小
     */
    size_t readable_size() const { return write_pos_ - read_pos_; }
    
    /**
     * 压缩缓冲区（移除已读数据）
     */
    void compact();
    
    /**
     * 清空缓冲区
     */
    void clear();
};

#endif // FILE_CHUNK_PROTOCOL_HPP
