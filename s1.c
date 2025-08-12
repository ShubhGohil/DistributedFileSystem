#define _DEFAULT_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <string.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <sys/stat.h>
#include <dirent.h>


extern int errno;

typedef struct {
    char *name[512];
    int count;
} File_names;


void connect_servers(int *client_socket, char **server_ip, int *server_port) {
    socklen_t len;
    struct sockaddr_in servAdd;
    int portNumber;

    for(int i=0; i<3; i++) {

        printf("Server IP: %s:%d\n", server_ip[i], server_port[i]);

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

        if(connect(client_socket[i], (struct sockaddr *) &servAdd,sizeof(servAdd))<0){//Connect()
            fprintf(stderr, "connect() failed, exiting %s\n", strerror(errno));
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


void send_file(int cl_s, char *name) {
    int bytes_read;
    int total_sent = 0;
    char buffer[1024];

    int fd = open(name, O_RDONLY);
    if (fd < 0) {
        perror("open failed");
        return;
    }

    while ((bytes_read = read(fd, buffer, sizeof(buffer))) > 0) {
        int sent = send(cl_s, buffer, bytes_read, MSG_NOSIGNAL);
        if (sent != bytes_read) {
            if (sent < 0 && errno == EPIPE) {
                fprintf(stderr, "Broken pipe detected - connection closed by server\n");
                close(fd);
                return;
            }
            perror("send failed");
            close(fd);
            return;
        }
        total_sent += sent;
    }

    if (bytes_read < 0) {
        perror("read failed");
    }

    fprintf(stdout, "File name: %s, Total bytes: %d\n", name, total_sent);
    close(fd);
}


void recv_file(int co_s, char *name, char *path, int file_size) {

    char buffer[1024];
    int bytes;
    int total_received = 0;

    chdir(path);

    int fd = open(name, O_CREAT | O_WRONLY, 0644);

    while (total_received < file_size) {
        // Calculate how much to receive (don't exceed buffer size)
        int to_receive = 1024;
        if (file_size - total_received < 1024) {
            to_receive = file_size - total_received;
        }
        
        bytes = recv(co_s, buffer, to_receive, 0);
        
        // Handle errors and connection close
        if (bytes <= 0) {
            if (bytes == 0) {
                fprintf(stdout, "Connection closed by peer\n");
            } else {
                perror("recv failed");
            }
            break;
        }
        
        write(fd, buffer, bytes);
        total_received += bytes;
    }

    if (bytes == 0) {
        fprintf(stdout, "Connection closed by sender\n");
    } else if (bytes < 0) {
        perror("recv failed");
    }

    fprintf(stdout, "File %s created. Total %d bytes written.\n", name, total_received);

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

    snprintf(command, sizeof(command), "mkdir -p %s", abs_path);
    system(command);

    return abs_path;
}

void tr_file(int *other_server, char *fname, int size, char *path, int server_number) {

    char command[128];

    snprintf(command, sizeof(command), "uploadf %s %s", fname, path);
    send(other_server[server_number - 2], command, strlen(command), 0);
    fprintf(stdout, "Command %s sent to S%d\n", command, server_number);

    send(other_server[server_number - 2], &size, sizeof(int), 0);
    fprintf(stdout, "Bytes %d to be sent to S%d\n", size, server_number);

    char *full_path = malloc(strlen(fname) + strlen(path) + 1);

    strcpy(full_path, path);
    strcat(full_path, "/");
    strcat(full_path, fname);

    fprintf(stdout, "Full path: %s\n", full_path);

    send_file(other_server[server_number - 2], get_abs_path(full_path));
    fprintf(stdout, "File %s uploaded to S%d\n", fname, server_number);

    if (remove(get_abs_path(full_path)) == 0) {
        printf("File '%s' deleted successfully.\n", full_path);
    } else {
        perror("Error deleting file\n");
    }

}

void transfer_up(int *other_server, char *fname, int size, char *path) {
    
    char *index = strrchr(fname, '.');

    if(!strcmp(index, ".zip")) {

        tr_file(other_server, fname, size, path, 4);

    } else if(!strcmp(index, ".pdf")) {

        tr_file(other_server, fname, size, path, 2);

    } else if(!strcmp(index, ".txt")) {

        tr_file(other_server, fname, size, path, 3);

    }
}


void uploadf(int connection_socket, int count, char *tokens[], int *cl_s) {
    int file_size = 0;

    char *destination_path = tokens[count - 1];

    char *path = create_dest(destination_path);

    fprintf(stdout, "%s\n", path);
    for(int i=1; i<count-1; i++) {

        int bytes_received = recv(connection_socket, &file_size, sizeof(int), 0);
        fprintf(stdout, "Size of file %s received is %d\n", tokens[i], file_size);

        recv_file(connection_socket, tokens[i], path, file_size);

        transfer_up(cl_s, tokens[i], file_size, destination_path);
    }

    fprintf(stdout, "Files received from uploadf\n");
}


int transfer_rm(int *cl_s, char *path, int server_number) {
    char command[128];
    int result;

    snprintf(command, sizeof(command), "removef %s", path);
    send(cl_s[server_number - 2], command, strlen(command), 0);
    fprintf(stdout, "Command %s sent to S%d\n", command, server_number);

    recv(cl_s[server_number - 2], &result, sizeof(int), 0);

    return result;
}


void removef(int connection_socket, int count, char *tokens[], int *cl_s) {
    int file_size = 0, scount = 0;

    for(int i=1; i<count; i++) {

        char *index = strrchr(tokens[i], '.');

        if(!strcmp(index, ".zip")) {

            scount += transfer_rm(cl_s, tokens[i], 4);

        } else if(!strcmp(index, ".pdf")) {

            scount += transfer_rm(cl_s, tokens[i], 2);

        } else if(!strcmp(index, ".txt")) {

            scount += transfer_rm(cl_s, tokens[i], 3);

        } else {
            if(remove(get_abs_path(tokens[i])) == 0) {
                fprintf(stdout, "File %s removed successfully\n", tokens[i]);
                scount++;
            } else {
                fprintf(stdout, "Error while removing file %s\n");
            }
        }

    }

    // recv(connection_socket, &count, sizeof(int), 0);
    fprintf(stdout, "Files deleted sucessfully: %d, Error: %d\n", scount, count - scount - 1);
}


void get_tar(int *cl_s, char *command, char *fname, int s_num, int *file_size) {
    int bytes_received;

    send(cl_s[s_num-2], command, strlen(command), 0);

    bytes_received = recv(cl_s[s_num-2], file_size, sizeof(int), 0);
    fprintf(stdout, "Size of tarfile to receive is %d\n", *file_size);

    char *path = malloc(strlen(getenv("HOME")) + strlen("/S1"));

    strcpy(path, getenv("HOME"));
    strcat(path, "/S1");

    recv_file(cl_s[s_num-2], fname, path, *file_size);

}


void send_tar(int cs, int *file_size, char *name) {

    char *path = malloc(strlen(getenv("HOME")) + strlen("/S1"));

    strcpy(path, getenv("HOME"));
    strcat(path, "/S1/");
    strcat(path, name);

    send(cs, file_size, sizeof(int), 0);
    fprintf(stdout, "Sent file size %d to client\n", *file_size);
    
    send_file(cs, path);

    if(remove(path) == 0) {
        fprintf(stdout, "File %s removed successfully\n", name);
    } else {
        fprintf(stdout, "Error while removing file %s\n");
    }

}


void downltar(int connection_socket, int count, char *tokens[], int *cl_s) {

    int file_size = 0;
    char *fname;
    
    if(!strcmp(tokens[1], ".txt")) {
        
        fname = strdup("txt.tar");
        fprintf(stdout, "Going to send command downltar to S3\n");

        get_tar(cl_s, tokens[0], fname, 3, &file_size);
        send_tar(connection_socket, &file_size, fname);

    } else if(!strcmp(tokens[1], ".pdf")) {

        fname = strdup("pdf.tar");
        fprintf(stdout, "Going to send command downltar to S2\n");

        get_tar(cl_s, tokens[0], fname, 2, &file_size);
        send_tar(connection_socket, &file_size, fname);

    } else {

        char cmd[512];
        char *tar_name = "cfiles.tar";
        char *base_dir = malloc(strlen(getenv("HOME") + strlen("/S1")));
        struct stat st;

        strcpy(base_dir, getenv("HOME"));
        strcat(base_dir, "/S1");

        snprintf(cmd, sizeof(cmd), "tar -cvf %s -C %s %s", tar_name, base_dir, base_dir);
        system(cmd);

            
        char *tar_path = malloc(strlen(base_dir) + strlen(tar_name));
        strcpy(tar_path, base_dir);
        strcat(tar_path, "/");
        strcat(tar_path, tar_name);

        stat(tar_path, &st);
        int size = st.st_size;

        send(connection_socket, &size, sizeof(int), 0);
        fprintf(stdout, "Sent file size %d to client\n", size);

        send_tar(connection_socket, &size, tar_path);

        free(base_dir);
    }

}


int remove_dir(const struct dirent *entry) {
    return entry->d_type == DT_REG; // Keep only regular files
}


void dispfnames(int connection_socket, int count, char *token[], int *cl_s, char *buff ) {

    struct dirent **fname;
    char *path = token[1];
    int n;
    File_names f = { { NULL }, 0};

    char *abs_path = malloc(strlen(token[1]) + strlen(getenv("HOME")));

    strcpy(abs_path, getenv("HOME"));
    strcat(abs_path, "/");
    strcat(abs_path, token[1]);

    n = scandir(abs_path, &fname, remove_dir, alphasort);
    if (n < 0) {
        perror("scandir");
        return;
    }

    for(int i=0; i<n; i++) {
        f.name[i] = strdup(fname[i]->d_name);
        f.count++;
    }

    for(int i=0; i<3; i++) {
        
        File_names *other_files = malloc(sizeof(File_names));

        send(cl_s[i], buff, strlen(buff), 0);
        fprintf(stdout, "Sent command %s to S%d", buff, i+2);

        recv(cl_s[i], other_files, sizeof(File_names), 0);
        fprintf(stdout, "Received struct of size %d from S%d", sizeof(File_names), i+2);

        for(int j=0; j<other_files->count; j++) {
            f.name[f.count+j] = other_files->name[j];
            other_files->count++;
        }
        f.count += other_files->count;

        for(int j=0; j<other_files->count; j++) {
            free(other_files->name[j]);
        }
        free(other_files);
    }

    send(connection_socket, &f, sizeof(f), 0);

    for (int i = 0; i < n; i++) {
        free(fname[i]);
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

        fprintf(stdout, "%d\n", bytes_read);

        fprintf(stdout, "Received commmand: %s\n", buff);

        int count = split_command(buff, tokens);

        if(!strcmp(tokens[0], "uploadf")) {

            uploadf(conn_soc, count, tokens, cl_s);

        } else if(!strcmp(tokens[0], "downlf")) {

            // downlf(conn_soc, count, tokens);

        } else if(!strcmp(tokens[0], "removef")) {

            removef(conn_soc, count, tokens, cl_s);

        } else if(!strcmp(tokens[0], "downltar")) {

            downltar(conn_soc, count, tokens, cl_s);

        } else if(!strcmp(tokens[0], "dispfnames")) {

            dispfnames(conn_soc, count, tokens, cl_s, buff);

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
    sscanf("15088", "%d", &portNumber);
    servAdd.sin_port = htons((uint16_t)portNumber);

    bind(ls,(struct sockaddr*)&servAdd,sizeof(servAdd));

    listen(ls, 5);

    if (getsockname(ls, (struct sockaddr *)&servAdd, &len) == 0) {
        inet_ntop(AF_INET, &servAdd.sin_addr, ip_str, INET_ADDRSTRLEN);
        fprintf(stdout, "Server listening on IP: %s, Port: %d\n", ip_str, ntohs(servAdd.sin_port));
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
        fprintf(stdout, "Client connected: %s:%d\n", ip_str, port);

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
    int s_port[3] = {15084, 15085, 15086};

    int *client_s = (int *)malloc(3 * sizeof(int));

    connect_servers(client_s, s_ip, s_port);

    connect_client(client_s);

    free(client_s);   
    return 0;
}
