net_vm=$1
ssd_vm=$2

net_pid=`expr $(sudo grep domstatus /var/run/libvirt/qemu/${net_vm}.xml | tr -dc '0-9') + 2`
ssd_pid=$(sudo grep domstatus /var/run/libvirt/qemu/${ssd_vm}.xml | tr -dc '0-9') 
dev_string=$(virsh dumpxml ${ssd_vm} | egrep 'nvme|sd')
name_dev=${dev_string#*\'}
name_dev=${name_dev%\'*}

net_cmd="n_maxcredit 2000 ${net_pid} null"
ssd_cmd="b_bw 300000 ${ssd_pid} ${name_dev}"

sudo echo 2000 > /proc/ancs/vif2/max_credit
echo ${net_cmd} > /proc/gos_vm_info
echo ${ssd_cmd} > /proc/gos_vm_info
