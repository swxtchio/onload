# SPDX-License-Identifier: GPL-2.0 OR BSD-2-Clause
# X-SPDX-Copyright-Text: (c) Copyright 2002-2020 Xilinx, Inc.
######################################################################
# Make the key variables globally visible.
#
export TOPPATH
export BUILD
export BUILDPATH
export CURRENT
export THISDIR
export PLATFORM
export VPATH
export VPATH_ENABLED
export SUBDIRS
export IMPORT
export BUILD_TREE_COPY
export DRIVER
export DRIVER_TYPE
export DRIVER_SIZE
export MAKE_SANITY_DONE
export MAKEWORLD
export INSTALLER

# Ensure these environment variables are not inherited.
cflags :=
cppflags :=
cxxflags :=
export cflags
export cppflags
export cxxflags


######################################################################
# Cancel some built-in rules.
#
%.o: %.c
%.o: %.cc
%:   %.c
%:   %.cc
%:   %.o


######################################################################
# Include directories.
#
MMAKE_INCLUDE_DIR	:= $(TOPPATH)/src/include
MMAKE_INCLUDE		:= -I. -I$(BUILD)/include -I$(MMAKE_INCLUDE_DIR)


######################################################################
# Some useful commands.
#
SUBDIRS	:=
DRIVER_SUBDIRS :=
INSTALLER_SUBDIRS :=

define MakeAllSubdirs
([ "$$subdirs" = "" ] && subdirs='$(SUBDIRS) $(OTHER_SUBDIRS)'; \
 [ "$$target" = "" ]  && target='$@'; \
 for d in $$subdirs ; do \
   [ ! -d "$$d" ] || $(MAKE) -C "$$d" $(passthruparams) "$$target" || exit ; done \
)
endef

ifeq ($(MAKECMDGOALS),world)

MAKEWORLD:=1

endif

ifeq ($(MAKEWORLD),1)

MakeSubdirs=$(MakeAllSubdirs)

else 

define MakeSubdirs
([ "$$subdirs" = "" ] && subdirs='$(SUBDIRS)'; \
 [ "$$target" = "" ]  && target='$@'; \
 for d in $$subdirs ; do \
   [ ! -d "$$d" ] || $(MAKE) -C "$$d" $(passthruparams) "$$target" || exit ; done \
)
endef

endif


define MakeClean
rm -f *.a *.so *.o *.ko *.d *.lib *.dll *.exp *.pdb $(TARGET) $(TARGETS); $(MakeAllSubdirs)
endef


######################################################################
# Misc.
#

# Other makefiles may define rules before we get to the makefile in the
# directory, but we don't want them to be the default!
default_all:	all

.PHONY: all clean lib default buildtree

# Do not delete intermediates (needed for dependancy checks).
.SECONDARY:

nullstring:=
space=$(nullstring) #<-do not edit this line

# Definition of all of the DPDK libs. If subdirectories need to link to DPDK they can just reference
# this variable to include it. This matches the same setup that we have for REPL
DPDK_STATIC_LOCATIONS = -L$(RTE_SDK)/build/lib -L$(RTE_SDK)/build/drivers 
DPDK_STATIC_LIBS = -lrte_hash -lrte_cmdline -lrte_pci -lrte_bus_pci -lrte_bus_vdev -lrte_mempool_ring -lrte_kni -lrte_ethdev -lrte_eal -lrte_mbuf -lrte_mempool -lrte_ring -lrte_kvargs -lrte_pmd_bond -lrte_pmd_virtio -lrte_pmd_enic -lrte_pmd_i40e -lrte_pmd_ixgbe -lrte_net -lrte_pmd_e1000 -lrte_pmd_ring -lrte_pmd_af_packet -lrte_pmd_mlx4 -lrte_pmd_mlx5 -lrte_pmd_ena -lrte_pmd_failsafe -lrte_pmd_netvsc -lrte_pmd_vdev_netvsc -lrte_bus_vmbus -lrte_pmd_tap -lrte_gso -lrte_timer -lrte_meter
DPDK_DYN_LIBS = -lm -ldl -lnuma -libverbs -lmlx4 -lmlx5
DPDK_STATIC = -Wl,-Bstatic -Wl,--whole-archive
DPDK_DYNAMIC = -Wl,--no-whole-archive -Wl,-Bdynamic 
DEFAULT_DPDK := $(DPDK_STATIC_LOCATIONS) $(DPDK_STATIC) $(DPDK_STATIC_LIBS) $(DPDK_DYNAMIC) $(DPDK_DYN_LIBS)

