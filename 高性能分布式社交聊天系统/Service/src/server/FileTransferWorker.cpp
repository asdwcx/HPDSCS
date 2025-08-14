#include "FileTransferWorker.hpp"
#include "Base64Util.hpp"
#include <iostream>
#include <fstream>
#include <filesystem>
#include <thread>

using namespace std;
using namespace std::filesystem;

FileTransferWorker::FileTransferWorker() : _stop_flag(false) {
    _worker_thread = thread(&FileTransferWorker::worker_thread, this);
}

FileTransferWorker::~FileTransferWorker() {
    stop();
}

void FileTransferWorker::post_task(shared_ptr<FileTask> task) {
    if (_stop_flag) {
        return;
    }
    
    {
        lock_guard<mutex> lock(_queue_mutex);
        _task_queue.push(task);
    }
    _condition.notify_one();
}

void FileTransferWorker::stop() {
    _stop_flag = true;
    _condition.notify_all();
    
    if (_worker_thread.joinable()) {
        _worker_thread.join();
    }
}

size_t FileTransferWorker::get_queue_size() {
    lock_guard<mutex> lock(_queue_mutex);
    return _task_queue.size();
}

void FileTransferWorker::worker_thread() {
    while (!_stop_flag) {
        unique_lock<mutex> lock(_queue_mutex);
        _condition.wait(lock, [this] {
            return _stop_flag || !_task_queue.empty();
        });
        
        if (_stop_flag) {
            break;
        }
        
        shared_ptr<FileTask> task = _task_queue.front();
        _task_queue.pop();
        lock.unlock();
        
        // 处理任务
        switch (task->task_type) {
            case TASK_UPLOAD_CHUNK:
                process_upload_chunk(task);
                break;
            case TASK_DOWNLOAD_FILE:
                process_download_file(task);
                break;
            case TASK_MERGE_CHUNKS:
                process_merge_chunks(task);
                break;
            case TASK_DELETE_TEMP:
                process_delete_temp(task);
                break;
            default:
                cout << "Unknown file task type: " << task->task_type << endl;
                break;
        }
    }
}

void FileTransferWorker::process_upload_chunk(shared_ptr<FileTask> task) {
    try {
        bool success = write_chunk_to_temp_file(task->session_id, task->chunk_seq, task->chunk_data);
        
        if (task->callback) {
            string error_msg = success ? "" : "Failed to write chunk to temp file";
            task->callback(success, error_msg);
        }
    } catch (const exception& e) {
        cout << "Error processing upload chunk: " << e.what() << endl;
        if (task->callback) {
            task->callback(false, e.what());
        }
    }
}

void FileTransferWorker::process_download_file(shared_ptr<FileTask> task) {
    try {
        ifstream file(task->file_path, ios::binary);
        if (!file.is_open()) {
            if (task->callback) {
                task->callback(false, "Cannot open source file");
            }
            return;
        }
        
        // 读取文件内容
        file.seekg(0, ios::end);
        size_t file_size = file.tellg();
        file.seekg(0, ios::beg);
        
        vector<char> buffer(file_size);
        file.read(buffer.data(), file_size);
        file.close();
        
        // Base64编码
        string encoded_data = Base64Util::encode(buffer);
        
        // 创建目标目录
        path target_path(task->target_path);
        if (target_path.has_parent_path()) {
            create_directories(target_path.parent_path());
        }
        
        // 写入目标文件
        ofstream outfile(task->target_path, ios::binary);
        if (!outfile.is_open()) {
            if (task->callback) {
                task->callback(false, "Cannot create target file");
            }
            return;
        }
        
        outfile.write(buffer.data(), buffer.size());
        outfile.close();
        
        if (task->callback) {
            task->callback(true, "");
        }
        
    } catch (const exception& e) {
        cout << "Error processing download file: " << e.what() << endl;
        if (task->callback) {
            task->callback(false, e.what());
        }
    }
}

void FileTransferWorker::process_merge_chunks(shared_ptr<FileTask> task) {
    try {
        bool success = merge_temp_chunks(task->session_id, task->target_path);
        
        if (task->callback) {
            string error_msg = success ? "" : "Failed to merge chunks";
            task->callback(success, error_msg);
        }
    } catch (const exception& e) {
        cout << "Error processing merge chunks: " << e.what() << endl;
        if (task->callback) {
            task->callback(false, e.what());
        }
    }
}

void FileTransferWorker::process_delete_temp(shared_ptr<FileTask> task) {
    try {
        // 删除临时文件和目录
        path temp_dir("./temp/" + task->session_id);
        if (exists(temp_dir)) {
            remove_all(temp_dir);
        }
        
        if (task->callback) {
            task->callback(true, "");
        }
    } catch (const exception& e) {
        cout << "Error deleting temp files: " << e.what() << endl;
        if (task->callback) {
            task->callback(false, e.what());
        }
    }
}

bool FileTransferWorker::write_chunk_to_temp_file(const string& session_id, int chunk_seq, const string& data) {
    lock_guard<mutex> lock(_file_mutex);
    
    try {
        // 创建临时目录
        string temp_dir = "./temp/" + session_id;
        create_directories(temp_dir);
        
        // 解码Base64数据
        string decoded_data = Base64Util::decode(data);
        
        // 写入分片文件
        string chunk_file = temp_dir + "/chunk_" + to_string(chunk_seq);
        ofstream file(chunk_file, ios::binary);
        if (!file.is_open()) {
            cout << "Cannot create chunk file: " << chunk_file << endl;
            return false;
        }
        
        file.write(decoded_data.data(), decoded_data.size());
        file.close();
        
        return true;
    } catch (const exception& e) {
        cout << "Error writing chunk to temp file: " << e.what() << endl;
        return false;
    }
}

bool FileTransferWorker::merge_temp_chunks(const string& session_id, const string& target_path) {
    lock_guard<mutex> lock(_file_mutex);
    
    try {
        string temp_dir = "./temp/" + session_id;
        path temp_path(temp_dir);
        
        if (!exists(temp_path)) {
            cout << "Temp directory not found: " << temp_dir << endl;
            return false;
        }
        
        // 创建目标文件目录
        path target_file_path(target_path);
        if (target_file_path.has_parent_path()) {
            create_directories(target_file_path.parent_path());
        }
        
        // 打开目标文件
        ofstream target_file(target_path, ios::binary);
        if (!target_file.is_open()) {
            cout << "Cannot create target file: " << target_path << endl;
            return false;
        }
        
        // 收集所有分片文件并排序
        vector<string> chunk_files;
        for (const auto& entry : directory_iterator(temp_path)) {
            if (entry.is_regular_file()) {
                string filename = entry.path().filename().string();
                if (filename.find("chunk_") == 0) {
                    chunk_files.push_back(entry.path().string());
                }
            }
        }
        
        // 按分片序号排序
        sort(chunk_files.begin(), chunk_files.end(), [](const string& a, const string& b) {
            size_t pos_a = a.find_last_of('_') + 1;
            size_t pos_b = b.find_last_of('_') + 1;
            int seq_a = stoi(a.substr(pos_a));
            int seq_b = stoi(b.substr(pos_b));
            return seq_a < seq_b;
        });
        
        // 合并分片
        for (const string& chunk_file : chunk_files) {
            ifstream chunk(chunk_file, ios::binary);
            if (!chunk.is_open()) {
                cout << "Cannot open chunk file: " << chunk_file << endl;
                target_file.close();
                remove(target_path);
                return false;
            }
            
            // 复制数据
            target_file << chunk.rdbuf();
            chunk.close();
        }
        
        target_file.close();
        
        // 清理临时文件
        remove_all(temp_path);
        
        return true;
    } catch (const exception& e) {
        cout << "Error merging chunks: " << e.what() << endl;
        return false;
    }
}

string FileTransferWorker::decode_base64(const string& encoded_data) {
    return Base64Util::decode(encoded_data);
}

string FileTransferWorker::encode_base64(const string& data) {
    return Base64Util::encode(data);
}

// FileTransferWorkerPool 实现
FileTransferWorkerPool& FileTransferWorkerPool::instance() {
    static FileTransferWorkerPool instance;
    return instance;
}

FileTransferWorkerPool::FileTransferWorkerPool() : _next_worker(0) {
    for (size_t i = 0; i < WORKER_COUNT; ++i) {
        _workers.push_back(make_unique<FileTransferWorker>());
    }
}

FileTransferWorkerPool::~FileTransferWorkerPool() {
    stop_all();
}

void FileTransferWorkerPool::post_task(shared_ptr<FileTask> task) {
    size_t worker_index = _next_worker.fetch_add(1) % WORKER_COUNT;
    _workers[worker_index]->post_task(task);
}

void FileTransferWorkerPool::stop_all() {
    for (auto& worker : _workers) {
        worker->stop();
    }
}

size_t FileTransferWorkerPool::get_total_queue_size() {
    size_t total_size = 0;
    for (const auto& worker : _workers) {
        total_size += worker->get_queue_size();
    }
    return total_size;
}
