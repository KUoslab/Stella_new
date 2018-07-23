#!/bin/bash

target_vm=$1
sla_option=$2
sla_value=$3

vm_path="/sys/fs/cgroup/cpu/machine/$1.libvirt-qemu"

vcpu_string=$(virsh dumpxml ${target_vm} | grep vcpu | grep -v grep)
num_vcpu=${vcpu_string#*>}
num_vcpu=${num_vcpu%<*}

dev_string=$(virsh dumpxml ${target_vm} | egrep 'nvme|sd')
name_dev=${dev_string#*\'}
name_dev=${name_dev%\'*}

cmd_string=""

for i in $(seq 0 $((num_vcpu - 1)))
do
	vcpu_path="${vm_path}/vcpu$i"

	cmd_string="${cmd_string} `cat "${vcpu_path}/tasks"`"
done

if [ ${sla_option} = "c_usage" ];then
	sla_value=`expr ${sla_value} / ${num_vcpu}`
fi

cmd_string="${cmd_string} ${sla_option} ${sla_value} ${name_dev}"

echo ${cmd_string} > /proc/gos_vm_info

echo "<----- CMD : ${target_vm} ${sla_option} ${sla_value} ----->"
cat /proc/gos_vm_info
echo "<----- ----->"
