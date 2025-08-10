#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <pthread.h>

int main() {
    int ls, cs, csd, portNumber, pid;
    socklen_t len;
    struct sockaddr_in servAdd;

    if ((ls=socket(AF_INET,SOCK_STREAM,0))<0){
        fprintf(stderr, "Cannot create socket\n");
        exit(1);
    }

    servAdd.sin_family = AF_INET;
    servAdd.sin_addr.s_addr = htonl(INADDR_ANY);
    sscanf("5085", "%d", &portNumber);
    servAdd.sin_port = htons((uint16_t)portNumber);

    bind(ls,(struct sockaddr*)&servAdd,sizeof(servAdd));

    listen(ls, 5);

    if((cs = accept(ls, (struct sockaddr*)NULL, NULL)) == -1) {
        fprintf(stderr, "Cannot accept client connections\n");
        exit(1);
    }

    printf("S3 has established connection with ...");

    return 0;
}