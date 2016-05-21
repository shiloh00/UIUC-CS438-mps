/*cs438 spring2016 http_client.cpp*/     
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

#include <iostream>
#include <string>
#include <vector>
#include <sstream>

using namespace std;

// get sockaddr, IPv4 or IPv6:
void *get_in_addr(struct sockaddr *sa)
{
	if (sa->sa_family == AF_INET) {
		return &(((struct sockaddr_in*)sa)->sin_addr);
	}

	return &(((struct sockaddr_in6*)sa)->sin6_addr);
}

vector<string> splitURL(const string &url){
	vector<string> res;
	if(url.size() < 7)
		return res;
	string protocol(url, 0, 7);
	if(protocol != "http://")
		return res;
	size_t index1 = url.find(':', 7);
	size_t index2 = url.find('/', 7);
	if(index1 != std::string::npos && index2 != std::string::npos){
		res.push_back(url.substr(7, (index1 - 7)));
		res.push_back(url.substr(index1 + 1, index2 - index1 - 1));
		res.push_back(url.substr(index2));
	}

	if(index1 == std::string::npos && index2 != std::string::npos){
		res.push_back(url.substr(7, index2 - 7));
		res.push_back("80");
		res.push_back(url.substr(index2));
	}

	if(index1 != std::string::npos && index2 == std::string::npos){
		res.push_back(url.substr(7, (index1 - 7)));
		res.push_back(url.substr(index1 + 1));
		res.push_back("/index.html");
	}

	if(index1 == std::string::npos && index2 == std::string::npos){
		res.push_back(url.substr(7));
		res.push_back("80");
		res.push_back("/index.html");
	}

	return res;
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

void saveFile(int sockfd) {
	char buf[1024];
	int nbyt = 0;
	string fname("./output");
	FILE *pf = fopen(fname.c_str(), "w");
	cout << fname << endl;
	while(1) {
		nbyt = recv(sockfd, buf, 1024, 0);
		if(nbyt <= 0)
			break;
		if(nbyt > 0)
			fwrite(buf, nbyt, 1, pf);
	}
	fclose(pf);
}

int main(int argc, char *argv[])
{
	int sockfd, numbytes;  
	char* buf;
	struct addrinfo hints, *servinfo, *p;
	int rv;
	char s[INET6_ADDRSTRLEN];

	if (argc != 2) {
	    fprintf(stderr,"usage: %s hostname\n", argv[0]);
	    exit(1);
	}

	vector<string> vec = splitURL(string(argv[1]));

	memset(&hints, 0, sizeof hints);
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;

	if ((rv = getaddrinfo(vec[0].c_str(), vec[1].c_str(), &hints, &servinfo)) != 0) {
		fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rv));
		return 1;
	}

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

	inet_ntop(p->ai_family, get_in_addr((struct sockaddr *)p->ai_addr),
			s, sizeof s);
	printf("client: connecting to %s\n", s);

	freeaddrinfo(servinfo); 

	buf = new char[vec[0].size() + vec[1].size() + vec[2].size() + 256];
    
	sprintf(buf, "GET %s HTTP/1.0\r\n"
			     "User-Agent: Wget/1.15 (linux-gnu)\r\n"
				 "Host: %s:%s\r\n\r\n", vec[2].c_str(), vec[0].c_str(), vec[1].c_str());
	send(sockfd, buf, strlen(buf), 0);
	
	int idx = 0;
	bool end = false;
	while(!end) {
		char c = 0;
		string s;
		while(1){
			recv(sockfd, &c, 1, 0);	
			if(c == '\n' && s.size() > 0 && (s[s.size() - 1] == '\r')){
				s.resize(s.size() - 1);
				if(!s.size() && idx) {
					saveFile(sockfd);
					end = true;
				} else {
					//read status code and determin whether we need to continue
					vector<string> v = splitLine(s);
					if(!(v.size() >= 2 && v[1] == "200") && idx == 0)
						end = true;
				}
				break;
			} else {
				s += c;
			}
		}
		idx++;
	}


	close(sockfd);

	return 0;
}

