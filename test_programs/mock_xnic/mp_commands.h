/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2010-2014 Intel Corporation
 */

#ifndef _SIMPLE_MP_COMMANDS_H_
#define _SIMPLE_MP_COMMANDS_H_

extern struct rte_ring *tx_ring, *tx_completion_ring, *rx_ring, *rx_fill_ring,
    *rx_prep_ring, *tx_prep_ring, *rx_pending_ring;
extern struct rte_mempool *mbuf_pool;
extern volatile int quit;

extern cmdline_parse_ctx_t simple_mp_ctx[];

#endif /* _SIMPLE_MP_COMMANDS_H_ */
