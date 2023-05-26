#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <dirent.h>
#include <pwd.h>
#include <grp.h>
#include <time.h>
#include <ctype.h>
#include <fnmatch.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <semaphore.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/select.h>
#include <sys/wait.h>
#include <sys/socket.h>
#include <sys/ipc.h>
#include <sys/shm.h>

const char* countFile = "count.db";

int main() {

    int fd;
    int i, num = 100;
    pid_t pid;

    sem_t *mysem = sem_open("mysem", O_CREAT | O_EXCL, 0700, 2);
    sem_close(mysem);

    fd = open(countFile, O_CREAT | O_TRUNC | O_RDWR, 0666);
    write(fd, (void *)&num, sizeof(num));
    close(fd);

    printf(">> start: 100\n");
    pid = fork();

    if (pid > 0) {
        
        mysem = sem_open("mysem", O_RDWR);
        for (i = 0; i < 10; ++i) {
            
            sem_wait(mysem);
            fd = open(countFile, O_RDWR);
            lseek(fd, 0, SEEK_SET);
            read(fd, (void *)&num, sizeof(num));
            printf("parent: %d\n", ++num);
            lseek(fd, 0, SEEK_SET);
            write(fd, (void *)&num, sizeof(num));
            close(fd);

            sem_post(mysem);
            usleep(100);
        }
        sem_close(mysem);
    }

    else if (!pid) {
        
        mysem = sem_open("mysem", O_RDWR);
        for (i = 0; i < 10; ++i) {
            
            sem_wait(mysem);
            fd = open(countFile, O_RDWR);
            lseek(fd, 0, SEEK_SET);
            read(fd, (void *)&num, sizeof(num));
            printf("child: %d\n", --num);
            lseek(fd, 0, SEEK_SET);
            write(fd, (void *)&num, sizeof(num));
            close(fd);

            sem_post(mysem);
            usleep(100);
        }
        sem_close(mysem);
        exit(0);
    }

    wait(NULL);
    fd = open(countFile, 0, O_RDWR);
    lseek(fd, 0, SEEK_SET);
    read(fd, (void *)&num, sizeof(num));
    close(fd);
    sem_unlink("mysem");
    return 0;
}