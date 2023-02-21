#!/bin/bash
[ -z "$1" ] && (echo "Usage: $0 log-file.log"; kill -PIPE $$)

TS=0
while read -r l; do
    NEWTS=$(echo $l | cut -f 2 -d ':')
    echo "$l"
    if [ $TS -ne 0 ]; then
	echo -n "    delta = "
	echo "$NEWTS - $TS" | bc
    fi
    TS=$NEWTS

done < <(cat "$1" | grep "^Time right")
