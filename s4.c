/*
 * S4 File Server
 * 
 * This program implements a file server (S4) that is part of a distributed
 * file system. It handles file operations including upload, download, removal,
 * tar creation, and directory listing. The server communicates with other
 * servers (S1, S3) and maintains files in a local S4 directory structure.
 * 
 * Server Details:
 * - Name: S4
 * - Port: 15086
 * - IP: 127.0.0.1 (localhost)
 * - Base Directory: $HOME/S4
 */

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

// Server identifier name
#define NAME "S4"

extern int errno;

// Structure to hold file names for directory listing
typedef struct {
    char *name[512];    // Array of file name pointers
    int count;          // Number of files
} File_names;

/**
 * Splits a command string into individual tokens
 * @param buffer: Input command string to be tokenized
 * @param result: Array to store resulting tokens
 * @return: Number of tokens parsed
 */
int split_command(char *buffer, char** result) {
    // Tokenize by space and tab characters
    char *token = strtok(buffer, " \t");
    int count = 0;

    while(token != NULL) {
        // Duplicate each token to avoid strtok overwriting issues
        result[count++] = strdup(token);
        if(result[count - 1] == NULL) {
            fprintf(stderr, "Memory allocation in strdup failed\n");
            exit(2);
        }
        token = strtok(NULL, " \t");
    }

    return count;
}

/**
 * Converts a relative path (starting with ~) to absolute path
 * @param path: Relative path starting with ~ (e.g., ~/S1/file.txt)
 * @return: Absolute path string (caller must free)
 */
char* get_abs_path(char *path) {
    char *home = getenv("HOME");
    char *expand = malloc(strlen(home) + strlen(path));

    strcpy(expand, home);
    strcat(expand, path + 1);  // Skip the '~' character

    return expand;
}

/**
 * Creates destination directory path and converts S1 references to S4
 * Also creates the directory structure if it doesn't exist
 * @param path: Original path (may contain S1 reference)
 * @return: Absolute path with S1 changed to S4 (caller must free)
 */
char* create_dest(char *path) {
    char command[256];
    char *abs_path = get_abs_path(path);

    // Replace S1 with S4 in the path (for server routing)
    char *pos = strstr(abs_path, "S1");
    if(pos != NULL) {
        pos[1] = '4';  // Change S1 to S4
    }

    // Create directory structure using system mkdir command
    snprintf(command, sizeof(command), "mkdir -p %s", abs_path);
    system(command);

    return abs_path;
}

/**
 * Receives a file from socket and saves it to disk
 * @param co_s: Socket file descriptor
 * @param name: Name of file to create
 * @param path: Directory path where file should be saved
 * @param file_size: Expected size of incoming file
 */
void recv_file(int co_s, char *name, char *path, int file_size) {
    char buffer[1024];
    int bytes;
    int total_received = 0;

    // Change to target directory
    chdir(path);

    // Create file with read/write permissions for owner, read for others
    int fd = open(name, O_CREAT | O_WRONLY, 0644);
    if (fd < 0) {
        perror("Failed to create file");
        return;
    }

    // Receive file data in chunks
    while (total_received < file_size) {
        // Calculate optimal receive size (don't exceed buffer or remaining data)
        int to_receive = 1024;
        if (file_size - total_received < 1024) {
            to_receive = file_size - total_received;
        }
        
        bytes = recv(co_s, buffer, to_receive, 0);
        
        // Handle connection errors or closure
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

    close(fd);

    // Report transfer status
    if (bytes == 0) {
        fprintf(stdout, "Connection closed by sender\n");
    } else if (bytes < 0) {
        perror("recv failed");
    }

    fprintf(stdout, "File %s created. Total %d bytes written.\n", name, total_received);
}

/**
 * Sends a file over socket connection
 * @param cl_s: Socket file descriptor
 * @param name: Path to file to be sent
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

    // Read and send file in chunks
    while ((bytes_read = read(fd, buffer, sizeof(buffer))) > 0) {
        int sent = send(cl_s, buffer, bytes_read, MSG_NOSIGNAL);
        if (sent != bytes_read) {
            // Handle broken pipe (connection closed)
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

/**
 * Handles file upload operation
 * Receives a file from client and stores it in the appropriate S4 directory
 * @param connection_socket: Client socket file descriptor
 * @param count: Number of command tokens
 * @param tokens: Array of command tokens [uploadf, filename, destination_path]
 */
void uploadf(int connection_socket, int count, char *tokens[]) {
    int file_size = 0;

    // Get destination path from last token
    char *destination_path = tokens[count - 1];

    // Create destination directory and get absolute path
    char *path = create_dest(destination_path);

    fprintf(stdout, "%s\n", path);
    
    // Receive file size from client
    int bytes_received = recv(connection_socket, &file_size, sizeof(int), 0);
    fprintf(stdout, "Size of file %s received is %d\n", tokens[1], file_size);

    // Receive the actual file content
    recv_file(connection_socket, tokens[1], path, file_size);
    fprintf(stdout, "Files received from uploadf\n");
    
    free(path);  // Clean up allocated memory
}

/**
 * Handles file removal operation
 * Removes a file from S3 directory (note: removes from S3, not S4)
 * @param connection_socket: Client socket file descriptor
 * @param count: Number of command tokens
 * @param tokens: Array of command tokens [removef, filename, path]
 */
void removef(int connection_socket, int count, char *tokens[]) {
    int file_size = 0, res = 0;

    // Get destination path from last token
    char *destination_path = tokens[count - 1];

    // Get absolute path
    char *abs_path = get_abs_path(destination_path);

    // Replace S1 with S3 in path (removes from S3, not S4)
    char *pos = strstr(abs_path, "S1");
    if(pos != NULL) {
        pos[1] = '3';  // Change S1 to S3
    }

    // Attempt to remove the file
    if(remove(abs_path) == 0) {
        res = 1;  // Success
        fprintf(stdout, "File %s deleted successfully\n", abs_path);
        send(connection_socket, &res, sizeof(int), 0);
    } else {
        fprintf(stdout, "Error deleting file\n");
        send(connection_socket, &res, sizeof(int), 0);  // res = 0 (failure)
    }
    
    free(abs_path);  // Clean up allocated memory
}

/**
 * Handles tar file creation and download operation
 * Creates a tar archive of all files in S4 directory and sends it to client
 * @param connection_socket: Client socket file descriptor
 * @param count: Number of command tokens
 * @param token: Array of command tokens [downltar, file_type]
 */
void downltar(int connection_socket, int count, char *token[]) {
    char cmd[512];
    char *tar_name = "txt.tar";  // Fixed tar filename
    
    // Allocate memory for base directory path
    char *base_dir = malloc(strlen(getenv("HOME")) + strlen("/S4") + 1);
    struct stat st;

    // Construct S4 base directory path
    strcpy(base_dir, getenv("HOME"));
    strcat(base_dir, "/S4");
    fprintf(stdout, "Basedir: %s\n", base_dir);

    // Create tar command to archive all files in S4 directory
    snprintf(cmd, sizeof(cmd), "tar -cvf %s/%s -C %s .", base_dir, tar_name, base_dir);
    fprintf(stdout, "cmd: %s\n", cmd);

    // Execute tar command
    int ret = system(cmd);
    fprintf(stdout, "ret value: %d\n", ret);

    // Construct full path to created tar file
    char *tar_path = malloc(strlen(base_dir) + strlen(tar_name) + 2);
    strcpy(tar_path, base_dir);
    strcat(tar_path, "/");
    strcat(tar_path, tar_name);

    // Get tar file size
    stat(tar_path, &st);
    int size = st.st_size;

    // Send tar file size to client
    send(connection_socket, &size, sizeof(int), 0);
    fprintf(stdout, "Tarfile size %d sent to S1\n", size);

    // Send the tar file content
    send_file(connection_socket, tar_path);

    // Clean up: remove temporary tar file
    if(remove(tar_path) == 0) {
        fprintf(stdout, "File %s removed successfully\n", tar_name);
    } else {
        fprintf(stdout, "Error while removing file %s\n", tar_name);
    }

    // Free allocated memory
    free(base_dir);
    free(tar_path);
}

/**
 * Filter function for scandir - selects only regular files
 * @param entry: Directory entry to examine
 * @return: 1 if entry is a regular file, 0 otherwise
 */
int remove_dir(const struct dirent *entry) {
    return entry->d_type == DT_REG; // Keep only regular files (exclude directories)
}

/**
 * Handles display filenames operation
 * Lists all files in the specified S4 directory and sends list to client
 * @param connection_socket: Client socket file descriptor
 * @param count: Number of command tokens
 * @param token: Array of command tokens [dispfnames, path]
 */
void dispfnames(int connection_socket, int count, char *token[]) {
    struct dirent **fname;
    int n;

    // Initialize file names structure
    File_names f = { { NULL }, 0};

    // Construct absolute path
    char *abs_path = malloc(strlen(token[1]) + strlen(getenv("HOME")) + 1);
    strcpy(abs_path, getenv("HOME"));
    strcat(abs_path, token[1] + 1);  // Skip '~' character

    // Convert S1 path to S4 path
    char *pos = strstr(abs_path, "S1");
    if(pos != NULL) {
        pos[1] = '4';  // Change S1 to S4
    }

    fprintf(stdout, "Absolute path: %s\n", abs_path);

    // Scan directory for regular files only
    n = scandir(abs_path, &fname, remove_dir, alphasort);
    if (n < 0) {
        perror("scandir");
        n = 0;  // No files found or error occurred
    }

    // Store file names in structure
    for(int i = 0; i < n; i++) {
        f.name[i] = strdup(fname[i]->d_name);
        f.count++;
    }

    // Send number of files to client
    send(connection_socket, &f.count, sizeof(int), 0);
    fprintf(stdout, "File name struct with size %d and file count %d sent to S1\n", sizeof(f), f.count);

    // Send each filename with its length
    for(int i = 0; i < f.count; i++) {
        int len = strlen(f.name[i]) + 1;  // Include null terminator
        send(connection_socket, &len, sizeof(int), 0);    // Send length first
        send(connection_socket, f.name[i], len, 0);       // Then send filename
    }

    // Clean up allocated memory
    for(int i = 0; i < n; i++) {
        free(fname[i]);
        if(i < f.count) {
            free(f.name[i]);
        }
    }
    free(fname);
    free(abs_path);
}

/**
 * Handles file download operation
 * Sends a specific file from S4 directory to client
 * @param connection_socket: Client socket file descriptor
 * @param count: Number of command tokens
 * @param tokens: Array of command tokens [downlf, filepath]
 */
void downlff(int connection_socket, int count, char *tokens[]) {
    char *filepath = tokens[1]; // File path to download
    struct stat st;
    
    // Convert S1 path to local S4 server path
    char *abs_path = get_abs_path(filepath);
    char *pos = strstr(abs_path, "S1");
    if(pos != NULL) {
        pos[1] = '4';  // Change S1 to S4
    }
    
    fprintf(stdout, "Looking for file: %s\n", abs_path);
    
    // Check if file exists and get its stats
    if(stat(abs_path, &st) != 0) {
        fprintf(stderr, "File %s not found\n", abs_path);
        // Send 0 size to indicate file not found
        int file_size = 0;
        send(connection_socket, &file_size, sizeof(int), 0);
        free(abs_path);
        return;
    }
    
    // Send file size to client
    int file_size = st.st_size;
    send(connection_socket, &file_size, sizeof(int), 0);
    fprintf(stdout, "Sending file size: %d\n", file_size);
    
    // Send file content to client
    send_file(connection_socket, abs_path);
    fprintf(stdout, "File %s sent to S1\n", filepath);
    
    free(abs_path);
}


void downlf(int connection_socket, int count, char *tokens[]) {

    char *filepath = tokens[1];
    struct stat st;

    char *abs_path = get_abs_path(filepath);
    char *pos = strstr(abs_path, "S1");
    if(pos != NULL) {
        pos[1] = '4';
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
 * Main server function
 * Sets up socket, binds to port 15086, accepts connections and processes commands
 * @return: 0 on successful completion (never reached due to infinite loop)
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
    servAdd.sin_addr.s_addr = inet_addr("127.0.0.1");  // Localhost
    sscanf("15086", "%d", &portNumber);  // S4 server port
    servAdd.sin_port = htons((uint16_t)portNumber);

    // Bind socket to address and port
    if(bind(ls,(struct sockaddr*)&servAdd,sizeof(servAdd)) < 0) {
        perror("bind failed");
        exit(1);
    }

    // Start listening for connections (queue up to 5 connections)
    if(listen(ls, 5) < 0) {
        perror("listen failed");
        exit(1);
    }

    // Get and display server's actual address and port
    len = sizeof(servAdd);
    if (getsockname(ls, (struct sockaddr *)&servAdd, &len) == 0) {
        inet_ntop(AF_INET, &servAdd.sin_addr, ip_str, INET_ADDRSTRLEN);
        fprintf(stdout, "Server listening on IP: %s, Port: %d\n", ip_str, ntohs(servAdd.sin_port));
    } else {
        perror("getsockname failed");
    }

    // Accept incoming connection
    if((cs = accept(ls, (struct sockaddr*)NULL, NULL)) == -1) {
        fprintf(stderr, "Cannot accept client connections\n");
        exit(1);
    }

    // Display client connection information
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

    // Main command processing loop
    char command[1024];
    char* tokens[3];
    int bytes_read;

    while(1) {
        // Receive command from client
        bytes_read = recv(cs, command, 1024, 0);
        printf("Bytes received: %d\n", bytes_read);
        fflush(stdout);
        fprintf(stdout, "Bytes received: %d\n", bytes_read);
        command[bytes_read] = '\0';  // Null terminate received string

        fprintf(stdout, "Received command %s\n", command);

        // Parse command into tokens
        int count = split_command(command, tokens);

        // Execute appropriate command handler
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

        } else {
            fprintf(stderr, "Unknown command: %s\n", tokens[0]);
        }

        // Clean up allocated memory for tokens
        for(int i = 0; i < count; i++) {
            free(tokens[i]);
        }
    }

    // Clean up sockets (never reached due to infinite loop)
    close(cs);
    close(ls);
    return 0;
}