#!/bin/bash

eval $(dirname $0)/../../scripts/run.py \
	-i $(dirname $0)/../../build/last/rr_server.img \
	--nogdb --novnc \
	-c 1 \
	-n -v --ip eth0,10.0.0.1,255.255.255.0 --mac 52:54:00:12:34:51 \
	-e \"/rr-latency $@\"
