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
#include <stdlib.h>
#include <arpa/inet.h>

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
#define DEBUG_TX        1
#define DEBUG_RX        1

static const char    *_TX_RING      = "TX_RING";
static const char    *_TX_PREP_RING = "TX_PREP_RING";
static const char    *_RX_PREP_RING = "RX_PREP_RING";
static const char    *_TX_COMP_RING = "TX_COMP_RING";
static const char    *_RX_RING      = "RX_RING";
static const char    *_RX_FILL_RING = "RX_FILL_RING";
static const char    *_RX_MBUF_POOL = "RX_MBUF_POOL";

struct rte_ring      *tx_ring, *tx_completion_ring, *rx_ring, *rx_fill_ring;

static const unsigned MAX_MESSAGE_SIZE = 2048;

#define RX_RING_SIZE    1024
#define TX_RING_SIZE    1024

#define NUM_MBUFS       8192
#define MBUF_CACHE_SIZE 0
#define BURST_SIZE      32

struct rte_mempool *mbuf_pool;
volatile int        quit = 0;

struct rte_mbuf    *tx_bufs[BURST_SIZE];
struct rte_mbuf    *rx_bufs[BURST_SIZE];
struct rte_mbuf    *rx_drop_bufs[BURST_SIZE];
struct rte_mbuf    *rx_final_bufs[BURST_SIZE];

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
static inline int port_init(uint16_t port)
{
  struct rte_eth_conf     port_conf = port_conf_default;
  const uint16_t          rx_rings = 1, tx_rings = 1;
  uint16_t                nb_rxd = RX_RING_SIZE;
  uint16_t                nb_txd = TX_RING_SIZE;
  int                     retval;
  uint16_t                q;
  struct rte_eth_dev_info dev_info;
  struct rte_eth_txconf   txconf;

  if( ! rte_eth_dev_is_valid_port(port) )
    return -1;

  retval = rte_eth_dev_info_get(port, &dev_info);
  if( retval != 0 ) {
    printf("Error during getting device (port %u) info: %s\n", port,
        strerror(-retval));
    return retval;
  }

  if( dev_info.tx_offload_capa & DEV_TX_OFFLOAD_MBUF_FAST_FREE )
    port_conf.txmode.offloads |= DEV_TX_OFFLOAD_MBUF_FAST_FREE;

  /* Configure the Ethernet device. */
  retval = rte_eth_dev_configure(port, rx_rings, tx_rings, &port_conf);
  if( retval != 0 )
    return retval;

  retval = rte_eth_dev_adjust_nb_rx_tx_desc(port, &nb_rxd, &nb_txd);
  if( retval != 0 )
    return retval;

  /* Allocate and set up 1 RX queue per Ethernet port. */
  for( q = 0; q < rx_rings; q++ ) {
    retval = rte_eth_rx_queue_setup(
        port, q, nb_rxd, rte_eth_dev_socket_id(port), NULL, mbuf_pool);
    if( retval < 0 )
      return retval;
  }

  txconf          = dev_info.default_txconf;
  txconf.offloads = port_conf.txmode.offloads;
  /* Allocate and set up 1 TX queue per Ethernet port. */
  for( q = 0; q < tx_rings; q++ ) {
    retval = rte_eth_tx_queue_setup(
        port, q, nb_txd, rte_eth_dev_socket_id(port), NULL);
    if( retval < 0 )
      return retval;
  }

  /* Start the Ethernet port. */
  retval = rte_eth_dev_start(port);
  if( retval < 0 )
    return retval;

  /* Display the port MAC address. */
  struct rte_ether_addr addr;
  retval = rte_eth_macaddr_get(port, &addr);
  if( retval != 0 )
    return retval;

  printf("Port %u MAC: %02" PRIx8 " %02" PRIx8 " %02" PRIx8 " %02" PRIx8
         " %02" PRIx8 " %02" PRIx8 "\n",
      port, addr.addr_bytes[0], addr.addr_bytes[1], addr.addr_bytes[2],
      addr.addr_bytes[3], addr.addr_bytes[4], addr.addr_bytes[5]);

  /* Enable RX in promiscuous mode for the Ethernet device. */
  retval = rte_eth_promiscuous_enable(port);
  if( retval != 0 )
    return retval;

  return 0;
}

static inline int init_ports(void)
{
  uint16_t portid;

  /* Initialize all ports. */
  RTE_ETH_FOREACH_DEV(portid)
  {
    if( port_init(portid) != 0 ) {
      printf("Cannot init port %d \n", portid);
      return -1;
    }
  }

  return 0;
}

static void print_mbufs(struct rte_mbuf **mbufs, int length, char *type)
{
  if( length != 0 ) {
    printf("TYPE: %s\n", type);
  }
  for( int i = 0; i < length; i++ ) {
    uint8_t *data     = rte_pktmbuf_mtod(mbufs[i], uint8_t *);
    int      data_len = rte_pktmbuf_data_len(mbufs[i]);
    printf("Data Len: %d\n", data_len);
    for( int j = 0; j < data_len; j++ ) {
      printf("%02X ", data[j]);
    }
    printf("\n");
  }
}


static int receive(uint16_t port)
{
  int should_drop = 0;
  int allowed     = rte_ring_dequeue_burst(
          rx_fill_ring, (void **) &rx_bufs[0], BURST_SIZE, NULL);
  if( allowed == 0 ) {
    allowed     = BURST_SIZE;
    should_drop = 1;
    *rx_bufs    = *rx_drop_bufs;
  }

  uint16_t recv = rte_eth_rx_burst(port, 0, &rx_bufs[0], allowed);

  if( should_drop ) {
    if( recv != 0 ) {
      printf("DROPPING RECV. No fill packets\n");
    }
    return 0;
  }

  int enqueueCount = 0;
  for( int i = 0; i < recv; i++ ) {
    struct rte_ether_hdr *eth_h =
        rte_pktmbuf_mtod(rx_bufs[i], struct rte_ether_hdr *);
    // drop this because it's breaking everything for now
    if( ntohs(eth_h->ether_type) == RTE_ETHER_TYPE_IPV6 ) {
      rte_ring_enqueue(rx_fill_ring, &rx_bufs[i]);
    } else if( ntohs(eth_h->ether_type) == RTE_ETHER_TYPE_ARP ) {
      struct rte_arp_hdr *arp = (struct rte_arp_hdr *) (eth_h + 1);
      if( ntohs(arp->arp_opcode) == RTE_ARP_OP_REPLY ) {
        char           ethStr[50];
        struct in_addr add;
        add.s_addr = arp->arp_data.arp_sip;
        rte_ether_format_addr(ethStr, 50, &arp->arp_data.arp_sha);
        char *ipAddr = inet_ntoa(add);
        char  str[100];
        sprintf(str, "ip neigh replace %s dev eth1 lladdr %s nud reachable",
            ipAddr, ethStr);
        printf("running system command: %s\n", str);
        if( system(str) != 0 ) {
          printf("Failed to add neighbor\n");
        }
      }

      // unsure whether to recycle this packet or send it along
      rte_ring_enqueue(rx_fill_ring, &rx_bufs[i]);
      // rx_final_bufs[enqueueCount++] = rx_bufs[i];
    } else {
      rx_final_bufs[enqueueCount++] = rx_bufs[i];
    }
  }

#if DEBUG_RX
  print_mbufs(rx_final_bufs, enqueueCount, "RX");
#endif

  if( recv != allowed ) {
    rte_ring_enqueue_bulk(
        rx_fill_ring, (void **) &rx_bufs[recv - 1], allowed - recv, NULL);
  }

  if( enqueueCount != 0 ) {
    if( rte_ring_enqueue_bulk(
            rx_ring, (void **) &rx_final_bufs[0], enqueueCount, NULL) == 0 ) {
      printf("FAILED TO HAND TO RX RING");
    }
  }

  return recv;
}

static int transmit(uint16_t port)
{
  // might be better to manually move the consumer tail manually?
  int allowed =
      rte_ring_dequeue_burst(tx_ring, (void **) &tx_bufs, BURST_SIZE, NULL);
  if( allowed == 0 ) {
    return 0;
  }

#if DEBUG_TX
  print_mbufs(tx_bufs, allowed, "TX");
#endif

  uint16_t txed = rte_eth_tx_burst(port, 0, &tx_bufs[0], allowed);

  rte_ring_enqueue_bulk(tx_completion_ring, (void **) &tx_bufs[0], txed, NULL);

  if( txed != allowed ) {
    // PUT back unused bufs? This is almost certainly problematic
    // rte_ring_enqueue_bulk(tx_ring, (void **)&tx_bufs[txed - 1], allowed -
    // txed, NULL);
    printf("DROPPING A PACKET\n");
  }

  return txed;
}

static int lcore_run(__attribute__((unused)) void *arg)
{
  /* Check that there is an even number of ports to send/receive on. */
  int nb_ports = rte_eth_dev_count_avail();
  if( nb_ports < 1 ) {
    printf("ERROR: failed to init. Bad number of ports: %d\n", nb_ports);
    return -1;
  }
  unsigned lcore_id = rte_lcore_id();
  uint16_t port;

  printf("Starting core %u\n", lcore_id);

  int inited = init_ports();
  if( inited < 0 ) {
    rte_eal_cleanup();
    rte_exit(-1, "Failed to init ports\n");
  }

  printf("Ports initialized\n");

  /*
   * Check that the port is on the same NUMA node as the polling thread
   * for best performance.
   */
  RTE_ETH_FOREACH_DEV(port)
  {
    if( rte_eth_dev_socket_id(port) > 0 &&
        rte_eth_dev_socket_id(port) != (int) rte_socket_id() )
      printf(
          "WARNING, port %u is on remote NUMA node to "
          "polling thread.\n\tPerformance will "
          "not be optimal.\n",
          port);

    printf("Using PORT: %d\n", port);

    while( ! quit ) {
      int recved = receive(port);
      int sent   = transmit(port);
      if( recved == 0 && sent == 0 ) {
        usleep(10);
      }
    }
  }

  return 0;
}

static void signal_handler(int signum)
{
  /* When we receive a RTMIN or SIGINT signal, stop kni processing */
  if( signum == SIGRTMIN || signum == SIGINT ) {
    printf("\nSIGRTMIN/SIGINT received. processing stopping.\n");
    quit = 1;
    return;
  }
}

int main(int argc, char **argv)
{
  const unsigned flags          = 0;
  const unsigned ring_size      = 512;
  const unsigned fill_ring_size = 1024;
  const unsigned pool_size      = 1024;
  const unsigned pool_cache     = 0;
  const unsigned priv_data_sz   = 0;

  int            ret;
  unsigned       lcore_id;

  signal(SIGRTMIN, signal_handler);
  signal(SIGINT, signal_handler);

  ret = rte_eal_init(argc, argv);
  if( ret < 0 )
    rte_exit(EXIT_FAILURE, "Cannot init EAL\n");

  if( rte_eal_process_type() != RTE_PROC_PRIMARY ) {
    rte_exit(EXIT_FAILURE, "This program must be run as primary");
  }

  tx_ring = rte_ring_create(_TX_RING, ring_size, SOCKET_ID_ANY, flags);
  rx_ring = rte_ring_create(_RX_RING, ring_size, SOCKET_ID_ANY, flags);
  rx_fill_ring =
      rte_ring_create(_RX_FILL_RING, fill_ring_size, SOCKET_ID_ANY, flags);
  tx_completion_ring =
      rte_ring_create(_TX_COMP_RING, ring_size, SOCKET_ID_ANY, flags);

  mbuf_pool = rte_pktmbuf_pool_create(_RX_MBUF_POOL, NUM_MBUFS * 1,
      MBUF_CACHE_SIZE, 0, RTE_MBUF_DEFAULT_BUF_SIZE, SOCKET_ID_ANY);
  rte_ring_create(_TX_PREP_RING, ring_size, SOCKET_ID_ANY, 0);
  rte_ring_create(_RX_PREP_RING, ring_size, SOCKET_ID_ANY, 0);

  if( tx_ring == NULL )
    rte_exit(EXIT_FAILURE, "Problem getting sending ring\n");
  if( rx_ring == NULL )
    rte_exit(EXIT_FAILURE, "Problem getting receiving ring\n");
  if( rx_fill_ring == NULL )
    rte_exit(EXIT_FAILURE, "Problem getting fill ring\n");
  if( tx_completion_ring == NULL )
    rte_exit(EXIT_FAILURE, "Problem getting comp ring\n");
  if( mbuf_pool == NULL )
    rte_exit(EXIT_FAILURE, "Problem getting message pool\n");

  // reserve some rx bufs to keep things empty
  if( rte_mempool_get_bulk(
          mbuf_pool, (void **) &rx_drop_bufs[0], BURST_SIZE) != 0 ) {
    RTE_LOG(ERR, APP, "Unable to get objects from mempool\n");
  } else {
    RTE_LOG(INFO, APP, "GOT DATA SUCCESSFULLY %d \n",
        rte_mempool_avail_count(mbuf_pool));
  }

  RTE_LOG(INFO, APP, "Finished Process Init.\n");

  /* call lcore_recv() on every slave lcore */
  RTE_LCORE_FOREACH_SLAVE(lcore_id)
  {
    rte_eal_remote_launch(lcore_run, NULL, lcore_id);
  }

  printf("MAIN CORE: %d\n", rte_lcore_id());
  /* call cmd prompt on master lcore */
  struct cmdline *cl = cmdline_stdin_new(simple_mp_ctx, "\nsimple_mp > ");
  if( cl == NULL )
    rte_exit(EXIT_FAILURE, "Cannot create cmdline instance\n");
  cmdline_interact(cl);
  cmdline_stdin_exit(cl);

  rte_eal_mp_wait_lcore();
  return 0;
}
