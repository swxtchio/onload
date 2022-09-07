#include "dpdk.h"
#include <rte_common.h>
#include <rte_memory.h>
#include <rte_launch.h>
#include <rte_eal.h>
#include <rte_per_lcore.h>
#include <rte_lcore.h>
#include <rte_debug.h>
#include <rte_ring.h>
#include <rte_log.h>
#include <rte_mempool.h>

static const char *_MSG_POOL = "MSG_POOL";
static const char *_SEC_2_PRI = "SEC_2_PRI";
static const char *_PRI_2_SEC = "PRI_2_SEC";

struct rte_ring *send_ring, *recv_ring;
struct rte_mempool *message_pool;

int dpdk_send(const char *buf, const unsigned short buf_len)
{
    void *msg = NULL;
    if (rte_mempool_get(message_pool, &msg) < 0)
        rte_panic("Failed to get message buffer\n");
    snprintf((char *)msg, buf_len, "%s", buf);
    if (rte_ring_enqueue(send_ring, msg) < 0)
    {
        printf("Failed to send message - message discarded\n");
        rte_mempool_put(message_pool, msg);
    }

    return 0;
}

int dpdk_init(void)
{
    char *argv[] = {
        "-l",
        "2-3",
        "-w",
        "fe34:00:02.0",
        "--vdev='net_vdev_netvsc1,iface=eth1'",
        "--proc-type=secondary",
    };

    int ret;

    ret = rte_eal_init(6, argv);
    if (ret < 0)
    {
        rte_exit(1, "Cannot init EAL\n");
    }

    recv_ring = rte_ring_lookup(_PRI_2_SEC);
    send_ring = rte_ring_lookup(_SEC_2_PRI);
    message_pool = rte_mempool_lookup(_MSG_POOL);
    if (send_ring == NULL)
        rte_exit(1, "Problem getting sending ring\n");
    if (recv_ring == NULL)
        rte_exit(1, "Problem getting receiving ring\n");
    if (message_pool == NULL)
        rte_exit(1, "Problem getting message pool\n");

    return 0;
}

int dpdk_quit(void)
{
    rte_eal_cleanup();
    return 0;
}
