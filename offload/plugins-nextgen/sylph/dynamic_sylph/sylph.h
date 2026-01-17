#ifndef SYLPH_H
#define SYLPH_H

#include <string>
#include <mutex>

#define NUM_SYLPH 1

#define SYHOST2DEVICE 0
#define SYDEVICE2HOST 1

typedef enum {
    SYLPH_STATUS_SUCCESS = 0x0,
    SYLPH_STATUS_ERROR = 0X1000,
} sy_status_t;

// plugin 中裝置的整數代號
typedef int sy_device_ptr;
// device 中 dl 的整數代號
typedef int sy_dl_ptr;
// dl 中全域變數（包含函數）的代號，因為要對齊 void * 所以是 uintptr_t
typedef uintptr_t sy_ptr_t;

// 初始化
sy_status_t sy_init();
// 中止
sy_status_t sy_finalize();
// 中止裝置
sy_status_t sy_quit(sy_device_ptr dev);
// 得到 dl 的全域變數（存入 ptr)
sy_status_t sy_get_symbol_ptr(sy_ptr_t &ptr, int &size, const char *name, sy_device_ptr dev, sy_dl_ptr dl);
// 建立 Device 上的行程
sy_status_t sy_spawn(sy_device_ptr &dev);
// 開啟目標檔案
sy_status_t sy_open(const char *path, sy_dl_ptr &dl, sy_device_ptr dev);
// 關閉目標檔案
sy_status_t sy_close(sy_dl_ptr &dl, sy_device_ptr dev);
// 為裝置分配一塊記憶體位置（主機不可讀）
sy_status_t sy_malloc(sy_ptr_t &ptr, size_t size, sy_device_ptr dev);
// 釋放裝置上的記憶體
sy_status_t sy_free(sy_ptr_t ptr, sy_device_ptr dev);
// 傳送資料
sy_status_t sy_data_transfer(sy_ptr_t src, sy_ptr_t dst, size_t size, int flag, sy_device_ptr dev);
// 執行 dl 內的函式
sy_status_t sy_exec(sy_device_ptr dev, sy_ptr_t &func_addr, int argc, void **argv, int64_t *args);

                                
#endif // ifndef SYLPH_H