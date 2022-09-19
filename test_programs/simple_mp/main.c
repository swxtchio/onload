/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2010-2014 Intel Corporation
 */

/*
 * This sample application is a simple multi-process application which
 * demostrates sharing of queues and memory pools between processes, and
 * using those queues/pools for communication between the processes.
 *
 * Application is designed to run with two processes, a primary and a
 * secondary, and each accepts commands on the commandline, the most
 * important of which is "send", which just sends a string to the other
 * process.
 */

#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <inttypes.h>
#include <stdarg.h>
#include <errno.h>
#include <unistd.h>
#include <termios.h>
#include <sys/queue.h>
#include <signal.h>

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
#include <rte_ip.h>
#include <cmdline_rdline.h>
#include <cmdline_parse.h>
#include <cmdline_parse_string.h>
#include <cmdline_socket.h>
#include <cmdline.h>
#include "mp_commands.h"

#define RTE_LOGTYPE_APP RTE_LOGTYPE_USER1

static const char *_MSG_POOL = "MSG_POOL";
static const char *_SEC_2_PRI = "SEC_2_PRI";
static const char *_PRI_2_SEC = "PRI_2_SEC";
static const unsigned MAX_MESSAGE_SIZE = 2048;

#define RX_RING_SIZE 1024
#define TX_RING_SIZE 1024

#define NUM_MBUFS 8191
#define MBUF_CACHE_SIZE 250
#define BURST_SIZE 32

struct rte_ring *send_ring, *recv_ring;
struct rte_mempool *message_pool;
volatile int quit = 0;

static const struct rte_eth_conf port_conf_default = {
	.rxmode = {
		.max_rx_pkt_len = RTE_ETHER_MAX_LEN,
	},
};

/* basicfwd.c: Basic DPDK skeleton forwarding example. */

/*
 * Initializes a given port using global settings and with the RX buffers
 * coming from the mbuf_pool passed as a parameter.
 */
static inline int
port_init(uint16_t port, struct rte_mempool *mbuf_pool)
{
	struct rte_eth_conf port_conf = port_conf_default;
	const uint16_t rx_rings = 1, tx_rings = 1;
	uint16_t nb_rxd = RX_RING_SIZE;
	uint16_t nb_txd = TX_RING_SIZE;
	int retval;
	uint16_t q;
	struct rte_eth_dev_info dev_info;
	struct rte_eth_txconf txconf;

	if (!rte_eth_dev_is_valid_port(port))
		return -1;

	retval = rte_eth_dev_info_get(port, &dev_info);
	if (retval != 0)
	{
		printf("Error during getting device (port %u) info: %s\n",
			   port, strerror(-retval));
		return retval;
	}

	if (dev_info.tx_offload_capa & DEV_TX_OFFLOAD_MBUF_FAST_FREE)
		port_conf.txmode.offloads |=
			DEV_TX_OFFLOAD_MBUF_FAST_FREE;

	/* Configure the Ethernet device. */
	retval = rte_eth_dev_configure(port, rx_rings, tx_rings, &port_conf);
	if (retval != 0)
		return retval;

	retval = rte_eth_dev_adjust_nb_rx_tx_desc(port, &nb_rxd, &nb_txd);
	if (retval != 0)
		return retval;

	/* Allocate and set up 1 RX queue per Ethernet port. */
	for (q = 0; q < rx_rings; q++)
	{
		retval = rte_eth_rx_queue_setup(port, q, nb_rxd,
										rte_eth_dev_socket_id(port), NULL, mbuf_pool);
		if (retval < 0)
			return retval;
	}

	txconf = dev_info.default_txconf;
	txconf.offloads = port_conf.txmode.offloads;
	/* Allocate and set up 1 TX queue per Ethernet port. */
	for (q = 0; q < tx_rings; q++)
	{
		retval = rte_eth_tx_queue_setup(port, q, nb_txd,
										rte_eth_dev_socket_id(port), NULL);
		if (retval < 0)
			return retval;
	}

	/* Start the Ethernet port. */
	retval = rte_eth_dev_start(port);
	if (retval < 0)
		return retval;

	/* Display the port MAC address. */
	struct rte_ether_addr addr;
	retval = rte_eth_macaddr_get(port, &addr);
	if (retval != 0)
		return retval;

	printf("Port %u MAC: %02" PRIx8 " %02" PRIx8 " %02" PRIx8
		   " %02" PRIx8 " %02" PRIx8 " %02" PRIx8 "\n",
		   port,
		   addr.addr_bytes[0], addr.addr_bytes[1],
		   addr.addr_bytes[2], addr.addr_bytes[3],
		   addr.addr_bytes[4], addr.addr_bytes[5]);

	/* Enable RX in promiscuous mode for the Ethernet device. */
	retval = rte_eth_promiscuous_enable(port);
	if (retval != 0)
		return retval;

	return 0;
}

static inline int init_ports(void)
{
	uint16_t portid;

	/* Initialize all ports. */
	RTE_ETH_FOREACH_DEV(portid)
	{
		if (port_init(portid, message_pool) != 0)
		{
			printf("Cannot init port %d \n", portid);
			return -1;
		}
	}

	return 0;
}

static int
lcore_run(__attribute__((unused)) void *arg)
{
	/* Check that there is an even number of ports to send/receive on. */
	int nb_ports = rte_eth_dev_count_avail();
	if (nb_ports < 1)
	{
		printf("ERROR: failed to init. Bad number of ports: %d\n", nb_ports);
		return -1;
	}
	unsigned lcore_id = rte_lcore_id();
	uint16_t port;
	uint16_t portToUse;

	printf("Starting core %u\n", lcore_id);

	int inited = init_ports();
	if (inited < 0)
	{
		return -1;
	}

	/*
	 * Check that the port is on the same NUMA node as the polling thread
	 * for best performance.
	 */
	RTE_ETH_FOREACH_DEV(port)
	{
		if (rte_eth_dev_socket_id(port) > 0 &&
			rte_eth_dev_socket_id(port) !=
				(int)rte_socket_id())
			printf("WARNING, port %u is on remote NUMA node to "
				   "polling thread.\n\tPerformance will "
				   "not be optimal.\n",
				   port);
		portToUse = port;
	}

	printf("Using PORT: %d", portToUse);

	int send, recv;
	struct rte_mbuf *bufs[BURST_SIZE];
	struct rte_mbuf **rx_bufs = bufs;
	struct rte_mbuf *tx_bufs[1];

	struct rte_ether_addr src_addr;
	struct rte_ether_addr dst_addr;
	rte_eth_macaddr_get(portToUse, &src_addr);
	rte_ether_unformat_addr("00:0d:3a:8c:f7:63", &dst_addr);
	// rte_ether_unformat_addr("00:0d:3a:8c:ff:ae", &src_addr);
	while (!quit)
	{
		struct rte_mbuf *msg;
		send = rte_ring_dequeue(recv_ring, (void **)&msg);
		if (send == 0)
		{
			uint8_t *data = rte_pktmbuf_mtod(msg, uint8_t *);
			uint16_t dataLen = rte_pktmbuf_data_len(msg);
			struct rte_ipv4_hdr *hdr = (struct rte_ipv4_hdr *)&data[14];
			struct rte_ether_hdr *eth_hdr = (struct rte_ether_hdr *)(&data[0]);
			hdr->src_addr = 94634506;  // my eth1 address 10.2.164.7
			hdr->dst_addr = 128188938; // hardcoded destination address //10.2.164.5
			hdr->hdr_checksum = 0;
			hdr->hdr_checksum = rte_ipv4_cksum(hdr);
			rte_ether_addr_copy(&src_addr, &eth_hdr->s_addr);
			rte_ether_addr_copy(&dst_addr, &eth_hdr->d_addr);

			tx_bufs[0] = msg;
			/*
			for (int i = 0; i < 40; i++)
			{
				printf("%02X ", data[i]);
			}
			printf("\n");
			*/
			int sent = rte_eth_tx_burst(portToUse, 0, tx_bufs, 1);
			if (sent != 1)
			{
				printf("failed to burst tx: %d\n", sent);
			}
			rte_pktmbuf_free(msg);
		}
		recv = rte_eth_rx_burst(portToUse, 0, rx_bufs, BURST_SIZE);
		if (recv != 0)
		{
			int index = 0;
			struct rte_mbuf *pktsToSend[BURST_SIZE];
			for (int i = 0; i < recv; i++)
			{
				uint8_t *data = rte_pktmbuf_mtod(rx_bufs[i], uint8_t *);
				uint16_t dataLen = rte_pktmbuf_data_len(rx_bufs[i]);
				struct rte_ether_hdr *hdr = rte_pktmbuf_mtod(rx_bufs[i], struct rte_ether_hdr *);
				if (hdr->ether_type == rte_cpu_to_be_16(RTE_ETHER_TYPE_IPV6) || rx_bufs[i]->packet_type & RTE_PTYPE_L4_TCP)
				{
					// printf("dropping iv6 packet\n");
				}
				else
				{
					uint8_t *data = rte_pktmbuf_mtod(rx_bufs[i], uint8_t *);
					uint16_t dataLen = rte_pktmbuf_data_len(rx_bufs[i]);
					struct rte_ipv4_hdr *hdr = (struct rte_ipv4_hdr *)&data[14];
					//  swap back
					hdr->dst_addr = 128057866; // 10.2.162.7
					hdr->src_addr = 94503434;  // 10.2.162.5
					hdr->hdr_checksum = 0;
					hdr->hdr_checksum = rte_ipv4_cksum(hdr);
					/*
					printf("Received\n");
					for (int i = 0; i < dataLen; i++)
					{
						printf("%02X ", data[i]);
					}
					printf("\n");
					*/
					pktsToSend[index++] = rx_bufs[i];
				}
			}
			if (index > 0)
			{
				rte_ring_enqueue_bulk(send_ring, (void *const *)pktsToSend, index, NULL);
			}
		}
		if (send == 0 && recv == 0)
		{
			usleep(5);
		}
	}
	return 0;
}

static void
signal_handler(int signum)
{
	/* When we receive a RTMIN or SIGINT signal, stop kni processing */
	if (signum == SIGRTMIN || signum == SIGINT)
	{
		printf("\nSIGRTMIN/SIGINT received. processing stopping.\n");
		quit = 1;
		return;
	}
}

int main(int argc, char **argv)
{
	const unsigned flags = 0;
	const unsigned ring_size = 64;
	const unsigned pool_size = 1024;
	const unsigned pool_cache = 32;
	const unsigned priv_data_sz = 0;

	int ret;
	unsigned lcore_id;

	signal(SIGRTMIN, signal_handler);
	signal(SIGINT, signal_handler);

	ret = rte_eal_init(argc, argv);
	if (ret < 0)
		rte_exit(EXIT_FAILURE, "Cannot init EAL\n");

	if (rte_eal_process_type() == RTE_PROC_PRIMARY)
	{
		send_ring = rte_ring_create(_PRI_2_SEC, ring_size, rte_socket_id(), flags);
		recv_ring = rte_ring_create(_SEC_2_PRI, ring_size, rte_socket_id(), flags);
		message_pool = rte_pktmbuf_pool_create(_MSG_POOL, NUM_MBUFS * 1,
											   MBUF_CACHE_SIZE, 0, RTE_MBUF_DEFAULT_BUF_SIZE, rte_socket_id());
	}
	else
	{
		recv_ring = rte_ring_lookup(_PRI_2_SEC);
		send_ring = rte_ring_lookup(_SEC_2_PRI);
		message_pool = rte_mempool_lookup(_MSG_POOL);
	}
	if (send_ring == NULL)
		rte_exit(EXIT_FAILURE, "Problem getting sending ring\n");
	if (recv_ring == NULL)
		rte_exit(EXIT_FAILURE, "Problem getting receiving ring\n");
	if (message_pool == NULL)
		rte_exit(EXIT_FAILURE, "Problem getting message pool\n");

	RTE_LOG(INFO, APP, "Finished Process Init.\n");

	/* call lcore_recv() on every slave lcore */
	RTE_LCORE_FOREACH_SLAVE(lcore_id)
	{
		rte_eal_remote_launch(lcore_run, NULL, lcore_id);
	}

	printf("MAIN CORE: %d\n", rte_lcore_id());
	/* call cmd prompt on master lcore */
	struct cmdline *cl = cmdline_stdin_new(simple_mp_ctx, "\nsimple_mp > ");
	if (cl == NULL)
		rte_exit(EXIT_FAILURE, "Cannot create cmdline instance\n");
	cmdline_interact(cl);
	cmdline_stdin_exit(cl);

	rte_eal_mp_wait_lcore();
	return 0;
}
