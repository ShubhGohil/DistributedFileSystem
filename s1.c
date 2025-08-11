#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <pthread.h>
#include <string.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <unistd.h>

void connect_servers(int *client_socket, char **server_ip, int *server_port) {
    socklen_t len;
    struct sockaddr_in servAdd;
    int portNumber;

    for(int i=0; i<3; i++) {
        if ((client_socket[i]=socket(AF_INET,SOCK_STREAM,0))<0){ //socket()
            fprintf(stderr, "Cannot create socket\n");
            exit(1);
        }

        //ADD the server's PORT NUMBER AND IP ADDRESS TO THE sockaddr_in object
        servAdd.sin_family = AF_INET; //Internet 
//        sscanf(server_port[i], "%d", &portNumber);
        portNumber = server_port[i];

        //htons is used to convert host byte order into network byte order
        servAdd.sin_port = htons((uint16_t)portNumber);//Host to network short (htons) 

        //inet_pton() is used to convert the IP address in text into binary
        //internet presentation to numeric  
        if(inet_pton(AF_INET, server_ip[i], &servAdd.sin_addr) < 0){
            fprintf(stderr, " inet_pton() has failed\n");
            exit(2);
        }


        if(connect(client_socket[i], (struct sockaddr  *) &servAdd,sizeof(servAdd))<0){//Connect()
            fprintf(stderr, "connect() failed, exiting\n");
            exit(3);
        }
    }

    // return client_socket;
}

int split_command(char *buffer, char** result) {
    
    char *token = strtok(buffer, " \t");
    int count = 0;

    while(token != NULL) {
        result[count++] = strdup(token);
        if(result[count - 1] == NULL) {
            fprintf(stderr, "Memory allocation in strdup failed\n");
            exit(2);
        }
        token = strtok(NULL, " \t");
    }

    return count;
}


void recv_file(int co_s, char *name, char *path) {

    char buffer[1024];
    int bytes_received;
    int total_received = 0;
    
    chdir(path);

    int fd = open(name, O_CREAT | O_WRONLY, 0644);

    while ((bytes_received = recv(co_s, buffer, 1024, 0)) > 0) {
        if (write(fd, buffer, bytes_received) != bytes_received) {
            perror("write failed");
            break;
        }
        total_received += bytes_received;
    }

    if (bytes_received == 0) {
        printf("Connection closed by sender\n");
    } else if (bytes_received < 0) {
        perror("recv failed");
    }

    printf("File %s created. Total %d bytes written.", name, total_received);

}


char* get_abs_path(char *path) {
    char *home = getenv("HOME");
    char *expand = malloc(strlen(home) + strlen(path));

    strcpy(expand, home);
    strcat(expand, path + 1);

    return expand;
}

char* create_dest(char *path) {
    
    char command[256];
    char *abs_path = get_abs_path(path);

    snprintf(command, "mkdir -p %s", abs_path);
    system(command);

    return abs_path;
}


void uploadf(int connection_socket, int count, char *tokens[]) {
    char *destination_path = tokens[count - 1];

    char *path = create_dest(destination_path);

    for(int i=1; i<count-1; i++) {
        recv_file(connection_socket, tokens[i], path);
    }

}

void prcclient(int conn_soc, int *cl_s) {
    char buff[1024];
    char* tokens[10];
    int bytes_read;
    
    while(1) {
        // get the command from client
        bytes_read = recv(conn_soc, buff, 1024, 0);
        buff[bytes_read] = '\0';

        printf("Received commmand: %s", buff);

        int count = split_command(buff, tokens);

        if(!strcmp(tokens[0], "uploadf")) {

            uploadf(conn_soc, count, tokens);

        } else if(!strcmp(tokens[0], "downlf")) {

            // downlf(conn_soc, count, tokens);

        } else if(!strcmp(tokens[0], "removef")) {

            // removef(conn_soc, count, tokens);

        } else if(!strcmp(tokens[0], "downltar")) {

            // downltar(conn_soc, count, tokens);

        } else if(!strcmp(tokens[0], "dispfnames")) {

            // dispfnames(conn_soc, count, tokens);

        }
    }
}


void connect_client(int *client_socket) {
    int ls, cs, csd, portNumber, pid, port;
    struct sockaddr_in servAdd;
    socklen_t len = sizeof(servAdd);
    char ip_str[INET6_ADDRSTRLEN];
    struct sockaddr_storage client_addr;

    socklen_t client_addr_len = sizeof(client_addr);


    if ((ls=socket(AF_INET,SOCK_STREAM,0))<0){
        fprintf(stderr, "Cannot create socket\n");
        exit(1);
    }

    servAdd.sin_family = AF_INET;
    // servAdd.sin_addr.s_addr = htonl(INADDR_ANY);
    servAdd.sin_addr.s_addr = inet_addr("127.0.0.1");
    sscanf("5088", "%d", &portNumber);
    servAdd.sin_port = htons((uint16_t)portNumber);

    bind(ls,(struct sockaddr*)&servAdd,sizeof(servAdd));

    listen(ls, 5);

    if (getsockname(ls, (struct sockaddr *)&servAdd, &len) == 0) {
        inet_ntop(AF_INET, &servAdd.sin_addr, ip_str, INET_ADDRSTRLEN);
        printf("Server listening on IP: %s, Port: %d\n", ip_str, ntohs(servAdd.sin_port));
    } else {
        perror("getsockname failed");
    }

    while(1){

    
        // int client_socket_fd = accept(server_socket_fd, (struct sockaddr *)&client_addr, &client_addr_len);
        if((cs = accept(ls, (struct sockaddr*)&client_addr, &client_addr_len)) == -1) {
            fprintf(stderr, "Cannot accept client connections\n");
            exit(1);
        }

        if (client_addr.ss_family == AF_INET) {
            struct sockaddr_in *s = (struct sockaddr_in *)&client_addr;
            port = ntohs(s->sin_port);
            inet_ntop(AF_INET, &s->sin_addr, ip_str, sizeof(ip_str));
        } else { // AF_INET6
            struct sockaddr_in6 *s = (struct sockaddr_in6 *)&client_addr;
            port = ntohs(s->sin6_port);
            inet_ntop(AF_INET6, &s->sin6_addr, ip_str, sizeof(ip_str));
        }
        printf("Client connected: %s:%d\n", ip_str, port);

        if((pid = fork()) == 0) {
            prcclient(cs, client_socket);
            exit(0);
        } else if(pid < 0) {
            fprintf(stderr, "Fork failed\n");
            exit(1);
        }
    }
 
}

int main(int argc, char *argv[]) {

    char* s_ip[3] = {"127.0.0.1", "127.0.0.1", "127.0.0.1"};
    int s_port[3] = {5084, 5085, 5086};

    int *client_s = (int *)malloc(3 * sizeof(int));

    // connect_servers(client_s, s_ip, s_port);

    connect_client(client_s);

    free(client_s);
   
    return 0;
}
