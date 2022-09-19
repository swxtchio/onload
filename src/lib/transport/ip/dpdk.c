#include "dpdk.h"
#include <rte_common.h>
#include <rte_memory.h>
#include <rte_launch.h>
#include <rte_eal.h>
#include <rte_per_lcore.h>
#include <rte_lcore.h>
#include <rte_debug.h>
#include <rte_atomic.h>
#include <rte_branch_prediction.h>
#include <rte_ring.h>
#include <rte_log.h>
#include <rte_mempool.h>
#include <rte_ether.h>
#include <rte_ethdev.h>
#include "string.h"

// Copy and pasted values from dpdk's examples/multi_process/simple_mp
static const char *_MSG_POOL = "MSG_POOL";
static const char *_SEC_2_PRI = "SEC_2_PRI";
static const char *_PRI_2_SEC = "PRI_2_SEC";
static volatile int quit = 0;

struct rte_ring *send_ring, *recv_ring;
struct rte_mempool *message_pool;
ci_netif *_netif;
citp_waitable *_wait;

int dpdk_send(ci_ip_pkt_fmt *pkt)
{
    if (!rte_eal_primary_proc_alive(NULL))
    {
        return -5;
    }

    struct rte_mbuf *msg = NULL;
    if (rte_mempool_get(message_pool, (void **)&msg) < 0)
    {
        dpdk_cleanup();
        rte_panic("Failed to get message buffer\n");
    }

    char *start = rte_pktmbuf_append(msg, pkt->pay_len);
    if (start == NULL)
    {
        printf("FAILED to append data");
        rte_pktmbuf_free(msg);
    }

    memcpy(start, pkt->dma_start, pkt->pay_len);
    if (rte_ring_enqueue(send_ring, msg) < 0)
    {
        printf("Failed to send message - message discarded\n");
        rte_pktmbuf_free(msg);
    }

    return 0;
}

static int __dpdk_recv(__attribute__((unused)) void *arg)
{
    ci_ip_pkt_fmt *pkt;
    int ip_paylen, hdr_size, ip_tot_len;
    while (!quit)
    {
        if (_netif == NULL)
        {
            usleep(10);
            continue;
        }

        struct rte_mbuf *msg;
        if (rte_ring_dequeue(recv_ring, (void **)&msg) < 0)
        {
            usleep(5);
            continue;
        }

        pkt = ci_netif_pkt_alloc(_netif, 0);
        pkt->flags |= CI_PKT_FLAG_RX;
        _netif->state->n_rx_pkts++;
        oo_tx_pkt_layout_init(pkt);

        uint8_t *data = rte_pktmbuf_mtod(msg, uint8_t *);
        uint16_t dataLen = rte_pktmbuf_data_len(msg);
        memcpy(pkt->dma_start, data, dataLen);
        pkt->pay_len = dataLen;

        ci_ip4_hdr *ip = oo_ip_hdr(pkt);
        hdr_size = CI_IP4_IHL(ip);
        ip_tot_len = CI_BSWAP_BE16(ip->ip_tot_len_be16);
        ip_paylen = ip_tot_len - hdr_size;
        char *payload = (char *)ip + hdr_size;

        ci_udp_handle_rx(_netif, pkt, (ci_udp_hdr *)payload, ip_paylen);

        rte_pktmbuf_free(msg);
    }

    return 0;
}

void dpdk_recv(ci_netif *netif, citp_waitable *wait)
{
    _netif = netif;
    _wait = wait;
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

    int lcore_id;
    RTE_LCORE_FOREACH_SLAVE(lcore_id)
    {
        rte_eal_remote_launch(__dpdk_recv, NULL, lcore_id);
    }

    return 0;
}

int dpdk_cleanup(void)
{
    quit = 1;
    rte_eal_cleanup();
    return 0;
}
