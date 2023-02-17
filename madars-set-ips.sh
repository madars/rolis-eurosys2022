#!/bin/bash
IP_LEADER=$1
IP_OHIO=$2
IP_OREGON=$3

echo $IP_LEADER > ip_leader_replica
echo $IP_OHIO > ip_p1_follower_replica
echo $IP_OREGON > ip_p2_follower_replica

for fn in third-party/paxos/config/1silo_1paxos_2follower/*.yml; do
    sed -i "s|localhost: .*\$|localhost: $IP_LEADER|" $fn
    sed -i "s|p1: .*\$|localhost: $IP_OHIO|" $fn
    sed -i "s|p2: .*\$|localhost: $IP_OREGON|" $fn
done
