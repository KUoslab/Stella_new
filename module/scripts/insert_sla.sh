vm=$1
sla_option=$2
sla_value=$3

pid=$(sudo grep domstatus /var/run/libvirt/qemu/${vm}.xml | tr -dc '0-9')
dev_string=$(virsh dumpxml ${vm} | egrep 'nvme|sd')
name_dev=${dev_string#*\'}
name_dev=${name_dev%\'*}

if [ ! "${name_dev}"];then
	name_dev="null"
fi

if [ "$2" = "n_maxcredit" ];then
	pid=`expr ${pid} + 2`
fi

cmd="$1 $2 $3 ${pid} ${name_dev}"

echo ${cmd} #> /proc/gos_vm_info
