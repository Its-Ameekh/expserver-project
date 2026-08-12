#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <unistd.h>
#include <netdb.h>

#define PORT 8080
#define BUFF_SIZE 10000
#define MAX_ACCEPT_BACKLOG 5


void strrev(char *str) {
  for (int start = 0, end = strlen(str) - 2; start < end; start++, end--) {
    char temp = str[start];
    str[start] = str[end];
    str[end] = temp;
  }
}

int main(void){
    //we create a listening socket
    int listening_socket_fd = socket(AF_INET, SOCK_STREAM, 0);
    if(listening_socket_fd==-1){
        perror("socket");
        //perror reads errno from operating system and prints a human readable error message to stderr
        //here we get socket: "Permission denied or something"
        //notice we gave the "socket"
        exit (1);
        //exit(0)means success, exit(1) means failure, 
        //difference from retrun is that this can exit pgm even from deep inside a function call not just from main
    }

    //setting soekt can be  reused immediately after program exits
    //before we bind the socket to a port, we set the SO_REUSEADDR option on the socket
    int enable=1;
    setsockopt(listening_socket_fd,SOL_SOCKET,SO_REUSEADDR,&enable,sizeof(enable));

    struct sockaddr_in server_addr;
    server_addr.sin_family=AF_INET;
    server_addr.sin_port=htons(PORT);
    server_addr.sin_addr.s_addr=htonl(INADDR_ANY);

    //bind the socket to the port

    bind(listening_socket_fd,(struct sockaddr*)&server_addr,sizeof(server_addr));

    //listen for incoming connections

    listen(listening_socket_fd, MAX_ACCEPT_BACKLOG);

    printf("[INFO] Server listening on port %d\n", PORT);

    //to store the client address information when accepting a connection, 
    //we need to create a sockaddr_in structure and a socklen_t variable to hold the length of the structure.
    // This is necessary because the accept() function requires these parameters to fill in the client's address information.

    struct sockaddr_in client_addr;
    socklen_t client_addr_len=sizeof(client_addr);

    //accept a connection from a client
    
    while (1) {
    // Create buffer to store client message
        int conn_sock_fd=accept(listening_socket_fd,(struct sockaddr*)&client_addr,&client_addr_len);
        if(conn_sock_fd==-1){
            perror("accept");
            exit(1);
        }else{
            printf("[INFO] Accepted connection from client\n");
        }
        while(1){
        //receive message from client
        char buff[BUFF_SIZE];
        ssize_t read_n=recv(conn_sock_fd,buff,sizeof(buff),0);
        if(read_n==-1){
            perror("recv");
            exit(1);
        }else if(read_n>0){
            buff[read_n]='\0';
            printf("[CLIENT MESSAGE] %s", buff);
            //reverse the string
            strrev(buff);
            //send the reversed string back to client
            send(conn_sock_fd,buff,strlen(buff),0);
        }else{
            close(conn_sock_fd);
            printf("[INFO] Client closed the connection\n");
            exit(1);
        }
        
        } 
    }

}