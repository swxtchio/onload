#include <rte_eal.h>
#include "internal.h"

int dpdk_cleanup(void)
{
  rte_eal_cleanup();
  return 0;
}

int dpdk_init(void)
{
  char *argv[] = {
    "-l",
    "1",
    "--proc-type=secondary",
  };

  int ret;
  ret = rte_eal_init(3, argv);
  if( ret < 0 ) {
    LOG_S(ci_log("Unable to intialize DPDK"));
    return -1;
  }

  ret = rte_eal_primary_proc_alive(NULL);
  if( ret == 0 ) {
    LOG_S(ci_log("DPDK Primary process is not alive"));
    dpdk_cleanup();
    return -2;
  }

  return 0;
}
