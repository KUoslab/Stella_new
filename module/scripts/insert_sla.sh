target_vm=$1
sla_option=$2
sla_value=$3

pid=$(sudo grep domstatus /var/run/libvirt/qemu/${target_vm}.xml | tr -dc '0-9')
dev_string=$(virsh dumpxml ${target_vm} | egrep 'nvme|sd')
name_dev=${dev_string#*\'}
name_dev=${name_dev%\'*}

if [ ${sla_option} = "n_maxcredit" ];then
        pid=`expr ${pid} + 2`
fi

cmd="${sla_option} ${sla_value} ${pid} ${name_dev}"

echo ${cmd} > /proc/gos_vm_info
