/*
   Include Area
*/

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>

#if defined(GOS_SOURCE) || defined(CPU_SOURCE)

#include <linux/sched.h>
#include <linux/list.h>
#include <linux/pid.h>
#include <linux/proc_fs.h>
#include <linux/stat.h>
#include <linux/seq_file.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/timer.h>
#include <asm/uaccess.h>

#endif

#ifdef DISK_SOURCE

#include <linux/genhd.h>
#include <linux/fs.h>
#include <linux/types.h>
#include <linux/ctype.h>
#include <linux/kdev_t.h>
#include <linux/mount.h>

#endif

/*
   Define Area
*/

#define VCPU_NUM 4
#define VM_NUM 2

enum scheduler {none, cpu, io};
enum sla {b_bw, b_iops, b_lat, c_usg};

/*
   Structure Area
*/

struct vm_perf {
	unsigned long iops;		// req/s
	unsigned long bandwidth;	// MB/s
	unsigned long latency;		// ms
	unsigned long cpu_usage;	// %
};

struct disk_stat {
	unsigned long ios;
	unsigned long sectors;
	unsigned long ticks;
	unsigned long wait;
};

struct gos_vm_info {
	struct task_struct *ts[VCPU_NUM];
	struct task_group *tg[VCPU_NUM];

	char dev_name[20];
	char sla_option[10];

	// Target SLA value
	struct vm_perf sla_target;

	// Status value now
	struct disk_stat now_io_stat;
	unsigned long now_cpu_time;

	// Performance value now
	struct vm_perf now_perf;

	// Prev stat feedback controller
	long prev_SLA;	// 100.00% == 10000
	long prev_quota;

	// Now stat for feedback controller
	long now_SLA;	// 100.00 % == 10000
	long now_quota;

	unsigned long elapsed_time;

	// SLA category number (b_bw: block bandwidth , b_iops: block iops , b_lat: block latency , c_usg: cpu usage)
	enum sla sla_type;

	// Target scheduler to control VM's resource
	enum scheduler control_sched;
};

/*
   Extern Area
*/

extern void cal_io_SLA_percent(int vm_num);
extern void cal_cpu_SLA(int vm_num);

extern long tg_get_cfs_quota(struct task_group *tg);
extern long tg_get_cfs_period(struct task_group *tg);
extern int tg_set_cfs_period(struct task_group *tg, long cfs_period_us);
extern int tg_set_cfs_quota(struct task_group *tg, long cfs_quota_us);

extern struct gos_vm_info *gos_vm_list[VM_NUM];
extern const unsigned long gos_interval;
