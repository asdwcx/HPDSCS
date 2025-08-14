#include "ProtobufMessageHelper.hpp"
#include <muduo/base/Logging.h>
#include <google/protobuf/util/json_util.h>

using namespace muduo;

// 静态实例化模板方法，避免链接错误
template string ProtobufMessageHelper::serialize_message<MessageWrapper>(const MessageWrapper&);
template bool ProtobufMessageHelper::deserialize_message<MessageWrapper>(const string&, MessageWrapper&);

template string ProtobufMessageHelper::serialize_message<FileUploadRequest>(const FileUploadRequest&);
template bool ProtobufMessageHelper::deserialize_message<FileUploadRequest>(const string&, FileUploadRequest&);

template string ProtobufMessageHelper::serialize_message<FileUploadResponse>(const FileUploadResponse&);
template bool ProtobufMessageHelper::deserialize_message<FileUploadResponse>(const string&, FileUploadResponse&);

template string ProtobufMessageHelper::serialize_message<FileChunkMessage>(const FileChunkMessage&);
template bool ProtobufMessageHelper::deserialize_message<FileChunkMessage>(const string&, FileChunkMessage&);

template string ProtobufMessageHelper::serialize_message<FileChunkResponse>(const FileChunkResponse&);
template bool ProtobufMessageHelper::deserialize_message<FileChunkResponse>(const string&, FileChunkResponse&);

template string ProtobufMessageHelper::serialize_message<FileDownloadRequest>(const FileDownloadRequest&);
template bool ProtobufMessageHelper::deserialize_message<FileDownloadRequest>(const string&, FileDownloadRequest&);

template string ProtobufMessageHelper::serialize_message<FileDownloadResponse>(const FileDownloadResponse&);
template bool ProtobufMessageHelper::deserialize_message<FileDownloadResponse>(const string&, FileDownloadResponse&);

template string ProtobufMessageHelper::serialize_message<FileChunkDownloadRequest>(const FileChunkDownloadRequest&);
template bool ProtobufMessageHelper::deserialize_message<FileChunkDownloadRequest>(const string&, FileChunkDownloadRequest&);

template string ProtobufMessageHelper::serialize_message<FileChunkDownloadResponse>(const FileChunkDownloadResponse&);
template bool ProtobufMessageHelper::deserialize_message<FileChunkDownloadResponse>(const string&, FileChunkDownloadResponse&);

template string ProtobufMessageHelper::serialize_message<ServiceInstance>(const ServiceInstance&);
template bool ProtobufMessageHelper::deserialize_message<ServiceInstance>(const string&, ServiceInstance&);

template string ProtobufMessageHelper::serialize_message<ServerInfo>(const ServerInfo&);
template bool ProtobufMessageHelper::deserialize_message<ServerInfo>(const string&, ServerInfo&);

/**
 * Protobuf消息转JSON字符串（用于调试）
 */
template<>
string ProtobufMessageHelper::message_to_debug_string<MessageWrapper>(const MessageWrapper& message) {
    string json_string;
    google::protobuf::util::MessageToJsonString(message, &json_string);
    return json_string.empty() ? message.DebugString() : json_string;
}

/**
 * Protobuf消息验证辅助函数
 */
namespace {
    // 验证文件上传请求
    bool validate_upload_request(const FileUploadRequest& request) {
        if (request.user_id() <= 0) {
            LOG_WARN << "Invalid user_id in upload request: " << request.user_id();
            return false;
        }
        
        if (request.filename().empty()) {
            LOG_WARN << "Empty filename in upload request";
            return false;
        }
        
        if (request.filesize() == 0) {
            LOG_WARN << "Zero filesize in upload request";
            return false;
        }
        
        if (request.chunk_size() == 0) {
            LOG_WARN << "Zero chunk_size in upload request";
            return false;
        }
        
        return true;
    }
    
    // 验证文件下载请求
    bool validate_download_request(const FileDownloadRequest& request) {
        if (request.user_id() <= 0) {
            LOG_WARN << "Invalid user_id in download request: " << request.user_id();
            return false;
        }
        
        if (request.file_id().empty()) {
            LOG_WARN << "Empty file_id in download request";
            return false;
        }
        
        return true;
    }
    
    // 验证文件分片消息
    bool validate_chunk_message(const FileChunkMessage& chunk) {
        if (chunk.upload_id().empty()) {
            LOG_WARN << "Empty upload_id in chunk message";
            return false;
        }
        
        if (chunk.chunk_data().empty()) {
            LOG_WARN << "Empty chunk_data in chunk message";
            return false;
        }
        
        if (chunk.chunk_size() != chunk.chunk_data().size()) {
            LOG_WARN << "Chunk size mismatch: expected=" << chunk.chunk_size() 
                     << ", actual=" << chunk.chunk_data().size();
            return false;
        }
        
        return true;
    }
}

/**
 * 特化的消息验证函数
 */
template<>
bool ProtobufMessageHelper::validate_message<FileUploadRequest>(const FileUploadRequest& message) {
    return message.IsInitialized() && validate_upload_request(message);
}

template<>
bool ProtobufMessageHelper::validate_message<FileDownloadRequest>(const FileDownloadRequest& message) {
    return message.IsInitialized() && validate_download_request(message);
}

template<>
bool ProtobufMessageHelper::validate_message<FileChunkMessage>(const FileChunkMessage& message) {
    return message.IsInitialized() && validate_chunk_message(message);
}

/**
 * 消息类型映射辅助函数
 */
class MessageTypeMapper {
public:
    static MsgType get_response_type(MsgType request_type) {
        switch (request_type) {
            case FILE_UPLOAD_MSG:
                return FILE_UPLOAD_RSP;
            case FILE_CHUNK_MSG:
                return FILE_CHUNK_RSP;
            case FILE_DOWNLOAD_MSG:
                return FILE_DOWNLOAD_RSP;
            case FILE_CHUNK_DOWN_MSG:
                return FILE_CHUNK_DOWN_RSP;
            default:
                LOG_WARN << "Unknown request type for response mapping: " << request_type;
                return static_cast<MsgType>(0);
        }
    }
    
    static string get_message_type_name(MsgType msg_type) {
        switch (msg_type) {
            case FILE_UPLOAD_MSG: return "FILE_UPLOAD_MSG";
            case FILE_UPLOAD_RSP: return "FILE_UPLOAD_RSP";
            case FILE_CHUNK_MSG: return "FILE_CHUNK_MSG";
            case FILE_CHUNK_RSP: return "FILE_CHUNK_RSP";
            case FILE_DOWNLOAD_MSG: return "FILE_DOWNLOAD_MSG";
            case FILE_DOWNLOAD_RSP: return "FILE_DOWNLOAD_RSP";
            case FILE_CHUNK_DOWN_MSG: return "FILE_CHUNK_DOWN_MSG";
            case FILE_CHUNK_DOWN_RSP: return "FILE_CHUNK_DOWN_RSP";
            default: return "UNKNOWN_MSG_TYPE";
        }
    }
};

/**
 * 全局消息处理辅助函数
 */
namespace ProtobufMessageUtils {
    
    // 打印消息信息（用于调试）
    void log_message_info(const MessageWrapper& wrapper, const string& direction = "RECV") {
        LOG_DEBUG << "[" << direction << "] MessageType: " 
                  << MessageTypeMapper::get_message_type_name(wrapper.msgid())
                  << ", DataSize: " << wrapper.data().size() << " bytes";
    }
    
    // 创建通用错误响应
    MessageWrapper create_error_wrapper(MsgType request_type, int32_t error_code, const string& error_msg) {
        MsgType response_type = MessageTypeMapper::get_response_type(request_type);
        
        switch (response_type) {
            case FILE_UPLOAD_RSP: {
                auto response = ProtobufMessageHelper::create_error_response<FileUploadResponse>(
                    response_type, error_code, error_msg);
                return ProtobufMessageHelper::create_wrapper(response_type, response);
            }
            case FILE_CHUNK_RSP: {
                FileChunkResponse response;
                response.set_msgid(response_type);
                response.set_errno(error_code);
                response.set_errmsg(error_msg);
                return ProtobufMessageHelper::create_wrapper(response_type, response);
            }
            case FILE_DOWNLOAD_RSP: {
                auto response = ProtobufMessageHelper::create_error_response<FileDownloadResponse>(
                    response_type, error_code, error_msg);
                return ProtobufMessageHelper::create_wrapper(response_type, response);
            }
            case FILE_CHUNK_DOWN_RSP: {
                FileChunkDownloadResponse response;
                response.set_msgid(response_type);
                response.set_errno(error_code);
                response.set_errmsg(error_msg);
                return ProtobufMessageHelper::create_wrapper(response_type, response);
            }
            default: {
                // 创建空的MessageWrapper作为fallback
                MessageWrapper wrapper;
                wrapper.set_msgid(static_cast<MsgType>(0));
                return wrapper;
            }
        }
    }
    
    // 检查消息完整性
    bool check_message_integrity(const string& binary_data) {
        MessageWrapper wrapper;
        if (!ProtobufMessageHelper::parse_message_wrapper(binary_data, wrapper)) {
            LOG_WARN << "Failed to parse MessageWrapper from binary data";
            return false;
        }
        
        if (!ProtobufMessageHelper::validate_message(wrapper)) {
            LOG_WARN << "MessageWrapper validation failed";
            return false;
        }
        
        return true;
    }
}
