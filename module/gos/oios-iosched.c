/*
 * elevator oios
 * 2013.03.03 Hyunchan, Park
 * Korea University, Seoul, Korea
 */
 
#include <linux/blkdev.h>
#include <linux/elevator.h>
#include <linux/bio.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/spinlock.h>
#include <linux/bitops.h>
#include <linux/pid.h>
#include <linux/sched.h>
#include <linux/jiffies.h>
#include <linux/hrtimer.h>
#include <linux/ktime.h>

#include <linux/proc_fs.h>
#include <linux/file.h>
#include <linux/fs.h>
#include <linux/fcntl.h>
#include <linux/kernel.h>
#include <linux/syscalls.h>
#include <linux/math64.h>

#define GOS_SOURCE
#define DISK_SOURCE
#define CPU_SOURCE
#include "gos.h"
//#define SSD_SHOWMSG 1		/* 1:show debug message 0: donot show*/
#define ON_KVM 1

#define PROC_NAME "oios_print_stat"

#define OIOS_HW_QUEUE_MIN (32)   //depth of SFQ(D)

#define NUM_VM 2

#define NUM_METRIC 4

#define METRIC_REQ 0		// # of requests in OIOS_WINDOW_SIZE_SECOND
#define METRIC_KBYTES 1		//Kbytes in OIOS_WINDOW_SIZE_SECOND
#define METRIC_LAT 2			//average latency during in OIOS_WINDOW_SIZE_SECOND
#define METRIC_UTIL 3		//utilizations in OIOS_WINDOW_SIZE_SECOND

//#define TARGET_METRIC METRIC_UTIL
#define TARGET_METRIC METRIC_KBYTES
//#define TARGET_METRIC METRIC_REQ

//#define TARGET_METRIC METRIC_LAT

#define RECIPROCAL_LAT (long)(100000000)

static long oios_limit[NUM_VM][NUM_METRIC];
static long oios_reserve[NUM_VM][NUM_METRIC];

/* For Postmark */
#define KBYTES (1024)		// 16K for five
#define FOUR_KB (4096)		// 16K for five

//static int point[2][12] = {{67,78,89,100,114,126,136,149,158,174,185},{91,109,128,147,171,187,208,238,251,274,301}};

#ifdef ON_KVM
//unsigned long partition[5][2] = {{4096, 20975615}, {20977664, 41949183}, {41951232, 62922751}, {62924800, 83896319}, {83898368, 104869887}};
unsigned long partition[5][2] = {{2048, 39065599}, {39065600, 78129151}, {1000100000, 1000200000}, {1000300000, 1000400000}, {1000500000, 1000600000}};
#else
unsigned long partition[5][2] = {{2048, 20973567}, {20973568, 41945087}, {41945088, 62916607}, {62918656, 83890175}, {83892224, 104863743}};
#endif

unsigned long swap_partition[5][2] = {{209733632,  213927935},{213929984,  218124287},{218126336,  222320639},{222322688,  226516991},{226519040,  230713343}};

//#define QOS_TEST
#define QOS_COMPENSATION

#ifdef QOS_TEST
static int prior[5]={100,200,100,100,100};		//  1 to 1
//static int prior[5]={100,100,100,100,200};		//  1 to 1

#else 

//static int prior[5]={100,100,100,100,100};		//  1 to 1
//static int prior[5]={100,125,150,175,200};		//  1 to 2
//static int prior[5]={100,150,200,250,300};		//  1 to 3
//static int prior[5]={100,200,300,400,500};		//  1 to 5
//static int prior[5]={100,300,500,700,900};		//  1 to 9
static int prior[5]={100, 200, 100, 100, 100};
//static int prior[5]={100, 100, 100, 100, 100};

#endif

//#define OIOS_WINDOW_SIZE_SECOND (5)
#define OIOS_WINDOW_SIZE_SECOND (3)
//#define OIOS_WINDOW_SIZE_SECOND (1)
//#define OIOS_WINDOW_SIZE_SECOND (5)

#define CALC_INTERVAL (OIOS_WINDOW_SIZE_SECOND*NSEC_PER_SEC)

//#define MAX_PAT_USEC 8000 // usec maximum


#define MAX_PAT_USEC 50000 // usec maximum
//#define MIN_ANTI_TIME (100*1000)	//less than 100 usec is meaningless

#define PAT_DELTA_MAX (1000)
//#define PAT_DELTA_MAX (5000)

#define PAT_DELTA_MAX_POS (PAT_DELTA_MAX) // 1000 usec. the largest change in one window
#define PAT_DELTA_MAX_NEG (-(PAT_DELTA_MAX)) // -1000 usec. the largest change in one window

#define DEF_PAT_USEC (1000)
#define MIN_PAT_USEC (100) // usec
#define IDLE_PAT_USEC (1000) // usec

static ktime_t anti_ktime[NUM_VM];

static atomic_t slot_cnt = ATOMIC_INIT(0);

#define DEF_NORMAL_JOB_PRIO 100
#define DEF_TQ (100000)

#define OIOS_VM_HAS_LIMIT 3
#define OIOS_VM_HAS_RESERVE 4
#define OIOS_VM_STRUCT_SIZE 5

static int vm[NUM_VM][OIOS_VM_STRUCT_SIZE]; // 0 = main pid, 1 = swap pid, 2 = queue_num, 3= has_requiremnt (0 or 1)

#define OIOS_MAX_QUEUE 6
#define DEF_SLOT (OIOS_MAX_QUEUE-1)

#define Q_PID 0
#define Q_PRIO 1
#define Q_ROUND_TQ 2
#define Q_REM_TQ 3
#define Q_METRIC 4

#define STATUS_LIST 5

static struct kmem_cache *ssd_tag_pool;

#define MAX_PROC_ENTRY (10000)
long proc_stats_in_one_window[OIOS_MAX_QUEUE][MAX_PROC_ENTRY][NUM_METRIC];
long proc_my_ktime[OIOS_MAX_QUEUE][MAX_PROC_ENTRY];
int proc_count;

static long all_sync_in_one_window[NUM_VM];
static long all_sync_in_total[NUM_VM];

static long all_in_one_window[NUM_VM][NUM_METRIC];
static long all_in_total[NUM_VM][NUM_METRIC];

int proc_called = 0;

struct ssd_req_tag {
	struct list_head my_entry;
	pid_t pid;
	int q_num;
	s64 arrival_time;
};
#define TAG0_OIOS(rq)		(struct ssd_req_tag *) ((rq)->elv.priv[0])
#define TAG1_OIOS(rq)		(struct ssd_req_tag *) ((rq)->elv.priv[1])

struct ssd_data {
/* Bundle Write 0: Normal(only for data block) Write Operation  */
/* Hurry 1: Priored IO */
	struct request_queue *q;

	struct list_head fifo_queue_sync;

	struct list_head sync_queue[OIOS_MAX_QUEUE];
	ktime_t pat_ktime[OIOS_MAX_QUEUE];
	ktime_t recent_arrival[OIOS_MAX_QUEUE];
	s64 anti_time_nsec_abs[OIOS_MAX_QUEUE];
	s64 virt_time_correct_nsec[OIOS_MAX_QUEUE];
	s64 next_expire_nsec_abs;
	s64 next_window_expire_nsec_abs;

	int was_active[OIOS_MAX_QUEUE];	//per window
	int was_active_in_prev_round[OIOS_MAX_QUEUE];	//per round	
	int is_over_used[OIOS_MAX_QUEUE];	//per round
	int is_in_pat[OIOS_MAX_QUEUE];
	
	//current activated	
	long queue_status[OIOS_MAX_QUEUE][STATUS_LIST]; //pid,prirority, num of requests, default TQ, remain TQ
	char names[OIOS_MAX_QUEUE][TASK_COMM_LEN];		

	struct hrtimer myOneSecTimer;
	
	ktime_t onesec_ktime;
	
	struct hrtimer PATTimer;

	struct work_struct myWork;	/* Deferred unplugging */	

	long def_quantum;
	int round_count;
	int is_round_all_consumed;
	s64 max_pat_in_round;
	int rounds_in_one_window;
	long avg_prior;
	int count_consume_tq;
};

asmlinkage inline void ssd_printk(const char *fmt, ...) {
	#ifdef SSD_SHOWMSG
	va_list args;
	
	va_start(args, fmt);
	printk(fmt, args);
	va_end(args);

	#endif
}

static void init_requirements(void) {
	int i,j;

	for(i=0; i<NUM_VM; i++) {
		for(j=0; j<NUM_METRIC; j++) {
			oios_reserve[i][j] = 0;
			oios_limit[i][j] = 0;
		}
	}

#ifdef QOS_TEST	
//	oios_limit[0][METRIC_KBYTES] = 25*KBYTES* OIOS_WINDOW_SIZE_SECOND;	//limits VM0's bandwidth by 20MB/s

//	oios_reserve[0][METRIC_KBYTES] = 40*KBYTES* OIOS_WINDOW_SIZE_SECOND;	//reserves VM0's bandwidth by 50MB/s
//	oios_limit[1][METRIC_REQ] = 300 * OIOS_WINDOW_SIZE_SECOND;	//limits VM0's bandwidth by 20MB/s
//	oios_reserve[2][METRIC_LAT] = (long)div_s64(RECIPROCAL_LAT, 5000);	//reserve latency: less than 5000 usec
//	oios_reserve[2][METRIC_REQ] = 1500*OIOS_WINDOW_SIZE_SECOND;	//reserves VM1's IOPS by 200 req/s	
	//oios_reserve[2][METRIC_LAT] = (long)div_s64(RECIPROCAL_LAT, 3000);	//reserve latency: less than 5000 usec

//	oios_reserve[0][METRIC_KBYTES] = 4096 * KBYTES * OIOS_WINDOW_SIZE_SECOND;
//	oios_reserve[1][METRIC_KBYTES] = 

	oios_reserve[0][METRIC_KBYTES] = 100 * KBYTES * OIOS_WINDOW_SIZE_SECOND;
//	oios_reserve[0][METRIC_KBYTES] = 200 * KBYTES * OIOS_WINDOW_SIZE_SECOND;
//	oios_reserve[0][METRIC_KBYTES] = 300 * KBYTES * OIOS_WINDOW_SIZE_SECOND;

//	oios_reserve[1][METRIC_KBYTES] = 100 * KBYTES * OIOS_WINDOW_SIZE_SECOND;
	oios_reserve[1][METRIC_KBYTES] = 200 * KBYTES * OIOS_WINDOW_SIZE_SECOND;
//	oios_reserve[1][METRIC_KBYTES] = 300 * KBYTES * OIOS_WINDOW_SIZE_SECOND;

//	oios_limit[0][METRIC_KBYTES] = 100 * KBYTES * OIOS_WINDOW_SIZE_SECOND;
//	oios_limit[0][METRIC_KBYTES] = 200 * KBYTES * OIOS_WINDOW_SIZE_SECOND;
//	oios_limit[0][METRIC_KBYTES] = 300 * KBYTES * OIOS_WINDOW_SIZE_SECOND;

//	oios_limit[1][METRIC_KBYTES] = 100 * KBYTES * OIOS_WINDOW_SIZE_SECOND;
//	oios_limit[1][METRIC_KBYTES] = 200 * KBYTES * OIOS_WINDOW_SIZE_SECOND;
//	oios_limit[1][METRIC_KBYTES] = 300 * KBYTES * OIOS_WINDOW_SIZE_SECOND;

	printk(KERN_CRIT "init_requirements() oios_reserve[0][METRIC_KBYTES] = %ld\n", oios_reserve[0][METRIC_KBYTES]);
	printk(KERN_CRIT "init_requirements() oios_reserve[1][METRIC_KBYTES] = %ld\n", oios_reserve[1][METRIC_KBYTES]);
/*
	printk(KERN_CRIT "init_requirements() oios_limit[0][METRIC_KBYTES] = %ld\n", oios_limit[0][METRIC_KBYTES]);
	printk(KERN_CRIT "init_requirements() oios_limit[1][METRIC_KBYTES] = %ld\n", oios_limit[1][METRIC_KBYTES]);
*//*
	printk(KERN_CRIT "init_requirements() oios_limit[0][METRIC_KBYTES] = %ld\n", oios_limit[0][METRIC_KBYTES]);
	printk(KERN_CRIT "init_requirements() oios_reserve[1][METRIC_KBYTES] = %ld\n", oios_reserve[1][METRIC_KBYTES]);
	printk(KERN_CRIT "init_requirements() oios_reserve[2][METRIC_REQ] = %ld\n", oios_reserve[2][METRIC_REQ]);
	printk(KERN_CRIT "init_requirements() oios_reserve[2][METRIC_LAT] = %ld\n", oios_reserve[2][METRIC_LAT]);
*/
#endif
}



/*
READ: 296 * (x) + 5400
WRITE: 525 * (x) + 6600
*/
static inline int calc_ssd_point(int kbytes, int is_write) {
	if(is_write <= 0) {
		return (296*kbytes+5400)/100;
	} else {
		return (525*kbytes+6600)/100;
	}
}

#define NUM_FLAGS 64

int flags[NUM_FLAGS];
uint32_t statFlag[NUM_FLAGS];
int lastFlagSlot;

static void initStatFlags(void) {
	int i;

	lastFlagSlot=0;
	
	for(i=0; i<NUM_FLAGS; i++) {
		flags[i] = 0xFFFFFFFF;
		statFlag[i] = 0;
	}
}

static void statFlags(int rw_flags) {
	int i;

	for(i=0; i<NUM_FLAGS; i++) {
		if(flags[i] == rw_flags) {statFlag[i]++; return;}		
	}

	if(lastFlagSlot == NUM_FLAGS) {
		ssd_printk(KERN_CRIT "PS-SSD statFlags  NUM_FLAGS OVERFLOW!!\n");
		return;
	}

	flags[lastFlagSlot] = rw_flags;
	statFlag[lastFlagSlot++] = 1;	
}

static void statFlagsPrint(void) {
	int i;
	
	printk(KERN_CRIT "\nPS-SSD statFlagsPrint # of flags = %d \n", lastFlagSlot);

	for(i=0; i<lastFlagSlot; i++) {
		//ssd_printk(KERN_CRIT "\t%03d Flag: 0x%x\tNUM: %09d\tIsHurry: %s\n", i, flags[i],  statFlag[i], (rq_is_hurry(flags[i])?"TRUE":"FALSE"));
		printk(KERN_CRIT "\t%02d Flag: 0x%x\tNUM: %08d\t\n", i, flags[i],  statFlag[i]);		
	}
	
	printk(KERN_CRIT "\n");	
	printk(KERN_CRIT "## PS-SSD Length of Dispatched Requests\n");
	
	printk(KERN_CRIT "\n");	
}

static void statQueuePrint(struct ssd_data *nd) {
	int i, last_slot = atomic_read(&slot_cnt);

	for(i=0; i<last_slot; i++) {
		printk(KERN_CRIT "\t%02d prio=%02ld name: %16s\n", i, nd->queue_status[i][Q_PRIO],  nd->names[i]);			
	}
}

static inline int rq_is_write(int rw_flags) {
        return (rw_flags & REQ_WRITE);  ///* not set, read. set, write */
}


static void write_pat_trace(void) {
	loff_t pos=0;
	char tmpString[64], str[256];
	struct file *filp;
	int i,j,q_num =0;

	filp = filp_open("/root/result/log_data_pat", O_CREAT | O_RDWR | O_TRUNC, 0666);
	
	if(IS_ERR(filp)) {
		printk("open error!\n");
		return;
	} else {
		ssd_printk(KERN_CRIT "open success!\n");
	}	
		
	for(i=0; i<proc_count; i++) {
		sprintf(str, "\0");
		for(j=0; j<NUM_VM; j++) {
			if(vm[j][2] >= 0) q_num = vm[j][2];
			else continue;
			
			sprintf(tmpString, "%08ld ",proc_my_ktime[q_num][i]);
			strcat(str, tmpString);
		}
		sprintf(tmpString, "\n");
		strcat(str, tmpString);

		vfs_write(filp, str, strlen(str), &pos);
	}

	filp_close(filp, NULL);
}

static void write_stats(void) {
	loff_t pos=0;
	char tmpString[64], str[256];
	struct file *filp[NUM_METRIC];
	int q_num, i,j,metric;

	//pre-processing
	for(i=0; i<proc_count; i++) {
		for(j=0; j<NUM_VM; j++) {
			proc_stats_in_one_window[j][i][METRIC_KBYTES] /= OIOS_WINDOW_SIZE_SECOND;
			proc_stats_in_one_window[j][i][METRIC_REQ] /= OIOS_WINDOW_SIZE_SECOND;
		}
	}

	filp[METRIC_REQ] = filp_open("/root/result/log_data_iops", O_CREAT | O_RDWR | O_TRUNC, 0666);
	filp[METRIC_KBYTES] = filp_open("/root/result/log_data_bandwidth", O_CREAT | O_RDWR | O_TRUNC, 0666);
	filp[METRIC_LAT] = filp_open("/root/result/log_data_latency", O_CREAT | O_RDWR | O_TRUNC, 0666);
	filp[METRIC_UTIL] = filp_open("/root/result/log_data_utilization", O_CREAT | O_RDWR | O_TRUNC, 0666);				

	for(i=0; i<NUM_METRIC; i++) {
		if(IS_ERR(filp)) {
			ssd_printk(KERN_CRIT "open error!METRIC = %d\n", i);
			return;
		}
		else {
			ssd_printk(KERN_CRIT "open success!METRIC = %d\n", i);
		}
	}
	
	for(metric=0; metric<NUM_METRIC; metric++) {
		pos=0;
		tmpString[0] = '\0';
		
		for(i=0; i<proc_count; i++) {
			sprintf(str, "\0");
			for(j=0; j<NUM_VM; j++) {
				if(vm[j][2] >= 0) q_num = vm[j][2];
				else continue;
				
				sprintf(tmpString, "%08ld ", proc_stats_in_one_window[q_num][i][metric]);
				//vfs_write(filp[metric], tmpString, strlen(tmpString), &pos);
				strcat(str, tmpString);
			}
			sprintf(tmpString, "\n");
			strcat(str, tmpString);
			vfs_write(filp[metric], str, strlen(str), &pos);
		}

		filp_close(filp[metric], NULL);
	}
}

static void oios_write_stats_proc(void)
{
	mm_segment_t old_fs = get_fs();

	set_fs(KERNEL_DS);
	write_pat_trace();
	write_stats();
	set_fs(old_fs);

	proc_called = 1;
}

//function for proc writing. not for read!
// This function declaration is for linux-3.9.0 (by hsko , 20170102)
//static int proc_read_profile(char *page, char **start, off_t off, int count, int *eof, void *data)
// This function declaration is for linux-4.4.0 (by hsko , 20170102)
static ssize_t proc_read_profile(struct file *a, char __user *b, size_t c, loff_t *d)
{
	//int len=0;	// linux-3.9.0 (by hsko , 20170102)
	ssize_t len = 0;	// linux-4.4.0 (by hsko , 20170102)
	oios_write_stats_proc();
	return len;
}

static int proc_init(void)
{
	int i,j,k;

	for (i=0; i<OIOS_MAX_QUEUE; i++) {
		for (j=0; j<MAX_PROC_ENTRY; j++) {
			for(k=0; k<NUM_METRIC; k++) {
				proc_stats_in_one_window[i][j][k]=0;
			}
			proc_my_ktime[i][j]=0;
		}
	}

	proc_called = 0;
	proc_count=0;
	return 0;
}

static void init_pat_stat(void) {
	int i,j;

	for(i=0; i<NUM_VM; i++) {
		all_sync_in_one_window[i] = 0;
		all_sync_in_total[i] = 0;
			
		for(j=0; j<NUM_METRIC; j++) {
			all_in_one_window[i][j] = 0;
			all_in_total[i][j] = 0;
		}
	}
}

static void print_pat_stat(struct ssd_data *nd) {
	int i, q_num;
	int last_slot = atomic_read(&slot_cnt);
	unsigned long total_util = 0;
	long latency = 0;

	//print stats of all VMs in a window
	printk(KERN_CRIT "\n# of VM\tUtilization\tBandwidth(KB/s)\tAvg. Latency(nsec)\tIOPS(req/s)\n");

	for(i=0; i<last_slot; i++) {
		total_util += all_in_total[i][METRIC_UTIL];
	}

	if(total_util == 0) return;
	
	for(i=0; i<last_slot; i++) {
		q_num = vm[i][2];	
		if(vm[i][2] < 0) continue;

		if(all_in_total[q_num][METRIC_REQ]/1000 == 0) latency = 0; // wj_by_170414 ( == 0  => /1000 == 0)
//		else latency = (long)div_s64(all_in_total[q_num][METRIC_LAT], (all_in_total[q_num][METRIC_REQ]/1000));
		else latency = all_in_total[q_num][METRIC_LAT]/(all_in_total[q_num][METRIC_REQ]/1000);		
		
		printk(KERN_CRIT "VM#%02d\t%04ld\t\t%ld\t%ld\t%ld\n", 
			i, all_in_total[q_num][METRIC_UTIL]*10000/total_util, all_in_total[q_num][METRIC_KBYTES], latency, all_in_total[q_num][METRIC_REQ]);
	}

	printk(KERN_CRIT "*****************************************************************\n");	
	

}

static inline void oios_reset_tq(struct ssd_data *nd) {
	int i, q_num;

	if(nd->is_round_all_consumed > 0) {
		//printk(KERN_CRIT "oios_reset_tq() nd->is_round_all_consumed %d nd->round_count %d\n", nd->is_round_all_consumed, nd->round_count);
		nd->count_consume_tq++;
			
		for(i=0; i<NUM_VM; i++) {
			q_num = vm[i][2];			
			if(q_num < 0) continue;

			//printk(KERN_CRIT "VM#%02d REM_TQ = %ld\tnd->is_active %d\n", i, nd->queue_status[q_num][Q_REM_TQ], nd->is_active[q_num]);

			nd->queue_status[q_num][Q_ROUND_TQ] = (nd->queue_status[q_num][Q_ROUND_TQ]*200)/100;
			nd->queue_status[q_num][Q_REM_TQ] = nd->queue_status[q_num][Q_ROUND_TQ]/nd->rounds_in_one_window;
		}
	} else {
		for(i=0; i<NUM_VM; i++) {
			nd->queue_status[i][Q_REM_TQ] += (nd->queue_status[i][Q_ROUND_TQ]/nd->rounds_in_one_window);
		}
	}
	
	nd->is_round_all_consumed = 0;
	nd->round_count++;
}

//feedback in one window is end, update total data and reset variables for new window

static inline void pat_update_stat_window(int q_num) {
	int i;

	all_sync_in_total[q_num] += all_sync_in_one_window[q_num];
	all_sync_in_one_window[q_num] = 0;

	if(all_in_one_window[q_num][METRIC_REQ] <= 0) {
		all_in_one_window[q_num][METRIC_LAT] = 0;
	}  else {
		all_in_one_window[q_num][METRIC_LAT] = all_in_one_window[q_num][METRIC_LAT]/all_in_one_window[q_num][METRIC_REQ];
	}

	for(i=0; i<NUM_METRIC; i++) {
		proc_stats_in_one_window[q_num][proc_count][i] = all_in_one_window[q_num][i];

		all_in_total[q_num][i] += all_in_one_window[q_num][i];
		all_in_one_window[q_num][i] = 0;
		all_sync_in_one_window[q_num] = 0;
	}
}

// PAT Feedback between WINDOWS is done in here. Main fuction
static void pat_update(struct ssd_data *nd) {
	int i, j, metric, q_num = 0, sr_avg = 0, active_vm = 0, need_revise_vm = 0, reserved[NUM_VM], temp, found, /*shrink_ratio, req_metric[NUM_METRIC]={0,0,0,0},*/ limited[NUM_VM], excluded[NUM_VM], reserve_lat[NUM_VM];
	long total_values = 0, avg_value =0, temp_latency, temp_reqs;
	long target_values[NUM_VM], candi_target_values[NUM_VM], delta_value=0, value_per_one_pat=0, /*min_delta, max_delta,*/ min_ktime_usec, max_ktime_usec, max_bound_of_pat, prev_pat, next_pat[NUM_VM], delta_pat[NUM_VM], candi_delta_pat[NUM_VM], candi_exclude_value[NUM_VM];
	long compensation;//, utils_per_value;
	long temp_in_one_window[NUM_VM][NUM_METRIC];

	long next_utils[NUM_VM], limit_next_utils[NUM_VM], reserve_next_utils[NUM_VM];
	char str[256], tmpString[64];
//	s64 min_virt_time_nsec = LLONG_MAX;

	nd->round_count = 0;
		
	for(i=0; i<NUM_VM; i++) {
		next_utils[i] = 0;
		limit_next_utils[i] = 0;
		reserve_next_utils[i] = 0;
		reserved[i] = 0;
		limited[i] = 0;
		target_values[i] = 0;
		candi_target_values[i] = 0;
		candi_delta_pat[i] =0;
		candi_exclude_value[i] = 0;
		delta_pat[i] = 0;
		excluded[i] =0;
		reserve_lat[i] = 0;
		
		//nd->is_in_pat[i] = 1;
		nd->is_over_used[i] = 0;
		nd->was_active_in_prev_round[i] = 0;
		nd->queue_status[i][Q_REM_TQ]=0;
			 
		q_num = vm[i][2];			
		if(q_num < 0) continue; // VM is power-off

		if(all_in_one_window[q_num][METRIC_REQ] <= 0) {
			nd->was_active[q_num] = 0;			
			nd->pat_ktime[q_num] = ktime_set(0, (unsigned long)(DEF_PAT_USEC*1000));
			nd->virt_time_correct_nsec[q_num] = 0;
			continue;
		}

		nd->was_active[q_num] = 1;
		nd->was_active_in_prev_round[q_num] = 1;
		
		active_vm++;
	}

	if(active_vm <= 1) {	// no competetion
		for(i=0; i<NUM_VM; i++) {
			nd->pat_ktime[i] = ktime_set(0, (unsigned long)(DEF_PAT_USEC*1000));
			nd->virt_time_correct_nsec[i] = 0;
			pat_update_stat_window(i);	//useless?
		}

		nd->max_pat_in_round = DEF_PAT_USEC*1000;

		return;
	}

	for(i=0; i<NUM_VM; i++) {
		for(metric=0; metric < NUM_METRIC; metric++) {
			temp_in_one_window[i][metric] = all_in_one_window[i][metric];
		}
	}
	
	for(i=0; i<NUM_VM; i++) {
		pat_update_stat_window(i);
	}

	/*
	for(i=0; i<NUM_VM; i++) {
		if(nd->was_active[i] > 0 && min_virt_time_nsec > nd->virt_time_correct_nsec[i]) {
			min_virt_time_nsec = nd->virt_time_correct_nsec[i];
		}
	}

	
	for(i=0; i<NUM_VM; i++) {		
		q_num = vm[i][2];
		if(q_num < 0) continue; // VM is power-off
		if(nd->was_active[q_num] == 0) continue;

		//nd->virt_time_correct_nsec[q_num] -= min_virt_time_nsec;
		printk(KERN_CRIT "pat_update()	VM#%02d nd->virt_time_correct_nsec = %lld\n", i, nd->virt_time_correct_nsec[q_num]);
	}
	*/
	
	//preprocessing to get average latency from all latencies
	for(i=0; i<NUM_VM; i++) {
		q_num = vm[i][2];
		if(q_num < 0) continue; // VM is power-off
		if(nd->was_active[q_num] == 0) continue;
		
		temp_latency = temp_in_one_window[q_num][METRIC_LAT];			
		temp_reqs = temp_in_one_window[q_num][METRIC_REQ];
			
		if(temp_reqs == 0) {
			temp_in_one_window[q_num][METRIC_LAT] = 0;
			
			//printk(KERN_CRIT "VM#%02d all_in_one_window[][METRIC_LAT] = %ld\n", i, temp_latency);
		} else {
			//temp_latency = all_in_one_window[q_num][METRIC_LAT]/all_in_one_window[q_num][METRIC_REQ];
			//temp_latency = (long)div_s64(temp_latency, temp_reqs);
			temp_latency = (long)temp_latency/temp_reqs;
			
			//printk(KERN_CRIT "VM#%02d all_in_one_window[][METRIC_LAT] = %ld temp_latency = %ld\n", i, temp_in_one_window[q_num][METRIC_LAT], temp_latency);
			temp_in_one_window[q_num][METRIC_LAT] = (long) RECIPROCAL_LAT/temp_latency;
			//temp_in_one_window[q_num][METRIC_LAT] = temp_latency;
		}
		
		total_values += temp_in_one_window[q_num][TARGET_METRIC];
	}

	//print stats of all VMs in a window
	printk(KERN_CRIT "\n*** STATS in one window:  active_vm %d total_values %ld\n", active_vm, total_values);
	printk(KERN_CRIT "VM###\tUtilization\tKBytes\t\tAvg.Ltnc(usec)\tIOPS(req/s)\n");
	
	for(i=0; i<NUM_VM; i++) {
		q_num = vm[i][2];			
		if(q_num < 0) continue; // VM is power-off

		if(temp_in_one_window[q_num][METRIC_LAT] <= 0) temp_latency = 0;
		//else temp_latency = (long) div_s64(RECIPROCAL_LAT, temp_in_one_window[q_num][METRIC_LAT]);
		else temp_latency = (long) RECIPROCAL_LAT/temp_in_one_window[q_num][METRIC_LAT];		
		
		printk(KERN_CRIT "VM#%02d\t%08ld\t%08ld\t\t%06ld\t\t%06ld\tSync: %ld\tLat: %ld\n", 
			i, temp_in_one_window[q_num][METRIC_UTIL], temp_in_one_window[q_num][METRIC_KBYTES], temp_in_one_window[q_num][METRIC_LAT], temp_in_one_window[q_num][METRIC_REQ], all_sync_in_one_window[q_num], temp_latency);
	}

	//printk(KERN_CRIT "*****************************************************************\n");	
	printk(KERN_CRIT "\n");

	//check for requirement and setup next PAT candidates
	for(i=0; i<NUM_VM; i++) {		
		q_num = vm[i][2];
		if(q_num < 0) continue;
		if(nd->was_active[q_num] == 0) continue;
		
		for(metric=0; metric < NUM_METRIC; metric++) {	
			//setup for limitation
			if(oios_limit[i][metric] > 0) {
#ifdef QOS_COMPENSATION
				if(oios_limit[i][metric]<temp_in_one_window[q_num][metric])
					compensation = temp_in_one_window[q_num][metric] - oios_limit[i][metric];
				else 
					compensation =0;
#else
				compensation =0;
#endif
				//compensation =0;

				target_values[q_num] = oios_limit[i][metric]-compensation; // goal of next window
				candi_target_values[q_num] = target_values[q_num];				
				
				prev_pat = (long) ktime_to_us(nd->pat_ktime[q_num]);
				if(prev_pat <= 0) prev_pat = 1;
				delta_value = target_values[q_num] - temp_in_one_window[q_num][metric];
				value_per_one_pat = (temp_in_one_window[q_num][metric]*1000/prev_pat);		///this '1000' will compenstated in below line
				
				if(value_per_one_pat == 0) {
					candi_delta_pat[q_num] = 0;
				} else {
					candi_delta_pat[q_num] = 1000*delta_value/value_per_one_pat;	//here <-this '1000' will compenstated in below line 
				}

				/*
				if(candi_delta_pat[q_num] >= 0) 
					candi_delta_pat[q_num] = candi_delta_pat[q_num] /2;
				*/
				
				candi_exclude_value[q_num] = candi_delta_pat[q_num]*temp_in_one_window[q_num][TARGET_METRIC]/prev_pat + temp_in_one_window[q_num][TARGET_METRIC];

				need_revise_vm++;
				limited[q_num] = 1;
			}

			//setup for reservation
			if(oios_reserve[i][metric] > 0) {
#ifdef QOS_COMPENSATION
				if(oios_reserve[i][metric]>temp_in_one_window[q_num][metric])
					compensation = oios_reserve[i][metric]-temp_in_one_window[q_num][metric];
				else 
					compensation =0;
#else
				compensation =0;
#endif

				target_values[q_num] = oios_reserve[i][metric]+compensation; // goal of next window
				candi_target_values[q_num] = target_values[q_num];

				prev_pat = (long) ktime_to_us(nd->pat_ktime[q_num]);
				if(prev_pat <= 0) prev_pat = 1;
				delta_value = target_values[q_num] - temp_in_one_window[q_num][metric];
				value_per_one_pat = (temp_in_one_window[q_num][metric]*1000/prev_pat);		///this '1000' will compenstated in below line
				
				if(value_per_one_pat == 0) {
					candi_delta_pat[q_num] = 0;
				} else {
					candi_delta_pat[q_num] = 1000*delta_value/value_per_one_pat;	//here <-this '1000' will compenstated in below line 
				}

				candi_exclude_value[q_num] = candi_delta_pat[q_num]*temp_in_one_window[q_num][TARGET_METRIC]/prev_pat + temp_in_one_window[q_num][TARGET_METRIC];
				
				need_revise_vm++;
				reserved[q_num] = 1;
			}
		}		
	}

	//proportional allocation for remaining utilization
	// 1st. get PAT values for active VMs
	temp = active_vm;
	sr_avg = 0;

	for(i=0; i<need_revise_vm+1; i++) {
		//select the target VMs for proportional sharing
		sr_avg = 0;
		for(j=0; j<NUM_VM; j++) {
			q_num = vm[j][2];
			if(q_num < 0) continue;
			if(nd->was_active[q_num] == 0 || excluded[q_num] == 1) continue;

			sr_avg += nd->queue_status[q_num][Q_PRIO];
		}

		avg_value =  total_values/temp;
		sr_avg = sr_avg /temp;	// divide by 0 check: temp is always higher than 1

		nd->def_quantum = avg_value;
		//printk(KERN_CRIT "pat_update_sec() REVISE: need_revise_vm %d temp %d total_values %ld avg_value %ld sr_avg %d\n", need_revise_vm, temp, total_values, avg_value, sr_avg);

		found = 0;
		//we assume target queues are sorted by increasing order for priority (lowest first)
		for(j=0; j<NUM_VM; j++) {
			q_num = vm[j][2];
			if(q_num < 0) continue;
			if(nd->was_active[q_num] == 0 || excluded[q_num] == 1) continue;

			if(sr_avg <= 0) {
				target_values[q_num] = avg_value; // goal of next window
				//nd->queue_status[q_num][Q_ROUND_TQ] = target_values[q_num];
				printk(KERN_CRIT "pat_update_sec() Never HAPPEN!!!\n");
			} else {
				target_values[q_num] = nd->queue_status[q_num][Q_PRIO]*1000/sr_avg * avg_value / 1000; // goal of next window
				//nd->queue_status[q_num][Q_ROUND_TQ] = target_values[q_num];				
			}
			
			prev_pat = (long) ktime_to_us(nd->pat_ktime[q_num]);
			if(prev_pat <= 0) prev_pat = 1;
			delta_value = target_values[q_num] - temp_in_one_window[q_num][TARGET_METRIC];
			value_per_one_pat = (temp_in_one_window[q_num][TARGET_METRIC]*1000/prev_pat);		///this '1000' will compenstated in below line
			
			if(value_per_one_pat == 0) {
				delta_pat[q_num] = 0;
			} else {
				delta_pat[q_num] = 1000*delta_value/value_per_one_pat; //here <-this '1000' will compenstated in below line 
			}

			// after calucalation, the target util is exceed the limited util
			if(limited[q_num] > 0 && delta_pat[q_num] > candi_delta_pat[q_num]) {
				printk(KERN_CRIT "pat_update_sec() LIMIT: VM%02d delta_pat %ld candi_delta_pat %ld candi_exclude_value %ld need_revise_vm %d\n", j, delta_pat[q_num], candi_delta_pat[q_num], candi_exclude_value[q_num], need_revise_vm);
				delta_pat[q_num] = candi_delta_pat[q_num];
				//nd->queue_status[q_num][Q_ROUND_TQ] = candi_target_values[q_num];				
				//total_values -= temp_in_one_window[q_num][TARGET_METRIC];
				total_values -= candi_exclude_value[q_num];
				excluded[q_num] = 1;
				found = 1;

				if(temp != 1) temp--;

				break;
			}
			//if(reserve_lat[q_num]) delta_pat[q_num] = -(delta_pat[q_num]);
			if(reserved[q_num] > 0 && delta_pat[q_num] < candi_delta_pat[q_num]) {
				printk(KERN_CRIT "pat_update_sec() RSERV: VM%02d delta_pat %ld candi_delta_pat %ld candi_exclude_value %ld need_revise_vm %d\n", 
					j, delta_pat[q_num], candi_delta_pat[q_num], candi_exclude_value[q_num], need_revise_vm);
				delta_pat[q_num] = candi_delta_pat[q_num];				
				//nd->queue_status[q_num][Q_ROUND_TQ] = candi_target_values[q_num];	
				//total_values -= temp_in_one_window[q_num][TARGET_METRIC];
				total_values -= candi_exclude_value[q_num];
				excluded[q_num] = 1;
				found = 1;

				if(temp != 1) temp--;

				break;
			}			
		}

		if(found == 0) break;

	}
	
	// 2nd refine delta PATs (shrinking)
	max_bound_of_pat = MAX_PAT_USEC * active_vm;
	min_ktime_usec = max_bound_of_pat;
	max_ktime_usec = 0;
	
	for(i=0; i<NUM_VM; i++) {
		q_num = vm[i][2];
		if(q_num < 0) continue;
		if(nd->was_active[q_num] == 0) continue;

		if(delta_pat[q_num] > PAT_DELTA_MAX_POS) {
			delta_pat[q_num] = PAT_DELTA_MAX_POS;
		} else if(delta_pat[q_num] < PAT_DELTA_MAX_NEG) {
			delta_pat[q_num] = PAT_DELTA_MAX_NEG;
		}

		next_pat[q_num] = delta_pat[q_num] + (long) ktime_to_us(nd->pat_ktime[q_num]);		

		if(next_pat[q_num] > max_bound_of_pat) next_pat[q_num] = max_bound_of_pat;
		
		if(min_ktime_usec > next_pat[q_num]) min_ktime_usec = next_pat[q_num];	
		if(max_ktime_usec < next_pat[q_num]) max_ktime_usec = next_pat[q_num];
	}


	min_ktime_usec -= MIN_PAT_USEC;
	max_ktime_usec -= min_ktime_usec;

	//min_ktime_usec = 0;
	printk(KERN_CRIT "Alignment is on. min_ktime_usec %ld\n", min_ktime_usec);


	for(i=0; i<NUM_VM; i++) {
		if(nd->was_active[i] == 0) continue;

		next_pat[i] -= min_ktime_usec;
		//if(next_pat[i] < MIN_PAT_USEC) next_pat[i] = MIN_PAT_USEC;
		if(next_pat[i] > max_bound_of_pat) {
			next_pat[i] = max_bound_of_pat;
			max_ktime_usec = max_bound_of_pat;
		}
		
		nd->pat_ktime[i] = ktime_set(0, (unsigned long)((next_pat[i])*1000));
	}
	
	nd->max_pat_in_round = max_ktime_usec*1000;
	//nd->rounds_in_one_window = (OIOS_WINDOW_SIZE_SECOND*1000*1000)/max_ktime_usec + 1;	//usec divides usec

	/*
	if(nd->max_pat_in_round < (s64)(MIN_PAT_USEC*1000)) {
		nd->max_pat_in_round = (s64)(MIN_PAT_USEC*1000);
		nd->rounds_in_one_window = (OIOS_WINDOW_SIZE_SECOND*1000*1000)/MIN_PAT_USEC + 1;
	}
	*/
	// print out PATs	
	sprintf(str, "\nPATs:  ");
	
	for(i=0; i<NUM_VM; i++) {		
		if(vm[i][2] < 0) 
			sprintf(tmpString, "NONE ");
		else
			sprintf(tmpString, "%lld ", ktime_to_us(nd->pat_ktime[vm[i][2]]));
				
		strcat(str, tmpString);
	}
	
	printk(KERN_CRIT "%s\n", str);

	// print out Quantums
	/*
	

	if(nd->rounds_in_one_window <= 0) 
		nd->rounds_in_one_window = 1;

	nd->is_round_all_consumed = 0;
	

	sprintf(str, "\nTQs:  ");
	
	for(i=0; i<NUM_VM; i++) {		
		if(vm[i][2] < 0) 
			sprintf(tmpString, "NONE ");
		else
			sprintf(tmpString, "%09ld ", nd->queue_status[vm[i][2]][Q_ROUND_TQ]);
				
		strcat(str, tmpString);
	}

	sprintf(tmpString, "max_pat %lld ", nd->max_pat_in_round);
	strcat(str, tmpString);
	sprintf(tmpString, "rounds %d", nd->rounds_in_one_window);
	strcat(str, tmpString);
	
	printk(KERN_CRIT "%s\n", str);
	
	for(i=0; i<NUM_VM; i++) {
		nd->queue_status[i][Q_REM_TQ] = nd->queue_status[i][Q_ROUND_TQ];
	}
	*/
}

static void ssd_ktime_setup(void) {
	int i, avg_prior=0;
	
	for(i=0; i< NUM_VM; i++) {
		avg_prior += prior[i];
	}

	avg_prior /= NUM_VM;

	printk(KERN_CRIT "ssd_ktime_setup() avg_prior = %d DEF_PAT_USEC = %d\n", avg_prior, DEF_PAT_USEC);

	avg_prior = 0; 	//temp

	if(avg_prior == 0) {
		for(i=0; i< NUM_VM; i++) {
			anti_ktime[i] = ktime_set(0,  DEF_PAT_USEC*1000);	// * 1000 for Nsec to Usec;
			}
	} else {
		for(i=0; i< NUM_VM; i++) {
			anti_ktime[i] = ktime_set(0,  (s64) (prior[i]*1000*DEF_PAT_USEC/avg_prior));	// * 1000 for Nsec to Usec;

			printk(KERN_CRIT "ssd_ktime_setup() anti_ktime[%d] = %lld\n", i, ktime_to_us(anti_ktime[i]));
		}
	}
}

/* return 1 if empty*/
static int ssd_queue_empty(struct request_queue *q)
{
	struct ssd_data *nd = q->elevator->elevator_data;		

	return list_empty(&nd->fifo_queue_sync);
}

static void my_setup(struct ssd_data *nd, struct task_struct *tsk, int q_num, sector_t sector) {
	if(sector >= partition[0][0] && sector <= partition[0][1]) {  nd->queue_status[q_num][Q_PRIO] = prior[0]; vm[0][0]=tsk->pid; vm[0][2]=q_num; nd->pat_ktime[q_num] = anti_ktime[0];}
	else if(sector >= partition[1][0] && sector <= partition[1][1]) { nd->queue_status[q_num][Q_PRIO] = prior[1]; vm[1][0]=tsk->pid; vm[1][2]=q_num; nd->pat_ktime[q_num] = anti_ktime[1];}
	else if(sector >= partition[2][0] && sector <= partition[2][1]) { nd->queue_status[q_num][Q_PRIO] = prior[2]; vm[2][0]=tsk->pid; vm[2][2]=q_num; nd->pat_ktime[q_num] = anti_ktime[2];}
	else if(sector >= partition[3][0] && sector <= partition[3][1]) { nd->queue_status[q_num][Q_PRIO] = prior[3]; vm[3][0]=tsk->pid; vm[3][2]=q_num; nd->pat_ktime[q_num] = anti_ktime[3];}
	else if(sector >= partition[4][0] && sector <= partition[4][1]) { nd->queue_status[q_num][Q_PRIO] = prior[4]; vm[4][0]=tsk->pid; vm[4][2]=q_num; nd->pat_ktime[q_num] = anti_ktime[4];}

	//swap 
	else if(sector >= swap_partition[0][0] && sector <= swap_partition[0][1]) { nd->queue_status[q_num][Q_PRIO] = prior[0]; vm[0][0]=tsk->pid; vm[0][2]=q_num; nd->pat_ktime[q_num] = anti_ktime[0];}
	else if(sector >= swap_partition[1][0] && sector <= swap_partition[1][1]) { nd->queue_status[q_num][Q_PRIO] = prior[1]; vm[1][0]=tsk->pid; vm[1][2]=q_num; nd->pat_ktime[q_num] = anti_ktime[1]; }
	else if(sector >= swap_partition[2][0] && sector <= swap_partition[2][1]) { nd->queue_status[q_num][Q_PRIO] = prior[2]; vm[2][0]=tsk->pid; vm[2][2]=q_num; nd->pat_ktime[q_num] = anti_ktime[2];}
	else if(sector >= swap_partition[3][0] && sector <= swap_partition[3][1]) { nd->queue_status[q_num][Q_PRIO] = prior[3]; vm[3][0]=tsk->pid; vm[3][2]=q_num; nd->pat_ktime[q_num] = anti_ktime[3];}
	else if(sector >= swap_partition[4][0] && sector <= swap_partition[4][1]) { nd->queue_status[q_num][Q_PRIO] = prior[4]; vm[4][0]=tsk->pid; vm[4][2]=q_num; nd->pat_ktime[q_num] = anti_ktime[4];}

	else return;	

	printk(KERN_CRIT "find_alloc_my_queue() Prior = %ld for %s\n", nd->queue_status[q_num][Q_PRIO], tsk->comm);
}


static int find_alloc_my_queue(pid_t pid, struct ssd_data *nd, sector_t sector) {
	int /*i,*/ last_slot;
	struct task_struct *tsk;
	
	last_slot = atomic_read(&slot_cnt);

	// 1. find
	tsk = pid_task(find_get_pid(pid), PIDTYPE_PID);
	if (!tsk) {
		ssd_printk(KERN_CRIT "ps-ssd find_alloc_my_queue() No TSK!!\n");		
		return -1;
	}

	if(sector >= partition[0][0] && sector <= partition[0][1]) { if(vm[0][2] >=0) return vm[0][2];}
	else if(sector >= partition[1][0] && sector <= partition[1][1]) { if(vm[1][2] >=0) return vm[1][2];}
	else if(sector >= partition[2][0] && sector <= partition[2][1]) { if(vm[2][2] >=0) return vm[2][2];}
	else if(sector >= partition[3][0] && sector <= partition[3][1]) { if(vm[3][2] >=0) return vm[3][2];}
	else if(sector >= partition[4][0] && sector <= partition[4][1]) { if(vm[4][2] >=0) return vm[4][2];}
	//swap
	else if(sector >= swap_partition[0][0] && sector <= swap_partition[0][1]) { if(vm[0][2] >=0) return vm[0][2];}
	else if(sector >= swap_partition[1][0] && sector <= swap_partition[1][1]) { if(vm[1][2] >=0) return vm[1][2];}
	else if(sector >= swap_partition[2][0] && sector <= swap_partition[2][1]) { if(vm[2][2] >=0) return vm[2][2];}
	else if(sector >= swap_partition[3][0] && sector <= swap_partition[3][1]) { if(vm[3][2] >=0) return vm[3][2];}
	else if(sector >= swap_partition[4][0] && sector <= swap_partition[4][1]) { if(vm[4][2] >=0) return vm[4][2];}
	else { return DEF_SLOT;}

	// 2. alloc	sync and async queue together
	//   no more slots
	if(last_slot == DEF_SLOT) {
		ssd_printk(KERN_CRIT "ps-ssd find_alloc_my_queue() No more slots for New Process!!\n");
		return DEF_SLOT;
	}

	//  new slot with default options	
	last_slot = atomic_read(&slot_cnt);
	atomic_inc(&slot_cnt);

	nd->queue_status[last_slot][Q_PID]=pid;
		
	strncpy(nd->names[last_slot], tsk->comm, TASK_COMM_LEN);
	printk(KERN_CRIT "oios find_alloc_my_queue() New Process: %d, %s last_slot = %d\n", pid, nd->names[last_slot], last_slot);

	my_setup(nd, tsk, last_slot, sector);

	return last_slot;
}

static int count =0;

static enum hrtimer_restart pat_timeout_handler(struct hrtimer *hrtimer) {	
	struct ssd_data *nd = container_of(hrtimer, struct ssd_data, PATTimer);
	struct request_queue *q = nd->q;
	//unsigned long flags;
	enum hrtimer_restart ret = HRTIMER_NORESTART;	
	s64 curr_time, anti_time_nsec;
	int i, q_num, temp;
	//struct request *rq;
	//struct ssd_req_tag *tag;
	//s64 virt_time_correct_nsec_temp;

	//spin_lock_irqsave(q->queue_lock, flags);

	curr_time = ktime_to_ns(ktime_get());

	//Window check
	if(curr_time > nd->next_window_expire_nsec_abs) {
		//spin_unlock_irqrestore(q->queue_lock, flags);	
		nd->next_window_expire_nsec_abs += CALC_INTERVAL;
				
		for(i=0; i<NUM_VM; i++) {
			q_num = vm[i][2];
			if(q_num < 0) continue; // VM is power-off
			
			proc_my_ktime[q_num][proc_count] = ktime_to_us(nd->pat_ktime[q_num]);
//			printk(KERN_INFO "vm_num : %d\n", i);
			cal_io_SLA_percent(i);
//			printk(KERN_INFO "vm_num : %d\n", i);
		}
/* wj_start */
//		printk(KERN_CRIT "----OIOStimer----\n");
//		cal_io_SLA_percent(0);
//		printk(KERN_CRIT "----OIOStimer----\n");
//		cal_io_SLA_percent(1);
//		printk(KERN_CRIT "----OIOStimer----\n");
		//printk(KERN_CRIT "----/dev/sdb6----\n");
		//io_SLA_percent_by_collector_2 = calc_io_SLA_percent("/dev/sdb6", request_SLA_2);
/* wj_end */
		pat_update(nd);

		if(proc_count < MAX_PROC_ENTRY -1)
			proc_count++;

		nd->pat_ktime[DEF_SLOT] = ktime_set(0,nd->max_pat_in_round);		
		nd->anti_time_nsec_abs[DEF_SLOT] = curr_time + nd->max_pat_in_round;
		nd->is_in_pat[DEF_SLOT] = 1;

		//spin_lock_irqsave(q->queue_lock, flags);
	}
	curr_time = ktime_to_ns(ktime_get());
	nd->next_expire_nsec_abs = nd->max_pat_in_round+curr_time;
	hrtimer_start(&nd->PATTimer, ns_to_ktime(nd->next_expire_nsec_abs), HRTIMER_MODE_ABS);
	//hrtimer_start(&nd->PATTimer, ns_to_ktime(nd->next_expire_nsec_abs), HRTIMER_MODE_ABS);

	//oios_reset_tq(nd);
	
	for(i=0; i<NUM_VM; i++) {
		q_num = vm[i][2];
		if(q_num < 0) continue; // VM is power-off

		temp = 0;
		
		anti_time_nsec = ktime_to_ns(nd->pat_ktime[q_num]);
		nd->anti_time_nsec_abs[q_num] = curr_time + anti_time_nsec;
		//virt_time_correct_nsec_temp = nd->max_pat_in_round - anti_time_nsec;

		//nd->virt_time_correct_nsec[q_num] += virt_time_correct_nsec_temp;

		/*
		if(!list_empty(&nd->sync_queue[q_num]) && virt_time_correct_nsec_temp > 0 && nd->was_active_in_prev_round[q_num] == 1) {
			list_for_each_entry(rq, &nd->sync_queue[q_num], queuelist) {
				//if(rq == NULL) break;
				tag = TAG0_OIOS(rq);
				tag->arrival_virt_time += virt_time_correct_nsec_temp;
				temp++;
			}

			nd->virt_time_correct_nsec[q_num] += virt_time_correct_nsec_temp;
		}
		*/
		//nd->virt_time_correct_nsec[q_num] = 0;

		nd->was_active_in_prev_round[q_num] = 0;
		
		if(nd->is_in_pat[q_num] == 1) 
			nd->virt_time_correct_nsec[q_num] = 0;		

		nd->is_in_pat[q_num] = 1;

		if(count%200 == 0) {
			ssd_printk(KERN_CRIT "pat_timeout_handler()	VM#%02d nd->virt_time_correct_nsec = %lld\n", i, nd->virt_time_correct_nsec[q_num]);
		}
	}

	if(count++ %1000 == 0) {
		ssd_printk(KERN_CRIT "pat_timeout_handler()	EXPIRE!! curr_time = %lld max_pat_in_round = %lld\n", curr_time, nd->max_pat_in_round);
	}
		
	if(!ssd_queue_empty(q)) {
		//kblockd_schedule_work(q, &nd->myWork);
		kblockd_schedule_work(&nd->myWork);	// request queue variable is not used in v4.4 (by wjlee, 20170104)
	}
	
	//spin_unlock_irqrestore(q->queue_lock, flags);	

	

	//hrtimer_start(&nd->PATTimer, ns_to_ktime(nd->max_pat_in_round), HRTIMER_MODE_REL);

	return ret;
}

static void my_work_handler(struct work_struct *work) {
	struct ssd_data *nd = container_of(work, struct ssd_data, myWork);
	struct request_queue *q = nd->q;
	
	spin_lock_irq(q->queue_lock);
	__blk_run_queue(q);
	spin_unlock_irq(q->queue_lock);
}

static inline int  oios_dispatch_add_tail(struct ssd_data *nd, struct request_queue *q, struct request *rq) {
	struct ssd_req_tag *tag;
	int q_num;

	if(rq->__data_len <= 0) {
		return 0;
	}

	tag = TAG0_OIOS(rq);

	q_num = tag->q_num;
	all_in_one_window[q_num][METRIC_UTIL] += calc_ssd_point(rq->__data_len/KBYTES, rq_is_write(rq->cmd_flags));	
	all_in_one_window[q_num][METRIC_KBYTES] += rq->__data_len/KBYTES;
	all_in_one_window[q_num][METRIC_REQ]++;

//	switch(nd->queue_status[q_num][Q_METRIC]) {

/*
	switch(TARGET_METRIC) {
		case METRIC_UTIL:
			nd->queue_status[q_num][Q_REM_TQ] -= calc_ssd_point(rq->__data_len/KBYTES, rq_is_write(rq->cmd_flags));
			break;
			
		case METRIC_KBYTES:
			nd->queue_status[q_num][Q_REM_TQ] -= rq->__data_len/KBYTES;
			break;
			
		case METRIC_REQ:			
			nd->queue_status[q_num][Q_REM_TQ]--;
			break;

		default:
			break;			
	}
*/
	//spin_lock(q->queue_lock);
	list_del_init(&rq->queuelist);	//remove from SYNC queue
	list_del_init(&tag->my_entry);		//remove from FIFO queue
	//spin_unlock(q->queue_lock);
	
	elv_dispatch_add_tail(q, rq);

	return 1; 
}

//dispatch all requests in queues
// never called in my exps
static int oios_forced_dispatch(struct request_queue *q) {
	int dispatched=0;
	struct ssd_data *nd = q->elevator->elevator_data;
	struct request *rq;
	struct ssd_req_tag *tag;
	int i;

	// 1st. Hurry Queue
	while(!list_empty(&nd->fifo_queue_sync)) {		
		for(i=0; i<OIOS_MAX_QUEUE; i++) {
			while(!list_empty(&nd->sync_queue[i])) {
				rq = list_entry(nd->sync_queue[i].next, struct request, queuelist);

				tag = TAG0_OIOS(rq);

				if(tag != NULL) {
					list_del_init(&rq->queuelist);	//remove from SYNC queue
					list_del_init(&tag->my_entry);	//remove from FIFO queue	
					
					kmem_cache_free(ssd_tag_pool, tag);
				} else {
					printk(KERN_CRIT "ssd_completed_request   (tag == NULL)\n");
				}

				elv_dispatch_add_tail(q, rq);

				dispatched ++;
			}
		}
	}
		
	printk(KERN_CRIT "oios_forced_dispatch() exit  cdispatched = %d\n", dispatched);

	return dispatched;
}

int order = 0;

static int oios_dispatch(struct request_queue *q, int force) {	
	struct ssd_data *nd = q->elevator->elevator_data;
	struct request *rq;
	struct ssd_req_tag *tag;
	int i, q_num=0;//, temp;
	//struct list_head *curr_q;

	int dispatched=0;
	
	if (unlikely(force))
		return oios_forced_dispatch(q);

	if(order > 0) order = 0;
	else order = 1; 	

	/*
	temp = 1;
	
	for(i=0; i<OIOS_MAX_QUEUE; i++) {
		if(nd->was_active_in_prev_round[i] == 1 && nd->queue_status[i][Q_REM_TQ] > 0) temp = 0;
	}

	nd->is_round_all_consumed = temp;
	*/
	
	//for all sync queues
	for(i=0; i<OIOS_MAX_QUEUE; i++) {
		if(order > 0) q_num = OIOS_MAX_QUEUE -1 - i;
		else q_num = i;

		if(nd->is_in_pat[q_num] <= 0) continue;		
		
		while(!list_empty(&nd->sync_queue[q_num])) {	//several requests can be processed		
			/*
			if(nd->queue_status[q_num][Q_REM_TQ] <= 0 && nd->is_round_all_consumed != 1) 
				break;
			*/
			
			rq = list_entry(nd->sync_queue[q_num].next, struct request, queuelist);

			tag = TAG0_OIOS(rq);
			
			//if((ktime_to_ns(tag->arrival_time)) > (nd->anti_time_nsec_abs[q_num])) {
			if(tag->arrival_time > nd->anti_time_nsec_abs[q_num]) {
//			if((tag->arrival_time + nd->virt_time_correct_nsec[q_num]) > nd->anti_time_nsec_abs[q_num]) {
				nd->is_in_pat[q_num] = 0;

				//nd->virt_time_correct_nsec[q_num] = nd->max_pat_in_round - ktime_to_ns(nd->pat_ktime[q_num]);
				/*
				if(nd->next_expire_nsec_abs < tag->arrival_time) {
					//printk(KERN_CRIT "oios_dispatch() WEIRD!! diff = %llu     nd->next_expire_nsec_abs %llu < tag->arrival_virt_time %llu\n", (tag->arrival_virt_time -nd->next_expire_nsec_abs), nd->next_expire_nsec_abs, tag->arrival_virt_time);

					printk(KERN_CRIT "oios_dispatch() WEIRD!! diff = %llu\n", (tag->arrival_virt_time -nd->next_expire_nsec_abs));
					//nd->virt_time_correct_nsec[q_num] = nd->max_pat_in_round - ktime_to_ns(nd->pat_ktime[q_num]);
				} else {
					//nd->virt_time_correct_nsec[q_num] = (nd->next_expire_nsec_abs - tag->arrival_virt_time);
					//nd->virt_time_correct_nsec[q_num] += (nd->max_pat_in_round - ktime_to_ns(nd->pat_ktime[q_num]));
					//nd->virt_time_correct_nsec[q_num] = (nd->next_expire_nsec_abs - tag->arrival_time);
					nd->virt_time_correct_nsec[q_num] = nd->max_pat_in_round - ktime_to_ns(nd->pat_ktime[q_num]);
					//if(nd->virt_time_correct_nsec[q_num] < 0) nd->virt_time_correct_nsec[q_num] = 0;
				}
				*/
				break;
			}
			
			dispatched += oios_dispatch_add_tail(nd, q, rq);
			nd->was_active_in_prev_round[q_num] = 1;
		}
	}

	ssd_printk(KERN_CRIT "oios_dispatch() exit  curr_queue = %d  dispatched = %d\n", q_num, dispatched);
	
	return dispatched;
}

static void ssd_completed_request(struct request_queue *q, struct request *rq) {
	//struct ssd_data *nd = q->elevator->elevator_data;
	struct ssd_req_tag *tag;
	long latency;
	
	tag = TAG0_OIOS(rq);

	if(tag != NULL) {
		//latency = (long) (ktime_to_us(ktime_sub(ktime_get(), ns_to_ktime(tag->arrival_time))));
		latency = (long) (ktime_to_ns(ktime_get()) - tag->arrival_time)/1000; // nsec to usec
		all_in_one_window[tag->q_num][METRIC_LAT] += latency;
		all_sync_in_one_window[tag->q_num]++;
	}
	/*
	if(!ssd_queue_empty(q)) {
		kblockd_schedule_work(q, &nd->myWork);
	}
	*/
	
	rq->elv.priv[0] = NULL;
	
	if(tag != NULL) {
		kmem_cache_free(ssd_tag_pool, tag);
	} else {
		printk(KERN_CRIT "ssd_completed_request   (tag == NULL)\n");
	}
}

static void ssd_add_request_three(struct request_queue *q, struct request *rq) {
	struct ssd_data *nd = q->elevator->elevator_data;
	struct ssd_req_tag *tag;
	int  q_num=0;

	statFlags(rq->cmd_flags);

	q_num = find_alloc_my_queue(current->pid, nd, rq->bio->bi_iter.bi_sector);	// Add bi_iter for linux v4.4 (by wjlee, 20170103)

	tag = (struct ssd_req_tag *) kmem_cache_alloc(ssd_tag_pool, GFP_ATOMIC | __GFP_ZERO);

	tag->pid = current->pid;
	tag->q_num = q_num;	
	tag->arrival_time = ktime_to_ns(ktime_get());
	//tag->arrival_virt_time = tag->arrival_time + nd->virt_time_correct_nsec[q_num];

	//tag->arrival_virt_time = tag->arrival_time;

	/*
	if(list_empty(&nd->sync_queue[q_num])) {
		printk(KERN_CRIT "ssd_add_request_three() q_num = %d nd->virt_time_correct_nsec  = %lld\n", q_num, nd->virt_time_correct_nsec[q_num]);
		nd->virt_time_correct_nsec[q_num] = 0;
	}
	*/
	
	//tag->arrival_time = ktime_to_ns(ktime_get());

	INIT_LIST_HEAD(&tag->my_entry);

	rq->elv.priv[0] = (void *)tag;

	/*
	if(nd->was_active_in_prev_round[q_num] == 0) {
		//nd->queue_status[q_num][Q_ROUND_TQ] = nd->def_quantum;
		nd->was_active_in_prev_round[q_num] = 1;
	}
	*/
	//spin_lock(q->queue_lock);
	list_add_tail(&tag->my_entry, &nd->fifo_queue_sync);		//Insert the rq into fifo_queue_sync
	list_add_tail(&rq->queuelist, &nd->sync_queue[q_num]);		//Insert the rq into task's sync_queue
	//spin_unlock(q->queue_lock);


}

//static int ssd_init_queue(struct request_queue *q) {	// linux v3.9 (20170105)
static int ssd_init_queue(struct request_queue *q, struct elevator_type *new_e) {	// linux v4.4(20170105)
	struct ssd_data *nd;
	int i,j;
	int err;
	
	ssd_printk(KERN_CRIT "ssd_init_queue() CALLED\n");	

	// Add for linux v4.4 to allocate elevator (by hsko, 20170105)
	err = -ENOMEM;
	q->elevator = elevator_alloc(q, new_e);
	if(!q->elevator) return err;

	nd = kmalloc_node(sizeof(struct ssd_data), GFP_KERNEL | __GFP_ZERO, q->node);
	if (!nd) {
		kobject_put(&q->elevator->kobj);	// Add for linux v4.4 (by hsko, 20170105)

		return -ENOMEM;
	}

	nd->q = q;
	q->elevator->elevator_data = nd;

	INIT_LIST_HEAD(&nd->fifo_queue_sync);
	
	for(i=0; i < OIOS_MAX_QUEUE; i++) {
		INIT_LIST_HEAD(&nd->sync_queue[i]);
		
		nd->queue_status[i][Q_PID]= -1;
		nd->queue_status[i][Q_PRIO] = -1;
		nd->queue_status[i][Q_ROUND_TQ] = -1;
		nd->queue_status[i][Q_REM_TQ] = -1;

		nd->pat_ktime[i] = ktime_set(0, (unsigned long) DEF_PAT_USEC*1000);
		nd->anti_time_nsec_abs[i] = 0;
		nd->virt_time_correct_nsec[i] = 0;
		nd->was_active[i] = 0;
		nd->was_active_in_prev_round[i] = 0;
		nd->is_over_used[i] = 0;
		nd->is_in_pat[i] = 0;
	}

	nd->max_pat_in_round = DEF_PAT_USEC*1000;
	nd->rounds_in_one_window = OIOS_WINDOW_SIZE_SECOND*1000*1000/DEF_PAT_USEC + 1;
	nd->avg_prior = 100;
	nd->next_expire_nsec_abs = ktime_to_ns(ktime_get()) + nd->max_pat_in_round;
	nd->next_window_expire_nsec_abs = nd->next_expire_nsec_abs + CALC_INTERVAL;
		
	for(i=0; i<NUM_VM; i++) {
		for(j=0; j<OIOS_VM_STRUCT_SIZE; j++) {		
			vm[i][j] = -1;
		}
	}

#ifdef ON_KVM
	nd->queue_status[DEF_SLOT][Q_PID]=0;
	nd->queue_status[DEF_SLOT][Q_PRIO] = DEF_NORMAL_JOB_PRIO;		
	//nd->queue_status[DEF_SLOT][Q_ROUND_TQ]=nd->queue_status[DEF_SLOT][Q_PRIO]*nd->def_quantum/100;
	//nd->queue_status[DEF_SLOT][Q_REM_TQ]=nd->queue_status[DEF_SLOT][Q_ROUND_TQ]/nd->rounds_in_one_window;
#endif
	
	ssd_ktime_setup();

	/*
	if (ssd_tag_pool == NULL)
		return -1;		
	*/
	nd->q = q;

	hrtimer_init(&nd->PATTimer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
	nd->PATTimer.function = &pat_timeout_handler;
	//hrtimer_start(&nd->PATTimer, ktime_set(0,(unsigned long) DEF_PAT_USEC*1000), HRTIMER_MODE_REL);
	hrtimer_start(&nd->PATTimer, ktime_set(0,nd->next_expire_nsec_abs), HRTIMER_MODE_ABS);

	/*
	hrtimer_init(&nd->myOneSecTimer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
	nd->myOneSecTimer.function = &my_hr_onesec_timer_handler;
	hrtimer_start(&nd->myOneSecTimer, ktime_set(0,NSEC_PER_SEC), HRTIMER_MODE_REL);
	*/
	
	nd->onesec_ktime = ktime_add_ns(ktime_get(), CALC_INTERVAL);	

	nd->def_quantum = DEF_TQ;
	nd->is_round_all_consumed = 0;
	nd->round_count = 0;
	nd->count_consume_tq = 0; 
		
	INIT_WORK(&nd->myWork, my_work_handler);

	atomic_set(&slot_cnt, 0);
	initStatFlags();
	init_requirements();
	init_pat_stat();

	proc_init();

	return 0;
}

static void ssd_exit_queue(struct elevator_queue *e) {
	struct ssd_data *nd = e->elevator_data;
	struct request_queue *q = nd->q;

	printk(KERN_CRIT "\n  ssd_exit_queue() count_consume_tq = %d\n", nd->count_consume_tq);

	spin_lock_irq(q->queue_lock);

	hrtimer_cancel(&nd->PATTimer);
	//hrtimer_cancel(&nd->myOneSecTimer);

	cancel_work_sync(&nd->myWork);	

	if(!list_empty(&nd->fifo_queue_sync)) {	
		oios_forced_dispatch(nd->q);
	}

	if(proc_called == 0)
		oios_write_stats_proc();	
		
	spin_unlock_irq(q->queue_lock);

	statFlagsPrint();

	statQueuePrint(nd);

	print_pat_stat(nd);

	BUG_ON(!list_empty(&nd->fifo_queue_sync));

	kfree(nd);
}

static struct elevator_type elevator_oios = {
        .ops = {
                .elevator_dispatch_fn           = oios_dispatch,
                .elevator_add_req_fn            = ssd_add_request_three,
                .elevator_completed_req_fn = ssd_completed_request,
                .elevator_init_fn               = ssd_init_queue,
                .elevator_exit_fn               = ssd_exit_queue,
        },
        .elevator_name = "oios",
        .elevator_owner = THIS_MODULE,
};

static int __init oios_init(void) {
	// This function is for linux-3.9.0 (by hsko, 20170102)
	//create_proc_read_entry(PROC_NAME, 0444, NULL, proc_read_profile, NULL)

	// This function is alternative function to create_proc_read_entry (by hsko, 20170102)
	static const struct file_operations oios_proc_fops = {
		.read 	= proc_read_profile,
	};
	proc_create_data(PROC_NAME, 0444, NULL, &oios_proc_fops, NULL);
		
	ssd_tag_pool = KMEM_CACHE(ssd_req_tag, 0);
	
	return elv_register(&elevator_oios);
}

static void __exit oios_exit(void) {
	elv_unregister(&elevator_oios);
	
	kmem_cache_destroy(ssd_tag_pool);

	remove_proc_entry(PROC_NAME, NULL);
}

module_init(oios_init);
module_exit(oios_exit);

MODULE_AUTHOR("Hyunchan, Park");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Hyunchan, Park Opportunistic IO scheduler");
