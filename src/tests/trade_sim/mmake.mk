# SPDX-License-Identifier: BSD-2-Clause
# X-SPDX-Copyright-Text: (c) Copyright 2018-2020 Xilinx, Inc.

TEST_APPS	:= exchange \
		trader_onload_ds_efvi

TARGETS		:= $(TEST_APPS:%=$(AppPattern))


all: $(TARGETS)

clean:
	@$(MakeClean)

MMAKE_DPDK := -L/usr/local/lib/x86_64-linux-gnu
MMAKE_DPDK_LIBS :=-lrte_bpf -lrte_flow_classify -lrte_pipeline -lrte_table -lrte_port -lrte_fib -lrte_ipsec -lrte_vhost -lrte_stack -lrte_security -lrte_sched -lrte_reorder -lrte_rib -lrte_rcu -lrte_rawdev -lrte_pdump -lrte_power -lrte_member -lrte_lpm -lrte_latencystats -lrte_kni -lrte_jobstats -lrte_ip_frag -lrte_gso -lrte_gro -lrte_eventdev -lrte_efd -lrte_distributor -lrte_cryptodev -lrte_compressdev -lrte_cfgfile -lrte_bitratestats -lrte_bbdev -lrte_acl -lrte_timer -lrte_hash -lrte_metrics -lrte_cmdline -lrte_pci -lrte_ethdev -lrte_meter -lrte_net -lrte_mbuf -lrte_mempool -lrte_ring -lrte_eal -lrte_kvargs


exchange: exchange.o utils.o
exchange: MMAKE_LIBS     += $(LINK_ONLOAD_EXT_LIB)
exchange: MMAKE_LIB_DEPS += $(ONLOAD_EXT_LIB_DEPEND)

trader_onload_ds_efvi: trader_onload_ds_efvi.o utils.o
trader_onload_ds_efvi: \
	MMAKE_LIBS     += $(LINK_ONLOAD_EXT_LIB) $(LINK_CIUL_LIB)
trader_onload_ds_efvi: \
	MMAKE_LIB_DEPS += $(ONLOAD_EXT_LIB_DEPEND) $(CIUL_LIB_DEPEND)
