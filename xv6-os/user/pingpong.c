#include "user.h"

// +1. create pipe
// +2. create child proc
// +3. send "ping" from parent proc to child
// 4. read this msg in child proc, output "<child pid>: got <msg>" and send
// "pong" back
// +5. read response in parent proc, output "<parent pid>: got <msg>"
//
//
//
// PS: WOW, YOU CAN ACTUALLY USE wait() AND GET AWAY WITH ONLY ONE PIPE :fire:

int main() {
    int pipefd[2]; // pipe, 0 for red, 1 for write
    char buf[10];
    int pid;

    if (pipe(pipefd) < 0) {
        printf("Ошибка при создании пайпа\n");
        exit(1);
    }

    if ((pid = fork()) < 0) {
        printf("Ошибка при создании процесса\n");
        exit(1);
    }

    if (pid > 0) {
        // parent proc => send "ping", receive "pong"
        write(pipefd[1], "ping", 4);

        wait(0);

        if (read(pipefd[0], buf, sizeof(buf)) > 0) {
            printf("%d: got %s\n", getpid(), buf);
        }
        close(pipefd[1]);
        close(pipefd[0]);
    } else {
        // child proc => receive "ping", send "pong"
        if (read(pipefd[0], buf, sizeof(buf)) > 0) {
            printf("%d: got %s\n", getpid(), buf);

            write(pipefd[1], "pong", 4);
        }
        close(pipefd[0]);
        close(pipefd[1]);
    }

    exit(0);
}
