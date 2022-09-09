#ifndef __DPDK_H__
#define __DPDK_H__
#include "ip_internal.h"

int dpdk_init(void);
int dpdk_send(ci_ip_pkt_fmt *pkt);
int dpdk_cleanup(void);

#endif
