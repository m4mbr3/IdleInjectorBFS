#!/bin/bash

for (( i=$1 ; $i< $2+1 ; i= $i+1 ))  
do
	echo $i,$3,t > /proc/schedidle/sched_pid
done
