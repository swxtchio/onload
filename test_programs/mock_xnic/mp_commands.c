/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2010-2014 Intel Corporation
 */
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <stdarg.h>
#include <inttypes.h>
#include <stdio.h>
#include <termios.h>
#include <errno.h>
#include <sys/queue.h>

#include <rte_common.h>
#include <rte_memory.h>
#include <rte_eal.h>
#include <rte_atomic.h>
#include <rte_branch_prediction.h>
#include <rte_launch.h>
#include <rte_log.h>
#include <rte_per_lcore.h>
#include <rte_lcore.h>
#include <rte_ring.h>
#include <rte_debug.h>
#include <rte_mempool.h>
#include <rte_string_fns.h>
#include <rte_ethdev.h>

#include <cmdline_rdline.h>
#include <cmdline_parse.h>
#include <cmdline_parse_string.h>
#include <cmdline_socket.h>
#include <cmdline.h>
#include "mp_commands.h"

/**********************************************************/

/**********************************************************/

struct cmd_quit_result {
  cmdline_fixed_string_t quit;
};

static void cmd_quit_parsed(__attribute__((unused)) void *parsed_result,
    struct cmdline *cl, __attribute__((unused)) void *data)
{
  quit = 1;
  cmdline_quit(cl);
}

cmdline_parse_token_string_t cmd_quit_quit =
    TOKEN_STRING_INITIALIZER(struct cmd_quit_result, quit, "quit");

cmdline_parse_inst_t cmd_quit = {
	.f = cmd_quit_parsed, /* function to call */
	.data = NULL,		  /* 2nd arg of func */
	.help_str = "close the application",
	.tokens = {
		/* token list, NULL terminated */
		(void *)&cmd_quit_quit,
		NULL,
	},
};

/**********************************************************/

struct cmd_help_result {
  cmdline_fixed_string_t help;
};

static void cmd_help_parsed(__attribute__((unused)) void *parsed_result,
    struct cmdline *cl, __attribute__((unused)) void *data)
{
  cmdline_printf(cl,
      "Simple demo example of multi-process in RTE\n\n"
      "This is a readline-like interface that can be used to\n"
      "send commands to the simple app. Commands supported are:\n\n"
      "- send [string]\n"
      "- help\n"
      "- quit\n\n");
}

cmdline_parse_token_string_t cmd_help_help =
    TOKEN_STRING_INITIALIZER(struct cmd_help_result, help, "help");

cmdline_parse_inst_t cmd_help = {
	.f = cmd_help_parsed, /* function to call */
	.data = NULL,		  /* 2nd arg of func */
	.help_str = "show help",
	.tokens = {
		/* token list, NULL terminated */
		(void *)&cmd_help_help,
		NULL,
	},
};

static void dump_rings(__attribute__((unused)) void *parsed_result,
    struct cmdline *cl, __attribute__((unused)) void *data)
{
  rte_mempool_dump(stdout, mbuf_pool);
  cmdline_printf(cl,
      "TX Ring Count: %d:%d\n"
      "TX Comp Ring Count: %d:%d\n"
      "RX Fill Ring Count: %d:%d\n"
      "RX Ring Count: %d:%d\n"
      "RX Prep Ring Count: %d:%d\n"
      "TX Prep Ring Count: %d:%d\n"
      "RX Pending Ring Count: %d:%d\n",
      rte_ring_count(tx_ring), rte_ring_get_capacity(tx_ring),
      rte_ring_count(tx_completion_ring),
      rte_ring_get_capacity(tx_completion_ring), rte_ring_count(rx_fill_ring),
      rte_ring_get_capacity(rx_fill_ring), rte_ring_count(rx_ring),
      rte_ring_get_capacity(rx_ring), rte_ring_count(rx_prep_ring),
      rte_ring_get_capacity(rx_prep_ring), rte_ring_count(tx_prep_ring),
      rte_ring_get_capacity(tx_prep_ring), rte_ring_count(rx_pending_ring),
      rte_ring_get_capacity(rx_pending_ring));
}

cmdline_parse_token_string_t cmd_dump_rings_help =
    TOKEN_STRING_INITIALIZER(struct cmd_help_result, help, "dump");

cmdline_parse_inst_t cmd_dump_rings= {
	.f = dump_rings, /* function to call */
	.data = NULL,		  /* 2nd arg of func */
	.help_str = "show ring state",
	.tokens = {
		/* token list, NULL terminated */
		(void *)&cmd_dump_rings_help,
		NULL,
	},
};

/****** CONTEXT (list of instruction) */
cmdline_parse_ctx_t simple_mp_ctx[] = {
  (cmdline_parse_inst_t *) &cmd_quit,
  (cmdline_parse_inst_t *) &cmd_help,
  (cmdline_parse_inst_t *) &cmd_dump_rings,
  NULL,
};
