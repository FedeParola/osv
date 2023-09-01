#!/bin/bash

eval $(dirname $0)/../../scripts/run.py \
	-i $(dirname $0)/../../build/last/usr-1.img \
	--nogdb --novnc \
	-c 1 \
	-m 1024 \
	-n -v --ip eth0,10.0.0.1,255.255.255.0 --mac 52:54:00:12:34:56 \
	--ovs-dpdk vhost-user-0 \
	-e \"/rr-latency $@\"