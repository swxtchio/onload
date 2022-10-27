# SPDX-License-Identifier: BSD-2-Clause
# X-SPDX-Copyright-Text: (c) Copyright 2003-2019 Xilinx, Inc.

EFSEND_APPS := efsend efsend_pio efsend_timestamping efsend_pio_warm
TEST_APPS	:= efforward efrss efsink \
		   efsink_packed efforward_packed eflatency stats \
		   efjumborx $(EFSEND_APPS)

ifeq (${PLATFORM},gnu_x86_64)
	TEST_APPS += efrink_controller efrink_consumer
endif

TARGETS		:= $(TEST_APPS:%=$(AppPattern))


MMAKE_LIBS	:= $(LINK_CIUL_LIB) $(LINK_CIAPP_LIB)
MMAKE_LIB_DEPS	:= $(CIUL_LIB_DEPEND) $(CIAPP_LIB_DEPEND)

MMAKE_DPDK := -L/usr/local/lib/x86_64-linux-gnu
MMAKE_DPDK_LIBS :=-lrte_bpf -lrte_flow_classify -lrte_pipeline -lrte_table -lrte_port -lrte_fib -lrte_ipsec -lrte_vhost -lrte_stack -lrte_security -lrte_sched -lrte_reorder -lrte_rib -lrte_rcu -lrte_rawdev -lrte_pdump -lrte_power -lrte_member -lrte_lpm -lrte_latencystats -lrte_kni -lrte_jobstats -lrte_ip_frag -lrte_gso -lrte_gro -lrte_eventdev -lrte_efd -lrte_distributor -lrte_cryptodev -lrte_compressdev -lrte_cfgfile -lrte_bitratestats -lrte_bbdev -lrte_acl -lrte_timer -lrte_hash -lrte_metrics -lrte_cmdline -lrte_pci -lrte_ethdev -lrte_meter -lrte_net -lrte_mbuf -lrte_mempool -lrte_ring -lrte_eal -lrte_kvargs


all: $(TARGETS)

clean:
	@$(MakeClean)


eflatency: eflatency.o utils.o

$(EFSEND_APPS): utils.o efsend_common.o

efsink: efsink.o utils.o

efjumborx: efjumborx.o utils.o

efsink_packed: efsink_packed.o utils.o

efforward_packed: efforward_packed.o utils.o

efpingpong: MMAKE_LIBS     := $(LINK_CITOOLS_LIB) $(MMAKE_LIBS)
efpingpong: MMAKE_LIB_DEPS := $(CITOOLS_LIB_DEPEND) $(MMAKE_LIB_DEPS)

$(EFSEND_APPS): MMAKE_LIBS := $(LINK_CITOOLS_LIB) $(MMAKE_LIBS)
$(EFSEND_APPS): MMAKE_LIB_DEPS := $(CITOOLS_LIB_DEPEND) $(MMAKE_LIB_DEPS)

eflatency: MMAKE_LIBS     := $(LINK_CITOOLS_LIB) $(MMAKE_LIBS)
eflatency: MMAKE_LIB_DEPS := $(CITOOLS_LIB_DEPEND) $(MMAKE_LIB_DEPS)

efrink_controller: efrink_controller.o utils.o

stats: stats.py
	cp $< $@
