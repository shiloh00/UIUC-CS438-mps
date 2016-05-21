#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <netdb.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <sys/socket.h>

#include <arpa/inet.h>

int main(int argc, char *argv[])
{
	int sockfd;  
	char* buff = NULL;
	struct addrinfo hints, *servinfo, *p;
	int rv;
    int len;
	int i;
	char ch;
	if (argc != 4) {
	    fprintf(stderr,"usage: mp0client <hostname> <port> <username>\n");
	    exit(1);
	}

	memset(&hints, 0, sizeof hints);
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;

	if ((rv = getaddrinfo(argv[1], argv[2], &hints, &servinfo)) != 0) {
		fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rv));
		return 1;
	}
//domain name, port -> ip address, struct addrinfo, servinfo is a pointer pointing to the struct//
	// loop through all the results and connect to the first we can
	for(p = servinfo; p != NULL; p = p->ai_next) {
		if ((sockfd = socket(p->ai_family, p->ai_socktype,
				p->ai_protocol)) == -1) {
			perror("client: socket");
			continue;
		}

		if (connect(sockfd, p->ai_addr, p->ai_addrlen) == -1) {
			close(sockfd);
			perror("client: connect");
			continue;
		}

		break;
	}

	if (p == NULL) {
		fprintf(stderr, "client: failed to connect\n");
		return 2;
	}

	
	freeaddrinfo(servinfo); // all done with this structure
	len = strlen(argv[3]) + 128;
	buff = malloc(strlen(argv[3]) + 128);
    send(sockfd, "HELO\n", strlen("HELO\n"), 0);
	recv(sockfd, buff, len, 0);
	sprintf(buff, "USERNAME %s\n", argv[3]);
	send(sockfd, buff, strlen(buff), 0);
	recv(sockfd, buff, len, 0);
    for(i = 0; i < 10; i++) {
	send(sockfd, "RECV\n", strlen("RECV\n"), 0);
	recv(sockfd, buff, 12, 0);
	ch = '0';
	printf("Received: ");
	while(ch != '\n') {
	recv(sockfd, &ch, 1, 0);
	printf("%c", ch);
	}
	}

	send(sockfd, "BYE\n", strlen("BYE\n"), 0);
	recv(sockfd, buff, len, 0);
	close(sockfd);
	free(buff);

	return 0;
}

