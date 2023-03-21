#include <sys/stat.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/eventfd.h>
#include <sys/epoll.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <pthread.h>
#include <getopt.h>

#define MAX_EPOLL_EVENTS 10
#define BUFFER_SIZE 200

typedef struct {
    int epoll_fd;
    int shut_fd;
} thread_args;

// TODO：细化对ET/LT的理解
// TODO: 搞出线程池
// TODO: 支持命令行参数

void *serve(void *arg) {
    thread_args *p = arg;
    int epoll_fd = p->epoll_fd;
    int shut_fd = p->shut_fd;
    int event_cnt;
    struct epoll_event events[MAX_EPOLL_EVENTS];
    int i, readsz;
    char buffer[BUFFER_SIZE];
    const char *msg = "Hello from the other side!";
    while(1) {
        if((event_cnt = epoll_wait(epoll_fd, events, MAX_EPOLL_EVENTS, -1)) == -1) {
            perror("Epoll wait error");
            break;
        }
        for(i = 0; i < event_cnt; i++) {
            if(events[i].events | EPOLLIN) {
                if((readsz = read(events[i].data.fd, buffer, BUFFER_SIZE - 1)) != -1) {
                    buffer[readsz] = 0;
                }
                if(readsz == 0) {
                    printf("Connection closed\n");
                    if(epoll_ctl(epoll_fd, EPOLL_CTL_DEL, events[i].data.fd, NULL)) {
                        perror("Failed to remove closed fd");
                    }
                    close(events[i].data.fd);
                } else {
                    printf("From client: %ld:  %s\n", strlen(buffer), buffer);
                    if(strcmp(buffer, "shutdown") == 0) {
                        long long tmp = 1;
                        write(shut_fd, &tmp, sizeof(long long));
                        goto thread_end;
                    }
                    write(events[i].data.fd, msg, strlen(msg));
                }
            }
        }
    }
    thread_end:
    printf("Server: IO thread terminated\n");
}

int main(int argc, char *argv[]) {
    int num_threads = 1;
    int port = 2333;

    int opt;
    const char *str = "t:l:p:";
    while ((opt = getopt(argc, argv, str)) != -1) {
        switch (opt) {
        case 't':
            num_threads = atoi(optarg);
            break;
        case 'p':
            port = atoi(optarg);
            break;
        }
    }


    int listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if(listen_fd == -1) {
        perror("Cannot create listen socket");
        return -1;
    }

    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(port);
    if(bind(listen_fd, (struct sockaddr *)&addr, sizeof(addr)) == -1) {
        close(listen_fd);
        perror("bind error");
        return -1;
    }

    if(listen(listen_fd, 10) == -1) {
        close(listen_fd);
        perror("Listen error");
        return -1;
    }

    int epoll_fd = epoll_create(1);
    if(epoll_fd == -1) {
        close(listen_fd);
        perror("Epoll create error");
        return -1;
    }

    int shut_fd = eventfd(0, 0);
    if(shut_fd == -1) {
        close(listen_fd);
        close(epoll_fd);
        perror("Cannot create eventfd");
        return -1;
    }

    int main_epoll_fd = epoll_create(1);
    if(main_epoll_fd == -1) {
        close(shut_fd);
        close(listen_fd);
        close(epoll_fd);
        perror("Cannot create main epoll fd");
        return -1;
    }

    
    struct epoll_event events;
    events.events = EPOLLIN | EPOLLET;

    events.data.fd = shut_fd;
    events.events = EPOLLIN | EPOLLET;
    if(epoll_ctl(main_epoll_fd, EPOLL_CTL_ADD, shut_fd, &events)) {
        close(shut_fd);
        close(listen_fd);
        close(epoll_fd);
        close(main_epoll_fd);
        perror("Cannot add shut_fd to main epoll fd");
        return -1;
    }

    events.data.fd = listen_fd;
    events.events = EPOLLIN | EPOLLET | EPOLLOUT;
    if(epoll_ctl(main_epoll_fd, EPOLL_CTL_ADD, listen_fd, &events)) {
        close(shut_fd);
        close(listen_fd);
        close(epoll_fd);
        close(main_epoll_fd);
        perror("Cannot add listen_fd to main epoll fd");
        return -1;
    }

    pthread_t info;
    pthread_attr_t attr;
    memset(&info, 0, sizeof(info));
    memset(&attr, 0, sizeof(attr));
    thread_args tmp = {epoll_fd, shut_fd};
    if(pthread_create(&info, &attr, serve, &tmp)) {
        close(shut_fd);
        close(listen_fd);
        close(epoll_fd);
        close(main_epoll_fd);
        perror("pthread create error");
        return -1;
    }

    struct sockaddr_in client_addr;
    socklen_t len = sizeof client_addr;
    memset(&client_addr, 0, sizeof(client_addr));
    struct epoll_event wait_events[5];
    int epoll_cnt = 0;
    int i;
    while((epoll_cnt = epoll_wait(main_epoll_fd, wait_events, 5, -1)) != -1) {
        for(i = 0; i < epoll_cnt; i++) {
            if((wait_events[i].events & EPOLLIN) && wait_events[i].data.fd == shut_fd) {
                goto main_end;
            } else if(wait_events[i].data.fd == listen_fd) {
                int accept_fd = accept(listen_fd, (struct sockaddr *)&client_addr, &len);
                if(accept_fd == -1) {
                    perror("Accept error");
                    break;
                }
                printf("Accepted a connection from %s\n", inet_ntoa(client_addr.sin_addr));
                events.data.fd = accept_fd;
                if(epoll_ctl(epoll_fd, EPOLL_CTL_ADD, accept_fd, &events)) {
                    perror("Epoll add error");
                    break;
                }
            }
        }
    }
    main_end:
    pthread_join(info, NULL);
    close(shut_fd);
    close(listen_fd);
    close(epoll_fd);
    close(main_epoll_fd);
    return 0;
}