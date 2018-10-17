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

cmd="$1 $2 $3 ${pid} ${name_dev}"
echo ${cmd} > /proc/gos_vm_info
