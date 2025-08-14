#ifndef FILE_TRANSFER_WORKER_HPP
#define FILE_TRANSFER_WORKER_HPP

#include <thread>
#include <mutex>
#include <queue>
#include <condition_variable>
#include <atomic>
#include <memory>
#include <string>
#include <functional>

using namespace std;

// 文件任务类型
enum FileTaskType {
    TASK_UPLOAD_CHUNK = 1,      // 上传文件分片
    TASK_DOWNLOAD_FILE = 2,     // 下载文件
    TASK_MERGE_CHUNKS = 3,      // 合并文件分片
    TASK_DELETE_TEMP = 4        // 删除临时文件
};

// 文件传输任务
struct FileTask {
    FileTaskType task_type;
    string file_id;
    string session_id;
    int chunk_seq;
    string chunk_data;
    string file_path;
    string target_path;
    function<void(bool, const string&)> callback; // 回调函数：成功标志，错误信息
    
    FileTask(FileTaskType type, const string& id) 
        : task_type(type), file_id(id), chunk_seq(0) {}
};

class FileTransferWorker {
public:
    FileTransferWorker();
    ~FileTransferWorker();
    
    // 提交任务到工作队列
    void post_task(shared_ptr<FileTask> task);
    
    // 停止工作器
    void stop();
    
    // 获取队列大小
    size_t get_queue_size();

private:
    // 工作线程函数
    void worker_thread();
    
    // 任务处理函数
    void process_upload_chunk(shared_ptr<FileTask> task);
    void process_download_file(shared_ptr<FileTask> task);
    void process_merge_chunks(shared_ptr<FileTask> task);
    void process_delete_temp(shared_ptr<FileTask> task);
    
    // 工具函数
    bool write_chunk_to_temp_file(const string& session_id, int chunk_seq, const string& data);
    bool merge_temp_chunks(const string& session_id, const string& target_path);
    string decode_base64(const string& encoded_data);
    string encode_base64(const string& data);
    
private:
    thread _worker_thread;
    queue<shared_ptr<FileTask>> _task_queue;
    mutex _queue_mutex;
    condition_variable _condition;
    atomic<bool> _stop_flag;
    
    // 文件操作互斥锁
    mutex _file_mutex;
};

// 文件传输工作器池
class FileTransferWorkerPool {
public:
    static FileTransferWorkerPool& instance();
    
    void post_task(shared_ptr<FileTask> task);
    void stop_all();
    size_t get_total_queue_size();

private:
    FileTransferWorkerPool();
    ~FileTransferWorkerPool();
    
    vector<unique_ptr<FileTransferWorker>> _workers;
    atomic<size_t> _next_worker;
    static const size_t WORKER_COUNT = 4; // 4个工作线程
    
    // 并发控制和统计
    mutex _pool_mutex;
    unordered_map<string, atomic<int>> _file_task_counters;  // 每个文件的并发任务计数
    unordered_map<int, atomic<int>> _user_task_counters;     // 每个用户的并发任务计数
    atomic<size_t> _total_active_tasks;                      // 全局活跃任务数
    
    // 并发限制配置
    static const size_t MAX_CONCURRENT_FILES_PER_USER = 5;   // 每用户最大并发文件数
    static const size_t MAX_CONCURRENT_CHUNKS_PER_FILE = 8;  // 每文件最大并发分片数
    static const size_t MAX_TOTAL_CONCURRENT_TASKS = 100;    // 全局最大并发任务数
    
    // 负载均衡和任务分发
    size_t select_best_worker();
    bool can_accept_new_task(int user_id, const string& file_id);
    void increment_task_counter(int user_id, const string& file_id);
    void decrement_task_counter(int user_id, const string& file_id);
};

#endif // FILE_TRANSFER_WORKER_HPP
