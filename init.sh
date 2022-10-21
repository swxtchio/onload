#! /bin/bash

sudo ethtool -K eth1 lro off
sudo /home/testadmin/onload/build/x86_64_linux-$(uname -r)/driver/linux/load.sh onload
echo eth1 | sudo tee /sys/module/sfc_resource/afxdp/register
sudo ip neigh add 10.2.164.7 dev eth1 lladdr 12:34:56:78:9a:bc
