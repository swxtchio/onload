#! /bin/bash

sudo ~/onload/build/x86_64_linux-$(uname -r)/driver/linux/load.sh onload
echo eth1 | sudo tee /sys/module/sfc_resource/afxdp/register
