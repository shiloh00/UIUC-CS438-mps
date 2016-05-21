#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "util.h"


static FILE *log_file = NULL;

static char log_line[256];

int init_log_file(char *path) {
	log_file = fopen(path, "wb");
	if(log_file)
		return 0;
	return -1;
}

static void write_log(char *message) {
	fwrite(message, 1, strlen(message), log_file);
	fflush(log_file);
}

void log_send(uint16_t dest, uint16_t nexthop, char *message) {
	sprintf(log_line, "sending packet dest %d nexthop %d message %s\n", (int)dest, (int)nexthop, message);
	write_log(log_line);
}

void log_forward(uint16_t dest, uint16_t nexthop, char *message) {
	sprintf(log_line, "forward packet dest %d nexthop %d message %s\n", (int)dest, (int)nexthop, message);
	write_log(log_line);
}

void log_receive(char *message) {
	sprintf(log_line, "receive packet message %s\n", message);
	write_log(log_line);
}

void log_unreachable(uint16_t dest) {
	sprintf(log_line, "unreachable dest %d\n", (int)dest);
	write_log(log_line);
}

