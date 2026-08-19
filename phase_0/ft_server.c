#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <unistd.h>

#define PORT 8080
#define BUFF_SIZE 10000
#define MAX_ACCEPT_BACKLOG 5

void write_to_file(int conn_sock_fd) {

    char buffer[BUFF_SIZE];
    ssize_t bytes_received;

    FILE *fp = fopen("t2.txt", "w");

    if (fp == NULL) {
        perror("Error in creating file");
        exit(EXIT_FAILURE);
    }

    printf("[INFO] Receiving data from client...\n");

    while ((bytes_received = recv(conn_sock_fd,buffer,sizeof(buffer) - 1,0)) > 0) {

        buffer[bytes_received] = '\0';

        printf("[FILE DATA] %s", buffer);

        fprintf(fp, "%s", buffer);

        memset(buffer, 0, sizeof(buffer));
    }

    if (bytes_received < 0) {
        perror("Error in receiving data");
    }

    fclose(fp);

    printf("[INFO] Data written to file successfully.\n");
}

int main(void) {

    int listening_socket_fd = socket(AF_INET,SOCK_STREAM,0);

    if (listening_socket_fd == -1) {
        perror("socket");
        exit(EXIT_FAILURE);
    }

    int enable = 1;

    setsockopt(listening_socket_fd,SOL_SOCKET,SO_REUSEADDR,&enable,sizeof(enable));

    struct sockaddr_in server_addr;

    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(PORT);
    server_addr.sin_addr.s_addr = htonl(INADDR_ANY);

    if (bind(listening_socket_fd,(struct sockaddr *)&server_addr,sizeof(server_addr)) == -1) {
        perror("bind");
        exit(EXIT_FAILURE);
    }

    if (listen(listening_socket_fd,MAX_ACCEPT_BACKLOG) == -1) {
        perror("listen");
        exit(EXIT_FAILURE);
    }

    printf("[INFO] Server listening on port %d\n", PORT);

    struct sockaddr_in client_addr;
    socklen_t client_addr_len = sizeof(client_addr);

    while (1) {

        int conn_sock_fd = accept(listening_socket_fd,(struct sockaddr *)&client_addr,&client_addr_len);

        if (conn_sock_fd == -1) {
            perror("accept");
            continue;
        }

        printf("[INFO] Accepted connection from client\n");

        write_to_file(conn_sock_fd);

        close(conn_sock_fd);

        printf("[INFO] Client disconnected\n");
    }

    close(listening_socket_fd);

    return 0;
}