/*cs438 spring2016 http_server.c*/ 
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <sys/wait.h>
#include <signal.h>

#include <string>
#include <vector>
#include <iostream>
#include <sstream>

using namespace std;

void sigchld_handler(int s)
{
	while(waitpid(-1, NULL, WNOHANG) > 0);
}

// get sockaddr, IPv4 or IPv6:
void *get_in_addr(struct sockaddr *sa)
{
	if (sa->sa_family == AF_INET) {
		return &(((struct sockaddr_in*)sa)->sin_addr);
	}

	return &(((struct sockaddr_in6*)sa)->sin6_addr);
}

vector<string> splitLine(const string &line) {
	stringstream ss(line);
	vector<string> res;
	string tmp;
	while(getline(ss, tmp, ' ')) {
		if(tmp.size())
			res.push_back(tmp);
	}
	return res;
}

void sendFile(int sockfd, const string &s) {
	char buf[1024];
	int nbyt = 0;
	string fname(".");
	fname += s;
	FILE *pf = fopen(fname.c_str(), "rb");
	cout << fname << endl;
	if(pf) {
		sprintf(buf, "HTTP/1.0 200 OK\r\n\r\n");
		send(sockfd, buf, strlen(buf), 0);
		while(1) {
			nbyt = fread(buf, 1, 1024, pf);
			//cout << nbyt << endl;
			if(nbyt <= 0)
				break;
			if(nbyt > 0)
				send(sockfd, buf, nbyt, 0);
		}
		fclose(pf);
	} else {
		sprintf(buf, "HTTP/1.0 404 Not Found\r\n\r\n");
		send(sockfd, buf, strlen(buf), 0);
	}
}

void badRequest(int sockfd) {
	char buf[1024];
	sprintf(buf, "HTTP/1.0 400 Bad Request\r\n\r\n");
	send(sockfd, buf, strlen(buf), 0);
}


int main(int argc, char* argv[])
{   
	int sockfd, new_fd;  
	struct addrinfo hints, *servinfo, *p;
	struct sockaddr_storage their_addr;
	socklen_t sin_size;
	struct sigaction sa;
	int yes=1;
	char s[INET6_ADDRSTRLEN];
	int rv;
    char buff[16384];
	int r = 0;

	if(argc != 2){
		fprintf(stderr, "usage: %s port", argv[0]);
		exit(1);
	}

	memset(&hints, 0, sizeof hints);
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_flags = AI_PASSIVE; // use my IP

	if ((rv = getaddrinfo(NULL, argv[1], &hints, &servinfo)) != 0) {
		fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rv));
		return 1;
	}

	// loop through all the results and bind to the first we can
	for(p = servinfo; p != NULL; p = p->ai_next) {
		if ((sockfd = socket(p->ai_family, p->ai_socktype,
				p->ai_protocol)) == -1) {
			perror("server: socket");
			continue;
		}

		if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &yes,
				sizeof(int)) == -1) {
			perror("setsockopt");
			exit(1);
		}

		if (bind(sockfd, p->ai_addr, p->ai_addrlen) == -1) {
			close(sockfd);
			perror("server: bind");
			continue;
		}

		break;
	}

	if (p == NULL)  {
		fprintf(stderr, "server: failed to bind\n");
		return 2;
	}

	freeaddrinfo(servinfo); // all done with this structure

	if (listen(sockfd, 10) == -1) {
		perror("listen");
		exit(1);
	}

	sa.sa_handler = sigchld_handler; // reap all dead processes
	sigemptyset(&sa.sa_mask);
	sa.sa_flags = SA_RESTART;
	if (sigaction(SIGCHLD, &sa, NULL) == -1) {
		perror("sigaction");
		exit(1);
	}

	printf("server: waiting for connections...\n");

	while(1) {  
		sin_size = sizeof their_addr;
		new_fd = accept(sockfd, (struct sockaddr *)&their_addr, &sin_size);
		if (new_fd == -1) {
			perror("accept");
			continue;
		}

		inet_ntop(their_addr.ss_family,
			get_in_addr((struct sockaddr *)&their_addr),
			s, sizeof s);
		printf("server: got connection from %s\n", s);

		//if (1) { // this is the child process
		if (!fork()) { // this is the child process
			close(sockfd); // child doesn't need the listener
			
			bool end = false, init = false;
			while(!end) {
				char c = 0;
				string s;
				while(1){
					recv(new_fd, &c, 1, 0);	
					if(c == '\n' && s.size() > 0 && (s[s.size() - 1]) == '\r') {
						s.resize(s.size() - 1);
						if(s.size()) {
							if(!init) {
								vector<string> v = splitLine(s);
								if(v.size() >= 2 && v[0] == "GET") { 
									sendFile(new_fd, v[1]);
								} else {
									badRequest(new_fd);	
								}
								init = true;
							}
						} else
							end = true;
						break;
					} else {
						s += c;
						if(s.size() > 1024*32) {
							end = true;
							break;
						}
					}
				}
			}
			close(new_fd);
			exit(0);
		}
		close(new_fd);  // parent doesn't need this
	}

	return 0;
}

