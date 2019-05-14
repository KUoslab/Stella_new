# Stella Integrated scheduler
* Guarantee of SSD block I/O SLA and network I/O
* Stella Integrated scheduler is a scheduling architecture which guarantees SSD block I/O and network I/O bandwidth for providing required performance to virtual machines running concurrently on a virtualized server.
Stella Integrated scheduler includes different types of resource scheduler in a physical server, block I/O and network I/O, to support diverse workload running on virtual machines with different characteristics.
Our goal is to guarantee QoS requirement of virtual machines through dynamic allocation of network and SSD storage resources in a virtualized server. We used network scheduling method proposed in the paper [ANCS](https://www.hindawi.com/journals/sp/2016/4708195/abs/)*. 

> *ANCS: Achieving QoS through Dynamic Allocation of Network Resources in Virtualized Clouds

---

## PREREQUISITE
Because Stella Integrated scheduler is designed for virtualized environment equipped with SSDs, it requires following environments.
* Modified Linux kernel 4.4.1
* KVM installed Linux
* SSD equipped virtual machines
* vhost-net enabled virtual machines

---

## SCHEDULING POLICIES

This module allocates CPU utilization, block I/O, network I/O bandwidth of virtual machines and 
guarantees sla values (block I/O bandwidth, network I/O bandwidth) for virtual machines.
You can set sla options and sla value to each virtual machine via ```insert_sla.sh``` script file.

### SSD block I/O

Stella Integrated scheduler allocates SSD I/O bandwidth of virtual machines dynamically to virtual machines.
you can set the specific type of SSD I/O performance
* Bandwidth, iops, latency can be set as upper bound of SSD I/O performance of vitrual machine. 

### Network I/O

Stella Integrated scheduler allocates network I/O bandwidth of virtual machines dynamically and propotionally to weight. 
*  Bandwidth of each virtual machine is set to "(weight / total weight) * (bandwidth capacity)".
*  Ex) If we have running virtual machines A,B,C with weight 1,2,2, then each virtual machine uses 20%, 40%, 40% of maximum bandwidth capacity.

You can set the specific network performance of virtual machines.
* If you give a specific value of the performance in "max_credit" using Stell scheduler interface, then upper limit of the network performance is configured.
* Setting lower limit can be done by changing "min_credit" as above.

This module supports work-conserving.
* If there's virtual machine not fully using it's bandwidth, then remaining bandwidth is reallocated to other virtual machine's so that utilization of network resources can be maximized.

---

## Stella Integrated scheduler INSTALL GUIDE

Stella Integrated scheduler is loadable kernel model independent of kernel version.
Stella Integrated scheduler can be installed both manually and automatically through scripts.

#### AUTOMATIC INSTALL
```sh
sudo ./setup-modules.sh
```
#### MANUAL INSTALL
1. make in the GoS directory
```sh
make
```
2. install drivers
```sh
insmod gos_start.ko
insmod vif.ko
insmod disk_stat.ko
insmod gos.ko
```

---

## HOW TO USE

### Code setting

For the experiment, source should be modified. 

1. Number of vcpus

    Modify ```#define VCPU_NUM N``` macro in gos.h file
    The VCPU_NUM is the number of cores in the CPUs

2. Number of VMs

    Modify ```#define VM_UM N``` macro in gos.h file
    The VM_NUM is the number of VMs which have SLA setting in ```test.sh``` script file 

### Insert performance requirments (SLA)

To run virtual machines with performance requirements and evaluate their performance using any network I/O, bock I/O benchmark,
First, edit your performance requirements fields (sla_option, sla_vlaue) in ```insert_sla.sh``` shell script to generate virtual machines with performance requirements (SLA).
* Format: ```sh insert_sla.sh [VM_NAME] [SLA_OPTION] [SLA_VALUE]```
* [VM_NAME]: virtual machine name
* [SLA_OPTION]: types of performance requirements
    * c_usage: Total VM CPU usage limit (%)
    * b_bw: SSD I/O bandwidth (KB unit)
    * b_iops: SSD I/O io operations
    * b_lat: SSD I/O latency
    * n_mincredit: network I/O minimum credit (lower limit) (packets per second unit)
    * n_maxcredit: network I/O maximum credit (upper limit) (packets per second unit)
    * n_weight: proportional network I/O limit according to weight
* [SLA_VALUE]: required performance value

for example, if you want to guarantee SSD I/O bandwidth of vm 1 to 100MB, 
you hava to set sla_option, sla_value of vm1 as b_bw, 100000 for sla_option and sla_valule

#### insert_sla.sh
```sh
#insert required performance of SSD I/O for vm1

./insert_sla.sh vm1 b_bw 100000

#insert required performance of network I/O for vm2

./insert_sla.sh vm2 n_maxcredit 10000
```

---