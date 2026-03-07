#include "sysapi.h"
#include "uconst.h"
#include "uprintf.h"
#include <stdint.h>

int main() {
    int pipefd[2];
    int pid;
    char write_msg[] = "Hello from parent!";
    char read_msg[100];

    // 创建管道
    if (pipe(pipefd) == -1) {
        printf("pipe create failed\n");
        exit(-1);
    }

    // 创建子进程
    pid = fork();
    if (pid == -1) {
        printf("fork failed\n");
        exit(-1);
    }

    if (pid == 0) {
        close(pipefd[1]);
        read(pipefd[0], read_msg, sizeof(read_msg));
        printf("Child received: %s\n", read_msg);
        close(pipefd[0]);
        exit(0);
    } else {
        close(pipefd[0]);
        write(pipefd[1], write_msg, sizeof(write_msg));
        close(pipefd[1]);
        waitpid(pid, NULL);
        printf("Parent done.\n");
        exit(0);
    }
}