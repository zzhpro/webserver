#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>

#define sz (sizeof(int) * 100)

int *get_shared(int trunc) {
    int fd = shm_open("/rekv", O_RDWR | O_CREAT, S_IRUSR | S_IWUSR);
    if(fd < 0) {
        printf("%d open failed beacuse of %d\n", getpid(), errno);
        return 0;
    }

    if(trunc) {
        if (ftruncate(fd, sz) == -1) {
            perror("ftruncate");
            return ;
        }
    }

    int *ptr = (int *)mmap(0, sz, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    close(fd);
    if(ptr == NULL) {
        printf("%d mmap failed\n", getpid());
        return 0;
    }
    return ptr;
}

void release_shared(void *ptr) {
    if(munmap(ptr, sz)) {
        exit(-1);
    }
    shm_unlink("/zzh/tmp");
}

int main() {
    int forkret = fork();
    if(forkret > 0) {
        int *ptr = get_shared(1);
        if(ptr) {
            for(int i = 0; i < 100; i++) {
                ptr[i] = i;
            }
            release_shared(ptr);
        }
        wait(NULL);
    } else if(forkret == 0) {
        sleep(1);
        int *ptr = get_shared(0);
        if(ptr) {
            for(int i = 0; i < 100; i+=10) {
                printf("%d ", ptr[i]);
            }
            puts("");
            release_shared(ptr);
        }
    } else {
        printf("Fork failed\n");
    }

    printf("%d exiting\n", getpid());
    return 0;
}