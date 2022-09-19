#! /bin/bash

sudo ethtool -K eth0 lro off
sudo /home/testadmin/onload/build/x86_64_linux-$(uname -r)/driver/linux/load.sh onload
echo eth0 | sudo tee /sys/module/sfc_resource/afxdp/register
