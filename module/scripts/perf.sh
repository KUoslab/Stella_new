cpupid=$(sudo grep domstatus /var/run/libvirt/qemu/$1.xml | tr -dc '0-9')

perf stat -p ${cpupid} -o cpu-perf.txt
