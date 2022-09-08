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

// Copy and pasted values from dpdk's examples/multi_process/simple_mp
static const char *_MSG_POOL = "MSG_POOL";
static const char *_SEC_2_PRI = "SEC_2_PRI";
static const char *_PRI_2_SEC = "PRI_2_SEC";

struct rte_ring *send_ring, *recv_ring;
struct rte_mempool *message_pool;

int dpdk_send(const char *buf, const unsigned short buf_len)
{
    if (!rte_eal_primary_proc_alive(NULL))
    {
        return -5;
    }
    if (message_pool != NULL)
    {
        printf("MESSAGE POOL IS NOT NULL\n");
    }
    if (send_ring != NULL)
    {
        printf("SEND RING ISNT NULL\n");
    }
    if (recv_ring != NULL)
    {
        printf("RECV RING ISNT NULL\n");
    }

    printf("PRIM PROC ALIVE:%d\n", rte_eal_primary_proc_alive(NULL));

    // returns -1
    printf("CORE ID %d\n", rte_lcore_id());

    // Segfaults eventually
    // rte_mempool_dump(stdout, message_pool);

    void *msg = NULL;
    if (rte_mempool_get(message_pool, &msg) < 0)
        rte_panic("Failed to get message buffer\n");
    snprintf((char *)msg, buf_len, "%s", buf);
    printf("ENQUEING MSG");
    if (rte_ring_enqueue(send_ring, msg) < 0)
    {
        printf("Failed to send message - message discarded\n");
        rte_mempool_put(message_pool, msg);
    }

    return 0;
}

int dpdk_init(void)
{
    // hard coded pci devices to work on my machine
    char *argv[] = {
        "-l",
        "0-1",
        "-w",
        "eddf:00:02.0", // change this for your machine
        "--vdev",
        "net_vdev_netvsc1,iface=eth1",
        "--proc-type=secondary",
        "-n",
        "4",
    };

    int ret;
    ret = rte_eal_init(9, argv);
    if (ret < 0)
    {
        return -1;
    }

    recv_ring = rte_ring_lookup(_PRI_2_SEC);
    send_ring = rte_ring_lookup(_SEC_2_PRI);
    message_pool = rte_mempool_lookup(_MSG_POOL);
    if (send_ring == NULL)
        return -2;
    if (recv_ring == NULL)
        return -3;
    if (message_pool == NULL)
        return -4;

    printf("ON CORE: %d\n", rte_lcore_id());

    // I suspected I was doing something wrong in the send message
    // by not having it run on a dpdk core so I tried to get some memory
    // immediately after initializing and it still fails
    /*
    void *msg = NULL;
    if (rte_mempool_get(message_pool, &msg) < 0)
        rte_panic("Failed to get message buffer\n");
    printf("putting the memory back\n");
    rte_mempool_put(message_pool, msg);
    */

    return 0;
}

int dpdk_cleanup(void)
{
    rte_eal_cleanup();
    return 0;
}
