#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#define SERVER_PORT 8080
#define BUFF_SIZE 10000

int main(void){
    //crete socket for listeinng
    int client_sock_fd=socket(AF_INET,SOCK_STREAM,0);

    //Now before connecting to server
    //need to know address of server
    struct sockaddr_in server_addr;

    server_addr.sin_family=AF_INET;
    server_addr.sin_port=htons(SERVER_PORT);
    server_addr.sin_addr.s_addr=inet_addr("127.0.0.1");

    //Now connect to server

    if(connect(client_sock_fd,(struct sockaddr *)&server_addr,sizeof(server_addr))<0){
        perror("connect failed");
        exit(EXIT_FAILURE);
    }else{
        printf("[INFO] Connected to tcp server\n");
    }

    //message sending and receiving

    while(1){
        //no need of buffer only need the linepttr
        //to hold the line read from stdin
        char *lineptr=NULL;
        size_t lineptr_len=0;
        ssize_t read_n=getline(&lineptr,&lineptr_len,stdin);
        if(read_n==-1){
            perror("getline");
            exit(EXIT_FAILURE);
        }else{
            //send the line to server
            send(client_sock_fd,lineptr,read_n,0);

            //now wait for response from server
            char buff[BUFF_SIZE];
            read_n = recv(client_sock_fd,buff,sizeof(buff),0);
            if(read_n==-1){
                perror("recv");
                exit(EXIT_FAILURE);
            }else{
                buff[read_n]='\0';
                printf("[SERVER MESSAGE] %s", buff);
            }
            free(lineptr);

        }



    }
    return 0;

}