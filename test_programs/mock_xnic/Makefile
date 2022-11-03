CFLAGS = -march=native -O3 -msse3 -mavx
INCLUDES = -I./ -I$(RTE_SDK)/build/install/include
LFLAGS = -L$(RTE_SDK)/build/drivers -L$(RTE_SDK)/build/lib
LIBS = -lrte_hash -lrte_cmdline -lrte_pci -lrte_bus_pci -lrte_bus_vdev -lrte_mempool_ring -lrte_kni -lrte_ethdev -lrte_eal -lrte_mbuf -lrte_mempool -lrte_ring -lrte_kvargs -lrte_pmd_bond -lrte_pmd_virtio -lrte_pmd_enic -lrte_pmd_i40e -lrte_pmd_ixgbe -lrte_net -lrte_pmd_e1000 -lrte_pmd_ring -lrte_pmd_af_packet -lrte_pmd_mlx4 -lrte_pmd_mlx5 -lrte_pmd_ena -lrte_pmd_failsafe -lrte_pmd_netvsc -lrte_pmd_vdev_netvsc -lrte_bus_vmbus -lrte_pmd_tap -lrte_gso -lrte_timer -lrte_meter
DYN_LIBS = -lm -ldl -lnuma -libverbs -lmlx4 -lmlx5
STATIC = -Wl,-Bstatic -Wl,--whole-archive
NO_STATIC = -Wl,--no-whole-archive -Wl,-Bdynamic 
SRCS = main.c mp_commands.c
OBJS = $(SRCS:.c=.o)
MAIN = xnic_data 

.PHONY: depend clean

all:    $(MAIN)
	@echo  done

$(MAIN): $(OBJS)
	$(CC) $(CFLAGS) $(INCLUDES) -o $(MAIN) $(OBJS) $(LFLAGS) $(STATIC) $(LIBS) $(NO_STATIC) $(DYN_LIBS)

.c.o:
	$(CC) $(CFLAGS) $(INCLUDES) -c $<  -o $@

clean:
	$(RM) *.o *~ $(MAIN)
