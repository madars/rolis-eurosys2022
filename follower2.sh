#!/bin/bash
ulimit -c 10000
export CPATH=/home/ubuntu/root/usr/local/include
export LIBRARY_PATH=/home/ubuntu/root/usr/local/lib
export LD_LIBRARY_PATH=/home/ubuntu/root/usr/local/lib

trd=$1
let yyml=trd+1
./out-perf.masstree/benchmarks/dbtest --verbose --bench micro --db-type mbta --scale-factor $trd --num-threads $trd --numa-memory 1G --parallel-loading --runtime 30 -F third-party/paxos/config/1silo_1paxos_2follower/$yyml.yml -F third-party/paxos/config/occ_paxos.yml --multi-process -P p2
