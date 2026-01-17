#include <mpi.h>

#include <iostream>
#include <sstream>
#include <vector>

#include <signal.h>
#include <string.h>
#include <sys/select.h>

#include "worker.h"
#include "utils/debug.h"
#include "utils/fifo.h"

#ifdef ENABLE_DAEMON // 編譯為後台守護程序
#include "utils/daemon.h"
#endif


enum class Result {
    SUCCESS,
    TERMINATE,
    ERROR
};


Fifo cmd_fifo("test");

std::vector<MPI_Comm> workers;

// 发送结果到结果管道
void send_result(const std::string& result) {
    cmd_fifo.fifo_write(result);
}


// 等待输入的函数
std::string wait_for_input() {
    fd_set readfds;
    int cmd_fd = cmd_fifo.get_read_fd();
    char buffer[1024];
    
    // 初始化 select 的文件描述符集合
    FD_ZERO(&readfds);
    FD_SET(STDIN_FILENO, &readfds);
    FD_SET(cmd_fd, &readfds);
    
    // 等待输入（阻塞模式）
    int ret = select(cmd_fd + 1, &readfds, NULL, NULL, NULL);
    if (ret < 0) {
        send_result("ERROR: Select error: " + std::string(strerror(errno)));
        return "";
    }

    // 讀取數據
    if (FD_ISSET(STDIN_FILENO, &readfds)) {
        std::string cmd;
        std::getline(std::cin, cmd);
        return cmd;
    }
    else if (FD_ISSET(cmd_fd, &readfds)) {
        ssize_t n = cmd_fifo.fifo_read(buffer, sizeof(buffer) - 1);
        buffer[n] = '\0';
        std::string cmd(buffer);
        return cmd.substr(0, cmd.find_last_not_of("\n") + 1);
    }
    return "";
}


void recv(void *&buffer, int &size, MPI_Comm &comm) {
    MPI_Status status;
    MPI_Probe(0, 0, comm, &status);
    MPI_Get_count(&status, MPI_BYTE, &size);
    buffer = malloc(size);
    MPI_Recv(buffer, size, MPI_PACKED, 0, 0, comm, MPI_STATUS_IGNORE);
}


bool terminate_task() {
    CommandType command = CommandType::TERMINATE; // 通用中止命令

    // 通知所有 worker 中止
    for (int i = 0; i < MAX_WORKERS; i++) {
        if (workers[i] != MPI_COMM_NULL) {
            MESSAGE0("Terminating worker %d", i);
            int buffer_size = sizeof(command);
            int position = 0;
            char buffer[buffer_size];
            MPI_Pack(&command, 1, MPI_INT, buffer, buffer_size, &position, workers[i]);
            MPI_Send(buffer, buffer_size, MPI_PACKED, 2, 0, workers[i]);
            MPI_Comm_free(&workers[i]);
            workers[i] = MPI_COMM_NULL;
        }
    }
    return true;
}


bool load_binary_task() {
    // 找到空闲的 worker
    int currentId = -1;
    for (int i = 0; i < MAX_WORKERS; i++) {
        if (workers[i] == MPI_COMM_NULL) {
            currentId = i;
            break;
        }
    }
    if (currentId == -1) {
        send_result("ERROR: Max workers reached");
        return false;
    }        

    // MPI_Info info;
    // MPI_Info_create(&info);
    // MPI_Info_set(info, "host", "sylph");

    // 生成新进程
    MPI_Comm interComm;
    MPI_Comm_spawn("/workspace/worker", MPI_ARGV_NULL, 1, MPI_INFO_NULL, 0, 
                    MPI_COMM_WORLD, &interComm, MPI_ERRCODES_IGNORE);

    // 保存进程通信域
    workers[currentId] = interComm;

    // 等待 worker 回传 rank
    int worker_rank;
    MPI_Status status;
    MPI_Recv(&worker_rank, 1, MPI_INT, 0, 0, interComm, &status);

    std::stringstream result;
    result << "SUCCESS: Started worker " << currentId << " with rank " << worker_rank;
    send_result(result.str());

    return true;
}

void allocate_task(int deviceId) {
    CommandType command = CommandType::ALLOCATE;
    int buffer_size = sizeof(command);
    int position = 0;
    char buffer[buffer_size];
    MPI_Pack(&command, 1, MPI_INT, buffer, buffer_size, &position, workers[deviceId]);
    MPI_Send(buffer, buffer_size, MPI_PACKED, 0, 0, workers[deviceId]);
}

void free_task(int deviceId) {
    CommandType command = CommandType::FREE;
    int buffer_size = sizeof(command);
    int position = 0;
    char buffer[buffer_size];
    MPI_Pack(&command, 1, MPI_INT, buffer, buffer_size, &position, workers[deviceId]);
    MPI_Send(buffer, buffer_size, MPI_PACKED, 0, 0, workers[deviceId]);
}

void open_task(int deviceId) {

    int size1, size2;
    // 傳送的指令 Open /workspace/test
    CommandType command = CommandType::Open;
    MPI_Pack_size(1, MPI_INT, workers[deviceId], &size1);
    char path[512] = "/workspace/test.so";
    MPI_Pack_size(512, MPI_CHAR, workers[deviceId], &size2);
    
    // 發送封包
    int ssize = size1 + size2;
    void *sbuffer = malloc(ssize);
    int soffset = 0;
    MPI_Pack(&command, 1, MPI_INT, sbuffer, ssize, &soffset, workers[deviceId]);
    MPI_Pack(&path, 512, MPI_CHAR, sbuffer, ssize, &soffset, workers[deviceId]);
    MPI_Send(sbuffer, ssize, MPI_PACKED, 0, 0, workers[deviceId]);
    free(sbuffer);

    // 等待回應
    ReturnType result;
    MPI_Recv(&result, 1, MPI_INT, 0, 0, workers[deviceId], MPI_STATUS_IGNORE);

    if (result != ReturnType::SUCCESS) {
        MESSAGE0("[client]open_task failed, error code = %d\n", result);
        if (result == ReturnType::DL_OPEN_ERROR) {
            char error_code[512];
            MPI_Recv(error_code, 512, MPI_CHAR, 0, 0, workers[deviceId], MPI_STATUS_IGNORE);
            error_code[511] = '\0';
            MESSAGE0("[client]Error: %s\n", error_code);
        }
        return;
    }

    // 成功接收 dl index
    int dl_index;
    MPI_Recv(&dl_index, 1, MPI_INT, 0, 0, workers[deviceId], MPI_STATUS_IGNORE);
    MESSAGE0("[client]open_task success, dl index = %d\n", dl_index);
}


#if 0
// 处理命令的函数
Result handle_command(const std::string& cmd) {
    std::stringstream ss(cmd);
    std::string command;
    ss >> command;

    if (command == "start") {
        // 找到空闲的 worker
        int currentId = -1;
        for (int i = 0; i < MAX_WORKERS; i++) {
            if (workers[i] == MPI_COMM_NULL) {
                currentId = i;
                break;
            }
        }
        if (currentId == -1) {
            send_result("ERROR: Max workers reached");
            return Result::ERROR;
        }        

        MPI_Comm tempComm, interComm, intraComm;

        MPI_Send(&currentId, 1, MPI_INT, 1, 0, MPI_COMM_WORLD);

        // 生成新进程
        MPI_Comm_dup(MPI_COMM_WORLD, &tempComm);
        MPI_Comm_spawn("" /* by server */, MPI_ARGV_NULL, 1, MPI_INFO_NULL, 1 /* server rank */, 
                        tempComm, &interComm, MPI_ERRCODES_IGNORE);

        // 合并父子进程通信域
        MPI_Intercomm_merge(interComm, 0 /* rank don't change */, &intraComm);

        // 释放临时通信域
        MPI_Comm_free(&interComm);
        MPI_Comm_free(&tempComm);

        // 保存进程通信域
        workers[currentId] = intraComm;

        // 等待 worker 回传 rank
        int worker_rank;
        MPI_Status status;
        MPI_Recv(&worker_rank, 1, MPI_INT, MPI_ANY_SOURCE, 0, intraComm, &status);
        
        std::stringstream result;
        result << "SUCCESS: Started worker " << currentId << " with rank " << worker_rank;
        send_result(result.str());

        return Result::SUCCESS;
    }
    else if (command == "exit") {
        int id;
        ss >> id;
        
        if (workers[id] == MPI_COMM_NULL) {
            send_result("ERROR: Worker " + std::to_string(id) + " not found");
            return Result::ERROR;
        }

        MPI_Send("exit", 256, MPI_CHAR, 0, 0, workers[id]);
        MPI_Comm_free(&workers[id]);
        workers[id] = MPI_COMM_NULL;
        send_result("SUCCESS: Worker " + std::to_string(id) + " stopped");

        return Result::SUCCESS;
    }
    else if (command == "terminate") {
        CommandType command = CommandType::TERMINATE;

        // 通知所有 worker 中止
        for (int i = 0; i < MAX_WORKERS; i++) {
            if (workers[i] != MPI_COMM_NULL) {
                MPI_Send("terminate", 256, MPI_CHAR, 0, 0, workers[i]);
                MPI_Comm_free(&workers[i]);
                workers[i] = MPI_COMM_NULL;
            }
        }

        // 通知 server 中止
        MPI_Send(&command, 1, MPI_INT, 1, 0, MPI_COMM_WORLD);

        return Result::TERMINATE;
    }
    else if (command == "allocate") {
        int id, size;
        ss >> id >> size;
        
        if (workers[id] == MPI_COMM_NULL) {
            send_result("ERROR: Worker " + std::to_string(id) + " not found");
            return Result::ERROR;
        }

        MPI_Send("allocate", 256, MPI_CHAR, 0, 0, workers[id]);
        MPI_Send(&size, 1, MPI_INT, 0, 0, workers[id]);

        void* ptr;
        MPI_Recv(&ptr, sizeof(ptr), MPI_BYTE, 0, MPI_ANY_TAG, workers[id], MPI_STATUS_IGNORE);
        
        std::stringstream result;
        result << "SUCCESS: Allocated " << size << " bytes at " << ptr;
        send_result(result.str());
        
        return Result::SUCCESS;
    }
    else {
        send_result("ERROR: Unknown command");
        return Result::ERROR;
    }
}
#else 
Result handle_command(const std::string& cmd) {
    MESSAGE0("handle_command: %s", cmd.c_str());
    if (cmd == "quit") {
        terminate_task();
        return Result::TERMINATE;
    }
    if (cmd == "device_num") {
        send_result("4");
    }
    else if (cmd == "open") {
        open_task(0);
    }

    else if (cmd == "load_binary") {
        load_binary_task();
        return Result::SUCCESS;
    }
    else if (cmd == "allocate") {
        allocate_task(0);
        return Result::SUCCESS;
    }
    else if (cmd == "free") {
        free_task(0);
        return Result::SUCCESS;
    }
    return Result::SUCCESS;
}
#endif


int main(int argc, char** argv) {
    MPI_Init(&argc, &argv);

    // 初始化 worker 通信域
    workers.resize(MAX_WORKERS);
    for (int i = 0; i < MAX_WORKERS; i++) {
        workers[i] = MPI_COMM_NULL;
    }


#ifdef ENABLE_DAEMON
    daemon_init();
#endif

    MESSAGE0("MPI Client started");

    Result ret;
    // 主循环
    while (true) {
        std::string cmd = wait_for_input();
        if (!cmd.empty()) {
            ret = handle_command(cmd);
        }
        if (ret == Result::TERMINATE) {
            break;
        }
    }
    
    MESSAGE0("MPI Client finished");

    // 清理
#ifdef ENABLE_DAEMON
    daemon_cleanup();
#endif

    MPI_Finalize();
    return 0;
} 