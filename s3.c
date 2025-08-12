#define _DEFAULT_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <sys/stat.h>
#include <dirent.h>


#define NAME "S3"

extern int errno;

typedef struct {
    char *name[512];
    int count;
} File_names;


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

    char *pos = strstr(abs_path, "S1");
    if(pos != NULL) {
        pos[1] = '3';
    }

    snprintf(command, sizeof(command), "mkdir -p %s", abs_path);
    system(command);

    return abs_path;
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


void uploadf(int connection_socket, int count, char *tokens[]) {
    int file_size = 0;

    char *destination_path = tokens[count - 1];

    char *path = create_dest(destination_path);

    fprintf(stdout, "%s\n", path);
    int bytes_received = recv(connection_socket, &file_size, sizeof(int), 0);
    fprintf(stdout, "Size of file %s received is %d\n", tokens[1], file_size);
    fflush(stdout);

    recv_file(connection_socket, tokens[1], path, file_size);
    fprintf(stdout, "Files received from uploadf\n");
}


void removef(int connection_socket, int count, char *tokens[]) {
    int file_size = 0, res=0;

    char *destination_path = tokens[count - 1];

    char *abs_path = get_abs_path(destination_path);

    char *pos = strstr(abs_path, "S1");
    if(pos != NULL) {
        pos[1] = '3';
    }

    if(remove(abs_path) == 0) {
        res = 1;
        fprintf(stdout, "File %s deleted successfully\n", abs_path);
        send(connection_socket, &res, sizeof(int), 0);
    } else {
        fprintf(stdout, "Error deleting file\n");
        send(connection_socket, &res, sizeof(int), 0);
    }

}


void downltar(int connection_socket, int count, char *token[]) {

    char cmd[512];
    char *tar_name = "txt.tar";
    char *base_dir = malloc(strlen(getenv("HOME") + strlen("/S3")));
    struct stat st;

    strcpy(base_dir, getenv("HOME"));
    strcat(base_dir, "/S3");
    fprintf(stdout, "Basedir: %s\n", base_dir);

    snprintf(cmd, sizeof(cmd), "tar -cvf %s/%s -C %s .", base_dir, tar_name, base_dir);
    fprintf(stdout, "cmd: %s\n", cmd);
    
    int ret = system(cmd);
    fprintf(stdout, "ret value: %d\n", ret);


    char *tar_path = malloc(strlen(base_dir) + strlen(tar_name));
    strcpy(tar_path, base_dir);
    strcat(tar_path, "/");
    strcat(tar_path, tar_name);
    fprintf(stdout, "Tarpath: %s\n", tar_path);

    stat(tar_path, &st);
    int size = st.st_size;

    send(connection_socket, &size, sizeof(int), 0);
    fprintf(stdout, "Tarfile size %d sent to S1\n", size);

    send_file(connection_socket, tar_path);

    if(remove(tar_path) == 0) {
        fprintf(stdout, "File %s removed successfully\n", tar_name);
    } else {
        fprintf(stdout, "Error while removing file %s\n");
    }

    free(base_dir);
}


int remove_dir(const struct dirent *entry) {
    return entry->d_type == DT_REG; // Keep only regular files
}


void dispfnames(int connection_socket, int count, char *token[]) {

    struct dirent **fname;
    int n;

    File_names f = { { NULL }, 0};

    char *abs_path = malloc(strlen(token[1]) + strlen(getenv("HOME")));

    strcpy(abs_path, getenv("HOME"));
    strcat(abs_path, "/");
    strcat(abs_path, token[1]);

    char *pos = strstr(abs_path, "S1");
    if(pos != NULL) {
        pos[1] = '3';
    }

    n = scandir(abs_path, &fname, remove_dir, alphasort);
    if (n < 0) {
        perror("scandir");
        n = 0;
    }

    for(int i=0; i<n; i++) {
        f.name[i] = strdup(fname[i]->d_name);
        f.count++;
    }

    send(connection_socket, &f, sizeof(f), 0);
    fprintf(stdout, "File name struct with size %d sent to S1\n", sizeof(f));

    for (int i = 0; i < n; i++) {
        free(fname[i]);
    }
    free(fname);
}


int main() {
    int ls, cs, csd, portNumber, pid, port;
    socklen_t len;
    struct sockaddr_in servAdd;
    char ip_str[INET6_ADDRSTRLEN];
    struct sockaddr_storage client_addr;

    socklen_t client_addr_len = sizeof(client_addr);

    if ((ls=socket(AF_INET,SOCK_STREAM,0))<0){
        fprintf(stderr, "Cannot create socket\n");
        exit(1);
    }

    servAdd.sin_family = AF_INET;
    servAdd.sin_addr.s_addr = inet_addr("127.0.0.1");
    sscanf("15085", "%d", &portNumber);
    servAdd.sin_port = htons((uint16_t)portNumber);

    bind(ls,(struct sockaddr*)&servAdd,sizeof(servAdd));

    listen(ls, 5);

    if (getsockname(ls, (struct sockaddr *)&servAdd, &len) == 0) {
        inet_ntop(AF_INET, &servAdd.sin_addr, ip_str, INET_ADDRSTRLEN);
        fprintf(stdout, "Server listening on IP: %s, Port: %d\n", ip_str, ntohs(servAdd.sin_port));
    } else {
        perror("getsockname failed");
    }

    if((cs = accept(ls, (struct sockaddr*)NULL, NULL)) == -1) {
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

    char command[1024];
    char* tokens[3];
    int bytes_read;
    while(1) {
        bytes_read = recv(cs, command, 1024, 0);
        command[bytes_read] = '\0';

        fprintf(stdout, "Received command %s\n", command);

        int count = split_command(command, tokens);

        if(!strcmp(tokens[0], "uploadf")) {

            uploadf(cs, count, tokens);

        } else if (!strcmp(tokens[0], "downlf"))
        {
            /* code */

        } else if (!strcmp(tokens[0], "removef"))
        {
            removef(cs, count, tokens);

        } else if (!strcmp(tokens[0], "downltar"))
        {
            downltar(cs, count, tokens);

        } else if (!strcmp(tokens[0], "displayfname"))
        {
            dispfnames(cs, count, tokens);
            
        }
    }

    return 0;
}