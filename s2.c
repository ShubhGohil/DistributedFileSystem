#define _DEFAULT_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <errno.h>
#include <dirent.h>

#define NAME "S2"

extern int errno;

// Structure to hold file names and count for directory listing
typedef struct {
    char *name[512];    // Array of file name pointers
    int count;          // Number of files
} File_names;

/*
 * Splits a command string into tokens separated by spaces or tabs
 */
int split_command(char *buffer, char** result) {
    
    char *token = strtok(buffer, " \t");
    int count = 0;

    // Tokenize the buffer and store each token
    while(token != NULL) {
        result[count++] = strdup(token);    // Duplicate token to avoid memory issues
        if(result[count - 1] == NULL) {
            fprintf(stderr, "Memory allocation in strdup failed\n");
            exit(2);
        }
        token = strtok(NULL, " \t");
    }

    return count;
}

/*
 * Converts a relative path starting with '~' to absolute path
 */
char* get_abs_path(char *path) {
    char *home = getenv("HOME");
    char *expand = malloc(strlen(home) + strlen(path));

    strcpy(expand, home);
    strcat(expand, path + 1);    // Skip the '~' character

    return expand;
}

/*
 * Creates destination directory path and converts S1 references to S2
 */
char* create_dest(char *path) {
    
    char command[256];
    char *abs_path = get_abs_path(path);

    // Replace S1 with S2 in the path (server-specific directory)
    char *pos = strstr(abs_path, "S1");
    if(pos != NULL) {
        pos[1] = '2';
    }

    // Create directory structure if it doesn't exist
    snprintf(command, sizeof(command), "mkdir -p %s", abs_path);
    system(command);

    return abs_path;
}

/*
 * Receives a file from client over socket connection
 */
void recv_file(int co_s, char *name, char *path, int file_size) {

    char buffer[1024];
    int bytes;
    int total_received = 0;

    // Change to destination directory
    chdir(path);

    // Create new file with write permissions
    int fd = open(name, O_CREAT | O_WRONLY, 0644);

    // Receive file data in chunks
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
        
        // Write received data to file
        write(fd, buffer, bytes);
        total_received += bytes;
    }

    // Report connection status
    if (bytes == 0) {
        fprintf(stdout, "Connection closed by sender\n");
    } else if (bytes < 0) {
        perror("recv failed");
    }

    fprintf(stdout, "File %s created. Total %d bytes written.\n", name, total_received);
    close(fd);
}

/*
 * Sends a file to client over socket connection
 */
void send_file(int cl_s, char *name) {
    int bytes_read;
    int total_sent = 0;
    char buffer[1024];

    // Open file for reading
    int fd = open(name, O_RDONLY);
    if (fd < 0) {
        perror("open failed");
        return;
    }

    // Read file in chunks and send over socket
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

/*
 * Handles file upload command from client
 * Protocol: Receives file size, then file data
 */
void uploadf(int connection_socket, int count, char *tokens[]) {
    int file_size = 0;

    char *destination_path = tokens[count - 1];    // Last token is destination path

    // Create destination directory and get absolute path
    char *path = create_dest(destination_path);

    fprintf(stdout, "%s\n", path);
    
    // Receive file size first
    int bytes_received = recv(connection_socket, &file_size, sizeof(int), 0);
    fprintf(stdout, "Size of file %s received is %d\n", tokens[1], file_size);
    fflush(stdout);
    
    // Receive the actual file
    recv_file(connection_socket, tokens[1], path, file_size);
    fprintf(stdout, "Files received from uploadf\n");
    
    free(path);
}

/*
 * Handles file removal command from client
 */
void removef(int connection_socket, int count, char *tokens[]) {
    int file_size = 0, res = 0;

    char *destination_path = tokens[count - 1];

    // Get absolute path and convert S1 to S2
    char *abs_path = get_abs_path(destination_path);
    char *pos = strstr(abs_path, "S1");
    if(pos != NULL) {
        pos[1] = '2';
    }

    // Attempt to remove file and send result back to client
    if(remove(abs_path) == 0) {
        res = 1;
        fprintf(stdout, "File %s deleted successfully\n", abs_path);
        send(connection_socket, &res, sizeof(int), 0);
    } else {
        fprintf(stdout, "Error deleting file\n");
        send(connection_socket, &res, sizeof(int), 0);
    }
    
    free(abs_path);
}

/*
 * Creates and sends a tar archive of all files in S2 directory
 */
void downltar(int connection_socket, int count, char *token[]) {

    char cmd[512];
    char *tar_name = "pdf.tar";
    char *base_dir = malloc(strlen(getenv("HOME")) + strlen("/S2") + 1);
    struct stat st;

    // Construct S2 directory path
    strcpy(base_dir, getenv("HOME"));
    strcat(base_dir, "/S2");

    // Create tar archive of all files in S2 directory
    snprintf(cmd, sizeof(cmd), "tar -cvf %s/%s -C %s .", base_dir, tar_name, base_dir);
    system(cmd);

    // Construct full path to tar file
    char *tar_path = malloc(strlen(base_dir) + strlen(tar_name) + 2);
    strcpy(tar_path, base_dir);
    strcat(tar_path, "/");
    strcat(tar_path, tar_name);

    // Get file size and send to client
    stat(tar_path, &st);
    int size = st.st_size;
    send(connection_socket, &size, sizeof(int), 0);
    fprintf(stdout, "Tarfile size %d sent to S1\n", size);

    // Send the tar file
    send_file(connection_socket, tar_path);

    // Clean up temporary tar file
    if(remove(tar_path) == 0) {
        fprintf(stdout, "File %s removed successfully\n", tar_name);
    } else {
        fprintf(stdout, "Error while removing file %s\n", tar_name);
    }

    free(base_dir);
    free(tar_path);
}

/*
 * Filter function for scandir - keeps only regular files
 */
int remove_dir(const struct dirent *entry) {
    return entry->d_type == DT_REG; // Keep only regular files
}

/*
 * Sends list of filenames in specified directory to client
 */
void dispfnames(int connection_socket, int count, char *token[]) {

    struct dirent **fname;
    int n;

    File_names f = { { NULL }, 0};    // Initialize file names structure

    // Get absolute path and convert S1 to S2
    char *abs_path = malloc(strlen(token[1]) + strlen(getenv("HOME")) + 1);
    strcpy(abs_path, getenv("HOME"));
    strcat(abs_path, token[1] + 1);    // Skip '~' character

    char *pos = strstr(abs_path, "S1");
    if(pos != NULL) {
        pos[1] = '2';
    }

    fprintf(stdout, "Absolute path: %s\n", abs_path);

    // Scan directory for regular files only
    n = scandir(abs_path, &fname, remove_dir, alphasort);
    if (n < 0) {
        perror("scandir");
        n = 0;
    }

    // Store filenames in structure
    for(int i=0; i<n; i++) {
        char *ext = strrchr(fname[i]->d_name, '.');
        if((strcmp(ext, ".pdf") == 0)) {
            f.name[i] = strdup(fname[i]->d_name);
            f.count++;
            free(fname[i]);    // Free scandir allocated memory
        }
    }
    free(fname);

    // Send file count to client
    send(connection_socket, &f.count, sizeof(int), 0);
    fprintf(stdout, "File name struct with size %d and file count %d sent to S1\n", sizeof(f), f.count);

    // Send each filename with its length
    for(int i=0; i<f.count; i++) {
        int len = strlen(f.name[i]) + 1;    // Include null terminator
        send(connection_socket, &len, sizeof(int), 0);
        send(connection_socket, f.name[i], len, 0);
        free(f.name[i]);    // Free duplicated strings
    }
    
    free(abs_path);
}


/*
 * Handles file download request from client
 */
void downlf(int connection_socket, int count, char *tokens[]) {

    char *filepath = tokens[1];
    struct stat st;

    char *abs_path = get_abs_path(filepath);
    char *pos = strstr(abs_path, "S1");
    if(pos != NULL) {
        pos[1] = '2';
    }

    fprintf(stdout, "Looking for file: %s\n", abs_path);
    
    // Check if file exists
    if(stat(abs_path, &st) != 0) {
        fprintf(stderr, "File %s not found\n", abs_path);
        int file_size = 0;
        send(connection_socket, &file_size, sizeof(int), 0);    // Send 0 size to indicate error
        free(abs_path);
        return;
    }

    int file_size = st.st_size;
    send(connection_socket, &file_size, sizeof(int), 0);
    fprintf(stdout, "Sending file size: %d\n", file_size);
    
    // Send file content
    send_file(connection_socket, abs_path);
    fprintf(stdout, "File %s sent to S1\n", filepath);
    
    // free(abs_path);

}


/**
 * Main server function - sets up socket, accepts connections, and processes commands
 */
int main() {
    int ls, cs, csd, portNumber, pid, port;
    socklen_t len;
    struct sockaddr_in servAdd;
    char ip_str[INET6_ADDRSTRLEN];
    struct sockaddr_storage client_addr;

    socklen_t client_addr_len = sizeof(client_addr);

    // Create TCP socket
    if ((ls=socket(AF_INET,SOCK_STREAM,0))<0){
        fprintf(stderr, "Cannot create socket\n");
        exit(1);
    }

    // Configure server address
    servAdd.sin_family = AF_INET;
    servAdd.sin_addr.s_addr = inet_addr("127.0.0.1");    // Listen on localhost
    sscanf("15084", "%d", &portNumber);                   // Fixed port number
    servAdd.sin_port = htons((uint16_t)portNumber);

    // Bind socket to address
    bind(ls,(struct sockaddr*)&servAdd,sizeof(servAdd));

    // Listen for incoming connections
    listen(ls, 5);

    // Display server information
    if (getsockname(ls, (struct sockaddr *)&servAdd, &len) == 0) {
        inet_ntop(AF_INET, &servAdd.sin_addr, ip_str, INET_ADDRSTRLEN);
        fprintf(stdout, "Server listening on IP: %s, Port: %d\n", ip_str, ntohs(servAdd.sin_port));
    } else {
        perror("getsockname failed");
    }

    // Accept client connection
    if((cs = accept(ls, (struct sockaddr*)NULL, NULL)) == -1) {
        fprintf(stderr, "Cannot accept client connections\n");
        exit(1);
    }

    // Display client connection info (Note: client_addr not properly filled)
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
    
    // Main command processing loop
    while(1) {
        // Receive command from client
        bytes_read = recv(cs, command, 1024, 0);
        fprintf(stdout, "Bytes received: %d\n", bytes_read);
        command[bytes_read] = '\0';    // Null terminate received string

        fprintf(stdout, "Received command %s\n", command);

        // Parse command into tokens
        int count = split_command(command, tokens);

        // Process different commands
        if(!strcmp(tokens[0], "uploadf")) {
            uploadf(cs, count, tokens);

        } else if (!strcmp(tokens[0], "downlf")) {
            downlf(cs, count, tokens);

        } else if (!strcmp(tokens[0], "removef")) {
            removef(cs, count, tokens);

        } else if (!strcmp(tokens[0], "downltar")) {
            downltar(cs, count, tokens);

        } else if (!strcmp(tokens[0], "dispfnames")) {
            dispfnames(cs, count, tokens);
        }
        
        // Free allocated memory for tokens
        for(int i = 0; i < count; i++) {
            free(tokens[i]);
        }
    }

    // Close sockets (unreachable due to infinite loop)
    close(cs);
    close(ls);
    
    return 0;
}