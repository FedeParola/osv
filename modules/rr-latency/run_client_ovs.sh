#!/bin/bash

eval $(dirname $0)/../../scripts/run.py \
	-i $(dirname $0)/../../build/last/usr-2.img \
	--nogdb --novnc \
	-c 1 \
	-m 1024 \
	-n -v --ip eth0,10.0.0.2,255.255.255.0 --mac 52:54:00:12:34:57 \
	--ovs-dpdk vhost-user-1 \
	-e \"/rr-latency -c $@\"