#ifdef __KERNEL__
#error "Non-kernel file"
#endif

#ifndef __DPDK_H__
#define __DPDK_H__

int dpdk_init(void);
int dpdk_send(const char *buf, const unsigned short buf_len);
int dpdk_cleanup(void);

#endif
