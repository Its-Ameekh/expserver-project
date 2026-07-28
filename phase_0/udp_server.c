#include<pthread.h>
#include<sys/socket.h>
#include<netinet/in.h>
#include<stdlib.h>



#define PORT 8080
#define BUFF_SIZE 10000


int main(){
    //need a socekt endpoint fo server 
    int server_sock_fd=socket(AF_INET,SOCK_DGRAM,0);
    if(server_sock_fd<0){
        perror("socket");
        exit(EXIT_FAILURE);
    }

    //lets set teh address adn port for this server
    struct sockaddr_in server_addr;
    server_addr.sin_family=AF_INET;
    server_addr.sin_port=htons(PORT);
    server_addr.sin_addr.s_addr=htonl(INADDR_ANY);

    //NOW WE HAVE TO BIND THE socket endpoint we created
    //to this address
    if(bind(server_sock_fd,(struct sockaddr *)&server_addr,sizeof(server_addr))<0){
        perror("bind");
        exit(EXIT_FAILURE);
    }
    //now  udp server can receive and send messages

    //assifn read buffer
    char buffer[BUFF_SIZE];

    sssize_t read_n=recvfrom(server_addr,buffer,BUFF_SIZE,0,)

}