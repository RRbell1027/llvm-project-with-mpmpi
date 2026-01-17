#pragma once

#include <cstddef>

#define MAX_WORKERS 4
#define MAX_DL 4
#define MAX_STRING_SIZE 512 // Assuming STRING_SIZE is also used by worker for paths/symbols
#define MAX_BUFFER_SIZE 1024

// Batch transfer constants
#define DATA_BATCH_SIZE (4 * 1024 * 1024) // 4MB batch size

// MPI Message Tags
#define DATA_TAG 1    // Tag for data batches
#define CONTROL_TAG 0 // Tag for control signals (ACK, END) - Note: Commands also use tag 0

#define BATCH_WINDOW_SIZE 4

enum class CommandType {
    TERMINATE = -1,
    ALLOCATE = 0,
    FREE = 1,
    Open = 2,
    GetSymbol = 3,
    Close = 4,
    Exec = 5,
    Receive = 6, // Host to Device transfer (Host sends, Worker receives)
    Send = 7     // Device to Host transfer (Worker sends, Host receives)
};

enum class ReturnType {
    SUCCESS = 1,
    DL_OPEN_ERROR = 1000,
    DL_MAX_ERROR = 1001,
    DL_CLOSE_ERROR = 1002,
    SYMBOL_NOT_FOUND = 1003,
    FUNCTION_CALL_ERROR = 1004,
    FFI_ERROR = 1005,
    BATCH_FAILURE = 1006, // Use 0 for negative acknowledgment

    // Batch transfer return types/signals
    BATCH_SUCCESS = 2, // Matches SUCCESS for positive acknowledgment
    BATCH_END = 100     // Use -1 for end signal
};

// 用于在进程间传递符号信息的结构
struct SymbolInfo {
    char name[512];
    size_t size;
    void* ptr;
};