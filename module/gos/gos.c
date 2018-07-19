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

/*
   Start of the feedback controller code
*/

static long io_quota = 0;

long get_vm_quota(int vm_num)
{
	long quota = 0;
	if (gos_vm_list[vm_num] == NULL)
		return 0;
	
	if (gos_vm_list[vm_num]->control_type == cpu)
		quota = tg_get_cfs_quota(gos_vm_list[vm_num]->vcpu[0]->sched_task_group);
	else if (gos_vm_list[vm_num]->control_type == network)
		quota = tg_get_cfs_quota(gos_vm_list[vm_num]->vhost->sched_task_group);
	else if (gos_vm_list[vm_num]->control_type == ssd) 
		quota = tg_get_cfs_quota(gos_vm_list[vm_num]->iothread->sched_task_group);
	return quota;
}

int set_vm_quota(int vm_num, long quota)
{
	int i = 0;

	if (gos_vm_list[vm_num] == NULL)
		return 1;

	if (gos_vm_list[vm_num]->control_type == cpu) {
		for (i = 0 ; i < VCPU_NUM ; i++)
		{
			if(gos_vm_list[vm_num]->vcpu[i] != NULL)
			{
				tg_set_cfs_quota(gos_vm_list[vm_num]->vcpu[i]->sched_task_group, quota);
			}
			else {
				printk(KERN_INFO "[ERROR] VM%d's task_group[%d] == NULL\n", vm_num, i);
				return 1;
			}
		}
	} else if (gos_vm_list[vm_num]->control_type == network)
		tg_set_cfs_quota(gos_vm_list[vm_num]->vhost->sched_task_group, quota);
	else if (gos_vm_list[vm_num]->control_type == ssd)
		tg_set_cfs_quota(gos_vm_list[vm_num]->iothread->sched_task_group, quota);
	
	return 0;
}

static void get_cpu_resource(void)
{
	long now_quota, disstaisfy_quantum = 0;
	int i, k;

	for (i = 0; i < VM_NUM; i++) {
		if (gos_vm_list[i] == NULL)
			continue;	
			
		if (gos_vm_list[i]->control_type != cpu)
			continue;

		now_quota = gos_vm_list[i]->now_quota;

		if (now_quota == WORK_CONSERVING)
			now_quota = PERIOD;

		for (k = 0; k < VM_NUM; k++) {
			if (gos_vm_list[k]->control_type != cpu)
				dissatisfy_quantum += SLA_GOAL - gos_vm_list[k]->now_sla;
		}

		if (dissatisfy_quantum > 0) {
			/* now_quota = ... */
		}

		set_vm_quota(i, now_quota);
		gos_vm_list[i]->now_quota = now_quota;	
	}
}

/* tmp version */
static unsigned long get_sys_cpu_util(void)
{
	u64 user, nice, system, idle, iowait, irq, softirq, steal;
	u64 guest, guest_nice;
	u64 total_time = 0, used_time = 0;
	u64 tmp_total, tmp_used;
	u64 sys_util, int_sys_util, flo_sys_util;
	int i;
	
	user = nice = system = idle = iowait = irq = softirq = steal = 0;
	guest = guest_nice = 0;

	for_each_possible_cpu(i) {
		user += kcpustat_cpu(i).cpustat[CPUTIME_USER];
		nice += kcpustat_cpu(i).cpustat[CPUTIME_NICE];
		system += kcpustat_cpu(i).cpustat[CPUTIME_SYSTEM];
		idle += kcpustat_cpu(i).cpustat[CPUTIME_IDLE];
		iowait += kcpustat_cpu(i).cpustat[CPUTIME_IOWAIT];
		irq += kcpustat_cpu(i).cpustat[CPUTIME_IRQ];
		softirq += kcpustat_cpu(i).cpustat[CPUTIME_SOFTIRQ];
		steal += kcpustat_cpu(i).cpustat[CPUTIME_STEAL];
		guest += kcpustat_cpu(i).cpustat[CPUTIME_GUEST];
		guest_nice += kcpustat_cpu(i).cpustat[CPUTIME_GUEST_NICE]; 
	}
	
	total_time = user + nice + system + idle + iowait + irq + softirq + steal
			+ guest + guest_nice;
	used_time = total_time - idle;

	if (prev_total_time == 0) {
		prev_total_time = total_time;
		prev_used_time = used_time;
		sys_util = 0;
	} else {
		tmp_total = total_time - prev_total_time;
		tmp_used = used_time - prev_used_time;
		sys_util = (tmp_used * 10000) / tmp_total;
		int_sys_util = sys_util / 100;
		flo_sys_util = sys_util % 100;
		prev_total_time = total_time;
		prev_used_time = used_time;
		printk("sys util : %lu.%lu\n", int_sys_util, flo_sys_util);
	}
	return sys_util;
}

static void update_vm_cpu_time(int vm_num)
{
	struct task_struct **ts, *vhost, *iothread;
	unsigned long tmp_total_time = 0;
	int i;

	if (gos_vm_list[vm_num]->control_type == cpu) {
		ts = gos_vm_list[vm_num]->vcpu;
		for (i = 0; i < VCPU_NUM; i++)
			tmp_total_time += ts[i]->utime + ts[i]->stime;
	} else if (gos_vm_list[vm_num]->control_type == network) {
		vhost = gos_vm_list[vm_num]->vhost;
		tmp_total_time = vhost->utime + vhost->stime;
	} else if (gos_vm_list[vm_num]->control_type == ssd) {
		iothread = gos_vm_list[vm_num]->iothread;
		tmp_total_time = iothread->utime + iothread->stime;
	}
	
	if (gos_vm_list[vm_num]->prev_cpu_time == 0)
		gos_vm_list[vm_num]->prev_cpu_time = tmp_total_time;
	else
		gos_vm_list[vm_num]->prev_cpu_time = gos_vm_list[vm_num]->now_cpu_time;
	
	gos_vm_list[vm_num]->now_cpu_time = tmp_total_time;

}

void feedback_controller(unsigned long elapsed_time)
{
	unsigned long vm_cpu_util;
	unsigned long sys_cpu_util = get_sys_cpu_util();
	unsigned long prev_cpu_time, now_cpu_time;
	long now_quota, prev_quota, tmp_quota = 0;
	unsigned long prev_sla, now_sla;
	int i;

	for (i = 0; i < VM_NUM; i++) {
		if (gos_vm_list[i] == NULL)
			continue;	
		
		if (gos_vm_list[i]->control_type == cpu)
			continue;

		now_quota = gos_vm_list[i]->now_quota;
		prev_quota = gos_vm_list[i]->prev_quota;
		now_sla = gos_vm_list[i]->now_sla;
		prev_sla = gos_vm_list[i]->prev_sla;
		update_vm_cpu_time(i);

		/* vm_cpu_util value 10000 = 100.00% */
		/* ms */
		prev_cpu_time = gos_vm_list[i]->prev_cpu_time * 1000 / HZ;
		now_cpu_time = gos_vm_list[i]->now_cpu_time * 1000 / HZ;
		/* 100.00% = 10000, gos_interval is 3s*/	
		vm_cpu_util = (now_cpu_time - prev_cpu_time) * 10000 / (gos_interval / 1000000);

		printk("gos: before now_sla: %lu, prev_sla: %lu\n", now_sla, prev_sla);
		printk("gos: before now_quota: %ld, prev_quota: %ld\n", now_quota, prev_quota); 
		printk("gos: vm cpu util = %lu, %lu.%lu tmp_quota = %ld\n", vm_cpu_util, vm_cpu_util / 100, vm_cpu_util % 100, tmp_quota);
		/* initial state */
		if (now_sla == 0 || vm_cpu_util < EXIT_CPU_UTIL) {
			tmp_quota = now_quota;
			now_quota = WORK_CONSERVING;
		} else if (now_sla > OVERSATISFY) {
			/* TODO tmp routine */
			/* remove this routine if oios can calculate SLA alone */
			tmp_quota = now_quota;
			if (now_quota == WORK_CONSERVING)
				now_quota = vm_cpu_util * PERIOD / 10000;
			now_quota -= (now_quota * (now_sla - SLA_GOAL) / SLA_GOAL) / 3;
		} else if (now_sla < DISSATISFY && now_quota > 0) {	
			tmp_quota = now_quota;
			/* remove hared coded 10500. this is for not creating minus value */
			/* slow down increasment speed */
			//now_quota += (now_quota * (10500 - vm_cpu_util) / 10500) / 3;
			now_quota += (now_quota * (SLA_GOAL - now_sla) / SLA_GOAL) / 3;
		} else if (now_sla < DISSATISFY && now_quota == WORK_CONSERVING) {
			tmp_quota = vm_cpu_util * PERIOD / 10000;
			/* This value has risk to be zero */
			now_quota = tmp_quota + ((tmp_quota * (SLA_GOAL - now_sla)) / SLA_GOAL) / 3;
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
		set_vm_quota(i, now_quota);
		gos_vm_list[i]->prev_quota = tmp_quota;
		gos_vm_list[i]->now_quota = now_quota;
		printk("gos: after now_quota: %ld, prev_quota: %ld\n", now_quota, tmp_quota); 
	}
	if (sys_cpu_util > SYS_CPU_MAX_UTIL)
		get_cpu_resource();
	
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

/*
   Start of the proc file system code9
*/

static inline int get_int_from_char(char *tmp_s, int start, int end)
{
	int int_result = 0, i = 0;

	for(i = start ; i < end-1 ; i++)
	{
		int_result *= 10;

		int_result += tmp_s[i] - 0x30;
	}

	return int_result;
}

static int gos_vm_info_show(struct seq_file *m, void *v)
{
	int i = 0;

	seq_puts(m, "VM_NUM\tSLA Option\tSLA Value\tSLA Percentage\tDevice Name\tPeriod\tQuota\n");

	for(i = 0 ; i < VM_NUM ; i++)
	{
		if(gos_vm_list[i] != NULL)
		{
			int j = 0, sla_value = 0;
			char *sla_option = gos_vm_list[i]->sla_option;

			if(gos_vm_list[i]->sla_type == b_iops)
				sla_value = gos_vm_list[i]->sla_target.iops;
			else if(gos_vm_list[i]->sla_type == b_bw)
				sla_value = gos_vm_list[i]->sla_target.bandwidth;
			else if(gos_vm_list[i]->sla_type == b_lat) 
				sla_value = gos_vm_list[i]->sla_target.latency;
			else if(gos_vm_list[i]->sla_type == c_usg)
				sla_value = gos_vm_list[i]->sla_target.cpu_usage;
			else
				sla_value = -1;

			if(sla_value > 100000000)
				seq_printf(m, "%d\t%s\t\t%d\t\t%ld\t%s", i, sla_option, 
					sla_value, gos_vm_list[i]->now_sla, gos_vm_list[i]->dev_name);
			else
				seq_printf(m, "%d\t%s\t\t%d\t\t%ld\t\t%s", i, sla_option, 
					sla_value, gos_vm_list[i]->now_sla, gos_vm_list[i]->dev_name);

			if(gos_vm_list[i]->vcpu[j] != NULL)
			{
			/*	if(gos_vm_list[i]->sla_type == c_usg)
					seq_printf(m, "\t%ld\t%ld\n", get_vm_period(i), get_vm_quota(i));
				else
					seq_printf(m, "\t%ld\t%ld\n", get_vm_period(i), gos_vm_list[i]->now_quota);*/

				seq_printf(m, "\t%ld\t%ld\n", PERIOD, gos_vm_list[i]->prev_quota);
			}
			else
				seq_printf(m, "\n");
		}
	}

	return 0;
}

static ssize_t gos_write(struct file *f, const char __user *u, size_t s, loff_t *l)
{
	struct gos_vm_info *tmp_vm_info = NULL;
	int index = 0, f_free = 0, f_dup = 0, f_vm_off = 0;
	char *tmp_buf = NULL;
	struct pid *tmp_pid;

	printk(KERN_INFO "Write information about target vm\n");

	tmp_buf = kzalloc(sizeof(char) * s, GFP_KERNEL);
	if (!tmp_buf)
	{
		printk(KERN_INFO "[Error] Can not allocate buffer\n");
		return s;
	}

	copy_from_user(tmp_buf, u, s);
	tmp_buf[s-1] = '\0';

	char *tosep = tmp_buf, *tok = NULL;
	char *sep = " ";
	int i_parm = 0;

	tmp_vm_info = kzalloc(sizeof(struct gos_vm_info), GFP_KERNEL);

	while((tok = strsep(&tosep, sep)) != NULL) {
		int err = 0;
		long long tmp_l = 0;

		if(i_parm == 0) {
			char *tmp_tok = strsep(&tosep, sep);
			err = kstrtoll(tmp_tok, 10, &tmp_l);

			if (strcmp(tok, "b_bw") == 0) {
				tmp_vm_info->sla_target.bandwidth = tmp_l;
				tmp_vm_info->sla_type = b_bw;
				tmp_vm_info->control_type = ssd;
			} else if (strcmp(tok, "b_iops") == 0) {
				tmp_vm_info->sla_target.iops = tmp_l;
				tmp_vm_info->sla_type = b_iops;
				tmp_vm_info->control_type = ssd;
			} else if (strcmp(tok, "b_lat") == 0) {
				tmp_vm_info->sla_target.latency = tmp_l;
				tmp_vm_info->sla_type = b_lat;
				tmp_vm_info->control_type = ssd;	
			} else if (strcmp(tok, "c_usage") == 0) {
				tmp_vm_info->sla_target.cpu_usage = tmp_l;
				tmp_vm_info->sla_type = c_usg;
				tmp_vm_info->control_type = cpu;
			} else if (strcmp(tok, "n_mincredit") == 0) {
				tmp_vm_info->sla_type = n_mincredit;
				tmp_vm_info->control_type = network;
			} else if (strcmp(tok, "n_maxcredit") == 0) {
				tmp_vm_info->sla_type = n_maxcredit;
				tmp_vm_info->control_type = network;
			} else if (strcmp(tok, "weight") == 0) {
				tmp_vm_info->sla_type = n_weight;
				tmp_vm_info->control_type = network;
			} else if(strcmp(tok, "free") == 0) {
				f_free = 1;
				break;
			} else {
				printk(KERN_INFO "gos: [Error] Wrong Parameter\n");
				goto out;
			}
			tmp_vm_info->now_quota = WORK_CONSERVING;
			strcpy(tmp_vm_info->sla_option, tok);
			i_parm++;
	
		} else if (i_parm  == 2) {
			if (tmp_vm_info->control_type == cpu) {
				for (index = 0; index < VCPU_NUM; index++) {
					kstrtoll(tok, 10, &tmp_l);
					tmp_pid = find_get_pid(tmp_l);
					tmp_vm_info->vcpu[index] = pid_task(tmp_pid, PIDTYPE_PID);
					if (tmp_vm_info->vcpu[index] == NULL) {
						err = 1;
						break;
					}
					if (index != VCPU_NUM -1)
						tok = strsep(&tosep, sep);
				}
			} else {				
				err = kstrtoll(tok, 10, &tmp_l);
				tmp_pid = find_get_pid(tmp_l);
				if (tmp_vm_info->control_type == network) {
					tmp_vm_info->vhost = pid_task(tmp_pid, PIDTYPE_PID);
					if (tmp_vm_info->vhost == NULL)
						err = 1;
				} else if (tmp_vm_info->control_type == ssd) {
					tmp_vm_info->iothread = pid_task(tmp_pid, PIDTYPE_PID);
					if (tmp_vm_info->iothread == NULL)
						err = 1;
				}
			}
			if (unlikely(err)) {
				printk(KERN_INFO "gos: [WARNING] VM(PID:%lld) is off\n", tmp_l);
				goto out;
			}
		} else if(i_parm == 3) {
			strcpy(tmp_vm_info->dev_name, tok);
			/* what doesi it mean? */
			if(strncmp(tmp_vm_info->dev_name, "/dev/nvme", 9) == 0)
				tmp_vm_info->control_type = cpu;
		}

		i_parm++;
	}

	for (index = 0 ; index < VM_NUM ; index++) {
		/*
		if ((gos_vm_list[index] != NULL && 
			task_pid_nr(tmp_vm_info->vhost) == task_pid_nr(gos_vm_list[index]->vhost)) ||
			(gos_vm_list[index] != NULL && 
			task_pid_nr(tmp_vm_info->iothread) == task_pid_nr(gos_vm_list[index]->iothread))) {
			
			kfree(gos_vm_list[index]);
			gos_vm_list[index] = NULL;

			if(f_free == 1) {
				printk(KERN_INFO "Free VM %d\n", index);
				goto out;
			} else {
				gos_vm_list[index] = tmp_vm_info;
				f_dup = 1;
			}
			break;
		} else if(gos_vm_list[index] == NULL) {
			if(f_free != 1 && f_dup != 1)
				gos_vm_list[index] = tmp_vm_info;
			else
				f_dup = 0;

			break;
		} else if(index == VM_NUM - 1 && gos_vm_list[index] != NULL)
			printk(KERN_INFO "[Error] Target VM is full\n");
		*/
		if (gos_vm_list[index] == NULL) {
			printk("gos: gos_vm_info[%d] inserted\n", index);
			gos_vm_list[index] = tmp_vm_info;
			break;	
		}
	}

	kfree(tmp_buf);

	return s;

out:
	kfree(tmp_buf);
	kfree(tmp_vm_info);
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

	for(i = 0 ; i < VM_NUM ; i++)
	{
		if(gos_vm_list[i] != NULL)
			kfree(gos_vm_list[i]);
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
