#include <stdio.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <arpa/inet.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>


#define MAX_ARGS 1024
#define FAIL 0
#define PASS 1

extern int errno;


typedef struct {
    char name[512];
    int size;
} File_header;


int connect_server(int argc, char* argv[]) {
    socklen_t len;
    struct sockaddr_in servAdd;
    int portNumber, client_socket;

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
    return client_socket;
}

int check_name(char *files[], char *command) {
    if(strcmp(files[0], command)) {
        fprintf(stderr, "Invalid command name\n");
        return FAIL;
    }
    return PASS;
}


int verify_path(char *name) {
    if(!access(name, F_OK))
        return PASS;
    fprintf(stderr, "Invalid filepath\n");
    return FAIL;
}


int verify_ext(char *name) {
    char *index = strrchr(name, '.');
    if (!index || !(!strcmp(index, ".pdf") || !strcmp(index, ".c") || !strcmp(index, ".txt") || !strcmp(index, ".zip"))) {
        fprintf(stderr, "Invalid file types\n");
        return FAIL;
    }
    return PASS;
}


int check_file_path_and_ext(char *files[], int count) {
    	int check = 1;
	for(int i=1; i<count-1; i++) {
        	if(!(verify_ext(files[i]) && verify_path(files[i]))) {
			return FAIL;
		}
    	}
    return PASS;
}


int parse_command_up(char *input, char* files[], int *i) {
    
    *i=0;
    char *inputcopy = strdup(input);
    
    char *token = strtok(inputcopy, " ");
    while(token != NULL) {
        files[(*i)++] = strdup(token);
        token = strtok(NULL, " ");
    }

    if(strncmp(files[*i - 1], "~/S1", 4)) {
        fprintf(stdout, "Invalid server addr\n");
        return 0;
    }


    if(*i < 3) {
        fprintf(stderr, "Not enough arguments\n");
        return FAIL;
    }

    return (check_name(files, "uploadf") && check_file_path_and_ext(files, *i));
}


void send_file(int cl_s, char *name) {
    
    int bytes_read, total_sent;
    char buffer[1024];

    int fd = open(name, O_RDONLY);

    while ((bytes_read = read(fd, buffer, 1024)) > 0) {
        if (send(cl_s, buffer, bytes_read, MSG_NOSIGNAL) != bytes_read) {
            if (errno == EPIPE) {
                fprintf(stderr, "Broken pipe detected - connection closed by server\n");
                exit(0);
            }
        }
        total_sent+=bytes_read;
    }
    fprintf(stdout, "File name: %s, Total bytes: %d\n", name, total_sent);
}


void uploadf(int cl_s, char *input, char *files[], int *count) {

    send(cl_s, input, strlen(input), 0);
    fprintf(stdout, "Bytes sent: %d\n", strlen(input));

	for(int i=1; i < *count - 1; i++) {
        int size=0;
        struct stat st;
        
        stat(files[i], &st);
        size = st.st_size;

        send(cl_s, &size, sizeof(int), 0);
        
        fprintf(stdout, "Sent size. Name-%s, size-%d\n", files[i], size);

		send_file(cl_s, files[i]);
	}

    fprintf(stdout, "\nFiles uploaded\n");
}


void get_commands(int cl_s) {
    
    char input[MAX_ARGS];
    char *files[5] = {0};
    int count;

    while(1) {
        fprintf(stdout, "s25client$ ");
        fgets(input, MAX_ARGS, stdin);
        input[strcspn(input, "\n")] = 0;

	if(!strcmp(input, "exit")) {
		fprintf(stdout, "I am shutting down. See you next time!\n");
		exit(0);
	}

        if(strstr(input, "uploadf")) {
            if(parse_command_up(input, files, &count)) {
                uploadf(cl_s, input, files, &count);
            }
        } else if(strstr(input, "downlf")) {
            if(parse_command_up(input, files, &count)) {
                
            }
        } else if(strstr(input, "removef")) {
            if(parse_command_up(input, files, &count)) {
                
            }
        } else if(strstr(input, "downltar")) {
            if(parse_command_up(input, files, &count)) {
                
            }
        } else if(strstr(input, "dispfnames")) {
            if(parse_command_up(input, files, &count)) {
                
            }
        }
    }
}


int main(int argc, char *argv[]) {
    
    int cs = connect_server(argc, argv);

    get_commands(cs);

    return 0;
}
