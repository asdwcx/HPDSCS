#include "ChatService.hpp"
#include "public.hpp"
#include "FileTransferWorker.hpp"
#include "Base64Util.hpp"
#include <iostream>
#include <vector>
#include <map>
#include <string>
#include <iomanip>
#include <sstream>
#include <fstream>
#include <filesystem>
#include "json.hpp"

using json = nlohmann::json;
using namespace std;
using namespace muduo;
using namespace muduo::net;
using namespace std::filesystem;

// 文件上传请求处理
void ChatService::file_upload_request(const TcpConnectionPtr &conn, const chat::MessageWrapper &wrapper, Timestamp time)
{
    try {
        json js = json::parse(wrapper.data());
        int userid = js["id"].get<int>();
        string file_name = js["file_name"].get<string>();
        int file_size = js["file_size"].get<int>();
        string file_type = js.value("file_type", "");
        int receiver_id = js.value("receiver_id", -1);
        int group_id = js.value("group_id", -1);
        int total_chunks = js["total_chunks"].get<int>();
        
        json response;
        response["msgid"] = FILE_UPLOAD_RSP;
        
        // 验证文件信息
        if (!file_model_.is_valid_file_type(file_name)) {
            response["errno"] = 1;
            response["errmsg"] = "不支持的文件类型";
        }
        else if (!file_model_.is_valid_file_size(file_size)) {
            response["errno"] = 2;
            response["errmsg"] = "文件大小超出限制（最大100MB）";
        }
        else if (receiver_id == -1 && group_id == -1) {
            response["errno"] = 3;
            response["errmsg"] = "必须指定接收者或群组";
        }
        else {
            // 生成文件ID和会话ID
            string file_id = file_model_.generate_file_id();
            string session_id = file_model_.generate_session_id();
            
            // 创建文件信息记录
            FileInfo file_info;
            file_info.file_id = file_id;
            file_info.file_name = file_name;
            file_info.file_size = file_size;
            file_info.file_type = file_type;
            file_info.sender_id = userid;
            file_info.receiver_id = receiver_id;
            file_info.group_id = group_id;
            file_info.status = 0; // 上传中
            file_info.file_path = file_model_.get_file_storage_path(file_id, file_name);
            
            // 创建传输会话
            FileTransferSession session;
            session.session_id = session_id;
            session.file_id = file_id;
            session.sender_id = userid;
            session.receiver_id = receiver_id;
            session.group_id = group_id;
            session.total_chunks = total_chunks;
            session.temp_file_path = file_model_.get_temp_file_path(session_id);
            session.transfer_status = 0; // 进行中
            
            bool success = file_model_.insert_file_info(file_info) && 
                          file_model_.create_transfer_session(session);
            
            if (success) {
                response["errno"] = 0;
                response["errmsg"] = "文件上传会话创建成功";
                response["file_id"] = file_id;
                response["session_id"] = session_id;
                response["chunk_size"] = 64 * 1024; // 64KB per chunk
            }
            else {
                response["errno"] = 4;
                response["errmsg"] = "创建上传会话失败";
            }
        }
        
        string response_str = response.dump();
        conn->send(response_str);
        
    } catch (const exception& e) {
        LOG_ERROR << "file upload request parse error: " << e.what();
        json error_response;
        error_response["msgid"] = FILE_UPLOAD_RSP;
        error_response["errno"] = 5;
        error_response["errmsg"] = "请求格式错误";
        string error_str = error_response.dump();
        conn->send(error_str);
    }
}

// 文件分片传输处理
void ChatService::file_chunk_transfer(const TcpConnectionPtr &conn, const chat::MessageWrapper &wrapper, Timestamp time)
{
    try {
        json js = json::parse(wrapper.data());
        string session_id = js["session_id"].get<string>();
        int chunk_seq = js["chunk_seq"].get<int>();
        string chunk_data = js["chunk_data"].get<string>();
        bool is_last = js.value("is_last", false);
        
        json response;
        response["msgid"] = FILE_CHUNK_MSG;
        response["session_id"] = session_id;
        response["chunk_seq"] = chunk_seq;
        
        // 查询传输会话
        FileTransferSession session = file_model_.query_transfer_session(session_id);
        if (session.session_id.empty()) {
            response["errno"] = 1;
            response["errmsg"] = "传输会话不存在";
        }
        else if (session.transfer_status != 0) {
            response["errno"] = 2;
            response["errmsg"] = "传输会话已结束";
        }
        else {
            // 创建文件任务并提交到工作器
            auto task = make_shared<FileTask>(TASK_UPLOAD_CHUNK, session.file_id);
            task->session_id = session_id;
            task->chunk_seq = chunk_seq;
            task->chunk_data = chunk_data;
            task->callback = [conn, response, session_id, chunk_seq, is_last, this](bool success, const string& error_msg) mutable {
                if (success) {
                    // 更新分片状态
                    file_model_.update_chunk_status(session_id, chunk_seq);
                    
                    response["errno"] = 0;
                    response["errmsg"] = "分片上传成功";
                    
                    if (is_last) {
                        // 如果是最后一个分片，启动合并任务
                        FileTransferSession updated_session = file_model_.query_transfer_session(session_id);
                        if (updated_session.received_chunks >= updated_session.total_chunks) {
                            FileInfo file_info = file_model_.query_file_info(updated_session.file_id);
                            
                            auto merge_task = make_shared<FileTask>(TASK_MERGE_CHUNKS, updated_session.file_id);
                            merge_task->session_id = session_id;
                            merge_task->target_path = file_info.file_path;
                            merge_task->callback = [session_id, file_info, this](bool merge_success, const string& merge_error) {
                                if (merge_success) {
                                    // 计算文件哈希
                                    string file_hash = file_model_.calculate_file_hash(file_info.file_path);
                                    
                                    // 更新文件状态为完成
                                    file_model_.update_file_status(file_info.file_id, 1);
                                    file_model_.update_transfer_status(session_id, 1);
                                    
                                    // 发送文件通知给接收者
                                    send_file_notification(file_info);
                                    
                                    // 清理传输会话
                                    auto cleanup_task = make_shared<FileTask>(TASK_DELETE_TEMP, file_info.file_id);
                                    cleanup_task->session_id = session_id;
                                    FileTransferWorkerPool::instance().post_task(cleanup_task);
                                }
                                else {
                                    file_model_.update_transfer_status(session_id, 2); // 失败
                                }
                            };
                            
                            FileTransferWorkerPool::instance().post_task(merge_task);
                        }
                    }
                }
                else {
                    response["errno"] = 3;
                    response["errmsg"] = "分片上传失败: " + error_msg;
                    file_model_.update_transfer_status(session_id, 2); // 失败
                }
                
                string response_str = response.dump();
                conn->send(response_str);
            };
            
            FileTransferWorkerPool::instance().post_task(task);
            return; // 异步处理，不在这里发送响应
        }
        
        string response_str = response.dump();
        conn->send(response_str);
        
    } catch (const exception& e) {
        LOG_ERROR << "file chunk transfer parse error: " << e.what();
        json error_response;
        error_response["msgid"] = FILE_CHUNK_MSG;
        error_response["errno"] = 4;
        error_response["errmsg"] = "请求格式错误";
        string error_str = error_response.dump();
        conn->send(error_str);
    }
}

// 文件下载请求处理
void ChatService::file_download_request(const TcpConnectionPtr &conn, const chat::MessageWrapper &wrapper, Timestamp time)
{
    try {
        json js = json::parse(wrapper.data());
        int userid = js["id"].get<int>();
        string file_id = js["file_id"].get<string>();
        
        json response;
        response["msgid"] = FILE_DOWNLOAD_RSP;
        response["file_id"] = file_id;
        
        // 查询文件信息
        FileInfo file_info = file_model_.query_file_info(file_id);
        if (file_info.file_id.empty()) {
            response["errno"] = 1;
            response["errmsg"] = "文件不存在";
        }
        else if (file_info.status != 1) {
            response["errno"] = 2;
            response["errmsg"] = "文件未上传完成";
        }
        else {
            // 检查权限：发送者、接收者或群成员
            bool has_permission = false;
            if (file_info.sender_id == userid || file_info.receiver_id == userid) {
                has_permission = true;
            }
            else if (file_info.group_id != -1) {
                // 检查是否为群成员
                vector<GroupUser> group_users = group_model_.query_group_users(file_info.group_id);
                for (const auto& group_user : group_users) {
                    if (group_user.get_id() == userid) {
                        has_permission = true;
                        break;
                    }
                }
            }
            
            if (!has_permission) {
                response["errno"] = 3;
                response["errmsg"] = "没有下载权限";
            }
            else {
                // 检查文件是否存在
                if (!exists(file_info.file_path)) {
                    response["errno"] = 4;
                    response["errmsg"] = "文件已被删除";
                }
                else {
                    // 读取文件并编码
                    ifstream file(file_info.file_path, ios::binary);
                    if (!file.is_open()) {
                        response["errno"] = 5;
                        response["errmsg"] = "文件读取失败";
                    }
                    else {
                        // 获取文件大小
                        file.seekg(0, ios::end);
                        size_t file_size = file.tellg();
                        file.seekg(0, ios::beg);
                        
                        // 读取文件数据
                        vector<char> buffer(file_size);
                        file.read(buffer.data(), file_size);
                        file.close();
                        
                        // Base64编码
                        string encoded_data = Base64Util::encode(buffer);
                        
                        response["errno"] = 0;
                        response["errmsg"] = "文件下载成功";
                        response["file_name"] = file_info.file_name;
                        response["file_size"] = file_info.file_size;
                        response["file_type"] = file_info.file_type;
                        response["file_data"] = encoded_data;
                        response["file_hash"] = file_info.file_hash;
                    }
                }
            }
        }
        
        string response_str = response.dump();
        conn->send(response_str);
        
    } catch (const exception& e) {
        LOG_ERROR << "file download request parse error: " << e.what();
        json error_response;
        error_response["msgid"] = FILE_DOWNLOAD_RSP;
        error_response["errno"] = 6;
        error_response["errmsg"] = "请求格式错误";
        string error_str = error_response.dump();
        conn->send(error_str);
    }
}

// 文件发送通知处理
void ChatService::file_send_notify(const TcpConnectionPtr &conn, const chat::MessageWrapper &wrapper, Timestamp time)
{
    try {
        json js = json::parse(wrapper.data());
        string file_id = js["file_id"].get<string>();
        
        FileInfo file_info = file_model_.query_file_info(file_id);
        if (!file_info.file_id.empty()) {
            send_file_notification(file_info);
        }
        
    } catch (const exception& e) {
        LOG_ERROR << "file send notify parse error: " << e.what();
    }
}

// 文件接收确认处理
void ChatService::file_receive_confirm(const TcpConnectionPtr &conn, const chat::MessageWrapper &wrapper, Timestamp time)
{
    try {
        json js = json::parse(wrapper.data());
        int userid = js["id"].get<int>();
        string file_id = js["file_id"].get<string>();
        bool confirmed = js["confirmed"].get<bool>();
        
        if (confirmed) {
            // 更新文件状态为已下载
            file_model_.update_file_status(file_id, 3);
        }
        
        // 可以在这里添加统计或通知发送者的逻辑
        
    } catch (const exception& e) {
        LOG_ERROR << "file receive confirm parse error: " << e.what();
    }
}

// 发送文件通知的辅助函数
void ChatService::send_file_notification(const FileInfo& file_info)
{
    json notify_msg;
    notify_msg["msgid"] = FILE_SEND_NOTIFY;
    notify_msg["file_id"] = file_info.file_id;
    notify_msg["file_name"] = file_info.file_name;
    notify_msg["file_size"] = file_info.file_size;
    notify_msg["file_type"] = file_info.file_type;
    notify_msg["sender_id"] = file_info.sender_id;
    notify_msg["upload_time"] = file_info.upload_time;
    
    User sender = user_model_.query(file_info.sender_id);
    notify_msg["sender_name"] = sender.get_name();
    
    string notify_str = notify_msg.dump();
    
    if (file_info.group_id != -1) {
        // 群文件通知
        vector<GroupUser> group_users = group_model_.query_group_users(file_info.group_id);
        for (const auto& group_user : group_users) {
            int user_id = group_user.get_id();
            if (user_id != file_info.sender_id) { // 不通知发送者自己
                // 查看用户是否在线
                {
                    lock_guard<mutex> lock(conn_mutex_);
                    auto it = user_connection_map_.find(user_id);
                    if (it != user_connection_map_.end()) {
                        // 在线，直接发送
                        it->second->send(notify_str);
                    }
                    else {
                        // 离线，存储离线消息
                        offline_message_model_.insert(user_id, notify_str);
                    }
                }
            }
        }
    }
    else if (file_info.receiver_id != -1) {
        // 一对一文件通知
        {
            lock_guard<mutex> lock(conn_mutex_);
            auto it = user_connection_map_.find(file_info.receiver_id);
            if (it != user_connection_map_.end()) {
                // 在线，直接发送
                it->second->send(notify_str);
            }
            else {
                // 离线，存储离线消息
                offline_message_model_.insert(file_info.receiver_id, notify_str);
            }
        }
    }
}
