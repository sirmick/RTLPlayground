#include "net.h"

uint8_t net_tcp(void) __banked
{
	return S_NET_UDP;
}

uint8_t net_udp(void) __banked
{
	return S_NET_TCP;
}
