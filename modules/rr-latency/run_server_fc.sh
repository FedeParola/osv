#!/bin/bash

eval $(dirname $0)/../../scripts/firecracker.py \
	-i $(dirname $0)/../../build/last/rr_server.img \
	-c 1 \
	-n -b virbr0 -t fc_tap0 --mac 52:54:00:12:34:51 \
	-e \"--ip=eth0,10.0.0.1,255.255.255.0 --defaultgw=10.0.0.254 /rr-latency $@\"
