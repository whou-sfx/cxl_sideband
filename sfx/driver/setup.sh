#########################################################################
# File Name: setup.sh
# Desc:
# Author: Andy-wei.hou
# mail: wei.hou@scaleflux.com
# Created Time: 2025年11月25日 星期二 15时25分36秒
# Log: 
#########################################################################
#!/bin/bash

sudo rmmod mctp_bridge
sudo insmod mctp_bridge.ko

## enable netdev
sudo ip link set mctp_bridge0 up

## CFG route and address bind
### remote EID 8

sudo mctp route add 8 via mctp_bridge0
#sudo mctp address add 8 dev mctp_bridge0

### set the local dev eid so bind/recvfrom can get msg
sudo mctp address add 19 dev mctp_bridge0

## show route & eid cfg
sudo mctp link show mctp_bridge0
sudo mctp route show mctp_bridge0
sudo mctp address show mctp_bridge0
