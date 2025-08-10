#include <stdio.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <string.h>
#include <unistd.h>

#define MAX_ARGS 1024
#define FAIL 0
#define PASS 1


int connect_server(int argc, char* argv[]) {
    socklen_t len;
    struct sockaddr_in servAdd;
    int portNumber, client_socket;

    for(int i=0; i<3; i++) {
        if ((client_socket=socket(AF_INET,SOCK_STREAM,0))<0){ //socket()
            fprintf(stderr, "Cannot create socket\n");
            exit(1);
        }

        //ADD the server's PORT NUMBER AND IP ADDRESS TO THE sockaddr_in object
        servAdd.sin_family = AF_INET; //Internet 
        sscanf(argv[2], "%d", &portNumber);

        //htons is used to convert host byte order into network byte order
        servAdd.sin_port = htons((uint16_t)portNumber);//Host to network short (htons) 

        //inet_pton() is used to convert the IP address in text into binary
        //internet presentation to numeric  
        if(inet_pton(AF_INET, argv[1], &servAdd.sin_addr) < 0){
            fprintf(stderr, " inet_pton() has failed\n");
            exit(2);
        }

        if(connect(client_socket, (struct sockaddr  *) &servAdd,sizeof(servAdd))<0){//Connect()
            fprintf(stderr, "connect() failed, exiting\n");
            exit(3);
        }
    }
    return client_socket;
}

int check_name(char *files[], int *command) {
    if(strcmp(files[0], command)) {
        fprintf(stderr, "Invalid command name\n");
        return FAIL;
    }
    return PASS;
}


int verify_path(char *name) {
    if(access(name, F_OK))
        return PASS;
    return FAIL;
}


int verify_ext(char *name) {
    char *index = strrchr(name, ".");
    (!index || !strcmp(index, ".pdf") || !strcmp(index, ".c") || !strcmp(index, ".txt") || !strcmp(index, ".zip")) {
        fprintf(stderr, "Invalid file types\n");
        return FAIL;
    }
    return PASS;
}


int check_file_path_and_ext(char *files[], int count) {
    for(int i=1; i<count-1; i++) {
        return (verify_ext(files[i]) && verify_path(files[i]));
    }
    return FAIL;
}


int parse_command_up(char *input) {
    
    int i=0;
    char *files[5] = {0};
    char *inputcopy = strdup(input);
    
    char *token = strtok(inputcopy, " ");
    while(token != NULL) {
        files[i++] = strdup(token);
        token = strtok(NULL, " ");
    }

    return (check_name(files, "uploadlf") && check_file_path_and_ext(files, i));
}


void uploadlf() {

}


void get_commands(int cl_s) {
    
    char input[MAX_ARGS];

    while(1) {
        printf("s25client$ ");
        fgets(input, MAX_ARGS, stdin);
        input[strcspn(input, "\n")] = 0;

        if(strstr(input, "uploadlf")) {
            if(parse_command_up(input)) {
                uploadlf(input);
            }
        } else if(strstr(input, "downlf")) {
            if(parse_command_up(input)) {
                
            }
        } else if(strstr(input, "removef")) {
            if(parse_command_up(input)) {
                
            }
        } else if(strstr(input, "downltar")) {
            if(parse_command_up(input)) {
                
            }
        } else if(strstr(input, "dispfnames")) {
            if(parse_command_up(input)) {
                
            }
        }
    }
}


int main(int argc, char *argv[]) {
    
    int cs = connect_server(argc, argv);

    get_commands(cs);

    return 0;
}