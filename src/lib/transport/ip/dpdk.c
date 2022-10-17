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
#include "ip_internal.h"

int dpdk_init(void)
{
    // hard coded pci devices to work on my machine
    char *argv[] = {
        "-l",
        "1",
        "-w",
        "06e6:00:02.0", // change this for your machine
        "--vdev",
        "net_vdev_netvsc1,iface=eth1",
        "--proc-type=secondary",
    };

    int ret;
    ci_log("INITING DPDK");
    ret = rte_eal_init(7, argv);
    ci_log("INITIED DPDK");
    if (ret < 0)
    {
        dpdk_cleanup();
        return -1;
    }

    return 0;
}

int dpdk_cleanup(void)
{
    rte_eal_cleanup();
    return 0;
}
