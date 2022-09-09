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
#include "string.h"

// Copy and pasted values from dpdk's examples/multi_process/simple_mp
static const char *_MSG_POOL = "MSG_POOL";
static const char *_SEC_2_PRI = "SEC_2_PRI";
static const char *_PRI_2_SEC = "PRI_2_SEC";

struct rte_ring *send_ring, *recv_ring;
struct rte_mempool *message_pool;
struct rte_mempool_cache *cache;

typedef struct Message
{
    uint8_t Data[2000];
    uint16_t DataLen;
} Message;

int dpdk_send(ci_ip_pkt_fmt *pkt)
{
    if (!rte_eal_primary_proc_alive(NULL))
    {
        return -5;
    }

    int size = oo_tx_l3_len(pkt);
    void *msg = NULL;
    if (rte_mempool_get(message_pool, &msg) < 0)
    {
        dpdk_cleanup();
        rte_panic("Failed to get message buffer\n");
    }
    Message *data = (Message *)msg;
    memcpy(data->Data, oo_tx_l3_hdr(pkt), size);
    data->DataLen = (uint16_t)size;
    // snprintf((char *)msg, 9, "hi there");
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
    printf("INITIALIZING\n");
    ret = rte_eal_init(9, argv);
    printf("INITIALIZED\n");
    if (ret < 0)
    {
        return -1;
    }

    recv_ring = rte_ring_lookup(_PRI_2_SEC);
    send_ring = rte_ring_lookup(_SEC_2_PRI);
    message_pool = rte_mempool_lookup(_MSG_POOL);
    cache = rte_mempool_cache_create(32, rte_socket_id());
    if (send_ring == NULL)
        return -2;
    if (recv_ring == NULL)
        return -3;
    if (message_pool == NULL)
        return -4;
    if (cache == NULL)
        return -5;

    return 0;
}

int dpdk_cleanup(void)
{
    rte_eal_cleanup();
    return 0;
}
