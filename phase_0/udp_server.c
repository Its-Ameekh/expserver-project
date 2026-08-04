#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <unistd.h>
#include <netdb.h>
#include <pthread.h>


#define PORT 8080
#define BUFF_SIZE 10000

typedef struct client_data{
    char message[BUFF_SIZE];
    struct sockaddr_in client_addr;
    int sockfd;
    socklen_t addr_len;
}client_data_t;

void strrev(char *str) {
  for (int start = 0, end = strlen(str) - 2; start < end; start++, end--) {
    char temp = str[start];
    str[start] = str[end];
    str[end] = temp;
  }
}

//lets create the fn ie what we want the thread to do

void * handle_client(void * arg){
    //why this has to be ptr why not normal client_data_t
    //so that 
    client_data_t * data=(client_data_t *)arg;
    printf("[CLIENT MESSAGE]:%s",data->message);

    strrev(data->message);

    sendto(data->sockfd,data->message,strlen(data->message),0,(struct sockaddr *)&(data->client_addr),data->addr_len);

    free(data);
    pthread_exit(NULL);

}

int main(){


    int listening_sock_fd=socket(AF_INET,SOCK_DGRAM,0);
    //study how EXIT_FAILURE WORKS
    
    if(listening_sock_fd<0){
        perror("socket:");
        exit(EXIT_FAILURE);
    }

    //SETTING UDP SERVER ADDR TO BIND

    //donot forget host to network 
    struct sockaddr_in server_addr;
    server_addr.sin_port=htons(PORT);
    server_addr.sin_family=AF_INET;
    server_addr.sin_addr.s_addr=htonl(INADDR_ANY);

    int enable=1;
    //before binding need to make reuase addr fro easy development
    setsockopt(listening_sock_fd,SOL_SOCKET,SO_REUSEADDR,&enable,sizeof(enable));

    //now we bind

    if(bind(listening_sock_fd,(struct sockaddr *)&server_addr,sizeof(server_addr))==-1){
        perror("bind:");
        exit(EXIT_FAILURE);
    }

    //acknowledge the bind
    printf("[INFO] server listening on port %d\n",PORT);

    //now udp server readdy to receive messages
    //ok udp dont have connections it just receives
    //packets from clients so data is the only thing that
    //gets here so no need of client loop
    //ie the loop where we send each client
    //to a new process or thread no need
    //so think what else we need??
    //a new thread to reverse and send each incomimg datagram


    //doubt what if our text exceeds MTU?

    while(1){
        char buff[BUFF_SIZE];
        //client data variaible to save incoming client data
        struct sockaddr_in client_addr;
        socklen_t addr_len=sizeof(client_addr);
        //in recvfrom fn we get client addr details in the address we give as client addr
        //
        ssize_t n = recvfrom(listening_sock_fd,buff,BUFF_SIZE,0,(struct sockaddr *)&client_addr,&addr_len);
        if (n < 0) {
            perror("recvfrom");
            continue;
        }
        //add terminating null character to receive string
        buff[n]='\0';

        //now what happened we rech here only after a udp packet has been received
        //since recvfrom is blocking which effectivey means need to  process this 
        //so send it to a thread to finish so i think effectivly our pgm is not
        //a threaed per client it is a thread per udp packet
        

        //now  we prepare all data we need for helper fn to work

        client_data_t* data = (client_data_t*)malloc(sizeof(client_data_t));
        strcpy(data->message,buff);
        data->client_addr=client_addr;
        data->sockfd=listening_sock_fd;
        data->addr_len=addr_len;
        pthread_t thread_id;
        pthread_attr_t attr;

        

        // Initialize attributes
        pthread_attr_init(&attr);

        // Set thread to detached state
        pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);


        if (pthread_create(&thread_id, &attr, handle_client, (void*)data) != 0) {
            perror("Failed to create thread");
            free(data);
        }

        // Destroy attribute object
        pthread_attr_destroy(&attr);


        
    }
    //close socket unreacheble in abv inifnite loop
    close(listening_sock_fd);

    return 0;


}