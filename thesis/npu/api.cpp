#include <sys/socket.h>
#include <arpa/inet.h>
#include <iostream>
#include <queue>
#include <cstring>
#include <set>

#include "npu.h"
#include "api.h"

namespace npu {

const int PORT = 8888;
const int IMG_WIDTH = 640;
const int IMG_HEIGHT = 640;
const std::string IPs[1] = {"192.168.1.2"};
// const std::string IPs[1] = {"192.168.42.1"};

typedef struct {
    uint32_t id;
    uint32_t size;
    void* buf;
} data;


uint32_t cur_tensor_id;
std::queue<data> ready_queue; // 裡面會放著要給 NPU 的資料
std::set<int> waiting_queue; // 放著等待預測完的 tensor id

pthread_t dispatch_task, receive_task;
void* dispatch(void* arg);
void* receive(void* arg);

bool waiting_for_join;
bool initialized = false;

void try_init() {

    if (initialized) return;
    initialized = true;

    waiting_for_join = false;
    cur_tensor_id = 1;

    // 初始化每個 NPU
    for(auto ip: IPs) {

        int sock = socket(AF_INET, SOCK_STREAM, 0);

        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(PORT);
        inet_pton(AF_INET, ip.c_str(), &addr.sin_addr);

        if (connect(sock, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
            perror("Connect to NPU A failed");
            return;
        }

        std::cout << "Connected to NPU " << std::endl;

        NPUHandle::get_instance().add_device(sock);
    }

    pthread_create(&dispatch_task, nullptr, dispatch, nullptr);
    pthread_create(&receive_task, nullptr, receive, nullptr);

}

void ending(NPUDevice * dev) {
    while (!dev->try_lock());
    int socket = dev->get_socket();
    uint32_t end_signal = 0;
    printf("end signal\n");
    if (send(socket, &end_signal, sizeof(uint32_t), 0) <= 0) {
        throw std::runtime_error("TCP error between Sylph and NPU 7");
    }
    dev->unlock();
}

std::vector<void*>& waiting_for_result() {
    void *arg;
    waiting_for_join = true;
    pthread_join(dispatch_task, nullptr);
    pthread_join(receive_task, &arg);
    NPUHandle::get_instance().clear(ending);
    initialized = false;
    return *static_cast<std::vector<void*>*>(arg);
}

void send_input(uint32_t size, void* input) {
    try_init();
    uint32_t id = cur_tensor_id++;
    void * buf = malloc(size);
    memcpy(buf, input, size);
    data d = {id, size, buf};
    ready_queue.push(d);
    waiting_queue.insert(id);
}


// 這是執行緒工作，負責偵測佇列資料並送資料給 NPU
void* dispatch(void* arg) {
    while (true) {
        // 空資料
        if (ready_queue.empty()) {
            if (waiting_for_join) return nullptr;
            continue;
        }

        data d = ready_queue.front();
        ready_queue.pop();
        uint32_t id = d.id;
        uint32_t size = d.size;
        void* input = d.buf;
        NPUDevice *device = nullptr;
        while (!device) 
            device = NPUHandle::get_instance().get_best_and_lock();
        int socket = device->get_socket();

        if (send(socket, &id, sizeof(uint32_t), 0) <= 0) {
            throw std::runtime_error("TCP error between Sylph and NPU 4");
        }
        if (send(socket, &size, sizeof(uint32_t), 0) <= 0) {
            throw std::runtime_error("TCP error between Sylph and NPU 5");
        }
        if (send(socket, input, size, 0) <= 0) {
            throw std::runtime_error("TCP error between Sylph and NPU 6");
        }
        free(input);
        device->unlock();
    }
}


// 這是執行緒工作，輪詢來自 NPU 的結果
void* receive(void* arg) {

    std::vector<void*>* result_queue = new std::vector<void*>;

    while (true) {
        if (waiting_queue.empty()) {
            if (waiting_for_join) return result_queue;
            continue;
        }
        // 輪詢
        NPUDevice* device = NPUHandle::get_instance().polling_and_lock();
        if (!device) continue;
        int socket = device->get_socket();

        uint32_t output_id;
        if (recv(socket, &output_id, sizeof(uint32_t), 0) <= 0)
            throw std::runtime_error("TCP error between Sylph and NPU 1");

        uint32_t size;
        if (recv(socket, &size, sizeof(uint32_t), 0) <= 0)
            throw std::runtime_error("TCP error between Sylph and NPU 2");
        
        void *buffer;
        if (size != 0) {
            buffer = malloc(size);
            if (recv(socket, buffer, size, 0) <= 0)
                throw std::runtime_error("TCP error between Sylph and NPU 3");
        } else {
            buffer = nullptr;
        }

        device->unlock();
        waiting_queue.erase(waiting_queue.find(output_id));
        if (output_id > result_queue->size()) {
            result_queue->resize(output_id);
        }
        (*result_queue)[output_id-1] = buffer;
    }
    return nullptr;
}

} // namespace npu