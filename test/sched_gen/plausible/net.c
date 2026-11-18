#include "net.h"

void net_tcp(void) __banked
{
	task_next(S_NET_UDP);
}

void net_udp(void) __banked
{
	task_next(S_NET_TCP);
}
