#!/bin/bash

num_vm=$1

./oios_end.sh ; rmmod gos.ko ; rmmod disk_stat.ko ; rmmod cpu_stat.ko; rmmod gos_start.ko ; make clean

for i in $(seq 1 $num_vm)
do
	for j in {0..9}
	do
		echo -1 > /sys/fs/cgroup/cpu/machine/vm$i.libvirt-qemu/vcpu$j/cpu.cfs_quota_us
	done
done

#rmmod gos.ko ; make clean
