# Stella Integrated scheduler
* GoS: Guarantee of CPU utilization and SSD block I/O SLA
* ANCS: Guarantee of network I/O SLA

## PREREQUISITE
Because GoS and ANCS are designed for virtualized environment equipped with SSDs, it requires following environments.
* Modified Linux kernel 4.4.1
* KVM installed Linux
* SSD equipped virtual machines
* vhost-net enabled virtual machines

---
## GoS: Guarantee of CPU utilization and SSD block I/O SLA

GoS framework is a scheduling architecture between CPU and SSD block I/O for providing required performance to virtual machines running concurrently on a virtualized server.
GoS includes different types of resource scheduler in a physical server, CPU and block I/O, to support diverse workload running on virtual machines with different characteristics (CPU-intensive or I/O-intensive).

### SCHEDULING POLICIES

This module allocates CPU utilization and block I/O bandwidth of virtual machines and 
guarantees sla values (CPU utilization, block I/O bandwidth) for virtual machines.
You can set sla options and sla value to each virtual machine via ```test.sh``` script file.

### MODULE INSTALL GUIDE

GoS is loadable kernel model independent of kernel version.
GoS can be installed both manually and automatically through scripts.

#### AUTOMATIC INSTALL
```sh
sudo ./set.sh
```
#### MANUAL INSTALL
1. make in the GoS directory
```sh
make
```
2. install drivers
```sh
insmod gos_start.ko
insmod cpu_stat.ko
insmod disk_stat.ko
insmod gos.ko
```
3. configure block I/O scheduler
```sh
echo noop > /sys/block/sdc/queue/scheduler
rmmod oios-iosched
insmod oios-iosched.ko && echo oios > /sys/block/sdc/queue/scheduler
echo 1 > /sys/block/sdc/queue/nomerges
```

### HOW TO USE

#### Code setting

For the experiment, source should be modified. 
1. Block allocation

    Modify ```unsigned long partition``` variable in oios-iosched.c file
    the array consists of {start block number, end block number}

2. Priority(performance) ratio of OIOS

    Modify ```static int prior[]``` variable in oios-iosched.c file
    It only supports 5 VMs

3. Number of vcpus

    Modify ```#define VCPU_NUM N``` macro in gos.h file
    The VCPU_NUM is the number of cores in the CPUs

4. Number of VMs

    Modify ```#define VM_UM N``` macro in gos.h file
    The VM_NUM is the number of VMs which have SLA setting in ```test.sh``` script file 

#### Script setting

To run virtual machines with performance requirements and evaluate their performance using any CPU or bock I/O benchmark.

First, edit your performance requirements fields (sla_option, sla_vlaue) in ```test.sh``` shell script to generate virtual machines with performance requirements (SLA).
* sla_optaion: b_bw-require I/O bandwidth (B), c_usage-require CPU utilization (%, 1 core = 100%)
* sal_value: bandwidth in B or CPU utilization

for example, if you want to guarantee SSD I/O bandwidth of vm 1 to 100MB and CPU utilization of vm 2 to 200% (2 cores), 
you hava to set sla_option, sla_value of vm1 as b_bw, 100000 and c_usage, 200 for sla_option and sla_valule of vm 2

##### test.sh
```sh
#!/bin/bash

./insert_target_vm.sh vm1 b_bw 100000
./insert_target_vm.sh vm2 c_usage 200
```

Second, apply your performance requirements to GoS
```sh
./test.sh
```

---
## ANCS: Guarantee of network I/O SLA

ANCS is a loadable kernel module which dynamically allocates network bandwidth to virtual machines. Our goal is to guarantee QoS requirement of virtual machines through dynamic allocation of network resources in a virtualized server. We used scheduling method proposed in the paper [ANCS](https://www.hindawi.com/journals/sp/2016/4708195/abs/)*. 

> *ANCS: Achieving QoS through Dynamic Allocation of Network Resources in Virtualized Clouds


### SCHEDULING POLICIES

This module allocates network bandwidth of virtual machines dynamically and propotionally to weight. 
   - Bandwidth of each virtual machine is set to "(weight / total weight) * (bandwidth capacity)".
   - Ex) If we have running virtual machines A,B,C with weight 1,2,2, then each virtual machine uses 20%, 40%, 40% of maximum bandwidth capacity.


You can set the specific network performance of virtual machines.
   - If you give a specific value of the performance in "max_credit" using proc file system, then upper limit of the network performance is configured.
   - Setting lower limit can be done by changing "min_credit" as above.

This module supports work-conserving.
   - If there's virtual machine not fully using it's bandwidth, then remaining bandwidth is reallocated to other virtual machine's so that utilization of network resources can be maximized.


### MODULE INSTALL GUIDE

1. install scheduling module 
   
	- "vif" folder has a module source code, header file, Makefile.
	- change current directory to "vif" folder and compile a module using "make". 
	- When compilation is done, you would get a loadable kernel module "vif.ko".
			```insmod vif.ko```

### HOW TO USE 

#### Run or start virtual machine after adding a module
   - must install a module first because virtual machines which are executed before a module installation are not affected by a module


#### Use proc file system to set weight, min, max bandwidth of each virtual machine if needed
   - weight is set to 1 by default
   - min, max credit is set to 0 by default, meaning it has no upper, under limitaion of bandwidth.
    
  
#### Printing attributes of each virtual machine.
  
   - A command that prints weight of second virtual machine. vif stands for virtual interface.
			```cat /proc/ancs/vif2/weight```		
		
   - A command that prints a maximum performance of first virtual machine in a form of bandwidth or packet per second (pps).
			```cat /proc/oslab/vif1/max_credit```		
		
   - A command that prints a minimum performance of first virtual machine.
			```cat /proc/oslab/vif1/min_credit```		
		
	
#### Setting attributes of each virtual machine
	
   - A command that sets a weight of first virtual machine "2". A bigger weight means a bigger priority.
			```echo 2 > /proc/oslab/vif1/weight```	
	
