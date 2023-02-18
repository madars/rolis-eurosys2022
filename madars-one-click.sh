#!/bin/bash
repos="rolis-eurosys2022"  # repos name, default
workdir="~"  # we default put our repos under the root
leadrIP=$( cat ./scripts/ip_leader_replica )
p1=$( cat ./scripts/ip_p1_follower_replica )
p2=$( cat ./scripts/ip_p2_follower_replica )

# minimum of the number of worker threads
start=16
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
	echo "[+] Killing old processes"
	bash ./batch_silo.sh kill

	echo "[+] Launching new experiment on $i threads"
	eval "cd $workdir/$repos/ && ./madars-mb0.sh $i" &
	ssh $p1 "cd $workdir/$repos/ && ./madars-mb1.sh $i" &
	ssh $p2 "cd $workdir/$repos/ && ./madars-mb2.sh $i" &

	echo "[+] Wait for new experiment to finish"
	FAIL=0

	while true; do
	    num=$(jobs -p | wc -l)
	    if [ $num -lt 3 ]; then
		break
	    fi
	    sleep 1
	done

	echo "[+] At least one process terminated. Results:"
	cat xxxx15_micro/leader-$i-1000.log | grep "agg_throughput"
	echo "[+] End results"
    done

    python3 scripts/extractor.py 0 xxxx15_micro  "agg_throughput:" "ops/sec" > results/scalability-ycsb.log
}

setup
experiment4
