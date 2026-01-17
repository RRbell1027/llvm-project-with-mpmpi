#pragma once

#include <string>

#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>

#include "utils/debug.h"
#include <syslog.h>

#ifdef MESSAGE0
#undef MESSAGE0
#define MESSAGE0(msg) syslog(LOG_INFO, msg)
#endif


std::string pidfile_path;

// 寫入 pid 文件
void write_pidfile() {
    int pidfile = open(pidfile_path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (pidfile != -1) {
        std::string pid_str = std::to_string(getpid()) + "\n";
        write(pidfile, pid_str.c_str(), pid_str.length());
        close(pidfile);
    }
}

// 清理 pid 文件
void cleanup_pidfile() {
    unlink(pidfile_path);
}

// 守护进程化
void daemonize() {
    pid_t pid = fork();
    if (pid < 0) {
        MESSAGE0("Fork failed");
        exit(1);
    } else if (pid > 0) {
        exit(0);  // 父进程退出
    }

    // 子进程继续
    umask(0);
    setsid();  // 创建新的会话

    // 写入 PID 文件
    write_pidfile();
}

void daemon_init(std::string pid_file_name) {
    pidfile_path = "/var/run/" + pid_file_name + ".pid";
    daemonize();
    openlog("mpi_client", LOG_PID | LOG_CONS, LOG_DAEMON);
    MESSAGE0("MPI Client daemon started");
}

void daemon_cleanup() {
    cleanup_pidfile();
    MESSAGE0("MPI Client daemon finished");
    closelog();
}
