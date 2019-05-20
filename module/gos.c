#define GOS_SOURCE
#include "gos.h"

#define PCPU_NUM 10
#define MAX_QUOTA(period) (PCPU_NUM * period)
#define INTERVAL 3000
#define DRIVER_DESC "Guarantee of SLA (GoS)"

#define PROC_NAME "gos_vm_info"
#define IO_VM_PERIOD 9

//#define DEBUG
#define OVERSATISFY 10200	// 102.00 %
#define DISSATISFY 9800		// 98.00 %

static struct timer_list gos_timer;

static struct proc_dir_entry *gos_proc_file;
static const struct file_operations gos_vm_info;

static unsigned long prev_jiffies = 0;
 u64 prev_total_time, prev_used_time;

unsigned long vm_cpu_util_out[VM_NUM];
long now_quota_out[VM_NUM];

//
unsigned long _vm_cpu_util[VM_NUM];


/*
   Start of the feedback controller code
*/


int set_vm_quota(int vm_num, long quota, enum gos_type control_type)
{
	int i = 0;

	if (gos_vm_list[vm_num] == NULL)
		return 1;

	if (control_type == cpu) {
		for (i = 0 ; i < VCPU_NUM ; i++)
			tg_set_cfs_quota(gos_vm_list[vm_num]->vcpu[i]->sched_task_group, quota);

	} else if (control_type == network)
		tg_set_cfs_quota(gos_vm_list[vm_num]->vhost->sched_task_group, quota);

	else if (control_type == ssd)
		tg_set_cfs_quota(gos_vm_list[vm_num]->iothread->sched_task_group, quota);

	return 0;
}

static void update_vm_cpu_time(int vm_num, struct gos_vm_sla *curr_sla)
{
	struct task_struct **ts, *vhost, *iothread;
	unsigned long tmp_total_time = 0;
	int i;

	if (curr_sla->control_type == cpu) {
		ts = gos_vm_list[vm_num]->vcpu;

		for (i = 0; i < VCPU_NUM; i++)
			tmp_total_time += ts[i]->utime + ts[i]->stime;

	} else if (curr_sla->control_type == network) {
		vhost = gos_vm_list[vm_num]->vhost;
		tmp_total_time = vhost->utime + vhost->stime;

	} else if (curr_sla->control_type == ssd) {
		iothread = gos_vm_list[vm_num]->iothread;
		tmp_total_time = iothread->utime + iothread->stime;
	}

	if (curr_sla->prev_cpu_time == 0)
		curr_sla->prev_cpu_time = tmp_total_time;
	else
		curr_sla->prev_cpu_time = curr_sla->now_cpu_time;

	curr_sla->now_cpu_time = tmp_total_time;

}

void feedback_controller(unsigned long elapsed_time)
{
	struct gos_vm_sla *curr_sla;
	unsigned long vm_cpu_util;
	unsigned long prev_cpu_time, now_cpu_time;
	long now_quota, prev_quota, tmp_quota = 0;
	unsigned long prev_sla, now_sla;
	int i;

	for (i = 0; i < VM_NUM; i++) {
		if (gos_vm_list[i] == NULL)
			continue;

		list_for_each_entry(curr_sla, &(gos_vm_list[i]->sla_list), sla_list) {
			printk("gos_debug_timer: vm name : %s sla type : %s ", gos_vm_list[i]->vm_name, curr_sla->sla_option);

			if (curr_sla->control_type == ssd)
				printk("sla value : %d\n", curr_sla->sla_target.bandwidth);

			else if (curr_sla->control_type == network)
				printk("sla value : %d\n", curr_sla->sla_target.credit);

			else if (curr_sla->control_type == cpu)
				printk("sla value : %d\n", curr_sla->sla_target.cpu_usage);

			if (curr_sla->control_type == ssd)
				cal_io_SLA_percent(i, curr_sla);

			now_quota = curr_sla->now_quota;
			prev_quota = curr_sla->prev_quota;
			now_sla = curr_sla->now_sla;
			prev_sla = curr_sla->prev_sla;
			update_vm_cpu_time(i, curr_sla);

			/* vm_cpu_util value 10000 = 100.00% */
			/* ms */
			prev_cpu_time = curr_sla->prev_cpu_time * 1000 / HZ;
			now_cpu_time = curr_sla->now_cpu_time * 1000 / HZ;
			/* 100.00% = 10000, gos_interval is 3s*/
			vm_cpu_util = (now_cpu_time - prev_cpu_time) * 10000 / (gos_interval / 1000000);
<<<<<<< HEAD
<<<<<<< HEAD
=======
>>>>>>> 46f4bdf17dfd81a9639ce97c473c7a62cde11f4b
			gos_vm_list[i]->now_perf.cpu_usage = vm_cpu_util;


			if (curr_sla->control_type == cpu) {
				now_quota = (PERIOD * curr_sla->sla_target.cpu_usage) / 100;
				if (curr_sla->now_quota != now_quota) {
					set_vm_quota(i, now_quota, curr_sla->control_type);
					curr_sla->now_quota = now_quota;
					printk("gos: insert cpu quota %d\n", now_quota);
				}
				curr_sla->now_sla = (vm_cpu_util * 100) / curr_sla->sla_target.cpu_usage;
				printk("gos: cpu now quota : %d\n", now_quota);
				printk("--------------------------------------------\n");
				continue;
			}

<<<<<<< HEAD
=======
            if(vm_cpu_util!=0) _vm_cpu_util[i]=vm_cpu_util;
>>>>>>> 768c5d8596b09e688a4adbb715bd4414c9e963db
=======
>>>>>>> 46f4bdf17dfd81a9639ce97c473c7a62cde11f4b

			if(vm_cpu_util!=0){
				vm_cpu_util_out[i]=vm_cpu_util;
			}
			printk("gos: before now_sla: %lu, prev_sla: %lu\n", now_sla, prev_sla);
			printk("gos: before now_quota: %ld, prev_quota: %ld\n", now_quota, prev_quota);
			printk("gos: vm cpu util = %lu.%lu\n", vm_cpu_util / 100, vm_cpu_util % 100);

			/*
			 * if you want increment and decrement speed of now_quota,
			 * set INC_DEC_SPEED as follow
			 * high speed = 3 or 2
			 * low speed = 7 or 8
			 * it has a risk that the now_quota value become minus if you
			 * set high value (10 or 11?) to INC_DEC_SPEED. check it.
			 */

			/* initial state */
			if (now_sla == 0 || vm_cpu_util < EXIT_CPU_UTIL) {
				tmp_quota = now_quota;
				now_quota = WORK_CONSERVING;
			} else if (now_sla > OVERSATISFY) {
				tmp_quota = now_quota;
				if (now_quota == WORK_CONSERVING)
					now_quota = vm_cpu_util * PERIOD / 10000;
				now_quota -= (now_quota * (now_sla - SLA_GOAL) / SLA_GOAL) / INC_DEC_SPEED;
			} else if (now_sla < DISSATISFY && now_quota > 0) {
				tmp_quota = now_quota;
				now_quota += (now_quota * (SLA_GOAL - now_sla) / SLA_GOAL) / INC_DEC_SPEED;
			} else if (now_sla < DISSATISFY && now_quota == WORK_CONSERVING) {
				if (now_sla < 5000)
					now_quota = WORK_CONSERVING;
				else {
					tmp_quota = vm_cpu_util * PERIOD / 10000;
					/* This value has risk to be zero */
					now_quota = tmp_quota + ((tmp_quota * (SLA_GOAL - now_sla)) / SLA_GOAL)
								/ INC_DEC_SPEED;
				}
				/*
				 * in the first time this timer function called
				 * if prev_cpu_time == now_cpu_time, then cpu util is zero
				 * and it can not get accurate cpu util. so, defer quota calculation
				 * to next time.
				 */
				if (unlikely(vm_cpu_util == 0)) {
					tmp_quota = WORK_CONSERVING;
					now_quota = WORK_CONSERVING;
				}
			}

			if (now_quota > PERIOD + (PERIOD / 3))
				now_quota = vm_cpu_util * PERIOD / 10000;

			if (now_quota < -1)
				now_quota = (vm_cpu_util * PERIOD / 10000) * 2;

			set_vm_quota(i, now_quota, curr_sla->control_type);
			curr_sla->prev_quota = tmp_quota;
			curr_sla->now_quota = now_quota;
			printk("gos: after now_quota: %ld, prev_quota: %ld\n", now_quota, tmp_quota);
			printk("--------------------------------------------\n");

		}
		now_quota_out[i] = now_quota;
	}
}

void gos_timer_callback(unsigned long data)
{
	unsigned long elapsed_time = jiffies_to_msecs(jiffies - prev_jiffies);
	prev_jiffies = jiffies;


	feedback_controller(elapsed_time);

	gos_timer.expires = jiffies + msecs_to_jiffies(INTERVAL);
	add_timer(&gos_timer);
}

static inline int init_gos_timer(void)
{
	int ret_init_gos_timer;

	printk(KERN_INFO "Timer module installing\n");

	setup_timer(&gos_timer, gos_timer_callback, 0);

	printk(KERN_INFO "Starting timer to fire in %dms (%ld)\n", INTERVAL, jiffies);

	prev_jiffies = jiffies;

	ret_init_gos_timer = mod_timer(&gos_timer, jiffies + msecs_to_jiffies(INTERVAL));
	if(ret_init_gos_timer) printk(KERN_INFO "[ERROR] mod_timer error in init_gos_timer\n");

	return 0;
}

static inline int exit_gos_timer(void)
{
	int et_ret = del_timer(&gos_timer);

	if(et_ret)
		printk(KERN_INFO "The timer is still in use... %d\n", timer_pending(&gos_timer));

	printk(KERN_INFO "Timer module uninstalling\n");

	return 0;
}

/* TODO we should change sla values (gos_vm_list[i]->XXX) to gos_vm_sla->XXX */
static int gos_vm_info_show(struct seq_file *m, void *v)
{
  int before_sla, after_sla;
	struct gos_vm_sla *curr_sla;
	int sla_value;
<<<<<<< HEAD
	int int_sla, flt_sla;
	unsigned long _iops, _credit, _bandwidth, _latency, _cpu_usage;
	_iops=0, _credit=0, _bandwidth=0, _latency=0, _cpu_usage=0;
	char *vm_name, *sla_option;
	long _cpu_quota=0;
	int i = 0;
<<<<<<< HEAD
<<<<<<< HEAD
<<<<<<< HEAD
<<<<<<< HEAD
        long cpu_quota;
	unsigned long iops, bandwidth, latency, pps;
	
	seq_puts(m, "VM_NAME\tSLO Option\tSLO Value\tSLO Percentage\tCPU quota\tDisk-IOPS\tDisk-Bandwidth\tDisk-Latency\tPPS\n");

=======
	char *vm_name, *sla_option;
	long cpu_quota;
	unsigned long b_iops, b_pps, b_bandwidth, b_latency, cpu_usage;
  int i = 0;

	seq_puts(m, "VM_NAME\tSLO Option\tSLO Value\tSLO Percentage\tCPU quota\tB_IOPS\tB_PPS\tB_Bandwidth\tB_Latency\tCPU Usage\n");
>>>>>>> 1cb5d62b1fe80fe5f57e398f0f952dadfcdde73e
=======

	unsigned long slo_iops, slo_pps, slo_bandwidth, slo_latency, slo_cpu_usage;	
	long cpu_quota;
	seq_puts(m, "VM_NAME\t\tSLO Option\tSLO Value\tSLO Percentage\tSLO IOPS\tSLO PPS\t\tSLO Bandwidth\tSLO latency\tSLO CPU Usage\tCPU Quota\n");
>>>>>>> c4fca867ea796663c5d23f04449deaa22d0d7bee
=======
	long pre_quota, now_quota ;
	unsigned long iops, pps, bandwidth, latency, cpu_usage ;

	seq_puts(m, "VM_NAME\tSLO Option\tSLO Value\tSLO Percentage\tPrevious Quota\tNow Quota\tIOPS\tPPS\tBandwidth\tLatency\tCPU usage\n");
>>>>>>> f0d07b1adf2e39ea00637ea22d3a1db7843570c7
=======
    long cpu_quota=0;

>>>>>>> 768c5d8596b09e688a4adbb715bd4414c9e963db
=======
	long cpu_quota;
	unsigned long iops, bandwidth, latency, pps, cpu_util;

	
	seq_puts(m, "VM_NAME\tSLO Option\tSLO Value\tSLO Percentage\tCPU quota\tDisk-IOPS\tDisk-Bandwidth\tDisk-Latency\tPPS\tCPU Util\n");
>>>>>>> 46f4bdf17dfd81a9639ce97c473c7a62cde11f4b

	for(i = 0 ; i < VM_NUM ; i++)
	{
		if(gos_vm_list[i] != NULL) {
            seq_puts(m, "VM_NAME\tSLO Option\tSLO Value\tSLO Percentage\n");
			vm_name = gos_vm_list[i]->vm_name;
<<<<<<< HEAD

			list_for_each_entry(curr_sla, &(gos_vm_list[i]->sla_list), sla_list) {

				if (curr_sla->sla_type == b_bw)
					sla_value = curr_sla->sla_target.bandwidth;
				else if (curr_sla->sla_type == b_iops)
					sla_value = curr_sla->sla_target.iops;
				else if (curr_sla->sla_type == b_lat)
					sla_value = curr_sla->sla_target.latency;
				else if (curr_sla->sla_type == n_mincredit)
					sla_value = curr_sla->sla_target.credit;
				else if (curr_sla->sla_type == n_maxcredit){
					sla_value = curr_sla->sla_target.credit;
				}
				else if (curr_sla->sla_type == n_weight)
					sla_value = curr_sla->sla_target.weight;
				else if (curr_sla->sla_type == c_usg)
					sla_value = curr_sla->sla_target.cpu_usage;
<<<<<<< HEAD
	
				gos_vm_list[i]->now_perf.cpu_usage = vm_cpu_util_out[i];
				
				
				int_sla = curr_sla->now_sla / 100;
				flt_sla = curr_sla->now_sla % 100;
				sla_option = curr_sla->sla_option;
<<<<<<< HEAD
				
				// seq_puts(m, "VM_NAME\tSLO Option\tSLO Value\tSLO Percentage\n");
				seq_printf(m, "%s\t%s\t%d\t%d.%d\n", vm_name, sla_option, sla_value, int_sla, flt_sla);
=======

<<<<<<< HEAD
					/*  Collect of performance statistic and CPU quota  */
				before_sla = curr_sla->now_sla / 100;
				after_sla = curr_sla->now_sla % 100
=======
				slo_iops = gos_vm_list[i]->now_perf.iops;
				slo_pps = gos_vm_list[i]->now_perf.credit;
				slo_bandwidth = gos_vm_list[i]->now_perf.bandwidth;
				slo_latency = gos_vm_list[i]->now_perf.latency;
				slo_cpu_usage = gos_vm_list[i]->now_perf.cpu_usage;
				cpu_quota = curr_sla->now_quota;

				seq_printf(m, "%s\t%s\t\t%d\t\t%d.%d\t\t%ld\t\t%ld\t\t%ld\t\t%ld\t\t%ld\t\t%lu\n", vm_name, sla_option, 
					sla_value, int_sla, flt_sla, slo_iops, slo_pps, slo_bandwidth, slo_latency, slo_cpu_usage, cpu_quota);

>>>>>>> c4fca867ea796663c5d23f04449deaa22d0d7bee

<<<<<<< HEAD
				sla_option = curr_sla->sla_option;
        cpu_quota = curr_sla->now_quota;
        cpu_usage = gos_vm_list[i]->now_perf.cpu_usage;
				b_iops = gos_vm_list[i]->now_perf.iops
				b_pps = gos_vm_list[i]->now_perf.credit;
				b_bandwidth = gos_vm_list[i]->now_perf.bandwidth;
				b_latency = gos_vm_list[i]->now_perf.latency;

        /*   Print out the collected of performance statistic and CPU quota */
        seq_printf(m, "%s\t%s\t%d\t%d.%d\t%d\t%lu\t%lu\t%lu\t%lu\t%lu.%lu\n", vm_name, sla_option,
          sla_value, before_sla, after_sla, cpu_quota, b_iops, b_pps, b_bandwidth, b_latency, cpu_usage / 100, cpu_usage % 100);
>>>>>>> 1cb5d62b1fe80fe5f57e398f0f952dadfcdde73e
=======
				pre_quota = curr_sla->prev_quota ;
				now_quota = curr_sla->now_quota ;
				iops = gos_vm_list[i]->now_perf.iops ;
				pps = gos_vm_list[i]->now_perf.credit;
				bandwidth = gos_vm_list[i]->now_perf.bandwidth;
				latency = gos_vm_list[i]->now_perf.latency;
				cpu_usage = gos_vm_list[i]->now_perf.cpu_usage;


				seq_printf(m, "%s\t%s\t%d\t%d.%d\t%d\t%d\t%lu\t%lu\t%lu\t%lu\t%lu.%lu\n",
					   vm_name, sla_option, sla_value, int_sla, flt_sla, pre_quota, now_quota, 
					   iops, pps, bandwidth, latency, cpu_usage/100, cpu_usage%100);

>>>>>>> f0d07b1adf2e39ea00637ea22d3a1db7843570c7
=======
				cpu_quota = curr_sla->now_quota;
				iops = gos_vm_list[i]->now_perf.iops;
				bandwidth = gos_vm_list[i]->now_perf.bandwidth;
				latency = gos_vm_list[i]->now_perf.latency;
				pps = gos_vm_list[i]->now_perf.credit;
				cpu_util = gos_vm_list[i]->now_perf.cpu_usage;

				seq_printf(m, "%s\t%s\t\t%d\t\t%d.%d\t\t%d\t\t%lu\t\t%lu\t\t%lu\t\t%lu\t%lu.%lu\n", vm_name, sla_option,
					sla_value, int_sla, flt_sla, cpu_quota, iops, bandwidth, latency, pps, cpu_util / 100, cpu_util % 100);


>>>>>>> 46f4bdf17dfd81a9639ce97c473c7a62cde11f4b
			}

<<<<<<< HEAD
				//Fetching the additional slo types
				cpu_quota = curr_sla->now_quota;
				iops = gos_vm_list[i]->now_perf.iops;
				bandwidth = gos_vm_list[i]->now_perf.bandwidth;
				latency = gos_vm_list[i]->now_perf.latency;
				pps = gos_vm_list[i]->now_perf.credit;
                                //Adding the additional SLO types to be printed
				seq_printf(m, "%s\t%s\t%d\t%d.%d\t%d\t%lu\t%lu\t%lu\t%lu\n", vm_name, sla_option,
					sla_value, int_sla, flt_sla, cpu_quota, iops, bandwidth, latency, pps);
=======
			_iops = gos_vm_list[i]->now_perf.iops;
			_credit = gos_vm_list[i]->now_perf.credit;
			_bandwidth = gos_vm_list[i]->now_perf.bandwidth;
			_latency = gos_vm_list[i]->now_perf.latency;
			_cpu_usage = gos_vm_list[i]->now_perf.cpu_usage;
			// _cpu_quota = curr_sla->now_quota;
			_cpu_quota = now_quota_out[i];
>>>>>>> 4955a39570570feb6f1f5ec46642a89702d47cd7

			seq_puts(m, "\nVM_NAME\tIOPS\tPPS\tbandwidth\tlatency\tCPU_usage\tCPU_quota\n");
            seq_printf(m, "%s\t%lu\t%lu\t%lu\t\t%lu\t%lu\t\t%ld\n\n", vm_name, _iops, _credit, _bandwidth, _latency, _cpu_usage, _cpu_quota);
=======
			list_for_each_entry(curr_sla, &(gos_vm_list[i]->sla_list), sla_list) {
				if (curr_sla->sla_type == b_bw){
                    //gos_vm_list[i]->now_perf.bandwidth=curr_sla->now_sla;
                     sla_value = curr_sla->sla_target.bandwidth;
                }
				else if (curr_sla->sla_type == b_iops){
                    //gos_vm_list[i]->now_perf.iops=curr_sla->now_sla;
                    sla_value = curr_sla->sla_target.iops;
                }
				else if (curr_sla->sla_type == b_lat){
                    //gos_vm_list[i]->now_perf.latency=curr_sla->now_sla;
                    sla_value = curr_sla->sla_target.latency;
                }
				else if (curr_sla->sla_type == n_mincredit){
                    gos_vm_list[i]->now_perf.credit=curr_sla->now_sla;
                    sla_value = curr_sla->sla_target.credit;
                }
				else if (curr_sla->sla_type == n_maxcredit){
                    gos_vm_list[i]->now_perf.credit=curr_sla->now_sla;
                    sla_value = curr_sla->sla_target.credit;
                    cpu_quota = curr_sla->now_quota;
                }
				else if (curr_sla->sla_type == n_weight){
                    //gos_vm_list[i]->now_perf.weight=curr_sla->sla_target.weight;
                    sla_value = curr_sla->sla_target.weight;
                }
				else if (curr_sla->sla_type == c_usg){
                    //gos_vm_list[i]->now_perf.cpu_usage=curr_sla->now_sla;
                    sla_value = curr_sla->sla_target.cpu_usage;
                    //cpu_quota = curr_sla->now_quota;
                }
                int_sla = curr_sla->now_sla / 100;
                flt_sla = curr_sla->now_sla % 100;
                sla_option = curr_sla->sla_option;
                gos_vm_list[i]->now_perf.cpu_usage = _vm_cpu_util[i];

                seq_printf(m, "%-8s%-16s%-16d%d.%d\n", vm_name, sla_option,
                sla_value, int_sla, flt_sla);
            }
            seq_puts(m, "\nVM_NAME\tBANDWIDTH\tLATENCY\tIOPS\tPPS\tCPU_USAGE\tCPU_QUOTA\n");
            seq_printf(m, "%-8s%-16lu%-8lu%-8lu%-8lu%-16lu%-16ld\n\n\n", 
                vm_name,
                gos_vm_list[i]->now_perf.bandwidth,
                gos_vm_list[i]->now_perf.latency,
                gos_vm_list[i]->now_perf.iops,
                gos_vm_list[i]->now_perf.credit,
                gos_vm_list[i]->now_perf.cpu_usage,
                cpu_quota);
>>>>>>> 768c5d8596b09e688a4adbb715bd4414c9e963db
		}
	}
	return 0;
}

/*
 * SLA insertion format
 * [VM_NAME] [SLA_TYPE] [SLA_VALUE] [PID] [DEVICE_NAME]
 */
static ssize_t gos_write(struct file *f, const char __user *u, size_t s, loff_t *l)
{
	struct gos_vm_info *tmp_vm_info = NULL;
	struct gos_vm_sla *tmp_vm_sla = NULL, *curr_sla;
	int index = 0, is_vm_exist = 0, replace_sla = 0;
	int i_parm = 0, str_len;
	char *tmp_buf = NULL, *tosep, *tok = NULL;
	char *sep = " ";
	struct pid *tmp_pid;

	printk(KERN_INFO "Write information about target vm\n");

	tmp_buf = kzalloc(sizeof(char) * s, GFP_KERNEL);
	if (!tmp_buf) {
		printk(KERN_INFO "[Error] Can not allocate buffer\n");
		return s;
	}

	copy_from_user(tmp_buf, u, s);
	tmp_buf[s-1] = '\0';

	tosep = tmp_buf;


	while((tok = strsep(&tosep, sep)) != NULL) {
		int err = 0;
		int k;
		long long tmp_l = 0;

		if (i_parm == 0) {
			for (index = 0; index < VM_NUM; index++) {
				if (!gos_vm_list[index])
					continue;

				str_len = strlen(tok);
				printk("gos_debug: gos_vm_list->vm_name : %s tok : %s, len : %d\n",
						gos_vm_list[index]->vm_name, tok, str_len);
				if (!strncmp(gos_vm_list[index]->vm_name, tok, str_len)) {
					printk("gos_debug: gos_vm_list is already exist\n");
					tmp_vm_info = gos_vm_list[index];
					is_vm_exist = 1;
					break;
				}
			}

			if (!is_vm_exist) {
				tmp_vm_info = kzalloc(sizeof(struct gos_vm_info), GFP_KERNEL);
				for (k = 0; k < VCPU_NUM; k++)
					tmp_vm_info->vcpu[k] = NULL;
				INIT_LIST_HEAD(&(tmp_vm_info->sla_list));
				strcpy(tmp_vm_info->vm_name, tok);
				printk("gos_debug: create new gos_vm_info and init sla list\n");
			}
			tmp_vm_sla = kzalloc(sizeof(*tmp_vm_sla), GFP_KERNEL);

		} else if (i_parm  == 1) {
			char *tmp_tok = strsep(&tosep, sep);
			err = kstrtoll(tmp_tok, 10, &tmp_l);

			if (strcmp(tok, "b_bw") == 0) {
				tmp_vm_sla->sla_target.bandwidth = tmp_l;
				tmp_vm_sla->sla_type = b_bw;
				tmp_vm_sla->control_type = ssd;
			} else if (strcmp(tok, "b_iops") == 0) {
				tmp_vm_sla->sla_target.iops = tmp_l;
				tmp_vm_sla->sla_type = b_iops;
				tmp_vm_sla->control_type = ssd;
			} else if (strcmp(tok, "b_lat") == 0) {
				tmp_vm_sla->sla_target.latency = tmp_l;
				tmp_vm_sla->sla_type = b_lat;
				tmp_vm_sla->control_type = ssd;
			} else if (strcmp(tok, "c_usage") == 0) {
				tmp_vm_sla->sla_target.cpu_usage = tmp_l;
				tmp_vm_sla->sla_type = c_usg;
				tmp_vm_sla->control_type = cpu;
			} else if (strcmp(tok, "n_mincredit") == 0) {
				tmp_vm_sla->sla_target.credit = tmp_l;
				tmp_vm_sla->sla_type = n_mincredit;
				tmp_vm_sla->control_type = network;
			} else if (strcmp(tok, "n_maxcredit") == 0) {
				tmp_vm_sla->sla_target.credit = tmp_l;
				tmp_vm_sla->sla_type = n_maxcredit;
				tmp_vm_sla->control_type = network;
			} else if (strcmp(tok, "n_weight") == 0) {
				tmp_vm_sla->sla_target.weight = tmp_l;
				tmp_vm_sla->sla_type = n_weight;
				tmp_vm_sla->control_type = network;
			} else {
				printk(KERN_INFO "gos: [Error] Wrong Parameter\n");
				goto out;
			}
			printk("gos_debug: sla option, value are inserted to gos_vm_sla\n");
			tmp_vm_sla->now_quota = WORK_CONSERVING;
			strcpy(tmp_vm_sla->sla_option, tok);
			i_parm++;
		} else if (i_parm == 3) {
			if (tmp_vm_sla->control_type == cpu) {
				if (tmp_vm_info->vcpu[0])
					replace_sla = 1;
				else {
					for (index = 0; index < VCPU_NUM; index++) {
						kstrtoll(tok, 10, &tmp_l);
						tmp_pid = find_get_pid(tmp_l);
						tmp_vm_info->vcpu[index] = pid_task(tmp_pid, PIDTYPE_PID);
						if (tmp_vm_info->vcpu[index] == NULL) {
							goto out;
							break;
						}
						if (index != VCPU_NUM -1)
							tok = strsep(&tosep, sep);
					}
				}
			} else {
				err = kstrtoll(tok, 10, &tmp_l);
				tmp_pid = find_get_pid(tmp_l);
				if (tmp_vm_sla->control_type == network) {
					if (tmp_vm_info->vhost) {
						printk("gos_debug: replace network sla value\n");
						replace_sla = 1;
					}
					else
						tmp_vm_info->vhost = pid_task(tmp_pid, PIDTYPE_PID);

					if (tmp_vm_info->vhost == NULL)
						goto out;
					if (add_network_sla(tmp_vm_info, tmp_vm_sla, tmp_l))
						goto out;
					printk("gos_debug: new vhost is inserted\n");
				} else if (tmp_vm_sla->control_type == ssd) {
					if (tmp_vm_info->iothread) {
						printk("gos_debug: replace ssd io sla value\n");
						replace_sla = 1;
					}
					else
						tmp_vm_info->iothread = pid_task(tmp_pid, PIDTYPE_PID);

					if (tmp_vm_info->iothread == NULL)
						goto out;
					printk("gos_debug: new iothread is inserted\n");
				}
			}
		} else if(i_parm == 4) {
			if (tmp_vm_sla->control_type == ssd) {
				strcpy(tmp_vm_info->dev_name, tok);
				printk("gos_debug: device name is inserted\n");
			}
		}

		i_parm++;
	}

	/* insert tmp_vm_info to gos_vm_list */
	if (is_vm_exist) {
		if (replace_sla) {
			list_for_each_entry(curr_sla, &(tmp_vm_info->sla_list), sla_list) {
				if (curr_sla->control_type == tmp_vm_sla->control_type)
					break;
			}
			list_del(&(curr_sla->sla_list));
			kfree(curr_sla);
			printk("gos: replace sla value\n");
		} else {
			printk("gos: add new sla value for already existed VM\n");
		}
	} else {
		for (index = 0; index < VM_NUM; index++) {
			if (gos_vm_list[index] == NULL) {
				printk("gos: gos_vm_info[%d] inserted\n", index);
				gos_vm_list[index] = tmp_vm_info;
				break;
			}
		}
	}
	list_add_tail(&(tmp_vm_sla->sla_list), &(tmp_vm_info->sla_list));

	kfree(tmp_buf);

	return s;

out:
	printk("gos_debug : error\n");
	kfree(tmp_buf);
	if (!is_vm_exist)
		kfree(tmp_vm_info);
	kfree(tmp_vm_sla);
	return s;
}

static int gos_open(struct inode *inode, struct file *file)
{
	return single_open(file, gos_vm_info_show, NULL);
}

static inline int init_gos_proc(void)
{
	gos_proc_file = proc_create(PROC_NAME, 774, NULL, &gos_vm_info);

	if(gos_proc_file == NULL)
	{
		remove_proc_entry(PROC_NAME, NULL);
		printk(KERN_ALERT "[Error] Could not initialize /proc/%s\n", PROC_NAME);
		return -ENOMEM;
	}

	printk(KERN_INFO "/proc/%s created\n", PROC_NAME);

	return 0;
}

static inline int exit_gos_proc(void)
{
	remove_proc_entry(PROC_NAME, NULL);

	printk(KERN_INFO "/proc/%s deleted\n", PROC_NAME);

	return 0;
}

/*
   End of the proc file system code
*/

static int __init gos_init(void)
{
	int i = 0, err = 0;

	printk(KERN_INFO "GoS ON\n\n");

	if((err = init_gos_proc())) return err;
	if((err = init_gos_timer())) return err;

	for(i = 0 ; i < VM_NUM ; i++)
		gos_vm_list[i] = NULL;

	return 0;
}

static void __exit gos_exit(void)
{
	int i = 0, err = 0;
	struct gos_vm_sla *curr_sla, *next_sla;

	for(i = 0 ; i < VM_NUM ; i++)
	{
		if(gos_vm_list[i] != NULL)
			list_for_each_entry_safe(curr_sla, next_sla, &(gos_vm_list[i]->sla_list), sla_list) {
				list_del(&(curr_sla->sla_list));
				kfree(curr_sla);
			}

			kfree(gos_vm_list[i]);
			gos_vm_list[i] = NULL;
	}

	if((err = exit_gos_proc())) printk(KERN_ALERT "[Error] Exit proc routine has error\n");
	if((err = exit_gos_timer())) printk(KERN_ALERT "[Error] Exit timer routine has error\n");

	printk(KERN_INFO "GoS OFF\n\n");
}

static const struct file_operations gos_vm_info = {
	.owner = THIS_MODULE,
	.open = gos_open,
	.write = gos_write,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = seq_release,
};

module_init(gos_init);
module_exit(gos_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION(DRIVER_DESC);
