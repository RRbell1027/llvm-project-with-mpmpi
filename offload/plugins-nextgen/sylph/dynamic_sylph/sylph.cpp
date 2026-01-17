#include "Shared/Debug.h"
#include <mpi.h>
#include <cstring>
#include <mutex>
#include <algorithm>
#include <fcntl.h>
#include <unistd.h>

#include "sylph.h"
#include "worker.h"

MPI_Comm workers[MAX_WORKERS];
static std::mutex worker_mutex;

sy_status_t sy_init() {
    MPI_Init(nullptr, nullptr);

    for (int i = 0; i < MAX_WORKERS; i++) {
        workers[i] = MPI_COMM_NULL;
    }

    return SYLPH_STATUS_SUCCESS;
}

sy_status_t sy_finalize() {
    MPI_Finalize();
    return SYLPH_STATUS_SUCCESS;
}

sy_status_t sy_quit(sy_device_ptr dev) {
    CommandType cmd = CommandType::TERMINATE;
    MPI_Send(&cmd, 1, MPI_INT, 0, CONTROL_TAG, workers[dev]);

    int result;
    MPI_Recv(&result, 1, MPI_INT, 0, CONTROL_TAG, workers[dev], MPI_STATUS_IGNORE);

    MPI_Comm_free(&workers[dev]);
    workers[dev] = MPI_COMM_NULL;

    return SYLPH_STATUS_SUCCESS;
}

sy_status_t sy_get_symbol_ptr(sy_ptr_t &ptr, int &size, const char *name, sy_device_ptr dev, sy_dl_ptr dl) {
    std::lock_guard<std::mutex> lock(worker_mutex);

    char buffer[MAX_BUFFER_SIZE];
    int position = 0;
    CommandType cmd = CommandType::GetSymbol;
    
    MPI_Pack(&cmd, 1, MPI_INT, buffer, MAX_BUFFER_SIZE, &position, workers[dev]);
    MPI_Pack(const_cast<char*>(name), MAX_STRING_SIZE, MPI_CHAR, buffer, MAX_BUFFER_SIZE, &position, workers[dev]);
    MPI_Pack(&dl, 1, MPI_INT, buffer, MAX_BUFFER_SIZE, &position, workers[dev]);
    
    MPI_Send(buffer, position, MPI_PACKED, 0, CONTROL_TAG, workers[dev]);
    
    int result;
    MPI_Recv(&result, 1, MPI_INT, 0, CONTROL_TAG, workers[dev], MPI_STATUS_IGNORE);
    
    if (result != static_cast<int>(ReturnType::SUCCESS)) {
        return SYLPH_STATUS_ERROR;
    }
    
    SymbolInfo info;
    MPI_Recv(&info, sizeof(SymbolInfo), MPI_BYTE, 0, CONTROL_TAG, workers[dev], MPI_STATUS_IGNORE);
    
    ptr = reinterpret_cast<sy_ptr_t>(info.ptr);
    size = info.size;
    
    return SYLPH_STATUS_SUCCESS;
}

sy_status_t sy_spawn(sy_device_ptr &dev) {
    std::lock_guard<std::mutex> lock(worker_mutex);

    int currentId = -1;
    for (int i = 0; i < MAX_WORKERS; i++) {
        if (workers[i] == MPI_COMM_NULL) {
            currentId = i;
            break;
        }
    }
    if (currentId == -1) {
        return SYLPH_STATUS_ERROR;
    }
    
    MPI_Comm interComm;
    MPI_Comm_spawn("/workspace/worker", MPI_ARGV_NULL, 1, MPI_INFO_NULL, 0, 
                    MPI_COMM_WORLD, &interComm, MPI_ERRCODES_IGNORE);
    
    workers[currentId] = interComm;
    
    int worker_rank;
    MPI_Status status;
    MPI_Recv(&worker_rank, 1, MPI_INT, 0, CONTROL_TAG, interComm, &status);
    
    dev = currentId;

    return SYLPH_STATUS_SUCCESS;
}

sy_status_t sy_open(const char *path, sy_dl_ptr &dl, sy_device_ptr dev) {
    std::lock_guard<std::mutex> lock(worker_mutex);
    
    char buffer[MAX_BUFFER_SIZE];
    int position = 0;
    CommandType cmd = CommandType::Open;
    
    MPI_Pack(&cmd, 1, MPI_INT, buffer, MAX_BUFFER_SIZE, &position, workers[dev]);
    MPI_Pack(const_cast<char*>(path), MAX_STRING_SIZE, MPI_CHAR, buffer, MAX_BUFFER_SIZE, &position, workers[dev]);
    
    MPI_Send(buffer, position, MPI_PACKED, 0, CONTROL_TAG, workers[dev]);
    int result;
    MPI_Recv(&result, 1, MPI_INT, 0, CONTROL_TAG, workers[dev], MPI_STATUS_IGNORE);
    if (result != static_cast<int>(ReturnType::SUCCESS)) {
        return SYLPH_STATUS_ERROR;
    }
    
    MPI_Recv(&dl, 1, MPI_INT, 0, CONTROL_TAG, workers[dev], MPI_STATUS_IGNORE);
    return SYLPH_STATUS_SUCCESS;
}

sy_status_t sy_close(sy_dl_ptr &dl, sy_device_ptr dev) {

    std::lock_guard<std::mutex> lock(worker_mutex);
    char buffer[MAX_BUFFER_SIZE];
    int position = 0;
    CommandType cmd = CommandType::Close;
    
    MPI_Pack(&cmd, 1, MPI_INT, buffer, MAX_BUFFER_SIZE, &position, workers[dev]);
    MPI_Pack(&dl, 1, MPI_INT, buffer, MAX_BUFFER_SIZE, &position, workers[dev]);
    
    MPI_Send(buffer, position, MPI_PACKED, 0, CONTROL_TAG, workers[dev]);

    int result;
    MPI_Recv(&result, 1, MPI_INT, 0, CONTROL_TAG, workers[dev], MPI_STATUS_IGNORE);
    
    if (result != static_cast<int>(ReturnType::SUCCESS)) {
        return SYLPH_STATUS_ERROR;
    }
    
    dl = -1;

    return SYLPH_STATUS_SUCCESS;
}

sy_status_t sy_malloc(sy_ptr_t &ptr, size_t size, sy_device_ptr dev) {

    std::lock_guard<std::mutex> lock(worker_mutex);
    char buffer[MAX_BUFFER_SIZE];
    int position = 0;
    CommandType cmd = CommandType::ALLOCATE;
    
    MPI_Pack(&cmd, 1, MPI_INT, buffer, MAX_BUFFER_SIZE, &position, workers[dev]);
    MPI_Pack(&size, 1, MPI_UNSIGNED_LONG, buffer, MAX_BUFFER_SIZE, &position, workers[dev]);
    
    MPI_Send(buffer, position, MPI_PACKED, 0, CONTROL_TAG, workers[dev]);
    
    int result;
    MPI_Recv(&result, 1, MPI_INT, 0, CONTROL_TAG, workers[dev], MPI_STATUS_IGNORE);

    if (result != static_cast<int>(ReturnType::SUCCESS)) {
        return SYLPH_STATUS_ERROR;
    }
    
    MPI_Recv(&ptr, 1, MPI_UNSIGNED_LONG, 0, CONTROL_TAG, workers[dev], MPI_STATUS_IGNORE);
    
    return SYLPH_STATUS_SUCCESS;
}

sy_status_t sy_free(sy_ptr_t ptr, sy_device_ptr dev) {

    std::lock_guard<std::mutex> lock(worker_mutex);
    char buffer[MAX_BUFFER_SIZE];
    int position = 0;
    CommandType cmd = CommandType::FREE;
    MPI_Pack(&cmd, 1, MPI_INT, buffer, MAX_BUFFER_SIZE, &position, workers[dev]);
    MPI_Pack(&ptr, 1, MPI_UNSIGNED_LONG, buffer, MAX_BUFFER_SIZE, &position, workers[dev]);
    
    MPI_Send(buffer, position, MPI_PACKED, 0, CONTROL_TAG, workers[dev]);
    
    int result;
    MPI_Recv(&result, 1, MPI_INT, 0, CONTROL_TAG, workers[dev], MPI_STATUS_IGNORE);
    
    if (result != static_cast<int>(ReturnType::SUCCESS)) {
        return SYLPH_STATUS_ERROR;
    }

    return SYLPH_STATUS_SUCCESS;
}

inline sy_status_t sy_batch_transfer(sy_ptr_t src, sy_ptr_t dst, size_t size,
                                     int flag, MPI_Comm comm) {
    size_t transferred_size = 0;
    int ack;

    if (flag == SYHOST2DEVICE) {
        while (transferred_size < size) {
            size_t current_batch_size = std::min((size_t)DATA_BATCH_SIZE, size - transferred_size);
            const void* current_src_ptr = reinterpret_cast<const void*>(src + transferred_size);

            MPI_Send(current_src_ptr, current_batch_size, MPI_BYTE, 0, DATA_TAG, comm);

            MPI_Recv(&ack, 1, MPI_INT, 0, CONTROL_TAG, comm, MPI_STATUS_IGNORE);

            if (ack == static_cast<int>(ReturnType::BATCH_SUCCESS)) {
                transferred_size += current_batch_size;
            } else {
                return SYLPH_STATUS_ERROR;
            }
        }
        int end_signal = static_cast<int>(ReturnType::BATCH_END);
        MPI_Send(&end_signal, 1, MPI_INT, 0, CONTROL_TAG, comm);

    } else { // SYDEVICE2HOST - Host receives from Device (Worker Sends)

        size_t received_size = 0;
        while (true) { // Loop indefinitely until BATCH_END signal is received
            MPI_Status status;
            // Host (receiver) probes for incoming messages from the worker (rank 0).
            // Expected messages are either data batches (DATA_TAG) or the end signal (CONTROL_TAG).
            MPI_Probe(0, MPI_ANY_TAG, comm, &status);

            if (status.MPI_TAG == DATA_TAG) {
                int batch_size;
                MPI_Get_count(&status, MPI_BYTE, &batch_size);

                if (received_size + batch_size > size) {
                    return SYLPH_STATUS_ERROR;
                }

                void* current_dst_ptr = reinterpret_cast<void*>(dst + received_size);

                MPI_Recv(current_dst_ptr, batch_size, MPI_BYTE, 0, DATA_TAG, comm, MPI_STATUS_IGNORE);

                received_size += batch_size;

            } else if (status.MPI_TAG == CONTROL_TAG) {
                int signal_val;
                MPI_Recv(&signal_val, 1, MPI_INT, 0, CONTROL_TAG, comm, MPI_STATUS_IGNORE);

                if (signal_val == static_cast<int>(ReturnType::BATCH_END)) {
                    break;
                } else if (signal_val == static_cast<int>(ReturnType::BATCH_FAILURE)) {
                    return SYLPH_STATUS_ERROR;
                }
                else {
                    return SYLPH_STATUS_ERROR;
                }
            } else {
                return SYLPH_STATUS_ERROR;
            }
        }

        // After the loop (exited by BATCH_END signal), check if the total received size matches the expected size.
        if (received_size != size) {
             return SYLPH_STATUS_ERROR;
        }
    }
    return SYLPH_STATUS_SUCCESS;
}

 
sy_status_t sy_data_transfer(sy_ptr_t src, sy_ptr_t dst, size_t size,
                             int flag, sy_device_ptr dev) {
    std::lock_guard<std::mutex> lock(worker_mutex);

    char buffer[MAX_BUFFER_SIZE];
    int position = 0;
    CommandType cmd = (flag == SYHOST2DEVICE) ? CommandType::Receive : CommandType::Send;
    
    MPI_Pack(&cmd, 1, MPI_INT, buffer, MAX_BUFFER_SIZE, &position, workers[dev]);
    MPI_Pack(&size, 1, MPI_UNSIGNED_LONG, buffer, MAX_BUFFER_SIZE, &position, workers[dev]);

    if (flag == SYHOST2DEVICE) {
        MPI_Pack(&dst, 1, MPI_UNSIGNED_LONG, buffer, MAX_BUFFER_SIZE, &position, workers[dev]);
    } else {
        MPI_Pack(&src, 1, MPI_UNSIGNED_LONG, buffer, MAX_BUFFER_SIZE, &position, workers[dev]);
    }
    
    MPI_Send(buffer, position, MPI_PACKED, 0, CONTROL_TAG, workers[dev]);

    int result;
    MPI_Recv(&result, 1, MPI_INT, 0, CONTROL_TAG, workers[dev], MPI_STATUS_IGNORE);

    if (result != static_cast<int>(ReturnType::SUCCESS)) {
        return SYLPH_STATUS_ERROR;
    }

    sy_status_t batch_status = sy_batch_transfer(src, dst, size, flag, workers[dev]);

    return batch_status;
}

sy_status_t sy_exec(sy_device_ptr dev, sy_ptr_t &func_addr, int argc, void **argv, int64_t *args) {
    std::lock_guard<std::mutex> lock(worker_mutex);

    char buffer[MAX_BUFFER_SIZE];
    int position = 0;
    CommandType cmd = CommandType::Exec;
    MPI_Pack(&cmd, 1, MPI_INT, buffer, MAX_BUFFER_SIZE, &position, workers[dev]);
    MPI_Pack(&func_addr, 1, MPI_UNSIGNED_LONG, buffer, MAX_BUFFER_SIZE, &position, workers[dev]);
    
    MPI_Pack(&argc, 1, MPI_INT, buffer, MAX_BUFFER_SIZE, &position, workers[dev]);
    
    for (int i = 0; i < argc; i++) {
        MPI_Pack(&args[i], 1, MPI_INT64_T, buffer, MAX_BUFFER_SIZE, &position, workers[dev]);
        MPI_Pack(argv[i], args[i], MPI_BYTE, buffer, MAX_BUFFER_SIZE, &position, workers[dev]);
    }
    MPI_Send(buffer, position, MPI_PACKED, 0, CONTROL_TAG, workers[dev]);

    int result;
    MPI_Recv(&result, 1, MPI_INT, 0, CONTROL_TAG, workers[dev], MPI_STATUS_IGNORE);

    if (result != static_cast<int>(ReturnType::SUCCESS)) {
        return SYLPH_STATUS_ERROR;
    }

    return SYLPH_STATUS_SUCCESS;
}
