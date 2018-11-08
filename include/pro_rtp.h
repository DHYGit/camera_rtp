#pragma once
#include <pthread.h>

int InitJrtp(struct sockaddr_in addr_in, int dstPort);

void *JrtpFun(void *ptr);

void *UDPSOCKFun(void *ptr);
