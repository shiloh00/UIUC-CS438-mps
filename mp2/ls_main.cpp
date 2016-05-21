#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <pthread.h>
#include <sys/time.h>
#include <string.h>
#include "util.h"
#include <unordered_set>
#include <unordered_map>
#include <queue>
#include <vector>
#include <fstream>
#include <iostream>

using namespace std;

//const int NODES = 256;
const int NODES = 256;

vector<bool> is_neighbor(NODES, false);

vector<vector<int64_t>> costs(NODES, vector<int64_t>(NODES, -1));

vector<int> next_hops(NODES, -1);

int globalMyID = 0;
//last time you heard from each node. TODO: you will want to monitor this
//in order to realize when a neighbor has gotten cut off from you.
struct timeval globalLastHeartbeat[256];

//our all-purpose UDP socket, to be bound to 10.1.1.globalMyID, port 7777
int globalSocketUDP;
//pre-filled for sending to 10.1.1.0 - 255, port 7777
struct sockaddr_in globalNodeAddrs[256];

vector<uint32_t> msg_ids(NODES, 0);

//Yes, this is terrible. It's also terrible that, in Linux, a socket
//can't receive broadcast packets unless it's bound to INADDR_ANY,
//which we can't do in this assignment.
void hackyBroadcast(const char* buf, int length)
{
	int i;
	for(i=0;i<256;i++)
		if(i != globalMyID) //(although with a real broadcast you would also get the packet yourself)
			sendto(globalSocketUDP, buf, length, 0,
					(struct sockaddr*)&globalNodeAddrs[i], sizeof(globalNodeAddrs[i]));
}

void* announceToNeighbors(void* unusedParam)
{
	struct timespec sleepFor;
	sleepFor.tv_sec = 0;
	sleepFor.tv_nsec = 900 * 1000 * 1000; //300 ms
	while(1)
	{
		hackyBroadcast("HEREIAM", 7);
		nanosleep(&sleepFor, 0);
	}
	return NULL;
}

int pick_next_hop(uint16_t dest) {
	return next_hops[dest];
}


void send_data(uint16_t next_hop, uint16_t dest, uint8_t ttl, char *data, int len) {
	int l = sizeof(struct message) + sizeof(struct message_data) + len;
	struct message *msg = (struct message*) malloc(l);
	msg->type = DATA;
	struct message_data *md = (struct message_data*) msg->data;
	msg->id = htons(dest);
	md->ttl = ttl;
	memcpy(md->data, data, len);
	sendto(globalSocketUDP, msg, l, 0, (struct sockaddr*)&globalNodeAddrs[next_hop],
			sizeof(globalNodeAddrs[next_hop]));
	free(msg);
}

void shortest_path() {
	vector<int> last(NODES, -1);
	vector<int64_t> dist(NODES, -1);
	//unordered_set<int> qs;
	auto cmp = [&] (int a, int b) -> bool {
		uint32_t av = costs[globalMyID][a], bv = costs[globalMyID][b];
		if(av == bv)
			return a >b;
		return av > bv;
	};
	priority_queue<int, vector<int>, decltype(cmp)> q(cmp);
	//qs.insert(globalMyID);
	dist[globalMyID] = 0;
	q.push(globalMyID);
	while(!q.empty()) {
		int cur = q.top();
		q.pop();
		//qs.erase(cur);
		for(int i = 0; i < NODES; i++) {
			if(costs[cur][i] > 0) {
				uint32_t val = dist[cur] + costs[cur][i];
				if(val == dist[i] && last[i] > cur) {
					last[i] = cur;
				} else if((val < dist[i] && dist[i] >= 0) || dist[i] < 0) {
					last[i] = cur;
					dist[i] = val;
					//if(qs.find(i) == qs.end()) {
					//	qs.insert(i);
					q.push(i);
					//}
				}
			}
		}
	}
	//cout << "result:" << endl;
	for(int i = 0; i < NODES; i++) {
		// next_hops[i] = -1;
		int cur = i;
		while(cur >= 0 && last[cur] != globalMyID) {
			cur = last[cur];
		}
		next_hops[i] = cur;
		//cout << "to " << i << " => " << cur << endl;
	}
}

void broadcast_link_state() {
	int len = sizeof(struct message) + sizeof(struct link_state) + NODES * sizeof(uint32_t);
	struct message *pm = (struct message*) malloc(len);
	pm->type = LINK;
	struct link_state *pl = (struct link_state*) pm->data;
	pm->type = LINK;
	pm->id = htons(globalMyID);
	pl->msg_id = htonl(++msg_ids[globalMyID]);
	for(int i = 0; i < NODES; i++) {
		if(is_neighbor[i]) {
			pl->costs[i] = htonl(costs[globalMyID][i]);
		} else {
			pl->costs[i] = 0;
		}
	}
	//cout << "send " << len << endl;
	for(int i = 0; i < NODES; i++) {
		if(is_neighbor[i]) {
			sendto(globalSocketUDP, pm, len, 0, (struct sockaddr*)&globalNodeAddrs[i],
					sizeof(globalNodeAddrs[i]));
		}
	}
	free(pm);
}

void* check_neighbors(void*) {
	struct timespec sleepFor;
	sleepFor.tv_sec = 0;
	sleepFor.tv_nsec = 910 * 1000 * 1000; //410 ms
	struct timeval now;
	while(1)
	{
		bool updated = false;
		gettimeofday(&now, 0);
		for(int i = 0; i < NODES; i++) {
			if(is_neighbor[i]) {
				int64_t gap = 1000000 * (now.tv_sec - globalLastHeartbeat[i].tv_sec)
					+ now.tv_usec - globalLastHeartbeat[i].tv_usec;
				if(gap > 350000) {
					updated = true;
					is_neighbor[i] = false;
					costs[globalMyID][i] = -costs[globalMyID][i];
				}
			}
		}
		if(updated) {
			shortest_path();
			broadcast_link_state();
		}

		nanosleep(&sleepFor, 0);
	}
	return NULL;
}


void listenForNeighbors()
{
	char fromAddr[100];
	struct sockaddr_in theirAddr;
	socklen_t theirAddrLen;
	char recvBuf[2048];

	int bytesRecvd;
	while(1)
	{
		theirAddrLen = sizeof(theirAddr);
		if ((bytesRecvd = recvfrom(globalSocketUDP, recvBuf, 2048 , 0, 
						(struct sockaddr*)&theirAddr, &theirAddrLen)) == -1)
		{
			perror("connectivity listener: recvfrom failed");
			exit(1);
		}

		inet_ntop(AF_INET, &theirAddr.sin_addr, fromAddr, 100);


		struct message *pm = (struct message*) recvBuf;
		pm->id = ntohs(pm->id);
		recvBuf[bytesRecvd] = 0;

		short int heardFrom = -1;
		if(strstr(fromAddr, "10.1.1."))
		{
			heardFrom = atoi(
					strchr(strchr(strchr(fromAddr,'.')+1,'.')+1,'.')+1);

			//TODO: this node can consider heardFrom to be directly connected to it; do any such logic now.

			//record that we heard from heardFrom just now.
			gettimeofday(&globalLastHeartbeat[heardFrom], 0);
			is_neighbor[heardFrom] = true;
			if(costs[globalMyID][heardFrom] < 0) {
				costs[globalMyID][heardFrom] = -costs[globalMyID][heardFrom];
				shortest_path();
				broadcast_link_state();
			}
		}

		//Is it a packet from the manager? (see mp2 specification for more details)
		//send format: 'send'<4 ASCII bytes>, destID<net order 2 byte signed>, <some ASCII message>
		if(!strncmp(recvBuf, "send", 4))
		{
			//TODO send the requested message to the requested destination node
			// ...
			int len = bytesRecvd - sizeof(struct message);
			int next_hop = pick_next_hop(pm->id);
			if(next_hop < 0) {
				log_unreachable(pm->id);
			} else if(pm->id == globalMyID) {
				log_send(pm->id, globalMyID, pm->data);
				log_receive(pm->data);
			} else {
				send_data(next_hop, pm->id, 255, pm->data, len);
				log_send(pm->id, next_hop, pm->data);
			}
		}
		//'cost'<4 ASCII bytes>, destID<net order 2 byte signed> newCost<net order 4 byte signed>
		else if(!strncmp(recvBuf, "cost", 4))
		{
			//TODO record the cost change (remember, the link might currently be down! in that case,
			//this is the new cost you should treat it as having once it comes back up.)
			// ...
			uint32_t cost = ntohl(*((uint32_t*)pm->data));
			if(is_neighbor[pm->id]) {
				costs[globalMyID][pm->id] = cost;
				shortest_path();
				broadcast_link_state();
			} else {
				costs[globalMyID][pm->id] = -cost;
			}
		}
		//TODO now check for the various types of packets you use in your own protocol
		//else if(!strncmp(recvBuf, "your other message types", ))
		// ... 
		else if(!strncmp(recvBuf, "data", 4))
		{
			struct message_data *pmd = (struct message_data*) pm->data;
			if(globalMyID == pm->id) {
				log_receive(pmd->data);
			} else if(pmd->ttl > 0) {
				int len = bytesRecvd - sizeof(struct message) - sizeof(struct message_data);
				int next_hop = pick_next_hop(pm->id);
				if(next_hop >= 0) {
					send_data(next_hop, pm->id, pmd->ttl - 1,  pmd->data, len);
					log_forward(pm->id, next_hop, pmd->data);
				} else {
					log_unreachable(pm->id);
				}
			}
		}
		else if(!strncmp(recvBuf, "link", 4))
		{
			struct link_state *pl = (struct link_state*) pm->data;
			//cout << "receive link from " << pm->id << " len=" << bytesRecvd << endl;
			pl->msg_id = ntohl(pl->msg_id);
			if(pl->msg_id > msg_ids[pm->id] && pm->id != globalMyID) {
				bool updated = false;
				msg_ids[pm->id] = pl->msg_id;
				for(int i = 0; i < NODES; i++) {
					int64_t c = ntohl(pl->costs[i]);
					int64_t old_val = costs[pm->id][i];
					if(c) {
						costs[pm->id][i] = c;
						//cout << "set " << i << " to " << c << endl;
					} else {
						int64_t oc = costs[pm->id][i];
						costs[pm->id][i] = oc > 0 ? -oc : oc;
					}
					if(old_val != costs[pm->id][i]) {
						updated = true;
					}
				}

				// neighbor status doesn't change, do not broadcast link state
				shortest_path();
				if(updated) {
					broadcast_link_state();
				}

				pl->msg_id = htonl(pl->msg_id);
				pm->id = htons(pm->id);
				for(int i = 0; i < NODES; i++) {
					if(is_neighbor[i]) {
						sendto(globalSocketUDP, pm, bytesRecvd, 0,
								(struct sockaddr*)&globalNodeAddrs[i],
								sizeof(globalNodeAddrs[i]));
					}
				}
			}
		}
	}
	//(should never reach here)
	close(globalSocketUDP);
}


int main(int argc, char** argv)
{
	if(argc != 4)
	{
		fprintf(stderr, "Usage: %s mynodeid initialcostsfile logfile\n\n", argv[0]);
		exit(1);
	}

	//initialization: get this process's node ID, record what time it is, 
	//and set up our sockaddr_in's for sending to the other nodes.
	globalMyID = atoi(argv[1]);
	int i;
	for(i=0;i<256;i++)
	{
		gettimeofday(&globalLastHeartbeat[i], 0);

		char tempaddr[100];
		sprintf(tempaddr, "10.1.1.%d", i);
		memset(&globalNodeAddrs[i], 0, sizeof(globalNodeAddrs[i]));
		globalNodeAddrs[i].sin_family = AF_INET;
		globalNodeAddrs[i].sin_port = htons(7777);
		inet_pton(AF_INET, tempaddr, &globalNodeAddrs[i].sin_addr);
	}

	//TODO: read and parse initial costs file. default to cost 1 if no entry for a node. file may be empty.
	ifstream fs(argv[2]);
	int64_t c, idx;
	while(fs >> idx >> c) {
		costs[globalMyID][idx] = -c;
	}
	init_log_file(argv[3]);
	//socket() and bind() our socket. We will do all sendto()ing and recvfrom()ing on this one.
	if((globalSocketUDP=socket(AF_INET, SOCK_DGRAM, 0)) < 0)
	{
		perror("socket");
		exit(1);
	}
	char myAddr[100];
	struct sockaddr_in bindAddr;
	sprintf(myAddr, "10.1.1.%d", globalMyID);	
	memset(&bindAddr, 0, sizeof(bindAddr));
	bindAddr.sin_family = AF_INET;
	bindAddr.sin_port = htons(7777);
	inet_pton(AF_INET, myAddr, &bindAddr.sin_addr);
	if(bind(globalSocketUDP, (struct sockaddr*)&bindAddr, sizeof(struct sockaddr_in)) < 0)
	{
		perror("bind");
		close(globalSocketUDP);
		exit(1);
	}

	//start threads... feel free to add your own, and to remove the provided ones.
	pthread_t announcerThread;
	pthread_t checkingThread;
	pthread_create(&announcerThread, 0, announceToNeighbors, (void*)0);
	pthread_create(&checkingThread, 0, check_neighbors, (void*)0);

	//good luck, have fun!
	listenForNeighbors();
}
