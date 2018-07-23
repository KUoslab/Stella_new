#echo noop > /sys/block/sdb/queue/scheduler 
echo noop > /sys/block/sda/queue/scheduler

rmmod oios-iosched 

#make clean 

#make && insmod oios-iosched.ko && echo oios > /sys/block/sdb/queue/scheduler

#lsmod | grep oios

#echo 1 > /sys/block/sdb/queue/nomerges

#cat /sys/block/sdb/queue/scheduler 
cat /sys/block/sda/queue/scheduler

#cat /sys/block/sdb/queue/nomerges
