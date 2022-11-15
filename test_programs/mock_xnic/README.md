This test application is intended to behave semi-similar to how our xNIC implementation would. There currently is no code
for Tunneling and Untunneling multicast traffic, but it is easy to see how we could implement that in the receive/transmit
functions.

This was pieced together from two dpdk samples. The first being `simple_mp` and the other being `???`. 

Build and run with: 
`make`

`sudo ./xnic_data <eal args here>`
E.G.
`sudo ./xnic_data -l 2-3 -w "06e6:00:02.0" --vdev="net_vdev_netvsc0,iface=eth1" --proc-type=primary`

To quit the application literally type `quit`. Thats leftovers from the `simple_mp`.
