// npu.h 提供 NPU 裝置的使用界面
// 每個 NPU 需註冊進 NPUHandle 單例做統一管理


#pragma once
#include <vector>
#include <mutex>
#include <atomic>
#include <climits> // for INT_MAX
#include <unistd.h>

// NPU 資源，會被多執行緒訪問
// 此類型指負責管理資源工作量與原子操作鎖
// 此類型請勿在 NPUHandler::add_device 實例化
class NPUDevice {
private: 
    int id; // NPU 代號
    int sock_fd; // 存取 NPU 所在的 SOCKET

    const size_t MAX_QUEUE_LEN = 2; // 佇列最大長度

    std::atomic_bool in_use =false; // 本類型操作是否使用中
    std::atomic<int> queue_size{0}; // NPU 內的 receive queue

public:

    NPUDevice(int _id, int _sock): id(_id), sock_fd(_sock) {}
    NPUDevice(): id(-1), sock_fd(-1) {}

    bool try_lock() { 
        bool expect = false;
        return in_use.compare_exchange_strong(expect, true);
    }
    void unlock()   { in_use.store(false); }

    void enqueue() { queue_size.fetch_add(1); }
    void dequeue() { queue_size.fetch_sub(1); }

    bool is_available() const { return !in_use.load() && queue_size < MAX_QUEUE_LEN; }
    int get_size() const { return queue_size.load(); }
    int get_socket() const { return sock_fd; }


};


// 單例類型，負責管理所有 NPUDevice
class NPUHandle {
// 成員
private:
    std::vector<NPUDevice*> resources;
    std::mutex mtx;

// 單例設計
private:
    NPUHandle() {}

public:
    NPUHandle(const NPUHandle&) = delete;
    
    NPUHandle& operator=(const NPUDevice) = delete;
    
    static NPUHandle& get_instance() {
        static NPUHandle instance;
        return instance;
    }

// 功能
public:
    void add_device(int socket) {
        std::lock_guard<std::mutex> lock(mtx);
        NPUDevice *new_device = new NPUDevice(resources.size(), socket);
        resources.push_back(std::move(new_device));
    }

    void clear(void (*ending)(NPUDevice*)) {
        std::lock_guard<std::mutex> lock(mtx);
        for (auto res : resources) {
            ending(res);
            close(res->get_socket());
            delete res;
        }
        resources.clear();
    }

    NPUDevice* get_best_and_lock() {
        std::lock_guard<std::mutex> lock(mtx);

        NPUDevice* best = nullptr;
        int min_workload = INT_MAX;

        // 尋找合適的 npu
        for (auto res : resources) {
            if (res->is_available()) {
                int load = res->get_size();
                if (load < min_workload) {
                    best = res;
                    min_workload = load;
                }
            }
        }

        // 找到並成功鎖住就返回
        if (best && best->try_lock()) {
            return best;
        }

        return nullptr;
    }

    NPUDevice* polling_and_lock() {
        std::lock_guard<std::mutex> lock(mtx);

        fd_set readfds;
        int maxfd = 0;

        for (auto res : resources) {
            int socket = res->get_socket();
            FD_SET(socket, &readfds);
            if (socket > maxfd) maxfd = socket;
        }

        timeval timeout = {0, 0};

        int ready = select(maxfd + 1, &readfds, NULL, NULL, &timeout);

        if (ready <= 0) {
            return nullptr;
        }

        for (auto res : resources) {
            int socket = res->get_socket();
            if (FD_ISSET(socket, &readfds)) {
                while(!res->try_lock());
                return res;
            }
        }

        return nullptr;
    }
};