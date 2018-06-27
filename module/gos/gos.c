#define GOS_SOURCE
#include "gos.h"

#define PCPU_NUM 10
#define MAX_QUOTA(period) (PCPU_NUM * period)
#define INTERVAL 3000

#define DRIVER_DESC "Guarantee of SLA (GoS)"

#define PROC_NAME "gos_vm_info"
#define IO_VM_PERIOD 9

//#define DEBUG
#define OVERSATISFY 10100	// 101.00 %
#define DISSATISFY 9900		// 99.00 %

static struct timer_list gos_timer;

static struct proc_dir_entry *gos_proc_file;
static const struct file_operations gos_vm_info;

static unsigned long prev_jiffies = 0;

/*
   Start of the feedback controller code
*/

static long io_quota = 0;

long get_vm_quota(int vm_num)
{
	if(gos_vm_list[vm_num] == NULL)
		return 0;

	else return tg_get_cfs_quota(gos_vm_list[vm_num]->tg[0]);
}

long get_vm_period(int vm_num)
{
	if(gos_vm_list[vm_num] == NULL)
		return 0;

	else return tg_get_cfs_period(gos_vm_list[vm_num]->tg[0]);
}

int set_vm_quota(int vm_num, long quota)
{
	int i = 0;

	if(gos_vm_list[vm_num] == NULL)
		return 1;

	for(i = 0 ; i < VCPU_NUM ; i++)
	{
		if(gos_vm_list[vm_num]->tg[i] != NULL)
		{
			tg_set_cfs_quota(gos_vm_list[vm_num]->tg[i], quota);
		}
		else {
			printk(KERN_INFO "[ERROR] VM%d's task_group[%d] == NULL\n", vm_num, i);
			return 1;
		}
	}

	return 0;
}

int set_vm_period(int vm_num, long period)
{
	int i = 0;

	if(gos_vm_list[vm_num] == NULL)
		return 1;

	for(i = 0 ; i < VCPU_NUM ; i++)
	{
		if(gos_vm_list[vm_num]->tg[i] != NULL)
			tg_set_cfs_period(gos_vm_list[vm_num]->tg[i], period);
		else {
			printk(KERN_INFO "[ERROR] VM%d's task_group[%d] == NULL\n", vm_num, i);
			return 1;
		}
	}

	return 0;
}

void feedback_controller(unsigned long elapsed_time)
{
	int i = 0;

	int f_vm[VM_NUM] = {0};	// -1 : non-satisfy , 0 : satisfy , 1 : over-satisfy
	
	long tmp_value[VM_NUM] = {0};
	long next_quota = 0;
	long tmp_quota = 0;

	unsigned long now_ok_SLA = 0;
	unsigned long io_vm_total_cpu_usage = 0;
	long over_total_quota = 0;

	long real_quota = 0;

	// Calculate proportional value about SLA percentage
#ifdef DEBUG
	printk(KERN_INFO "\n<---- Before ---->\n");
#endif
	for(i = 0 ; i < VM_NUM ; i++)
	{
		if(gos_vm_list[i] != NULL)
		{
			//cal_io_SLA_percent(i);
			cal_cpu_SLA(i);

			//gos_vm_list[i]->elapsed_time += elapsed_time;	// Real elapsed time
			gos_vm_list[i]->elapsed_time += INTERVAL;
#ifdef DEBUG
			printk(KERN_INFO "[VM%d - %ldms] now_SLA = %ld, now_quota = %ld, sla_type = %d\n", i, gos_vm_list[i]->elapsed_time, gos_vm_list[i]->now_SLA, gos_vm_list[i]->now_quota, gos_vm_list[i]->sla_type);
#endif
			// So small cpu usage need -1 quota value
			if(gos_vm_list[i]->now_SLA < 500 && gos_vm_list[i]->now_quota > -1)
			{
				int f_error = 0;
#ifdef DEBUG
				printk(KERN_INFO "[VM%d - %ldms | in 1] tmp_quota = %ld, cpu_usage = %ld\n", i, gos_vm_list[i]->elapsed_time, tmp_quota, gos_vm_list[i]->now_perf.cpu_usage);
#endif
				if(get_vm_quota(i) != -1)
				{
					f_error = set_vm_quota(i, -1);
					if(f_error)
						printk(KERN_INFO "[Error] Set VM[%d]'s quota in 'SLA < 100' (quota = %d)\n", i, -1);
				}

				gos_vm_list[i]->elapsed_time = 0;
				gos_vm_list[i]->now_quota = -1;
			}
			// Set quota value which related with proportional to current cpu usage if quota value is -1
			else if(gos_vm_list[i]->sla_type == c_usg && (gos_vm_list[i]->now_quota <= 0 || get_vm_quota(i) <= 0))
			{
				int f_error = 0;
				long tmp_period = get_vm_period(i);
				long tmp_quota = gos_vm_list[i]->now_perf.cpu_usage * tmp_period / 10000;
#ifdef DEBUG
				printk(KERN_INFO "[VM%d - %ldms | in 2] tmp_quota = %ld, cpu_usage = %ld\n", i, gos_vm_list[i]->elapsed_time, tmp_quota, gos_vm_list[i]->now_perf.cpu_usage);
#endif
				if(tmp_quota > tmp_period) tmp_quota = tmp_period;

				f_error = set_vm_quota(i, tmp_quota);
				if(f_error)
					printk(KERN_INFO "[Error] Set VM[%d]'s quota (quota = %ld)\n", i, tmp_quota);

				gos_vm_list[i]->prev_quota = gos_vm_list[i]->now_quota;
				gos_vm_list[i]->now_quota = tmp_quota;
			}
			else if(gos_vm_list[i]->sla_type != c_usg)
			{
#ifdef DEBUG
				printk(KERN_INFO "[VM%d - %ldms | in 3] tmp_quota = %ld, cpu_usage = %ld\n", i, gos_vm_list[i]->elapsed_time, tmp_quota, gos_vm_list[i]->now_perf.cpu_usage);
#endif
				io_vm_total_cpu_usage += (gos_vm_list[i]->now_perf.cpu_usage > 0)?gos_vm_list[i]->now_perf.cpu_usage:0;
			}
#ifdef DEBUG
			printk(KERN_INFO "[VM %d] cpu_usage = %ld, now_quota = %ld", i, gos_vm_list[i]->now_perf.cpu_usage, gos_vm_list[i]->now_quota);
#endif
		}
	}
#ifdef DEBUG
	printk(KERN_INFO "io_vm_total_cpu_usage = %ld", io_vm_total_cpu_usage);
#endif
	for(i = 0 ; i < VM_NUM ; i++)
	{
		if(gos_vm_list[i] != NULL)
		{
			if(gos_vm_list[i]->sla_type != c_usg)
			{
				if(gos_vm_list[i]->now_SLA < 500 || io_vm_total_cpu_usage <= 0)
					gos_vm_list[i]->now_quota = -1;
				else
					gos_vm_list[i]->now_quota = io_quota * gos_vm_list[i]->now_perf.cpu_usage / io_vm_total_cpu_usage;
			}

			// If prev value(quota, p_SLa) is not set, do this routine
			if(gos_vm_list[i]->prev_quota <= 0 || gos_vm_list[i]->prev_SLA <= 0)
			{
				gos_vm_list[i]->prev_quota = gos_vm_list[i]->now_quota;
				gos_vm_list[i]->prev_SLA = gos_vm_list[i]->now_SLA;
			}

			// Feedback Exception
			// Exception 1 : 99 <= SLA percentage <= 101
			else if(gos_vm_list[i]->now_SLA >= DISSATISFY && gos_vm_list[i]->now_SLA <= OVERSATISFY)
			{
				gos_vm_list[i]->prev_quota = gos_vm_list[i]->now_quota;
				gos_vm_list[i]->prev_SLA = gos_vm_list[i]->now_SLA;
			}

			// Feedback Routine for dissatisfying SLA
			else if(gos_vm_list[i]->now_SLA < DISSATISFY)
			{
				long diff_quota = (10000 - gos_vm_list[i]->now_SLA) * (gos_vm_list[i]->now_quota - gos_vm_list[i]->prev_quota);
#ifdef DEBUG
				printk(KERN_INFO "[VM%d-%ld] now_SLA = %ld, now_quota = %ld, prev_quota = %ld, diff_quota = %ld\n", i, gos_vm_list[i]->elapsed_time, gos_vm_list[i]->now_SLA, gos_vm_list[i]->now_quota, gos_vm_list[i]->prev_quota, diff_quota);
#endif
				if(gos_vm_list[i]->now_SLA == gos_vm_list[i]->prev_SLA)
					diff_quota = get_vm_period(i) / 100;
				else
					diff_quota /= (gos_vm_list[i]->now_SLA - gos_vm_list[i]->prev_SLA);

				if(diff_quota <= 0) diff_quota = get_vm_period(i) / 100;

				next_quota += diff_quota;

				f_vm[i] = -1;
				tmp_value[i] = diff_quota;
			}

			// Feedback routine for over-satisfying SLA
			else if(gos_vm_list[i]->now_SLA > OVERSATISFY)
			{
				f_vm[i] = 1;
				tmp_value[i] = gos_vm_list[i]->now_SLA - 10000; 
				now_ok_SLA += tmp_value[i];
				//over_total_quota += (gos_vm_list[i]->now_quota - (gos_vm_list[i]->sla_target.cpu_usage * get_vm_period(i) / 100));
				over_total_quota += (gos_vm_list[i]->now_SLA > 0)?((tmp_value[i] * gos_vm_list[i]->now_quota) / gos_vm_list[i]->now_SLA):0;
			}
		}
	}
#ifdef DEBUG
	printk(KERN_INFO "f_vm : %d %d\n", f_vm[0], f_vm[1]);
	printk(KERN_INFO "tmp_value : %ld %ld\n", tmp_value[0], tmp_value[1]);
	printk(KERN_INFO "next_quota = %ld, now_ok_SLA = %ld", next_quota, now_ok_SLA);
#endif
	io_quota = 0;
	real_quota = (next_quota > over_total_quota)?over_total_quota:next_quota;
	tmp_quota = real_quota;

	// Assign new quota to all VMs
#ifdef DEBUG
	printk(KERN_INFO "<---- After ---->");
#endif
	for(i = 0 ; i < VM_NUM ; i++)
	{
		if(gos_vm_list[i] != NULL)
		{
			if(io_quota == 0) io_quota = get_vm_period(i);
#ifdef DEBUG
			printk(KERN_INFO "[VM%d] (before) now_quota = %ld\n", i, gos_vm_list[i]->now_quota);
#endif
			if(now_ok_SLA <= 0 || next_quota <= 0)
			{
				gos_vm_list[i]->prev_quota = gos_vm_list[i]->now_quota;
				gos_vm_list[i]->prev_SLA = gos_vm_list[i]->now_SLA;
			}			

			else if(f_vm[i] == 1)
			{
				int f_error = 0;
				long new_quota = ((real_quota * tmp_value[i]) / now_ok_SLA);

				if(tmp_quota - new_quota < 0) new_quota = tmp_quota;
				else tmp_quota -= new_quota;

				new_quota = gos_vm_list[i]->now_quota - new_quota;

				if(new_quota <= 0) new_quota = -1;
				else if(new_quota > get_vm_period(i)) new_quota = get_vm_period(i);

				if(gos_vm_list[i]->sla_type == c_usg)
				{
					f_error = set_vm_quota(i, new_quota);
					if(f_error)
						printk(KERN_INFO "[ERROR] Set VM[%d]'s quota in assigning new quota (quota = %ld)\n", i, new_quota);
				}

				gos_vm_list[i]->prev_quota = gos_vm_list[i]->now_quota;
				gos_vm_list[i]->prev_SLA = gos_vm_list[i]->now_SLA;

				gos_vm_list[i]->now_quota = (new_quota < -1)?0:new_quota;
			}

			else if(f_vm[i] == -1)
			{
				int f_error = 0;
				long new_quota = gos_vm_list[i]->now_quota + ((real_quota * tmp_value[i]) / next_quota);

				if(new_quota <= 0) new_quota = -1;
				else if(new_quota > get_vm_period(i)) new_quota = get_vm_period(i);

				if(gos_vm_list[i]->sla_type == c_usg)
				{
					f_error = set_vm_quota(i, new_quota);
					if(f_error)
						printk(KERN_INFO "[ERROR] Set VM[%d]'s quota in assigning new quota (quota = %ld)\n", i, new_quota);
				}

				gos_vm_list[i]->prev_quota = gos_vm_list[i]->now_quota;
				gos_vm_list[i]->prev_SLA = gos_vm_list[i]->now_SLA;

				gos_vm_list[i]->now_quota = (new_quota < -1)?0:new_quota;
			}

			if(gos_vm_list[i]->sla_type == c_usg) io_quota -= (gos_vm_list[i]->now_quota > 0)?gos_vm_list[i]->now_quota:0;
#ifdef DEBUG
			printk(KERN_INFO "[VM%d] (after) now_quota = %ld\n", i, gos_vm_list[i]->now_quota);
			printk(KERN_INFO "[VM%d - %ldms] now_SLA = %ld, now_quota = %ld\n", i, gos_vm_list[i]->elapsed_time, gos_vm_list[i]->now_SLA, gos_vm_list[i]->now_quota);
#endif
		}
	}
#ifdef DEBUG
	printk(KERN_INFO "\n");
#endif
}

void gos_timer_callback(unsigned long data)
{
	unsigned long elapsed_time = jiffies_to_msecs(jiffies - prev_jiffies);
	prev_jiffies = jiffies;
#ifdef DEBUG
	int i = 0;
#endif

#ifdef DEBUG
	printk(KERN_INFO "gos_timer_callback called (%ld)\n", jiffies);
	printk(KERN_INFO "interval time = %ld", elapsed_time);	
#endif

	feedback_controller(elapsed_time);

#ifdef DEBUG
	for(i = 0 ; i < VM_NUM ; i++)
	{
		if(gos_vm_list[i] != NULL)
		{

			if(gos_vm_list[i]->sla_type == b_iops)
				printk(KERN_INFO "[VM%d] SLA Option = %s, SLA Value = %d, Perf Value = %d, SLA Percent = %d\n", i, gos_vm_list[i]->sla_option, gos_vm_list[i]->sla_target.iops, gos_vm_list[i]->now_perf.iops, gos_vm_list[i]->now_SLA);
			else if(gos_vm_list[i]->sla_type == b_bw)
				printk(KERN_INFO "[VM%d] SLA Option = %s, SLA Value = %d, Perf Value = %d, SLA Percent = %d\n", i, gos_vm_list[i]->sla_option, gos_vm_list[i]->sla_target.bandwidth, gos_vm_list[i]->now_perf.bandwidth, gos_vm_list[i]->now_SLA);
			else if(gos_vm_list[i]->sla_type == b_lat)
				printk(KERN_INFO "[VM%d] SLA Option = %s, SLA Value = %d, Perf Value = %d, SLA Percent = %d\n", i, gos_vm_list[i]->sla_option, gos_vm_list[i]->sla_target.latency, gos_vm_list[i]->now_perf.latency, gos_vm_list[i]->now_SLA);			
			else if(gos_vm_list[i]->sla_type == c_usg)
				printk(KERN_INFO "[VM%d] SLA Option = %s, SLA Value = %d, Perf Value = %d, SLA Percent = %d\n", i, gos_vm_list[i]->sla_option, gos_vm_list[i]->sla_target.cpu_usage, gos_vm_list[i]->now_perf.cpu_usage, gos_vm_list[i]->now_SLA);
		}
	}
#endif

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
				seq_printf(m, "%d\t%s\t\t%d\t\t%ld\t%s", i, sla_option, sla_value, gos_vm_list[i]->now_SLA, gos_vm_list[i]->dev_name);
			else
				seq_printf(m, "%d\t%s\t\t%d\t\t%ld\t\t%s", i, sla_option, sla_value, gos_vm_list[i]->now_SLA, gos_vm_list[i]->dev_name);

			if(gos_vm_list[i]->tg[j] != NULL)
			{
			/*	if(gos_vm_list[i]->sla_type == c_usg)
					seq_printf(m, "\t%ld\t%ld\n", get_vm_period(i), get_vm_quota(i));
				else
					seq_printf(m, "\t%ld\t%ld\n", get_vm_period(i), gos_vm_list[i]->now_quota);*/

				seq_printf(m, "\t%ld\t%ld\n", get_vm_period(i), gos_vm_list[i]->prev_quota);
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

	printk(KERN_INFO "Write information about target vm\n");

	tmp_buf = kzalloc(sizeof(char) * s, GFP_KERNEL);

	if(!tmp_buf)
	{
		printk(KERN_INFO "[Error] Can not allocate buffer\n");
		return s;
	}

	copy_from_user(tmp_buf, u, s);
	tmp_buf[s-1] = '\0';

	{
		char *tosep = tmp_buf, *tok = NULL;
		char *sep = " ";
		int i_parm = 0;

		tmp_vm_info = kzalloc(sizeof(struct gos_vm_info), GFP_KERNEL);

		tmp_vm_info->control_sched = io;

		while((tok = strsep(&tosep, sep)) != NULL)
		{
			int err = 0;
			long long tmp_l = 0;

			err = kstrtoll(tok, 10, &tmp_l);

			if(i_parm < VCPU_NUM)
			{
				struct pid *tmp_pid = find_get_pid(tmp_l);
				tmp_vm_info->ts[i_parm] = pid_task(tmp_pid, PIDTYPE_PID);

				if(tmp_vm_info->ts[i_parm] == NULL)
				{
					f_vm_off = 1;
					printk(KERN_INFO "[WARNING] VM(PID:%lld) is off\n", tmp_l);
					break;
				}

				tmp_vm_info->tg[i_parm] = tmp_vm_info->ts[i_parm]->sched_task_group;
#ifdef DEBUG
				printk(KERN_INFO "pid[%d] = %d", i_parm, task_pid_nr(tmp_vm_info->ts[i_parm]));
#endif
			}
			else if(i_parm  == VCPU_NUM)
			{
				char *tmp_tok = strsep(&tosep, sep);

				i_parm++;

				err = kstrtoll(tmp_tok, 10, &tmp_l);

				if(strcmp(tok, "b_bw") == 0)
				{
					tmp_vm_info->sla_target.bandwidth = tmp_l;
					tmp_vm_info->sla_type = b_bw;
				}
				else if(strcmp(tok, "b_iops") == 0)
				{
					tmp_vm_info->sla_target.iops = tmp_l;
					tmp_vm_info->sla_type = b_iops;
				}
				else if(strcmp(tok, "b_lat") == 0)
				{
					tmp_vm_info->sla_target.latency = tmp_l;
					tmp_vm_info->sla_type = b_lat;
				}
				else if(strcmp(tok, "c_usage") == 0)
				{
					tmp_vm_info->sla_target.cpu_usage = tmp_l;
					tmp_vm_info->sla_type = c_usg;

					tmp_vm_info->control_sched = cpu;
				}
				else if(strcmp(tok, "free") == 0)
				{
					f_free = 1;
					break;
				}
				else
					printk(KERN_INFO "[Error] Wrong Parameter\n");

				strcpy(tmp_vm_info->sla_option, tok);
#ifdef DEBUG
				printk(KERN_INFO "sla_option = %s\n", tmp_vm_info->sla_option);
#endif
			}
			else if(i_parm == (VCPU_NUM + 2))
			{
				strcpy(tmp_vm_info->dev_name, tok);

				if(strncmp(tmp_vm_info->dev_name, "/dev/nvme", 9) == 0)
					tmp_vm_info->control_sched = cpu;
#ifdef DEBUG
				printk(KERN_INFO "dev_name = %s\n", tmp_vm_info->dev_name); 
#endif
			}

			i_parm++;
		}

		if(f_vm_off == 1)
		{
			kfree(tmp_vm_info);
			tmp_vm_info = NULL;

			return s;
		}
	}

	for(index = 0 ; index < VM_NUM ; index++)
	{
		if(gos_vm_list[index] != NULL && task_pid_nr(tmp_vm_info->ts[0]) == task_pid_nr(gos_vm_list[index]->ts[0]))
		{
			printk(KERN_INFO "tmp = %d, gos[%d] = %d\n", task_pid_nr(tmp_vm_info->ts[0]), index, task_pid_nr(gos_vm_list[index]->ts[0]));

			kfree(gos_vm_list[index]);
			gos_vm_list[index] = NULL;

			if(f_free == 1)
			{
				kfree(tmp_vm_info);
				tmp_vm_info = NULL;

				printk(KERN_INFO "Free VM %d\n", index);
			}

			else
			{
				gos_vm_list[index] = tmp_vm_info;

				f_dup = 1;
			}

			break;
		}
		else if(gos_vm_list[index] == NULL)
		{
			if(f_free != 1 && f_dup != 1)
				gos_vm_list[index] = tmp_vm_info;
			else
				f_dup = 0;

			break;
		}

		else if(index == VM_NUM - 1 && gos_vm_list[index] != NULL)
			printk(KERN_INFO "[Error] Target VM is full\n");
	}

	kfree(tmp_buf);

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
