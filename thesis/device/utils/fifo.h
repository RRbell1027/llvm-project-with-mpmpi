#pragma once

#include <string>
#include <sys/types.h>
#include <sys/stat.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>

/// @brief 雙向管道
/// @note 使用方法：
/// 假設行程 1 與行程 2 需要雙向溝通
/// 然後行程 1 創建一個 Fifo 物件
/// 行程 2 也創建一個 Fifo 物件，使用 create = False，等待管道創建完成
/// 使用 fifo_write 寫入資料，使用 fifo_read 讀取資料
class Fifo {
public:
    Fifo(const std::string& pipe_name, bool create = true);
    ~Fifo();

    int fifo_write(const std::string& data);
    int fifo_read(void* data, size_t size);

    // 取得讀取 FIFO 的文件描述符，用於 select 等待輸入
    int get_read_fd() const { return read_fd; }

private:
    std::string write_fifo_path;
    std::string read_fifo_path;
    int write_fd;
    int read_fd;

    void create_fifos();
    void open_fifo();

    void cleanup_fifos();
};


Fifo::Fifo(const std::string& pipe_name, bool create)
    : write_fd(-1), read_fd(-1)
{
    if (create) {
        write_fifo_path = "/tmp/" + pipe_name + "_ab";
        read_fifo_path = "/tmp/" + pipe_name + "_ba";
        create_fifos();
    } else {
        write_fifo_path = "/tmp/" + pipe_name + "_ba";
        read_fifo_path = "/tmp/" + pipe_name + "_ab";
        open_fifo();
    }

    if (write_fd == -1 || read_fd == -1) {
        throw std::runtime_error("Failed to open FIFO");
    }
}


Fifo::~Fifo() {
    cleanup_fifos();
}


int Fifo::fifo_write(const std::string& data) {
    return write(write_fd, data.c_str(), data.length() + 1);
}


int Fifo::fifo_read(void* data, size_t size) {
    return read(read_fd, data, size);
}


void Fifo::open_fifo() {
    while (access(write_fifo_path.c_str(), F_OK) == -1 || access(read_fifo_path.c_str(), F_OK) == -1) {
        usleep(100000);
    }
    read_fd = open(read_fifo_path.c_str(), O_RDONLY | O_NONBLOCK);
    write_fd = open(write_fifo_path.c_str(), O_WRONLY);
}


// 創建 FIFO 管道
void Fifo::create_fifos() {
    // 創建寫入 FIFO
    if (mkfifo(write_fifo_path.c_str(), 0666) == -1 && errno != EEXIST) {
        return;
    }

    // 創建讀取 FIFO
    if (mkfifo(read_fifo_path.c_str(), 0666) == -1 && errno != EEXIST) {
        unlink(write_fifo_path.c_str());
        return;
    }

    // 打開寫入 FIFO
    write_fd = open(write_fifo_path.c_str(), O_WRONLY);
    if (write_fd == -1) {
        unlink(write_fifo_path.c_str());
        unlink(read_fifo_path.c_str());
        return;
    }

    fprintf(stderr, "create_fifos start: %s, %s\n", write_fifo_path.c_str(), read_fifo_path.c_str());
    // 打開讀取 FIFO
    read_fd = open(read_fifo_path.c_str(), O_RDONLY | O_NONBLOCK);
    if (read_fd == -1) {
        close(write_fd);
        unlink(write_fifo_path.c_str());
        unlink(read_fifo_path.c_str());
        return;   
    }
    fprintf(stderr, "create_fifos end: %s, %s\n", write_fifo_path.c_str(), read_fifo_path.c_str());
}

// 清理 FIFO 管道
void Fifo::cleanup_fifos() {
    close(write_fd);
    close(read_fd);
    unlink(write_fifo_path.c_str());
    unlink(read_fifo_path.c_str());
}