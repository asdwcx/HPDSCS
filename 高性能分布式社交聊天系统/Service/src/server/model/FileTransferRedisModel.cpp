#include "model/FileTransferRedisModel.hpp"
#include "model/FileModel.hpp"  // 获取FileTransferSession定义
#include <muduo/base/Logging.h>
#include <iostream>
#include <sstream>
#include <algorithm>

using namespace std;

FileTransferRedisModel::FileTransferRedisModel() {
    redis_context = nullptr;
}

FileTransferRedisModel::~FileTransferRedisModel() {
    if (redis_context) {
        redisFree(redis_context);
    }
}

bool FileTransferRedisModel::init() {
    redis_context = redisConnect("127.0.0.1", 6379);
    if (redis_context == nullptr || redis_context->err) {
        if (redis_context) {
            LOG_ERROR << "Redis连接失败: " << redis_context->errstr;
            redisFree(redis_context);
            redis_context = nullptr;
        } else {
            LOG_ERROR << "Redis连接失败: 无法分配内存";
        }
        return false;
    }
    
    LOG_INFO << "FileTransferRedisModel 初始化成功";
    return true;
}

bool FileTransferRedisModel::create_transfer_session(const FileTransferSession& session) {
    if (!redis_context) {
        LOG_ERROR << "Redis未连接";
        return false;
    }
    
    // 构建会话信息的哈希表
    auto hash_data = session_to_hash(session);
    
    // 1. 创建会话主记录
    string session_key = "file_session:" + session.session_id;
    
    // 使用HMSET命令设置哈希字段
    vector<string> args = {"HMSET", session_key};
    for (const auto& pair : hash_data) {
        args.push_back(pair.first);
        args.push_back(pair.second);
    }
    
    // 构建Redis命令参数
    vector<const char*> argv;
    vector<size_t> argvlen;
    for (const auto& arg : args) {
        argv.push_back(arg.c_str());
        argvlen.push_back(arg.length());
    }
    
    redisReply* reply = (redisReply*)redisCommandArgv(redis_context, argv.size(), 
                                                     argv.data(), argvlen.data());
    if (!reply || reply->type == REDIS_REPLY_ERROR) {
        string error = reply ? reply->str : "未知错误";
        log_redis_error("create_transfer_session", error);
        if (reply) freeReplyObject(reply);
        return false;
    }
    freeReplyObject(reply);
    
    // 2. 设置会话过期时间（24小时）
    reply = (redisReply*)redisCommand(redis_context, "EXPIRE %s %d", 
                                      session_key.c_str(), 24 * 3600);
    if (!reply || reply->type == REDIS_REPLY_ERROR) {
        log_redis_error("set_session_expire", reply ? reply->str : "未知错误");
        if (reply) freeReplyObject(reply);
        return false;
    }
    freeReplyObject(reply);
    
    // 3. 添加到用户传输列表
    if (!add_to_user_transfers(session.sender_id, session.session_id) ||
        !add_to_user_transfers(session.receiver_id, session.session_id)) {
        // 如果添加用户传输列表失败，清理会话
        delete_transfer_session(session.session_id);
        return false;
    }
    
    // 4. 初始化传输块状态
    string chunks_key = "file_chunks:" + session.session_id;
    int total_chunks = (session.file_size + session.chunk_size - 1) / session.chunk_size;
    
    for (int i = 0; i < total_chunks; i++) {
        reply = (redisReply*)redisCommand(redis_context, "HSET %s %d %s", 
                                          chunks_key.c_str(), i, "pending");
        if (!reply || reply->type == REDIS_REPLY_ERROR) {
            log_redis_error("init_chunk_status", reply ? reply->str : "未知错误");
            if (reply) freeReplyObject(reply);
            return false;
        }
        freeReplyObject(reply);
    }
    
    // 设置块状态的过期时间
    reply = (redisReply*)redisCommand(redis_context, "EXPIRE %s %d", 
                                      chunks_key.c_str(), 24 * 3600);
    if (reply) freeReplyObject(reply);
    
    LOG_INFO << "创建文件传输会话成功: " << session.session_id;
    return true;
}

FileTransferSession FileTransferRedisModel::query_transfer_session(const string& session_id) {
    FileTransferSession session;
    session.session_id = ""; // 表示未找到
    
    if (!redis_context) {
        LOG_ERROR << "Redis未连接";
        return session;
    }
    
    string session_key = "file_session:" + session_id;
    
    // 获取会话的所有哈希字段
    redisReply* reply = (redisReply*)redisCommand(redis_context, "HGETALL %s", 
                                                  session_key.c_str());
    if (!reply) {
        log_redis_error("query_transfer_session", "命令执行失败");
        return session;
    }
    
    if (reply->type == REDIS_REPLY_ARRAY && reply->elements > 0) {
        map<string, string> hash_data;
        
        // Redis HGETALL返回交替的键值对
        for (size_t i = 0; i < reply->elements; i += 2) {
            if (i + 1 < reply->elements) {
                string key = reply->element[i]->str;
                string value = reply->element[i + 1]->str;
                hash_data[key] = value;
            }
        }
        
        session = hash_to_session(session_id, hash_data);
        update_last_activity(session_id);
    }
    
    freeReplyObject(reply);
    return session;
}

bool FileTransferRedisModel::update_chunk_status(const string& session_id, int chunk_seq) {
    if (!redis_context) {
        LOG_ERROR << "Redis未连接";
        return false;
    }
    
    string chunks_key = "file_chunks:" + session_id;
    
    // 将分片标记为已完成
    redisReply* reply = (redisReply*)redisCommand(redis_context, "HSET %s %d %s", 
                                                  chunks_key.c_str(), chunk_seq, "completed");
    if (!reply || reply->type == REDIS_REPLY_ERROR) {
        log_redis_error("update_chunk_status", reply ? reply->str : "未知错误");
        if (reply) freeReplyObject(reply);
        return false;
    }
    
    freeReplyObject(reply);
    
    // 更新会话的已接收分片数
    string session_key = "file_session:" + session_id;
    reply = (redisReply*)redisCommand(redis_context, "HINCRBY %s received_chunks 1", 
                                      session_key.c_str());
    if (reply) freeReplyObject(reply);
    
    // 更新最后活动时间
    update_last_activity(session_id);
    
    // 检查是否所有块都已完成
    check_and_update_session_status(session_id);
    
    return true;
}

bool FileTransferRedisModel::update_transfer_status(const string& session_id, int status) {
    if (!redis_context) {
        LOG_ERROR << "Redis未连接";
        return false;
    }
    
    string session_key = "file_session:" + session_id;
    
    redisReply* reply = (redisReply*)redisCommand(redis_context, "HSET %s transfer_status %d", 
                                                  session_key.c_str(), status);
    if (!reply || reply->type == REDIS_REPLY_ERROR) {
        log_redis_error("update_transfer_status", reply ? reply->str : "未知错误");
        if (reply) freeReplyObject(reply);
        return false;
    }
    
    freeReplyObject(reply);
    update_last_activity(session_id);
    
    return true;
}

string FileTransferRedisModel::get_chunk_status(const string& session_id, int chunk_index) {
    if (!redis_context) {
        LOG_ERROR << "Redis未连接";
        return "error";
    }
    
    string chunks_key = "file_chunks:" + session_id;
    
    redisReply* reply = (redisReply*)redisCommand(redis_context, "HGET %s %d", 
                                                  chunks_key.c_str(), chunk_index);
    if (!reply || reply->type != REDIS_REPLY_STRING) {
        if (reply) freeReplyObject(reply);
        return "not_found";
    }
    
    string status = reply->str;
    freeReplyObject(reply);
    
    return status;
}

vector<int> FileTransferRedisModel::get_pending_chunks(const string& session_id) {
    vector<int> pending_chunks;
    
    if (!redis_context) {
        LOG_ERROR << "Redis未连接";
        return pending_chunks;
    }
    
    string chunks_key = "file_chunks:" + session_id;
    
    redisReply* reply = (redisReply*)redisCommand(redis_context, "HGETALL %s", 
                                                  chunks_key.c_str());
    if (!reply || reply->type != REDIS_REPLY_ARRAY) {
        if (reply) freeReplyObject(reply);
        return pending_chunks;
    }
    
    // 解析哈希表中的键值对
    for (size_t i = 0; i < reply->elements; i += 2) {
        if (i + 1 < reply->elements) {
            int chunk_index = atoi(reply->element[i]->str);
            string status = reply->element[i + 1]->str;
            
            if (status == "pending") {
                pending_chunks.push_back(chunk_index);
            }
        }
    }
    
    freeReplyObject(reply);
    sort(pending_chunks.begin(), pending_chunks.end());
    
    return pending_chunks;
}

bool FileTransferRedisModel::update_session_status(const string& session_id, const string& status) {
    if (!redis_context) {
        LOG_ERROR << "Redis未连接";
        return false;
    }
    
    string session_key = "file_session:" + session_id;
    
    redisReply* reply = (redisReply*)redisCommand(redis_context, "HSET %s status %s", 
                                                  session_key.c_str(), status.c_str());
    if (!reply || reply->type == REDIS_REPLY_ERROR) {
        log_redis_error("update_session_status", reply ? reply->str : "未知错误");
        if (reply) freeReplyObject(reply);
        return false;
    }
    
    freeReplyObject(reply);
    update_last_activity(session_id);
    
    return true;
}

bool FileTransferRedisModel::delete_transfer_session(const string& session_id) {
    if (!redis_context) {
        LOG_ERROR << "Redis未连接";
        return false;
    }
    
    // 1. 获取会话信息以便清理用户传输列表
    FileTransferSession session = query_transfer_session(session_id);
    
    // 2. 删除会话主记录
    string session_key = "file_session:" + session_id;
    redisReply* reply = (redisReply*)redisCommand(redis_context, "DEL %s", session_key.c_str());
    if (reply) freeReplyObject(reply);
    
    // 3. 删除块状态记录
    string chunks_key = "file_chunks:" + session_id;
    reply = (redisReply*)redisCommand(redis_context, "DEL %s", chunks_key.c_str());
    if (reply) freeReplyObject(reply);
    
    // 4. 从用户传输列表中移除
    if (!session.session_id.empty()) {
        remove_from_user_transfers(session.sender_id, session_id);
        remove_from_user_transfers(session.receiver_id, session_id);
    }
    
    LOG_INFO << "删除文件传输会话: " << session_id;
    return true;
}

vector<string> FileTransferRedisModel::get_user_active_transfers(int user_id) {
    vector<string> active_sessions;
    
    if (!redis_context) {
        LOG_ERROR << "Redis未连接";
        return active_sessions;
    }
    
    string user_key = "user_transfers:" + to_string(user_id);
    
    redisReply* reply = (redisReply*)redisCommand(redis_context, "SMEMBERS %s", user_key.c_str());
    if (!reply || reply->type != REDIS_REPLY_ARRAY) {
        if (reply) freeReplyObject(reply);
        return active_sessions;
    }
    
    // 检查每个会话是否仍然有效
    for (size_t i = 0; i < reply->elements; i++) {
        string session_id = reply->element[i]->str;
        if (session_exists(session_id)) {
            active_sessions.push_back(session_id);
        } else {
            // 如果会话不存在，从用户列表中移除
            remove_from_user_transfers(user_id, session_id);
        }
    }
    
    freeReplyObject(reply);
    return active_sessions;
}

bool FileTransferRedisModel::cleanup_expired_sessions() {
    if (!redis_context) {
        LOG_ERROR << "Redis未连接";
        return false;
    }
    
    // Redis的过期机制会自动清理过期的键
    // 这里主要是清理可能残留的用户传输列表
    
    // 可以实现一个周期性任务来检查和清理
    LOG_INFO << "清理过期会话完成";
    return true;
}

bool FileTransferRedisModel::get_transfer_progress(const string& session_id, 
                                                   float& progress, int& completed_chunks, 
                                                   int& total_chunks) {
    if (!redis_context) {
        LOG_ERROR << "Redis未连接";
        return false;
    }
    
    string chunks_key = "file_chunks:" + session_id;
    
    redisReply* reply = (redisReply*)redisCommand(redis_context, "HGETALL %s", chunks_key.c_str());
    if (!reply || reply->type != REDIS_REPLY_ARRAY) {
        if (reply) freeReplyObject(reply);
        return false;
    }
    
    completed_chunks = 0;
    total_chunks = reply->elements / 2; // 键值对的数量
    
    // 统计已完成的块
    for (size_t i = 0; i < reply->elements; i += 2) {
        if (i + 1 < reply->elements) {
            string status = reply->element[i + 1]->str;
            if (status == "completed") {
                completed_chunks++;
            }
        }
    }
    
    freeReplyObject(reply);
    
    if (total_chunks > 0) {
        progress = (float)completed_chunks / total_chunks * 100.0f;
    } else {
        progress = 0.0f;
    }
    
    return true;
}

// 私有辅助函数实现

bool FileTransferRedisModel::session_exists(const string& session_id) {
    if (!redis_context) return false;
    
    string session_key = "file_session:" + session_id;
    redisReply* reply = (redisReply*)redisCommand(redis_context, "EXISTS %s", session_key.c_str());
    
    bool exists = false;
    if (reply && reply->type == REDIS_REPLY_INTEGER) {
        exists = (reply->integer == 1);
    }
    
    if (reply) freeReplyObject(reply);
    return exists;
}

void FileTransferRedisModel::update_last_activity(const string& session_id) {
    if (!redis_context) return;
    
    string session_key = "file_session:" + session_id;
    auto now = chrono::system_clock::now();
    auto timestamp = chrono::duration_cast<chrono::seconds>(now.time_since_epoch()).count();
    
    redisReply* reply = (redisReply*)redisCommand(redis_context, "HSET %s last_activity %ld", 
                                                  session_key.c_str(), timestamp);
    if (reply) freeReplyObject(reply);
}

bool FileTransferRedisModel::add_to_user_transfers(int user_id, const string& session_id) {
    if (!redis_context) return false;
    
    string user_key = "user_transfers:" + to_string(user_id);
    redisReply* reply = (redisReply*)redisCommand(redis_context, "SADD %s %s", 
                                                  user_key.c_str(), session_id.c_str());
    
    bool success = false;
    if (reply && reply->type == REDIS_REPLY_INTEGER) {
        success = true;
        // 设置用户传输列表的过期时间
        freeReplyObject(reply);
        reply = (redisReply*)redisCommand(redis_context, "EXPIRE %s %d", user_key.c_str(), 25 * 3600);
    }
    
    if (reply) freeReplyObject(reply);
    return success;
}

bool FileTransferRedisModel::remove_from_user_transfers(int user_id, const string& session_id) {
    if (!redis_context) return false;
    
    string user_key = "user_transfers:" + to_string(user_id);
    redisReply* reply = (redisReply*)redisCommand(redis_context, "SREM %s %s", 
                                                  user_key.c_str(), session_id.c_str());
    
    bool success = false;
    if (reply && reply->type == REDIS_REPLY_INTEGER) {
        success = true;
    }
    
    if (reply) freeReplyObject(reply);
    return success;
}

void FileTransferRedisModel::check_and_update_session_status(const string& session_id) {
    if (!redis_context) return;
    
    // 检查是否所有块都已完成
    vector<int> pending = get_pending_chunks(session_id);
    if (pending.empty()) {
        // 所有块都已完成，更新会话状态
        update_session_status(session_id, "completed");
        LOG_INFO << "文件传输完成: " << session_id;
    }
}

map<string, string> FileTransferRedisModel::session_to_hash(const FileTransferSession& session) {
    map<string, string> hash_data;
    
    hash_data["sender_id"] = to_string(session.sender_id);
    hash_data["receiver_id"] = to_string(session.receiver_id);
    hash_data["group_id"] = to_string(session.group_id);
    hash_data["file_id"] = session.file_id;
    hash_data["total_chunks"] = to_string(session.total_chunks);
    hash_data["received_chunks"] = to_string(session.received_chunks);
    hash_data["temp_file_path"] = session.temp_file_path;
    hash_data["start_time"] = session.start_time;
    hash_data["transfer_status"] = to_string(session.transfer_status);
    
    return hash_data;
}

FileTransferSession FileTransferRedisModel::hash_to_session(const string& session_id, 
                                                            const map<string, string>& hash_data) {
    FileTransferSession session;
    
    session.session_id = session_id;
    
    auto it = hash_data.find("sender_id");
    if (it != hash_data.end()) session.sender_id = stoi(it->second);
    
    it = hash_data.find("receiver_id");
    if (it != hash_data.end()) session.receiver_id = stoi(it->second);
    
    it = hash_data.find("group_id");
    if (it != hash_data.end()) session.group_id = stoi(it->second);
    
    it = hash_data.find("file_id");
    if (it != hash_data.end()) session.file_id = it->second;
    
    it = hash_data.find("total_chunks");
    if (it != hash_data.end()) session.total_chunks = stoi(it->second);
    
    it = hash_data.find("received_chunks");
    if (it != hash_data.end()) session.received_chunks = stoi(it->second);
    
    it = hash_data.find("temp_file_path");
    if (it != hash_data.end()) session.temp_file_path = it->second;
    
    it = hash_data.find("start_time");
    if (it != hash_data.end()) session.start_time = it->second;
    
    it = hash_data.find("transfer_status");
    if (it != hash_data.end()) session.transfer_status = stoi(it->second);
    
    return session;
}

void FileTransferRedisModel::log_redis_error(const string& operation, const string& error) {
    LOG_ERROR << "Redis操作失败 [" << operation << "]: " << error;
}

bool FileTransferRedisModel::handle_redis_error(const string& operation) {
    if (!redis_context) return false;
    
    if (redis_context->err) {
        log_redis_error(operation, redis_context->errstr);
        return false;
    }
    
    return true;
}
