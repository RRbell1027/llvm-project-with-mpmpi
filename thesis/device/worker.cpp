#include <mpi.h>
#include <iostream>
#include <dlfcn.h>
#include <string.h>
#include <ffi.h>
#include "trace.h"

#include "worker.h"

#define MAX_DL 4

MPI_Comm parentComm;
void *dynamic_libraries[MAX_DL];

void init(int argc, char** argv) {
    MPI_Init(&argc, &argv);

    for (int i=0; i < MAX_DL; i++) {
        dynamic_libraries[i] = nullptr;
    }

    // 获取父通信器
    MPI_Comm_get_parent(&parentComm);
    if (parentComm == MPI_COMM_NULL) {
        std::cerr << "[Worker] No parent communicator" << std::endl;
        MPI_Finalize();
        exit(1);
    }

    // 发送自己的 rank 给客户端
    int myRank;
    MPI_Comm_rank(MPI_COMM_WORLD, &myRank);
    MPI_Send(&myRank, 1, MPI_INT, 0, CONTROL_TAG, parentComm);
}

int main(int argc, char** argv) {
    init(argc, argv);

    while (true) {
        MPI_Status status;
        MPI_Probe(0, 0, parentComm, &status);
 
        int commandSize;
        MPI_Get_count(&status, MPI_BYTE, &commandSize);

        char *buffer = (char*)malloc(commandSize);
        MPI_Recv(buffer, commandSize, MPI_PACKED, 0, 0, parentComm, MPI_STATUS_IGNORE);

        int offset = 0;
        CommandType commandType;
        MPI_Unpack(buffer, commandSize, &offset, &commandType, 1, MPI_INT, parentComm);

        switch (commandType) {
        case CommandType::Open: {
            // 寻找空 dl
            int currentId = -1;
            for (int i = 0; i < MAX_DL; i++) {
                if (dynamic_libraries[i] == nullptr) {
                    currentId = i;
                    break;
                }
            }
            if (currentId == -1) {
                int res = static_cast<int>(ReturnType::DL_MAX_ERROR);
                MPI_Send(&res, 1, MPI_INT, 0, CONTROL_TAG, parentComm);
                break;
            }
            
            // 解包 dl 路径
            char path[MAX_STRING_SIZE];
            MPI_Unpack(buffer, commandSize, &offset, path, MAX_STRING_SIZE, MPI_CHAR, parentComm);
            
            // 打开 dl
            void *handle = dlopen(path, RTLD_LAZY);
            if (!handle) {
                int res = static_cast<int>(ReturnType::DL_OPEN_ERROR);
                MPI_Send(&res, 1, MPI_INT, 0, CONTROL_TAG, parentComm);
                printf("%s\n", dlerror());
                break;
            }
            dynamic_libraries[currentId] = handle;

            // 回传成功
            int res = static_cast<int>(ReturnType::SUCCESS);
            MPI_Send(&res, 1, MPI_INT, 0, CONTROL_TAG, parentComm);

            // 回传 dl index
            MPI_Send(&currentId, 1, MPI_INT, 0, CONTROL_TAG, parentComm);
            break;
        }

        case CommandType::ALLOCATE: {
            size_t size;
            MPI_Unpack(buffer, commandSize, &offset, &size, 1, MPI_UNSIGNED_LONG, parentComm);
            
            void* ptr = malloc(size);
            if (!ptr) {
                int res = static_cast<int>(ReturnType::FUNCTION_CALL_ERROR);
                MPI_Send(&res, 1, MPI_INT, 0, CONTROL_TAG, parentComm);
                break;
            }
            
            int res = static_cast<int>(ReturnType::SUCCESS);
            MPI_Send(&res, 1, MPI_INT, 0, CONTROL_TAG, parentComm);
            
            MPI_Send(&ptr, 1, MPI_UNSIGNED_LONG, 0, CONTROL_TAG, parentComm);
            break;
        }

        case CommandType::FREE: {
            uintptr_t ptr;
            MPI_Unpack(buffer, commandSize, &offset, &ptr, 1, MPI_UNSIGNED_LONG, parentComm);

            // 释放内存
            free(reinterpret_cast<void*>(ptr));
            
            // 发送成功响应
            int res = static_cast<int>(ReturnType::SUCCESS);
            MPI_Send(&res, 1, MPI_INT, 0, CONTROL_TAG, parentComm);
            break;
        }

        case CommandType::Send: {
            // 设备到主机传输
            size_t size;
            uintptr_t src_addr;
            
            // 解包大小和源地址
            MPI_Unpack(buffer, commandSize, &offset, &size, 1, MPI_UNSIGNED_LONG, parentComm);
            MPI_Unpack(buffer, commandSize, &offset, &src_addr, 1, MPI_UNSIGNED_LONG, parentComm);
            
            // 发送成功响应
            int res = static_cast<int>(ReturnType::SUCCESS);
            MPI_Send(&res, 1, MPI_INT, 0, CONTROL_TAG, parentComm);
            
            // 分批发送数据
            size_t transferred_size = 0;
            
            while (transferred_size < size) {
                trace_begin("send");
                size_t current_batch_size = std::min((size_t)DATA_BATCH_SIZE, size - transferred_size);
                const void* current_src_ptr = reinterpret_cast<const void*>(src_addr + transferred_size);
                MPI_Send(current_src_ptr, current_batch_size, MPI_BYTE, 0, DATA_TAG, parentComm);
                trace_end("send");
                transferred_size += current_batch_size;
            }
            
            // 发送结束信号

            trace_begin("send result");
            int end_signal = static_cast<int>(ReturnType::BATCH_END);
            MPI_Send(&end_signal, 1, MPI_INT, 0, CONTROL_TAG, parentComm);
            trace_end("send result");
            break;
        }

        case CommandType::Receive: {
            // 主机到设备传输
            size_t size;
            uintptr_t dst_addr;
            
            // 解包大小和目标地址
            MPI_Unpack(buffer, commandSize, &offset, &size, 1, MPI_UNSIGNED_LONG, parentComm);
            MPI_Unpack(buffer, commandSize, &offset, &dst_addr, 1, MPI_UNSIGNED_LONG, parentComm);
            
    
            // 发送成功响应
            int res = static_cast<int>(ReturnType::SUCCESS);
            MPI_Send(&res, 1, MPI_INT, 0, CONTROL_TAG, parentComm);
            
            // 分批接收数据
            size_t transferred_size = 0;
            while (true) {
                MPI_Status status;
                MPI_Probe(0, MPI_ANY_TAG, parentComm, &status);
                
                if (status.MPI_TAG == CONTROL_TAG) {
                    int signal_val;
                    MPI_Recv(&signal_val, 1, MPI_INT, 0, CONTROL_TAG, parentComm, MPI_STATUS_IGNORE);
                    if (signal_val == static_cast<int>(ReturnType::BATCH_END)) {
                        break;
                    }
                } else if (status.MPI_TAG == DATA_TAG) {
                    int batch_size;
                    MPI_Get_count(&status, MPI_BYTE, &batch_size);

                    trace_begin("receive");
                    void* current_dst_ptr = reinterpret_cast<void*>(dst_addr + transferred_size);
                    MPI_Recv(current_dst_ptr, batch_size, MPI_BYTE, 0, DATA_TAG, parentComm, MPI_STATUS_IGNORE);
                    trace_end("receive");
                    
                    transferred_size += batch_size;
                    
                    // 发送确认
                    trace_begin("receive success");
                    int ack = static_cast<int>(ReturnType::BATCH_SUCCESS);
                    MPI_Send(&ack, 1, MPI_INT, 0, CONTROL_TAG, parentComm);
                    trace_end("receive success");
                }
            }
            
            if (transferred_size != size) {
                int error = static_cast<int>(ReturnType::BATCH_FAILURE);
                MPI_Send(&error, 1, MPI_INT, 0, CONTROL_TAG, parentComm);
            }
            break;
        }

        case CommandType::GetSymbol: {
            char symbolName[MAX_STRING_SIZE];
            int dlIndex;
            
            // 解包符号名和 dl index
            MPI_Unpack(buffer, commandSize, &offset, symbolName, MAX_STRING_SIZE, MPI_CHAR, parentComm);
            MPI_Unpack(buffer, commandSize, &offset, &dlIndex, 1, MPI_INT, parentComm);
            
            // 获取符号
            void* symbol = dlsym(dynamic_libraries[dlIndex], symbolName);
            if (!symbol) {
                int res = static_cast<int>(ReturnType::SYMBOL_NOT_FOUND);
                MPI_Send(&res, 1, MPI_INT, 0, CONTROL_TAG, parentComm);
                break;
            }
            
            // 回传成功
            int res = static_cast<int>(ReturnType::SUCCESS);
            MPI_Send(&res, 1, MPI_INT, 0, CONTROL_TAG, parentComm);
            
            // 回传符号信息
            SymbolInfo info;
            strncpy(info.name, symbolName, MAX_STRING_SIZE);
            info.ptr = symbol;
            info.size = 0;  // 目前无法获取符号大小
            MPI_Send(&info, sizeof(SymbolInfo), MPI_BYTE, 0, CONTROL_TAG, parentComm);
            break;
        }

        case CommandType::Close: {
            int dlIndex;
            MPI_Unpack(buffer, commandSize, &offset, &dlIndex, 1, MPI_INT, parentComm);
            if (dlclose(dynamic_libraries[dlIndex]) != 0) {
                int res = static_cast<int>(ReturnType::DL_CLOSE_ERROR);
                MPI_Send(&res, 1, MPI_INT, 0, CONTROL_TAG, parentComm);
                break;
            }
            
            dynamic_libraries[dlIndex] = nullptr;
            
            int res = static_cast<int>(ReturnType::SUCCESS);
            MPI_Send(&res, 1, MPI_INT, 0, CONTROL_TAG, parentComm);
            break;
        }

        case CommandType::Exec: {
            int dlIndex;
            uintptr_t func_addr;
            int argc;
            MPI_Unpack(buffer, commandSize, &offset, &func_addr, 1, MPI_UNSIGNED_LONG, parentComm);
            MPI_Unpack(buffer, commandSize, &offset, &argc, 1, MPI_INT, parentComm);

            // 准备参数
            void** argv = new void*[argc];
            int64_t* arg_sizes = new int64_t[argc];
            
            // 解包每个参数的大小和值
            for (int i = 0; i < argc; i++) {
                // 先解包参数大小
                MPI_Unpack(buffer, commandSize, &offset, &arg_sizes[i], 1, MPI_INT64_T, parentComm);
                // 分配内存并解包参数值
                argv[i] = malloc(arg_sizes[i]);
                MPI_Unpack(buffer, commandSize, &offset, argv[i], arg_sizes[i], MPI_BYTE, parentComm);
            }


            // 设置 libffi 调用
            ffi_cif cif;
            ffi_type** arg_types = new ffi_type*[argc];
            for (int i = 0; i < argc; i++) {
                // 所有参数都作为指针传递
                arg_types[i] = &ffi_type_pointer;
            }

            // 准备返回值
            void* result = nullptr;

            // 初始化 cif
            if (ffi_prep_cif(&cif, FFI_DEFAULT_ABI, argc, &ffi_type_void, arg_types) != FFI_OK) {
                for (int i = 0; i < argc; i++) {
                    free(argv[i]);
                }
                delete[] argv;
                delete[] arg_sizes;
                delete[] arg_types;
                int res = static_cast<int>(ReturnType::FFI_ERROR);
                MPI_Send(&res, 1, MPI_INT, 0, CONTROL_TAG, parentComm);
                break;
            }

            // 调用函数
            try {
                void (*func)() = reinterpret_cast<void (*)()>(func_addr);
                ffi_call(&cif, FFI_FN(func), result, argv);
                int res = static_cast<int>(ReturnType::SUCCESS);
                MPI_Send(&res, 1, MPI_INT, 0, CONTROL_TAG, parentComm);
            } catch (...) {
                for (int i = 0; i < argc; i++) {
                    free(argv[i]);
                }
                delete[] argv;
                delete[] arg_sizes;
                delete[] arg_types;
                MPI_Abort(parentComm, 1);
            }

            // 清理内存
            for (int i = 0; i < argc; i++) {
                free(argv[i]);
            }
            delete[] argv;
            delete[] arg_sizes;
            delete[] arg_types;
            break;
        }

        case CommandType::TERMINATE:
            
            // 释放所有动态加载的库
            for (int i = 0; i < MAX_DL; i++) {
                if (dynamic_libraries[i] != nullptr) {
                    dlclose(dynamic_libraries[i]);
                    dynamic_libraries[i] = nullptr;
                }
            }
            
            free(buffer);


            int res = static_cast<int>(ReturnType::SUCCESS);
            MPI_Send(&res, 1, MPI_INT, 0, CONTROL_TAG, parentComm);
            
            MPI_Comm_free(&parentComm);

            MPI_Finalize();

            trace_save("device_trace.json");
            exit(0);
        }

        free(buffer);
    }
    return 0;
}