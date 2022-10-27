# SPDX-License-Identifier: GPL-2.0
# X-SPDX-Copyright-Text: (c) Copyright 2016-2019 Xilinx, Inc.

TEST_APPS	:= rtt
TARGETS		:= $(TEST_APPS:%=$(AppPattern))


all: $(TARGETS)

clean:
	@$(MakeClean)


MMAKE_LIBS	:= $(LINK_CIAPP_LIB) $(LINK_CITOOLS_LIB) $(LINK_CIUL_LIB)
MMAKE_LIB_DEPS	:= $(CIAPP_LIB_DEPEND) $(CITOOLS_LIB_DEPEND) $(CIUL_LIB_DEPEND)

MMAKE_DPDK := -L/usr/local/lib/x86_64-linux-gnu
MMAKE_DPDK_LIBS :=-lrte_bpf -lrte_flow_classify -lrte_pipeline -lrte_table -lrte_port -lrte_fib -lrte_ipsec -lrte_vhost -lrte_stack -lrte_security -lrte_sched -lrte_reorder -lrte_rib -lrte_rcu -lrte_rawdev -lrte_pdump -lrte_power -lrte_member -lrte_lpm -lrte_latencystats -lrte_kni -lrte_jobstats -lrte_ip_frag -lrte_gso -lrte_gro -lrte_eventdev -lrte_efd -lrte_distributor -lrte_cryptodev -lrte_compressdev -lrte_cfgfile -lrte_bitratestats -lrte_bbdev -lrte_acl -lrte_timer -lrte_hash -lrte_metrics -lrte_cmdline -lrte_pci -lrte_ethdev -lrte_meter -lrte_net -lrte_mbuf -lrte_mempool -lrte_ring -lrte_eal -lrte_kvargs


rtt: rtt.o rtt_socket.o rtt_efvi.o
