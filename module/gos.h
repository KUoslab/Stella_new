/*
   Include Area
*/

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/cpumask.h>
#include <linux/kernel_stat.h>
#include <linux/tick.h>
#include <linux/time.h>

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
<<<<<<< HEAD

#define VCPU_NUM 2
<<<<<<< HEAD
=======
/*
#define VCPU_NUM 4
*/
#define VCPU_NUM 4
>>>>>>> 4955a39570570feb6f1f5ec46642a89702d47cd7
#define VM_NUM 10 
=======
#define VM_NUM 2
>>>>>>> 1cb5d62b1fe80fe5f57e398f0f952dadfcdde73e
#define SYS_CPU_UTIL_THRESHOLD 8500 /* 8500 = 85.00% */
#define WORK_CONSERVING -1
#define SLA_GOAL 10000
#define EXIT_CPU_UTIL 50 /* 0.5% */
#define PERIOD 100000	/* us, 100ms */
#define SYS_CPU_MAX_UTIL 9800 /* 98.00% */
#define INC_DEC_SPEED 4

enum gos_type {cpu, ssd, network};
enum sla {b_bw, b_iops, b_lat, c_usg, n_mincredit, n_maxcredit, n_weight};

/*
   Structure Area
*/

struct vm_perf {
	unsigned long iops;		// req/s
	unsigned long bandwidth;	// MB/s
	unsigned long latency;		// ms
	unsigned long cpu_usage;	// %
	unsigned long credit;		// pps
	unsigned long weight;
};

struct disk_stat {
	unsigned long ios;
	unsigned long sectors;
	unsigned long ticks;
	unsigned long wait;
};

struct gos_vm_sla {
	/*
	 * SLA info
	 * SLA : 100.00% = 10000
	 */
	unsigned long prev_sla;
	unsigned long now_sla;
	char sla_option[10];
	struct vm_perf sla_target;
	enum sla sla_type;

	/* quota info */
	long prev_quota;
	long now_quota;

	/* Target type to control VM's resource */
	enum gos_type control_type;

	/* for vm cpu uage */
	unsigned long prev_cpu_time;
	unsigned long now_cpu_time;

	/* sla list */
	struct list_head sla_list;
};


struct gos_vm_info {
	/* VM info */
	char vm_name[20];

	/* sla list head */
	struct list_head sla_list;

	/* task_struct */
	struct task_struct *vcpu[VCPU_NUM];
	struct task_struct *vhost;
	struct task_struct *iothread;

	/*
	 * status value used by other modules which measure
	 * performance stats.
	 */
	char dev_name[20];
	struct disk_stat now_io_stat;
	struct vm_perf now_perf;
	void *priv_data;
	
};

/*
   Extern Area
*/

extern int add_network_sla(struct gos_vm_info *tmp_vm_info, struct gos_vm_sla *tmp_vm_sla, long long vhost_pid);

extern void cal_io_SLA_percent(int vm_num, struct gos_vm_sla *curr_sla);
extern void cal_cpu_SLA(int vm_num);

extern long tg_get_cfs_quota(struct task_group *tg);
extern long tg_get_cfs_period(struct task_group *tg);
extern int tg_set_cfs_period(struct task_group *tg, long cfs_period_us);
extern int tg_set_cfs_quota(struct task_group *tg, long cfs_quota_us);

extern struct gos_vm_info *gos_vm_list[VM_NUM];
extern const unsigned long gos_interval;
