#!/bin/bash
export CPATH=/home/ubuntu/root/usr/local/include
export LIBRARY_PATH=/home/ubuntu/root/usr/local/lib
export LD_LIBRARY_PATH=/home/ubuntu/root/usr/local/lib

sudo cgdelete -g cpuset:/cpulimitl
mkdir -p xxxx15_micro
sudo cgcreate -t $USER:$USER -a $USER:$USER  -g cpuset:/cpulimitl
trd=$1
let yyml=trd+1
sudo cgset -r cpuset.mems=0 cpulimitl 
sudo cgset -r cpuset.cpus=0-$trd cpulimitl
set -x
sudo LD_LIBRARY_PATH="$LD_LIBRARY_PATH" cgexec -g cpuset:cpulimitl ./out-perf.masstree/benchmarks/dbtest --verbose --bench micro --db-type mbta --scale-factor $trd --num-threads $trd --numa-memory 1G --parallel-loading --runtime 30 -F third-party/paxos/config/1silo_1paxos_2follower/$yyml.yml -F third-party/paxos/config/occ_paxos.yml --paxos-leader-config --multi-process -P localhost -S 10000 > ./xxxx15_micro/leader-$trd-1000.log 2>&1 &
#tail -f ./xxxx15_micro/leader-$trd.log
