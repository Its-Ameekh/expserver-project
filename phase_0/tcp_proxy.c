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
#define UPSTREAM_PORT 3000
#define MAX_SOCKS 10

//lets modularise the previous epoll code

//module to create an epoll instance

int epoll_fd,listen_sock_fd;
struct epoll_event events[MAX_EPOLL_EVENTS];
int route_table[MAX_SOCKS][2], route_table_size = 0;

void epoll_attach(int epoll_fd,int fd,int events){
    //fn used to attach a fd to an epoll instance
    struct epoll_event event;

    event.events = events;
    event.data.fd = fd;

    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, fd, &event) == -1) {
        perror("epoll_ctl");
        exit(EXIT_FAILURE);
    }
}

int create_loop(){
    epoll_fd=epoll_create1(0);
    if(epoll_fd==-1){
        perror("epoll_create");
        exit(EXIT_FAILURE);
    }
    return epoll_fd;
}

int connect_upstream(){
    //pyhron server uses tcp
    int upstream_sock_fd=socket(AF_INET,SOCK_STREAM,0);
    struct sockaddr_in upstream_addr;
    upstream_addr.sin_family=AF_INET;
    upstream_addr.sin_port=htons(UPSTREAM_PORT);
    upstream_addr.sin_addr.s_addr=inet_addr("127.0.0.1");

    if(connect(upstream_sock_fd,(struct sockaddr *)&upstream_addr,sizeof(upstream_addr))==-1){
        perror("connect");
        close(upstream_sock_fd);
        return -1;
    }
    return upstream_sock_fd;
    //the tcp connection on upstream server is handled by the python server
}

void accept_connection(int listening_socket_fd){
    struct sockaddr_in client_addr;
    socklen_t client_len=sizeof(client_addr);
    int conn_sock_fd=accept(listening_socket_fd,(struct sockaddr *)&client_addr,&client_len);

    if(conn_sock_fd==-1){
    perror("accept");
    return;
    }
    //need to add this new client sock also to epoll to monitor
    epoll_attach(epoll_fd,conn_sock_fd,EPOLLIN);

    //now we need to remember our code is a proxy server
    //so this new connection we need to connect to upstream server
    //lets connect to python server
    int upstream_sock_fd=connect_upstream();
    if(upstream_sock_fd==-1){
    close(conn_sock_fd);
    return;
    }
    //need to monitor this socket also
    epoll_attach(epoll_fd,upstream_sock_fd,EPOLLIN);
    //now we have a  connection socket and a corresponding upstream socket lets store in 
    //routing table

    route_table[route_table_size][0] = conn_sock_fd;
    route_table[route_table_size][1] = upstream_sock_fd;

    route_table_size++;

}

void handle_client(int conn_sock_fd) {

    char buff[BUFF_SIZE];

    int read_n = recv(conn_sock_fd,buff,sizeof(buff),0);

    // client closed connection or error occurred
    if (read_n <= 0) {
        close(conn_sock_fd);
        return;
    }

    // print client message
    printf("[CLIENT MESSAGE] %.*s", read_n, buff);
    //.*s to make only print the first n characters

    // find the right upstream socket from the route table
    int upstream_sock_fd;

    for (int i = 0; i < route_table_size; i++) {
        if (route_table[i][0] == conn_sock_fd) {
            upstream_sock_fd = route_table[i][1];
            break;
        }
    }

    // sending client message to upstream
    int bytes_written = 0;
    int message_len = read_n;

    while (bytes_written < message_len) {

        int n = send(upstream_sock_fd,buff + bytes_written,message_len - bytes_written,0);

        bytes_written += n;
    }
}

void handle_upstream(int upstream_sock_fd) {

    char buff[BUFF_SIZE];

    int read_n = recv(upstream_sock_fd,buff,sizeof(buff),0);

    // Upstream closed connection or error occurred
    if (read_n <= 0) {
        close(upstream_sock_fd);
        return;
    }

    // find the right client socket from the route table
    int conn_sock_fd;

    for (int i = 0; i < route_table_size; i++) {
        if (route_table[i][1] == upstream_sock_fd) {
            conn_sock_fd = route_table[i][0];
            break;
        }
    }

    // send upstream message to client
    int bytes_written = 0;
    int message_len = read_n;

    while (bytes_written < message_len) {

        int n = send(
            conn_sock_fd,
            buff + bytes_written,
            message_len - bytes_written,
            0
        );

        bytes_written += n;
    }
}

void loop_run(int epoll_fd){
    while(1){
        printf("[DEBUG] epoll wait\n");
        
        int n_ready_fds = epoll_wait(epoll_fd,events,MAX_EPOLL_EVENTS,-1);

        if (n_ready_fds == -1) {
            perror("epoll_wait");
            exit(EXIT_FAILURE);
        }

        for(int i=0;i<n_ready_fds;i++){
            //curr_fd is the fd that has an event occuring on it rn in the epoll
            int curr_fd = events[i].data.fd;
            //if event occured on listening socket
            if(curr_fd==listen_sock_fd){
                //means new client connection so accept it
                accept_connection(listen_sock_fd);
            }else{
                for (int j = 0; j < route_table_size; j++){
                    if (route_table[j][0] == curr_fd){
                        // fd is a client socket
                        handle_client(curr_fd);
                        break;
                    }

                    if (route_table[j][1] == curr_fd){
                        // fd is an upstream socket
                        handle_upstream(curr_fd);
                        break;
                    }
                }
            }
        }
    }
}

int create_server(){
    int server_sock=socket(AF_INET,SOCK_STREAM,0);

    //now socket need to be bound to adddr before that remove timewait

    int enable = 1;

    if (setsockopt(server_sock,SOL_SOCKET,SO_REUSEADDR,&enable,sizeof(enable)) == -1) {
        perror("setsockopt");
        close(server_sock);
        exit(EXIT_FAILURE);
    }

    struct sockaddr_in server_addr;

    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(PORT);

    if (bind(server_sock,(struct sockaddr *)&server_addr,sizeof(server_addr)) == -1) {
        perror("bind");
        close(server_sock);
        exit(EXIT_FAILURE);
    }

    if (listen(server_sock, MAX_ACCEPT_BACKLOG) == -1) {
        perror("listen");
        close(server_sock);
        exit(EXIT_FAILURE);
    }
    printf("[INFO] Server listening on port 8080\n");

    return server_sock;
}

int main(){
    epoll_fd = create_loop();

    listen_sock_fd=create_server();

    epoll_attach(epoll_fd,listen_sock_fd,EPOLLIN);

    loop_run(epoll_fd);
}