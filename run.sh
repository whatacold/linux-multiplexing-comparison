#!/bin/bash

if [ "0" != "`id -u`" ]; then
    echo "please rerun as root, who has the privilege to change ulimit setting."
    exit 1
fi

ulimit -n 50000

./dummyd &
DUMMY_PID=$!

./heartbeatd &
HB_PID=$!

./multiplexing_compare 10 15000 100 100 > multiplexing_compare.dat 2>/dev/null
gnuplot plot.gpl

kill $DUMMY_PID $HB_PID

echo "check multiplexing_compare.png out"
