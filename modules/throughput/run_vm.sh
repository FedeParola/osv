#!/bin/bash

if [ -z $1 ]; then
	echo "usage: $0 <sidecar_id> <app_options>"
	exit 1
fi

id=$1
shift

eval $(dirname $0)/../../scripts/run.py \
	-i $(dirname $0)/../../build/last/usr-$id.img \
	--nogdb --novnc \
	-c 1 \
	-n -v --ip eth0,10.0.0.$id,255.255.255.0 --mac 52:54:00:12:34:5$id \
	-e \"/throughput $@\"