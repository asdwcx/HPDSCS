#pragma once

#include "message.pb.h"
#include <string>
#include <memory>
#include <functional>

using namespace std;
using namespace chat;

/**
 * Protobuf消息序列化和反序列化辅助类
 * 用于替换项目中的JSON处理
 */
class ProtobufMessageHelper {
public:
    // 序列化消息到字符串
    template<typename T>
    static string serialize_message(const T& message) {
        string serialized_data;
        if (message.SerializeToString(&serialized_data)) {
            return serialized_data;
        }
        return "";
    }

    // 从字符串反序列化消息
    template<typename T>
    static bool deserialize_message(const string& data, T& message) {
        return message.ParseFromString(data);
    }

    // 创建MessageWrapper包装消息
    template<typename T>
    static MessageWrapper create_wrapper(MsgType msg_type, const T& message) {
        MessageWrapper wrapper;
        wrapper.set_msgid(msg_type);
        
        string serialized_data = serialize_message(message);
        wrapper.set_data(serialized_data);
        
        return wrapper;
    }

    // 从MessageWrapper解包消息
    template<typename T>
    static bool unwrap_message(const MessageWrapper& wrapper, T& message) {
        return deserialize_message(wrapper.data(), message);
    }

    // 创建错误响应
    template<typename T>
    static T create_error_response(MsgType msg_type, int32_t error_code, const string& error_msg) {
        T response;
        response.set_msgid(msg_type);
        response.set_errno(error_code);
        response.set_errmsg(error_msg);
        return response;
    }

    // 验证消息完整性
    template<typename T>
    static bool validate_message(const T& message) {
        return message.IsInitialized();
    }

    // 消息转JSON字符串（用于调试和日志）
    template<typename T>
    static string message_to_debug_string(const T& message) {
        return message.DebugString();
    }

    // 从二进制数据解析MessageWrapper
    static bool parse_message_wrapper(const string& binary_data, MessageWrapper& wrapper) {
        return wrapper.ParseFromString(binary_data);
    }

    // 序列化MessageWrapper到二进制
    static string serialize_message_wrapper(const MessageWrapper& wrapper) {
        return serialize_message(wrapper);
    }
};

/**
 * 文件传输Protobuf消息构建器
 * 用于构建和处理文件传输相关的Protobuf消息
 */
class FileTransferMessageBuilder {
public:
    // 创建文件上传请求
    static FileUploadRequest create_upload_request(
        int32_t user_id,
        const string& filename,
        uint64_t filesize,
        const string& file_hash,
        const string& file_type = "",
        uint32_t chunk_size = 65536,
        const string& upload_id = ""
    ) {
        FileUploadRequest request;
        request.set_msgid(FILE_UPLOAD_MSG);
        request.set_user_id(user_id);
        request.set_filename(filename);
        request.set_filesize(filesize);
        request.set_file_hash(file_hash);
        request.set_file_type(file_type);
        request.set_chunk_size(chunk_size);
        request.set_upload_id(upload_id);
        return request;
    }

    // 创建文件上传响应
    static FileUploadResponse create_upload_response(
        int32_t error_code,
        const string& error_msg,
        const string& file_id = "",
        const string& upload_id = "",
        uint32_t next_chunk_index = 0
    ) {
        FileUploadResponse response;
        response.set_msgid(FILE_UPLOAD_RSP);
        response.set_errno(error_code);
        response.set_errmsg(error_msg);
        response.set_file_id(file_id);
        response.set_upload_id(upload_id);
        response.set_next_chunk_index(next_chunk_index);
        return response;
    }

    // 创建文件分片消息
    static FileChunkMessage create_chunk_message(
        const string& upload_id,
        const string& file_id,
        uint32_t chunk_index,
        const string& chunk_data,
        const string& chunk_hash,
        bool is_last_chunk = false,
        uint32_t total_chunks = 0
    ) {
        FileChunkMessage chunk;
        chunk.set_msgid(FILE_CHUNK_MSG);
        chunk.set_upload_id(upload_id);
        chunk.set_file_id(file_id);
        chunk.set_chunk_index(chunk_index);
        chunk.set_chunk_size(chunk_data.size());
        chunk.set_chunk_data(chunk_data);
        chunk.set_chunk_hash(chunk_hash);
        chunk.set_is_last_chunk(is_last_chunk);
        chunk.set_total_chunks(total_chunks);
        return chunk;
    }

    // 创建文件分片响应
    static FileChunkResponse create_chunk_response(
        int32_t error_code,
        const string& error_msg,
        const string& upload_id,
        uint32_t received_chunk_index,
        uint32_t next_chunk_index,
        float upload_progress = 0.0f,
        bool upload_complete = false
    ) {
        FileChunkResponse response;
        response.set_msgid(FILE_CHUNK_RSP);
        response.set_errno(error_code);
        response.set_errmsg(error_msg);
        response.set_upload_id(upload_id);
        response.set_received_chunk_index(received_chunk_index);
        response.set_next_chunk_index(next_chunk_index);
        response.set_upload_progress(upload_progress);
        response.set_upload_complete(upload_complete);
        return response;
    }

    // 创建文件下载请求
    static FileDownloadRequest create_download_request(
        int32_t user_id,
        const string& file_id,
        const string& filename = "",
        uint32_t chunk_size = 65536,
        uint32_t start_chunk = 0,
        const string& download_id = ""
    ) {
        FileDownloadRequest request;
        request.set_msgid(FILE_DOWNLOAD_MSG);
        request.set_user_id(user_id);
        request.set_file_id(file_id);
        request.set_filename(filename);
        request.set_chunk_size(chunk_size);
        request.set_start_chunk(start_chunk);
        request.set_download_id(download_id);
        return request;
    }

    // 创建文件下载响应
    static FileDownloadResponse create_download_response(
        int32_t error_code,
        const string& error_msg,
        const string& file_id = "",
        const string& filename = "",
        uint64_t filesize = 0,
        const string& file_hash = "",
        const string& file_type = "",
        uint32_t total_chunks = 0,
        uint32_t chunk_size = 0,
        const string& download_id = "",
        uint32_t start_chunk_index = 0
    ) {
        FileDownloadResponse response;
        response.set_msgid(FILE_DOWNLOAD_RSP);
        response.set_errno(error_code);
        response.set_errmsg(error_msg);
        response.set_file_id(file_id);
        response.set_filename(filename);
        response.set_filesize(filesize);
        response.set_file_hash(file_hash);
        response.set_file_type(file_type);
        response.set_total_chunks(total_chunks);
        response.set_chunk_size(chunk_size);
        response.set_download_id(download_id);
        response.set_start_chunk_index(start_chunk_index);
        return response;
    }

    // 创建文件分片下载请求
    static FileChunkDownloadRequest create_chunk_download_request(
        const string& download_id,
        const string& file_id,
        uint32_t chunk_index
    ) {
        FileChunkDownloadRequest request;
        request.set_msgid(FILE_CHUNK_DOWN_MSG);
        request.set_download_id(download_id);
        request.set_file_id(file_id);
        request.set_chunk_index(chunk_index);
        return request;
    }

    // 创建文件分片下载响应
    static FileChunkDownloadResponse create_chunk_download_response(
        int32_t error_code,
        const string& error_msg,
        const string& download_id,
        const string& file_id,
        uint32_t chunk_index,
        const string& chunk_data,
        const string& chunk_hash,
        bool is_last_chunk = false,
        float download_progress = 0.0f
    ) {
        FileChunkDownloadResponse response;
        response.set_msgid(FILE_CHUNK_DOWN_RSP);
        response.set_errno(error_code);
        response.set_errmsg(error_msg);
        response.set_download_id(download_id);
        response.set_file_id(file_id);
        response.set_chunk_index(chunk_index);
        response.set_chunk_size(chunk_data.size());
        response.set_chunk_data(chunk_data);
        response.set_chunk_hash(chunk_hash);
        response.set_is_last_chunk(is_last_chunk);
        response.set_download_progress(download_progress);
        return response;
    }
};

/**
 * 服务发现Protobuf消息构建器
 */
class ServiceDiscoveryMessageBuilder {
public:
    // 创建服务实例
    static ServiceInstance create_service_instance(
        const string& id,
        const string& name,
        const string& host,
        int32_t port,
        const vector<string>& tags = {},
        const map<string, string>& metadata = {},
        const string& status = "healthy"
    ) {
        ServiceInstance instance;
        instance.set_id(id);
        instance.set_name(name);
        instance.set_host(host);
        instance.set_port(port);
        
        for (const auto& tag : tags) {
            instance.add_tags(tag);
        }
        
        for (const auto& pair : metadata) {
            (*instance.mutable_metadata())[pair.first] = pair.second;
        }
        
        instance.set_status(status);
        instance.set_timestamp(time(nullptr));
        
        return instance;
    }

    // 创建服务器信息
    static ServerInfo create_server_info(
        const string& id,
        const string& host,
        int32_t port,
        const string& zone,
        const vector<string>& services = {},
        int32_t load = 0,
        const string& status = "active"
    ) {
        ServerInfo info;
        info.set_id(id);
        info.set_host(host);
        info.set_port(port);
        info.set_zone(zone);
        
        for (const auto& service : services) {
            info.add_services(service);
        }
        
        info.set_load(load);
        info.set_status(status);
        info.set_last_heartbeat(time(nullptr));
        
        return info;
    }
};
