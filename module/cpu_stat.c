//#include <linux/module.h>
//#include <linux/kernel.h>
//#include <linux/init.h>
//#include <linux/jiffies.h>
//#include <linux/sched.h>
#define GOS_SOURCE


#include "gos.h"

//#define DEBUG
//#define VCPU_NUM 10
//#define VM_NUM 5

/* declaration - struct */
//struct cpu_SLA { // 100.00% is 10000
//	int cpu_SLA_time; // max usage is (100% * core)
//	int cpu_usage_time; // max usage_time is (period * core)
//	int cpu_SLA_percent; // max usage is 100%
//	int cpu_SLA_achieve_percent; // max usage is 100%
//};

/* declaration - struct variable*/
//extern struct gos_vm_info *gos_vm_list[VM_NUM];

/* declaration - function */
void cal_cpu_SLA(int vm_num);
int get_cpu_usage(int vm_num);

/* declaration - basic variable */
//unsigned long temp_cpu_time = -1;
int prev_total_time[VM_NUM];
int cnt[VM_NUM];

void cal_cpu_SLA(int vm_num)
{
	int /*cpu_SLA_time, cpu_usage_time, */now_total_time;

	if (gos_vm_list[vm_num] == NULL) return ;

//	gos_vm_list[vm_num]->prev_SLA = gos_vm_list[vm_num]->now_SLA;
//	if (gos_vm_list[vm_num]->sla_type == c_usg){

	cnt[vm_num]++;

	if (cnt[vm_num]==1){
		prev_total_time[vm_num] = get_cpu_usage(vm_num);
	}

	else {
		//cpu_SLA_time = gos_vm_list[vm_num]->sla_target.cpu_usage * VCPU_NUM * gos_interval / 100000000; // cpu_SLA_percent to cpu_SLA_usage_time // ms
		now_total_time = get_cpu_usage(vm_num);
		gos_vm_list[vm_num]->now_perf.cpu_usage = (now_total_time - prev_total_time[vm_num])*10000 / (gos_interval/1000000 * VCPU_NUM); // calculate cpu_usage
		prev_total_time[vm_num] = now_total_time;

		if(gos_vm_list[vm_num]->sla_type == c_usg) {
			gos_vm_list[vm_num]->prev_SLA = gos_vm_list[vm_num]->now_SLA;
			gos_vm_list[vm_num]->now_SLA = gos_vm_list[vm_num]->now_perf.cpu_usage * 100 / gos_vm_list[vm_num]->sla_target.cpu_usage;
		}

#ifdef DEBUG
		printk(KERN_INFO "CPU_STAT : SLA: %d(%% * 100) / usage: %d(%% * 100) / achievement: %d(%% * 100)\n", \
			gos_vm_list[vm_num]->sla_target.cpu_usage*100, gos_vm_list[vm_num]->now_perf.cpu_usage, gos_vm_list[vm_num]->now_SLA);
#endif
	}

/*	}
	else {
		printk(KERN_INFO "NOT CPU VM!\n");
	}*/
}
EXPORT_SYMBOL(cal_cpu_SLA);

///////////////////////////////////////////////////////////////////////////////////
int get_cpu_usage(int vm_num)
{
	int i;
	int temp_total_time = 0;
	for (i=0; i<VCPU_NUM; i++){
		temp_total_time += gos_vm_list[vm_num]->ts[i]->utime + gos_vm_list[vm_num]->ts[i]->stime;
	}
	temp_total_time = temp_total_time * 1000 / HZ; // maybe ms
//	printk(KERN_INFO "cpu_usage_time : %lu\n", temp_total_time);
	return temp_total_time;
}

static int __init simple_init(void)
{
	printk(KERN_INFO "CPU-SLA monitoring module start\n");
	return 0;
}

static void __exit simple_exit(void)
{
	printk(KERN_INFO "CPU-SLA monitoring module exit\n");
}

module_init(simple_init);
module_exit(simple_exit);

MODULE_AUTHOR("wjlee");
MODULE_DESCRIPTION("HELLO");
MODULE_LICENSE("GPL");
MODULE_VERSION("NEW");
