#!/bin/sh

sleeptime=$1;
month=`date | busybox awk '{print $2}'`;
day=`date | busybox awk '{print $3}'`;
time=`date | busybox grep -o "[0-9]*:[0-9]*:[0-9]*" | busybox sed -e 's/://g'`;
logdir=`ls /storage/sdcard0/slog/ | busybox grep -o -E "[0-9]+-[0-9]+-[0-9]+-[0-9]+-[0-9]+-[0-9]+"`;
echo

#mount -t debugfs none /sys/kernel/debug  2>/dev/null
cd /sys/kernel/debug/tracing
   
   #echo 50000 > buffer_size_kb
   echo 1411 > buffer_size_kb
   echo nop > current_tracer
   #  exemple for also tracing all function starting with hsi:
   echo function > current_tracer
   echo *futex* >> set_ftrace_filter

   echo  > set_event
   (
   while read i
   do
     echo $i >> set_event
   done
   ) <<EOF
sched:sched_wakeup
sched:sched_switch
sched:sched_stat_iowait
sched:sched_stat_runtime
sched:sched_stat_sleep
workqueue:workqueue_execution
workqueue:workqueue_execution_end
workqueue:workqueue_execute
workqueue:workqueue_execute_end
irq:*
timer:*
EOF
   echo >trace
   echo 1 >tracing_on

   sleep $sleeptime

   echo >set_event
   echo 0 >tracing_on
   
output=/storage/sdcard0/slog/$logdir/misc/trace`date +%y-%m-%d-%H-%M-%S`.txt
cat trace > $output
echo trace written to $output

