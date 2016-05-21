#ifndef __UTIL_H__
#define __UTIL_H__

#include <stdint.h>

#define SEND (0x646e6573)
#define COST (0x74736f63)
#define LINK (0x6b6e696c)
#define VECT (0x74636576)
#define DATA (0x61746164)

struct __attribute__((__packed__)) message {
	uint32_t type;
	uint16_t id;
	char data[];
};

struct __attribute__((__packed__)) link_state {
	uint32_t msg_id;
	uint32_t costs[];
};

struct __attribute__((__packed__)) message_data {
	uint8_t ttl;
	char data[];
};

int init_log_file(char *path);

void log_send(uint16_t dest, uint16_t nexthop, char *message);

void log_forward(uint16_t dest, uint16_t nexthop, char *message);

void log_receive(char *message);

void log_unreachable(uint16_t dest);

#endif
