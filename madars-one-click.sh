#!/bin/bash
repos="rolis-eurosys2022"  # repos name, default
workdir="~"  # we default put our repos under the root
leadrIP=$( cat ./scripts/ip_leader_replica )
p1=$( cat ./scripts/ip_p1_follower_replica )
p2=$( cat ./scripts/ip_p2_follower_replica )
ulimit -n 10000

# minimum of the number of worker threads
start=1
# maximum of the number of worker threads
end=31

setup () {
    bash ./batch_silo.sh kill
    mkdir -p results
    #rm ./results/*
}

experiment4 () {
    echo 'start experiment-4'
    sudo bash ./multi.sh
    bash ./batch_silo.sh scp

    for i in $(seq $start $end); do
	bash ./batch_silo.sh kill

	eval "ulimit -n 10000; cd $workdir/$repos/ && sudo ./madars-mb0.sh $i" &
	sleep 1

	ssh $p2 "ulimit -n 10000; cd $workdir/$repos/ && sudo ./madars-mb2.sh $i" &
	sleep 1

	ssh $p1 "ulimit -n 10000; cd $workdir/$repos/ && sudo ./madars-mb1.sh $i" &
	sleep 1

	echo "Wait for jobs..."
	FAIL=0

	while true; do
	    num=$(jobs -p | wc -l)
	    if [ $num -lt 3 ]; then
		break
	    fi
	    sleep 1
	done

	sleep 10
    done

    python3 scripts/extractor.py 0 xxxx15_micro  "agg_throughput:" "ops/sec" > results/scalability-ycsb.log
}

setup
experiment4
