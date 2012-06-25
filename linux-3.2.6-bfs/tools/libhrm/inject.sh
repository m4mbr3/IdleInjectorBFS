#!/bin/bash
for i in 1 2 3 4
do
	 printf "I start from here\n" >> w`echo $i`.log
	 printf "I start from here\n" >> w`echo $i`_2.log

done
for (( i=$1 ; $i< $2+1 ; i= $i+1 ))  
do
	echo $i,$3,t > /proc/schedidle/sched_pid
done
