#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <unistd.h>
#include <netdb.h>
#include <sys/epoll.h>

#define PORT 8080
#define BUFF_SIZE 10000
#define MAX_ACCEPT_BACKLOG 5
#define MAX_EPOLL_EVENTS 10

void strrev(char *str)
{
  for (int start = 0, end = strlen(str) - 2; start < end; start++, end--)
  {
    char temp = str[start];
    str[start] = str[end];
    str[end] = temp;
  }
}

int main(void) {

    // both event and events are completely different
    struct epoll_event event, events[MAX_EPOLL_EVENTS];

    // we create a listening socket
    int listening_socket_fd = socket(AF_INET, SOCK_STREAM, 0);

    if (listening_socket_fd == -1) {
        perror("socket");
        // perror reads errno from operating system and prints a human readable error message to stderr
        // here we get socket: "Permission denied or something"
        // notice we gave the "socket"
        exit(1);
        // exit(0) means success, exit(1) means failure,
        // difference from return is that this can exit pgm even from deep inside a function call not just from main
    }

    // setting soekt can be reused immediately after program exits
    // before we bind the socket to a port, we set the SO_REUSEADDR option on the socket
    int enable = 1;

    if (setsockopt(listening_socket_fd,
                   SOL_SOCKET,
                   SO_REUSEADDR,
                   &enable,
                   sizeof(enable)) == -1) {
        perror("setsockopt");
        close(listening_socket_fd);
        exit(1);
    }

    struct sockaddr_in server_addr;

    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(PORT);
    server_addr.sin_addr.s_addr = htonl(INADDR_ANY);

    // bind the socket to the port
    if (bind(listening_socket_fd,
             (struct sockaddr *)&server_addr,
             sizeof(server_addr)) == -1) {
        perror("bind");
        close(listening_socket_fd);
        exit(1);
    }

    // listen for incoming connections
    if (listen(listening_socket_fd, MAX_ACCEPT_BACKLOG) == -1) {
        perror("listen");
        close(listening_socket_fd);
        exit(1);
    }

    printf("[INFO] Server listening on port %d\n", PORT);

    // to store the client address information when accepting a connection,
    // we need to create a sockaddr_in structure and a socklen_t variable to hold the length of the structure.
    // This is necessary because the accept() function requires these parameters to fill in the client's address information.

    struct sockaddr_in client_addr;
    socklen_t client_addr_len = sizeof(client_addr);

    // before accepting lets create an epoll instance
    int epoll_fd = epoll_create1(0);

    // IN ROADMAP FLAG IS 0, usually closeexec is better
    if (epoll_fd == -1) {
        perror("epoll_create1");
        close(listening_socket_fd);
        exit(1);
    }

    // add the listening socket to epoll
    event.events = EPOLLIN;
    event.data.fd = listening_socket_fd;

    if (epoll_ctl(epoll_fd,
                  EPOLL_CTL_ADD,
                  listening_socket_fd,
                  &event) == -1) {
        perror("epoll_ctl");
        close(epoll_fd);
        close(listening_socket_fd);
        exit(1);
    }

    while (1) {

        // epoll wait is monitoring all the sockets that we have added to epoll
        // initially this is only the listening socket
        printf("[DEBUG] Epoll wait\n");

        int n_ready_fds = epoll_wait(
            epoll_fd,
            events,
            MAX_EPOLL_EVENTS,
            -1
        );

        if (n_ready_fds == -1) {
            perror("epoll_wait");
            continue;
        }

        // now need to iterate through the events array
        // events array has all the ready fds

        for (int i = 0; i < n_ready_fds; i++) {

            int curr_fd = events[i].data.fd;

            // if event on listen socket need to accept the connection and add to epoll

            if (curr_fd == listening_socket_fd) {

                int conn_sock_fd = accept(
                    listening_socket_fd,
                    (struct sockaddr *)&client_addr,
                    &client_addr_len
                );

                if (conn_sock_fd == -1) {
                    perror("accept");
                    continue;
                } else {
                    printf("[INFO] Accepted connection from client\n");
                }

                // add conn_sock to epoll to monitor
                event.events = EPOLLIN;
                event.data.fd = conn_sock_fd;

                if (epoll_ctl(epoll_fd,
                              EPOLL_CTL_ADD,
                              conn_sock_fd,
                              &event) == -1) {
                    perror("epoll_ctl");
                    close(conn_sock_fd);
                    continue;
                }
            }

            else { // event on connection socket

                // here we need to receive things from curr_fd
                // as that is the fd that is ready rn
                // receive message from client

                char buff[BUFF_SIZE];

                ssize_t read_n = recv(
                    curr_fd,
                    buff,
                    sizeof(buff) - 1,
                    0
                );

                if (read_n <= 0) {

                    // client has disconnected or recv encountered an error
                    epoll_ctl(
                        epoll_fd,
                        EPOLL_CTL_DEL,
                        curr_fd,
                        NULL
                    );

                    close(curr_fd);

                    printf("[INFO] Client closed the connection\n");

                    // remove exit as even if client disconnects server needs to serve
                    // other clients
                    // exit(1);

                } else {

                    buff[read_n] = '\0';

                    printf("[CLIENT MESSAGE] %s", buff);

                    // reverse the string
                    strrev(buff);

                    // send the reversed string back to client
                    ssize_t sent_n = send(
                        curr_fd,
                        buff,
                        read_n,
                        0
                    );

                    if (sent_n == -1) {
                        perror("send");

                        epoll_ctl(
                            epoll_fd,
                            EPOLL_CTL_DEL,
                            curr_fd,
                            NULL
                        );

                        close(curr_fd);
                    }
                }
            }
        }
    }

    close(epoll_fd);
    close(listening_socket_fd);

    return 0;
}