#!/bin/sh

vm=$1
sla_option=$2
sla_value=$3

count=`expr 0`
pid=$(sudo grep domstatus /var/run/libvirt/qemu/${vm}.xml | tr -dc '0-9')
dev_string=$(virsh dumpxml ${vm} | egrep 'nvme|sd')
name_dev=${dev_string#*\'}
name_dev=${name_dev%\'*}

if [ ! "${name_dev}"];then
	name_dev="null"
fi

if [ "$2" = "n_maxcredit" ];then
	vhost_cmd=$(ps -el | grep vhost | grep ${pid})
	for vhost_pid in ${vhost_cmd}
	do
		count=`expr ${count} + 1`

		if [ ${count} = 4 ];then
			pid=${vhost_pid}
		fi
	done
	#check this command line
	#name_dev="null"
fi

vm_path="/sys/fs/cgroup/cpu/machine/${vm}.libvirt-qemu"
vcpu_string=$(virsh dumpxml ${vm} | grep vcpu | grep -v grep)
num_vcpu=${vcpu_string#*>}
num_vcpu=${num_vcpu%<*}

for i in $(seq 0 $((num_vcpu - 1)))
do
	vcpu_path="${vm_path}/vcpu$i"

	pid2="${pid2} `cat "${vcpu_path}/tasks"`"
done

if [ "$2" = "c_usage" ];then
	cmd="$1 $2 $3 ${pid2} ${name_dev}"
else
	cmd="$1 $2 $3 ${pid} ${name_dev}"
fi
echo "${cmd}"
echo ${cmd} > /proc/gos_vm_info
