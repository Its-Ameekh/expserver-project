#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <unistd.h>

#define PORT 8080
#define BUFF_SIZE 10000

void send_file(int sockfd) {

    FILE *fp;
    const char *filename = "t1.txt";

    fp = fopen(filename, "r");

    if (fp == NULL) {
        perror("Error in opening file");
        exit(EXIT_FAILURE);
    }

    char data[BUFF_SIZE] = {0};

    printf("[INFO] Sending data to server...\n");

    while (fgets(data, BUFF_SIZE, fp) != NULL) {

        if (send(sockfd, data, strlen(data), 0) == -1) {
            perror("Error in sending data");
            fclose(fp);
            exit(EXIT_FAILURE);
        }

        printf("[FILE DATA] %s", data);

        memset(data, 0, BUFF_SIZE);
    }

    printf("[INFO] File data sent successfully.\n");

    fclose(fp);
}

int main(void) {

    // create socket
    int client_sock_fd = socket(
        AF_INET,
        SOCK_STREAM,
        0
    );

    if (client_sock_fd == -1) {
        perror("socket");
        exit(EXIT_FAILURE);
    }

    // server address
    struct sockaddr_in server_addr;

    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(PORT);
    server_addr.sin_addr.s_addr = inet_addr("127.0.0.1");

    // connect to server
    if (connect(
        client_sock_fd,
        (struct sockaddr *)&server_addr,
        sizeof(server_addr)
    ) == -1) {
        perror("connect");
        close(client_sock_fd);
        exit(EXIT_FAILURE);
    }

    printf("[INFO] Connected to server\n");

    // send file
    send_file(client_sock_fd);

    // IMPORTANT:
    // tell server that we are done sending
    shutdown(client_sock_fd, SHUT_WR);

    close(client_sock_fd);

    printf("[INFO] Connection closed\n");

    return 0;
}