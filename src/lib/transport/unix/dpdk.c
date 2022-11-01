#include <rte_eal.h>
#include "internal.h"

static char *__dpdk_eal_argv[] = { "-l", "1", "--proc-type=secondary",
  "--log-level", "0" };

/* These values should be kept in sync with the argv list above. The last two
 * should always be the log level so that we can conditionally enable or
 * disable verbose logging by simply changing the number of args passed to
 * dpdk*/
static const int __dpdk_eal_argc = 5;
static const int __dpdk_eal_verbose_argc = __dpdk_eal_argc - 2;

int dpdk_cleanup(void)
{
  rte_eal_cleanup();
  return 0;
}

int dpdk_init(void)
{
  int ret;
  int argc = __dpdk_eal_argc;
  Log_V(ci_log("Enabling Verbose DPDK initialization logging");
        argc = __dpdk_eal_verbose_argc);
  ret = rte_eal_init(argc, __dpdk_eal_argv);
  if( ret < 0 ) {
    LOG_U(ci_log("Unable to intialize DPDK"));
    return -1;
  }

  ret = rte_eal_primary_proc_alive(NULL);
  if( ret == 0 ) {
    LOG_U(ci_log("DPDK Primary process is not alive"));
    dpdk_cleanup();
    return -2;
  }

  return 0;
}
