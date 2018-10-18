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


int set_vm_quota(int vm_num, long quota, enum gos_type control_type)
{
	int i = 0;

	if (gos_vm_list[vm_num] == NULL)
		return 1;

	if (control_type == cpu) {
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

			if (curr_sla->control_type == cpu)
				continue;

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
	struct gos_vm_sla *curr_sla;
	int sla_value;
	int int_sla, flt_sla;
	char *vm_name, *sla_option;
	int i = 0;
	
	seq_puts(m, "VM_NAME\tSLA Option\tSLA Value\tSLA Percentage\n");

	for(i = 0 ; i < VM_NUM ; i++)
	{
		if(gos_vm_list[i] != NULL) {
			vm_name = gos_vm_list[i]->vm_name;
					
			list_for_each_entry(curr_sla, &(gos_vm_list[i]->sla_list), sla_list) {
				if (curr_sla->sla_type == b_bw)
					sla_value = curr_sla->sla_target.bandwidth;
				else if (curr_sla->sla_type == b_iops)
					sla_value = curr_sla->sla_target.iops;
				else if (curr_sla->sla_type == b_lat)
					sla_value = curr_sla->sla_target.latency;
				else if (curr_sla->sla_type == n_mincredit)
					sla_value = curr_sla->sla_target.credit;
				else if (curr_sla->sla_type == n_maxcredit)
					sla_value = curr_sla->sla_target.credit;
				else if (curr_sla->sla_type == n_weight)
					sla_value = curr_sla->sla_target.weight;
	
				int_sla = curr_sla->now_sla / 100;
				flt_sla = curr_sla->now_sla % 100;
				sla_option = curr_sla->sla_option;

				seq_printf(m, "%s\t%s\t%d\t%d.%d\n", vm_name, sla_option, 
					sla_value, int_sla, flt_sla);


			}
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
				if (tmp_vm_info->vcpu)
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
