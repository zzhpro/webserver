#include <sys/stat.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>

#define BUFFER_SIZE 1024

int main() {
    int client_fd = socket(AF_INET, SOCK_STREAM, 0);
    if(client_fd == -1) {
        perror("Cannot create listen socket");
        return -1;
    }

    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_port = htons(2333);
    if(inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr) <= 0) {
        perror("Cannot convert to IP addr");
        return -1;
    }
    if(connect(client_fd, (struct sockaddr *)&addr, sizeof(addr))) {
        perror("Cannot connect to server");
        return -1;
    }

    char cmd_buf[BUFFER_SIZE], server_buf[BUFFER_SIZE];
    while(fgets(cmd_buf, BUFFER_SIZE, stdin)) {
        size_t read_len;
        cmd_buf[strlen(cmd_buf) - 1] = 0;
        write(client_fd, cmd_buf, strlen(cmd_buf));
        if(read_len = read(client_fd, server_buf, BUFFER_SIZE - 10)) {
            server_buf[read_len] = 0;
            printf("%s\n", server_buf);
        }
        if(read_len == 0 || strcmp(cmd_buf, "shutdown") == 0) {
            break;
        }
    }
    close(client_fd);
    
    return 0;
}