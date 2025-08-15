
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

// Maximum number of command arguments
#define MAX_ARGS 1024
// Return values for validation functions
#define FAIL 0
#define PASS 1

extern int errno;

// Structure to hold file names received from server
typedef struct {
    char *name[512];    // Array of file name pointers
    int count;          // Number of files
} File_names;

int connect_server(int argc, char* argv[]) {
    socklen_t len;
    struct sockaddr_in servAdd;
    int portNumber, client_socket;

    // Create TCP socket
    if ((client_socket=socket(AF_INET,SOCK_STREAM,0))<0){ 
        fprintf(stderr, "Cannot create socket\n");
        exit(1);
    }

    // Configure server address structure
    servAdd.sin_family = AF_INET; // IPv4 Internet protocol
    sscanf(argv[2], "%d", &portNumber); // Parse port number from command line

    // Convert port number from host to network byte order
    servAdd.sin_port = htons((uint16_t)portNumber);

    // Convert IP address from text to binary format
    if(inet_pton(AF_INET, argv[1], &servAdd.sin_addr) < 0){
        fprintf(stderr, " inet_pton() has failed\n");
        exit(2);
    }

    // Establish connection to server
    if(connect(client_socket, (struct sockaddr  *) &servAdd,sizeof(servAdd))<0){
        fprintf(stderr, "connect() failed, exiting\n");
        exit(3);
    }
    return client_socket;
}

/*
 * Validates that the first argument matches the expected command name
 */
int check_name(char *files[], char *command) {
    if(strcmp(files[0], command)) {
        fprintf(stderr, "Invalid command name\n");
        return FAIL;
    }
    return PASS;
}

/*
 * Verifies that a file path exists and is accessible
 */
int verify_path(char *name) {
    if(!access(name, F_OK))  // F_OK checks for file existence
        return PASS;
    fprintf(stderr, "Invalid filepath\n");
    return FAIL;
}

/*
 * Validates file extension against allowed types (.pdf, .c, .txt, .zip)
 */
int verify_ext(char *name) {
    // Find the last occurrence of '.' in the filename
    char *index = strrchr(name, '.');
    
    // Check if extension exists and is one of the allowed types
    if (!index || (strcmp(index, ".pdf") != 0 && strcmp(index, ".c")   != 0 && 
                   strcmp(index, ".txt") != 0 && strcmp(index, ".zip") != 0)) {
        fprintf(stderr, "Invalid file type for %s. Only .pdf .c .txt and .zip supported.\n", name);
        return FAIL;
    }
    return PASS;
}

/*
 * Validates file paths and extensions for multiple files
 */
int check_file_path_and_ext(char *files[], int count) {
    int check = 1;
    // Skip first argument (command name) and last argument (server path)
    for(int i=1; i<count-1; i++) {
        if(!(verify_ext(files[i]) && verify_path(files[i]))) {
            return FAIL;
        }
    }
    return PASS;
}

/*
 * Parses and validates upload command arguments
 * Format: uploadf <file1> [file2] ... ~/S1/<path>
 */
int parse_command_up(char *input, char* files[], int *i) {
    *i=0;
    char *inputcopy = strdup(input);  // Create copy to avoid modifying original
    
    // Tokenize input string by spaces
    char *token = strtok(inputcopy, " ");
    while(token != NULL) {
        files[(*i)++] = strdup(token);  // Store each token
        token = strtok(NULL, " ");
    }

    // Validate server address format (must start with ~/S1)
    if(strncmp(files[*i - 1], "~/S1", 4)) {
        fprintf(stdout, "Invalid server addr\n");
        return 0;
    }

    // Check minimum argument count (command + at least 1 file + server path)
    if((*i < 3) || (*i > 5)) {
        fprintf(stderr, "Invalid number of arguments. Max 3 files expected\n");
        return FAIL;
    }

    // Validate command name and file paths/extensions
    return (check_name(files, "uploadf") && check_file_path_and_ext(files, *i));
}

/*
 * Parses and validates remove file command arguments
 * Format: removef <filename> ~/S1/<path>
 */
int parse_command_rm(char *input, char* files[], int *i) {
    *i=0;
    char *inputcopy = strdup(input);
    
    // Tokenize input string
    char *token = strtok(inputcopy, " ");
    while(token != NULL) {
        files[(*i)++] = strdup(token);
        token = strtok(NULL, " ");
    }

    // Validate server address contains ~/S1
    if(strstr(files[(*i) - 1], "~/S1") == NULL) {
        fprintf(stdout, "Invalid server addr\n");
        return 0;
    }
    
    // Check minimum argument count
    if((*i < 2) || (*i > 4)) {
        fprintf(stderr, "Invalid number of arguments. Max 2 files expected\n");
        return FAIL;
    }

    return (check_name(files, "removef"));
}

/*
 * Parses and validates download tar command arguments
 * Format: downltar <.pdf|.txt|.c>
 */
int parse_command_tar(char *input, char* files[], int *i) {
    *i = 0;
    char *inputcopy = strdup(input);
    
    // Tokenize input string
    char *token = strtok(inputcopy, " ");
    while(token != NULL) {
        files[(*i)++] = strdup(token);
        token = strtok(NULL, " ");
    }

    // Check minimum argument count
    if((*i < 2) || (*i > 2)) {
        fprintf(stderr, "Invalid number of arguments. Max 1 files expected\n");
        return FAIL;
    }

    // Validate file type for tar operation
    if((strcmp(files[1], ".pdf") != 0) && (strcmp(files[1], ".txt") != 0) && 
       (strcmp(files[1], ".c") != 0)) {
        fprintf(stderr, "Invalid file type\n");
        return FAIL;
    }

    return (check_name(files, "downltar"));
}

/*
 * Parses and validates display filenames command arguments
 * Format: dispfnames <path>
 */
int parse_command_disp(char *input, char* files[], int *i) {
    *i = 0;
    char *inputcopy = strdup(input);
    
    // Tokenize input string
    char *token = strtok(inputcopy, " ");
    while(token != NULL) {
        files[(*i)++] = strdup(token);
        token = strtok(NULL, " ");
    }

    // Check minimum argument count
    if((*i < 2) || (*i > 2)) {
        fprintf(stderr, "Invalid number of arguments. Max 1 files expected\n");
        return FAIL;
    }

    return (check_name(files, "dispfnames"));
}

/*
 * Parses and validates download file command arguments
 * Format: downlf <file1> [file2] (files must be in ~/S1 path)
 */
int parse_command_dwn(char *input, char* files[], int *i) {

    *i = 0;
    char *inputcopy = strdup(input);
    
    // Tokenize input string
    char *token = strtok(inputcopy, " ");
    while(token != NULL) {
        files[(*i)++] = strdup(token);
        token = strtok(NULL, " ");
    }

    // Validate argument count (1-2 files + command name)
    if(*i < 2 || *i > 3) {
        fprintf(stderr, "Invalid number of arguments. Max 2 files expected\n");
        return FAIL;
    }

    // Validate each file
    for(int j = 1; j < *i; j++) {
        // Check file extension
        char *ext = strrchr(files[j], '.');
        if (!ext || (strcmp(ext, ".c") != 0 && strcmp(ext, ".pdf") != 0 &&  
                     strcmp(ext, ".txt") != 0 && strcmp(ext, ".zip") != 0)) {
            fprintf(stderr, "Invalid file type for %s\n", files[j]);
            return FAIL;
        }
        
        // Check if path starts with ~/S1
        if(strncmp(files[j], "~/S1", 4) != 0) {
            fprintf(stderr, "Invalid file path %s. Must start with ~/S1\n", files[j]);
            return FAIL;
        }
    }

    return (check_name(files, "downlf"));
}

/*
 * Sends a file over the socket connection
 */
void send_file(int cl_s, char *name) {
    int bytes_read, total_sent = 0;
    char buffer[1024];

    // Open file for reading
    int fd = open(name, O_RDONLY);

    // Read file in chunks and send over socket
    while ((bytes_read = read(fd, buffer, 1024)) > 0) {
        if (send(cl_s, buffer, bytes_read, MSG_NOSIGNAL) != bytes_read) {
            // Handle broken pipe (connection closed by server)
            if (errno == EPIPE) {
                fprintf(stderr, "Broken pipe detected - connection closed by server\n");
                exit(0);
            }
        }
        total_sent += bytes_read;
    }
    
    close(fd);  // Close file descriptor
    fprintf(stdout, "File name: %s, Total bytes: %d\n", name, total_sent);
}

/*
 * Receives a file from the socket connection
 */
void recv_file(int co_s, char *name, int file_size) {
    char buffer[1024];
    int bytes;
    int total_received = 0;

    // Create file for writing (with permissions 644)
    int fd = open(name, O_CREAT | O_WRONLY, 0644);

    // Receive file data in chunks
    while (total_received < file_size) {
        // Calculate how much data to receive (don't exceed buffer size)
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

    close(fd);  // Close file descriptor

    // Report status
    if (bytes == 0) {
        fprintf(stdout, "Connection closed by sender\n");
    } else if (bytes < 0) {
        perror("recv failed");
    }

    fprintf(stdout, "File %s created. Total %d bytes written.\n", name, total_received);
}

/*
 * Handles file upload operation
 * Sends command and then uploads each specified file
 */
void uploadf(int cl_s, char *input, char *files[], int *count) {
    // Send command string to server
    send(cl_s, input, strlen(input), 0);
    fprintf(stdout, "Bytes sent: %d\n", strlen(input));

    // Send each file (skip command name and server path)
    for(int i=1; i < *count - 1; i++) {
        int size=0;
        struct stat st;
        
        // Get file size
        stat(files[i], &st);
        size = st.st_size;

        // Send file size first, then file data
        send(cl_s, &size, sizeof(int), 0);
        fprintf(stdout, "Sent size. Name-%s, size-%d\n", files[i], size);

        send_file(cl_s, files[i]);
    }

    fprintf(stdout, "\nFiles uploaded\n");
}

/*
 * Handles file removal operation
 * Sends remove command to server
 */
void removef(int cl_s, char *input, char *files[], int *count) {
    
    int res;
    // Send command string to server
    send(cl_s, input, strlen(input), 0);
    // fprintf(stdout, "Bytes sent: %d\n", strlen(input));
    fprintf(stdout, "\nCommand sent\n");

    for(int i=1; i<*count; i++) {
        recv(cl_s, &res, sizeof(int), 0);
        if(res) {
            fprintf(stdout, "File %s deleted successfully.\n", files[i]);
        } else {
            fprintf(stdout, "Error while deleting file %s.\n", files[i]);
        }
    }
}

/*
 * Handles download tar file operation
 * Requests and downloads a tar file containing files of specified type
 */
void downltar(int cl_s, char *input, char *files[], int *count) {
    // Send command to server
    send(cl_s, input, strlen(input), 0);
    fprintf(stdout, "Command %s sent to S1\n", files[0]);

    int bytes_received = 0, file_size = 0;

    // Create tar filename based on file type
    char *fname = malloc(strlen(files[1]) + strlen("tar") + 1);
    strcpy(fname, files[1] + 1);  // Skip the '.' in extension
    strcat(fname, ".tar");

    // Receive file size from server
    bytes_received = recv(cl_s, &file_size, sizeof(int), 0);
    fprintf(stdout, "Tarfile to receive has size of %d bytes\n", file_size);

    // Receive the tar file
    recv_file(cl_s, fname, file_size);
    
    free(fname);  // Clean up allocated memory
}

/*
 * Handles display filenames operation
 * Requests and displays list of files from server
 */
void dispfnames(int cl_s, char *input, char *files[], int *count) {
    File_names *file_names = malloc(sizeof(File_names));
    int bytes_read;
    
    // Send command to server
    send(cl_s, input, strlen(input), 0);
    fprintf(stdout, "Command %s sent to S1\n", files[0]);

    // Receive number of files
    bytes_read = recv(cl_s, &file_names->count, sizeof(int), 0);
    fprintf(stdout, "Received %d bytes from the server S1\n", bytes_read);

    fprintf(stdout, "The files under given path is/are:\n");
    
    // Receive each filename
    for(int i=0; i<file_names->count; i++) {
        int len;
        recv(cl_s, &len, sizeof(int), 0);  // Receive filename length

        file_names->name[i] = malloc(len + 1);
        recv(cl_s, file_names->name[i], len, 0);  // Receive filename
        file_names->name[i][len] = '\0';  // Null terminate
        fprintf(stdout, "%s\t", file_names->name[i]);
    }
    
    fprintf(stdout, "\n");
    
    // Clean up memory
    for(int i = 0; i < file_names->count; i++) {
        free(file_names->name[i]);
    }
    free(file_names);
}


/*
 * Handles download file operation
 * Downloads specified files from server
 */
void downlf(int cl_s, char *input, char *files[], int *count) {
    send(cl_s, input, strlen(input), 0);

    for(int i=1; i < *count; i++) {
        int file_size;
        recv(cl_s, &file_size, sizeof(int), 0);

        char *filename = strrchr(files[i], '/');
        recv_file(cl_s, filename + 1, file_size);
    }
}


/*
 * Main command loop - reads user input and executes commands
 */
void get_commands(int cl_s) {
    char input[MAX_ARGS];
    char *files[5] = {0};  // Array to store parsed command arguments
    int count;

    while(1) {
        // Display prompt and read user input
        fprintf(stdout, "s25client$ ");
        fgets(input, MAX_ARGS, stdin);
        input[strcspn(input, "\n")] = 0;  // Remove newline character

        // Handle exit command
        if(!strcmp(input, "exit")) {
            fprintf(stdout, "I am shutting down. See you next time!\n");
            exit(0);
        }

        // Parse and execute commands based on input
        if(strstr(input, "uploadf")) {
            if(parse_command_up(input, files, &count)) {
                uploadf(cl_s, input, files, &count);
            }
        } else if(strstr(input, "downlf")) {
            if(parse_command_dwn(input, files, &count)) {  // Note: This should probably be parse_command_downlf
                downlf(cl_s, input, files, &count);
            }
        } else if(strstr(input, "removef")) {
            if(parse_command_rm(input, files, &count)) {
                removef(cl_s, input, files, &count);
            }
        } else if(strstr(input, "downltar")) {
            if(parse_command_tar(input, files, &count)) {
                downltar(cl_s, input, files, &count);
            }
        } else if(strstr(input, "dispfnames")) {
            if(parse_command_disp(input, files, &count)) {
                dispfnames(cl_s, input, files, &count);
            }
        }
        
        // Clean up allocated memory for parsed arguments
        for(int i = 0; i < count && files[i] != NULL; i++) {
            free(files[i]);
            files[i] = NULL;
        }
    }
}

/*
 * Main function - establishes server connection and starts command loop
 */
int main(int argc, char *argv[]) {
    // Establish connection to server
    int cs = connect_server(argc, argv);

    // Start interactive command loop
    get_commands(cs);

    // Close socket (unreachable due to infinite loop in get_commands)
    close(cs);
    return 0;
}