/*
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/genhd.h>
#include <linux/fs.h>
#include <linux/types.h>
#include <linux/ctype.h>
#include <linux/kdev_t.h>
#include <linux/mount.h>
*/

#define DISK_SOURCE
#include "gos.h"

#define GoS
//#define DEBUG

/*
struct io_SLA_percent { // 100.00% is 10000
	int p_IOPS;
	int p_Bandwidth;
	int p_Latency;
};
struct io_stat_wj {
	unsigned long s_IOPS; // req/s
	unsigned long s_Bandwidth; // MB/s
	unsigned long s_Latency; // ms
};
struct disk_stat_wj {
	unsigned long ios;
	unsigned long sectors;
	unsigned long ticks;
	unsigned long wait;
};*/

/* declaration - struct variable*/
//struct io_SLA_percent io_SLA_percent_wj;
		//static struct io_stat_wj now_io_stat;
static struct disk_stat prev_disk_stat[VM_NUM];
		//static struct disk_stat_wj now_disk_stat;
//EXPORT_SYMBOL(io_SLA_percent_wj);*/

/* declaration - function */
void get_disk_stat(char *, struct disk_stat *);
//extern struct block_device *bdget_disk(struct gendisk *, int);

/* declaration - basic variable */
static struct hd_struct *gos_hd_struct = NULL;
static struct gendisk *gos_gendisk = NULL;
dev_t gos_dev_t = -1;

void cal_io_SLA_percent(int vm_num, struct gos_vm_sla *curr_sla)
{
	unsigned long sector_size = 512; // temp_wj
//	struct vm_perf_wj now_io_stat; // 10000 is 100.00
	struct disk_stat now_disk_stat;
	unsigned long ios = 0, sectors = 0, ticks = 0, wait = 0;

	if (gos_vm_list[vm_num] == NULL) 
		return;

	if (strncmp(gos_vm_list[vm_num]->dev_name, "null", 4) == 0) {
		printk("disk stat: null device error\n");
		return;
	}		

	get_disk_stat(gos_vm_list[vm_num]->dev_name, &now_disk_stat); // now_disk_stat update
#ifdef DEBUG
	printk(KERN_INFO "disk stat : ios:%lu / sectors:%lu / ticks:%lums / wait:%lums\n", \
			now_disk_stat.ios, now_disk_stat.sectors, now_disk_stat.ticks, now_disk_stat.wait);
#endif

	if (prev_disk_stat[vm_num].ios == -1)
		prev_disk_stat[vm_num].ios = now_disk_stat.ios;
	if (prev_disk_stat[vm_num].sectors == -1)
		prev_disk_stat[vm_num].sectors = now_disk_stat.sectors;
	if (prev_disk_stat[vm_num].ticks == -1)
		prev_disk_stat[vm_num].ticks = now_disk_stat.ticks;
	if (prev_disk_stat[vm_num].wait == -1)
		prev_disk_stat[vm_num].wait = now_disk_stat.wait;

	ios = now_disk_stat.ios - prev_disk_stat[vm_num].ios;
	sectors = now_disk_stat.sectors - prev_disk_stat[vm_num].sectors;
	ticks = now_disk_stat.ticks - prev_disk_stat[vm_num].ticks;
	wait = now_disk_stat.wait - prev_disk_stat[vm_num].wait;

//	if (ios != 0)// && ticks != 0)
	gos_vm_list[vm_num]->now_perf.iops = ios * 100 / 3;//100 * ios * 1000 / ticks; // req/s * 100
//	if (sectors != 0)// && ticks != 0)
	gos_vm_list[vm_num]->now_perf.bandwidth = (sectors * sector_size * 100) / (3 * 1024);//(sectors * sector_size * 100) / (ticks*10); // MB/s * 100
//	if (wait != 0 && ios != 0)
	gos_vm_list[vm_num]->now_perf.latency = (ios <= 0) ? 0 : wait * 100 / ios;//100 * wait / ios; // ms * 100

	if (curr_sla->sla_type == b_iops) {
		//if (ios != 0 && ticks != 0){
		//	now_io_stat.iops = 100 * ios * 1000 / ticks; // req/s * 100
		curr_sla->prev_sla = curr_sla->now_sla;
		curr_sla->now_sla = gos_vm_list[vm_num]->now_perf.iops * 100 / curr_sla->sla_target.iops; // % * 100
#ifdef DEBUG
		printk(KERN_INFO "     IOPS : %lu(req/s * 100)\n", gos_vm_list[vm_num]->now_perf.iops);
#endif
		//}
	}
	else if (curr_sla->sla_type == b_bw) {
		//if (sectors != 0 && ticks != 0){
		//	now_io_stat.bandwidth = (sectors * sector_size) / (ticks*10); // MB/s * 100
		curr_sla->prev_sla = curr_sla->now_sla;
		curr_sla->now_sla = gos_vm_list[vm_num]->now_perf.bandwidth * 100 / curr_sla->sla_target.bandwidth;// % * 100
#ifdef DEBUG
		printk(KERN_INFO "Bandwidth : %lu(MB/s * 100)\n", gos_vm_list[vm_num]->now_perf.bandwidth);
#endif
		//}
	}
	else if (curr_sla->sla_type == b_lat) {
		//if (wait != 0 && ios != 0){
		//	now_io_stat.bandwidth = 100 * wait / ios; // ms * 100
		curr_sla->prev_sla = curr_sla->now_sla;
		curr_sla->now_sla = gos_vm_list[vm_num]->now_perf.latency * 100 / curr_sla->sla_target.latency; // % * 100
#ifdef DEBUG
		printk(KERN_INFO "  Latency : %lu(ms * 100)\n", gos_vm_list[vm_num]->now_perf.latency);
#endif
		//}
	}
	else {
#ifdef DEBUG
		printk(KERN_INFO "NOT IO VM (empty)\n");
#endif
	}

	prev_disk_stat[vm_num].ios = now_disk_stat.ios;
	prev_disk_stat[vm_num].sectors = now_disk_stat.sectors;
	prev_disk_stat[vm_num].ticks = now_disk_stat.ticks;
	prev_disk_stat[vm_num].wait = now_disk_stat.wait;
}
EXPORT_SYMBOL(cal_io_SLA_percent);

/*static char* diskname_wj (struct gendisk *hd, int part_no, char *buf)
{
	if (!part_no)
		snprintf (buf, BDEVNAME_SIZE, "%s", hd->disk_name);
	else if (isdigit(hd->disk_name[strlen(hd->disk_name)-1]))
		snprintf (buf, BDEVNAME_SIZE, "%sp%d", hd->disk_name, part_no);
	else
		snprintf (buf, BDEVNAME_SIZE, "%s%d", hd->disk_name, part_no);
	return buf;
}
*/
///////////////////////////////////////////////////////////////////////////////////
void get_disk_stat(char *dev_name, struct disk_stat *now_disk_stat)
{
	int part_no=0;
	//char disk_name[BDEVNAME_SIZE];
	gos_dev_t = name_to_dev_t (dev_name);
	gos_gendisk = get_gendisk(gos_dev_t, &part_no);
	gos_hd_struct = disk_get_part(gos_gendisk, part_no);
	//printk(KERN_INFO "block_size : %u", (bdget_disk(gendisk_wj, part_no))->bd_block_size);
/**/
	//printk(KERN_INFO "nr_sects : %d", gendisk_wj->part_tbl->len);
/**/

	now_disk_stat -> ios = part_stat_read(gos_hd_struct, ios[READ]) + part_stat_read(gos_hd_struct, ios[WRITE]);
	now_disk_stat -> sectors = part_stat_read(gos_hd_struct, sectors[READ]) + part_stat_read(gos_hd_struct, sectors[WRITE]);
	now_disk_stat -> ticks = jiffies_to_msecs(part_stat_read(gos_hd_struct, io_ticks));
	now_disk_stat -> wait = jiffies_to_msecs(part_stat_read( gos_hd_struct, time_in_queue));
}

/*void disk_stat_print_wj(char *dev_name_wj)
{
	int part_no;
	char disk_name_wj[BDEVNAME_SIZE];
	//char *dev_name_wj = "/dev/sda";

	dev_t_wj = name_to_dev_t (dev_name_wj);
	gendisk_wj = get_gendisk(dev_t_wj, &part_no);
	hd_struct_wj = disk_get_part(gendisk_wj, part_no);

	printk(KERN_INFO "--------------------");
	printk(KERN_INFO "%s %d\n", gendisk_wj->disk_name, part_no);
	printk(KERN_INFO "%4d %7d %s %lu %lu %lu %u %lu %lu %lu %u %u %u %u\n", 
			MAJOR(part_devt(hd_struct_wj)), 
			MINOR(part_devt(hd_struct_wj)), 
			diskname_wj(gendisk_wj, part_no, disk_name_wj), 
			part_stat_read(hd_struct_wj, ios[READ]),
			part_stat_read(hd_struct_wj, merges[READ]),
			part_stat_read(hd_struct_wj, sectors[READ]),
			jiffies_to_msecs(part_stat_read(hd_struct_wj, ticks[READ])),
			part_stat_read(hd_struct_wj, ios[WRITE]),
			part_stat_read(hd_struct_wj, merges[WRITE]),
			part_stat_read(hd_struct_wj, sectors[WRITE]),
			jiffies_to_msecs(part_stat_read(hd_struct_wj, ticks[WRITE])),
			part_in_flight(hd_struct_wj),
			jiffies_to_msecs(part_stat_read(hd_struct_wj, io_ticks)),
			jiffies_to_msecs(part_stat_read(hd_struct_wj, time_in_queue))
		);
}
EXPORT_SYMBOL(disk_stat_print_wj);
*/

static int __init simple_init(void)
{
	int i;
	printk(KERN_INFO "IO-SLA monitoring module start\n");
	//disk_stat_print_wj("/dev/sda");
	for (i=0; i<VM_NUM; i++){
		prev_disk_stat[i].ios = -1;
		prev_disk_stat[i].sectors = -1;
		prev_disk_stat[i].ticks = -1;
		prev_disk_stat[i].wait = -1;
	}
	return 0;
}

static void __exit simple_exit(void)
{
	printk(KERN_INFO "IO-SLA monitoring module exit\n");
}

module_init(simple_init);
module_exit(simple_exit);

MODULE_AUTHOR("wjlee");
MODULE_DESCRIPTION("Disk stat monitoring module");
MODULE_LICENSE("GPL");
MODULE_VERSION("NEW");
