# SPDX-License-Identifier: GPL-2.0
# X-SPDX-Copyright-Text: (c) Copyright 2014-2020 Xilinx, Inc.

APPS := orm_json

SRCS := orm_json orm_json_lib

OBJS := $(patsubst %,%.o,$(SRCS))

MMAKE_LIB_DEPS	:= $(CIIP_LIB_DEPEND) $(CIAPP_LIB_DEPEND) \
		   $(CITOOLS_LIB_DEPEND) $(CIUL_LIB_DEPEND) \
		   $(CPLANE_LIB_DEPEND)

ifeq  ($(shell CC="${CC}" CFLAGS="${CFLAGS} ${MMAKE_CFLAGS}" check_library_presence pcap.h pcap 2>/dev/null),1)
MMAKE_LIBS_LIBPCAP=-lpcap
CFLAGS += -DCI_HAVE_PCAP=1
else
CFLAGS += -DCI_HAVE_PCAP=0
endif

ifeq  ($(shell CC="${CC}" CFLAGS="${CFLAGS} ${MMAKE_CFLAGS}" check_library_presence czmq.h czmq 2>/dev/null),1)
APPS	+= orm_zmq_publisher zmq_subscriber
ZMQ_LIBS	:= -lzmq -lczmq
ZMQ_INCS	:= -I/usr/include
endif

MMAKE_LIBS	:= $(LINK_CIIP_LIB) $(LINK_CIAPP_LIB) $(MMAKE_LIBS_LIBPCAP) \
		   $(LINK_CITOOLS_LIB) $(LINK_CIUL_LIB) \
		   -lpthread $(LINK_CPLANE_LIB)
MMAKE_INCLUDE	+= -I$(TOPPATH)/src/tools/ip

MMAKE_DPDK := -L/usr/local/lib/x86_64-linux-gnu
MMAKE_DPDK_LIBS :=-lrte_bpf -lrte_flow_classify -lrte_pipeline -lrte_table -lrte_port -lrte_fib -lrte_ipsec -lrte_vhost -lrte_stack -lrte_security -lrte_sched -lrte_reorder -lrte_rib -lrte_rcu -lrte_rawdev -lrte_pdump -lrte_power -lrte_member -lrte_lpm -lrte_latencystats -lrte_kni -lrte_jobstats -lrte_ip_frag -lrte_gso -lrte_gro -lrte_eventdev -lrte_efd -lrte_distributor -lrte_cryptodev -lrte_compressdev -lrte_cfgfile -lrte_bitratestats -lrte_bbdev -lrte_acl -lrte_timer -lrte_hash -lrte_metrics -lrte_cmdline -lrte_pci -lrte_ethdev -lrte_meter -lrte_net -lrte_mbuf -lrte_mempool -lrte_ring -lrte_eal -lrte_kvargs

LIBS      += $(MMAKE_LIBS) $(ZMQ_LIBS)
INCS      += $(MMAKE_INCLUDE) $(ZMQ_INCS)
DEPS      += $(OBJS) $(MMAKE_LIB_DEPS)

%.o: %.c
	$(CC) $(CFLAGS) $(INCS) -c $< -o $@

all: $(APPS)

orm_json: $(DEPS)
	(libs="$(LIBS)"; $(MMakeLinkCApp))

orm_zmq_publisher: orm_zmq_publisher.o orm_json_lib.o
	(libs="$(LIBS)"; $(MMakeLinkCApp))

zmq_subscriber: zmq_subscriber.o
	(libs="$(LIBS)"; $(MMakeLinkCApp))

clean:
	@$(MakeClean)
	rm -f *.o $(APPS)
