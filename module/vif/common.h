//#define pr_fmt(fmt) KBUILD_MODNAME ":%s: " fmt, __func__

#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/slab.h>
#include <linux/ip.h>
#include <linux/in.h>
#include <linux/io.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/wait.h>
#include <linux/sched.h>
#include <../../linux-4.4.41/drivers/vhost/vhost.h>

#ifndef ANCS
#define ANCS
#endif

//#ifndef CPU_CONTROL
#define CPU_CONTROL
//#endif

#define MAX_CREDIT 8000000	//kwlee
#define MIN_CREDIT 100000
#define MAX_NUMBER_VCPU	4
#define VCPU_IDX	7

#define NW_intensive 0
#define CPU_intensive 1

struct ancs_vm;

#define MAX(a,b) \  
({ __typeof__ (a) _a = (a); \  
__typeof__ (b) _b = (b); \  
_a > _b ? _a : _b; })

struct credit_allocator{
	struct list_head active_vif_list;
	spinlock_t active_vif_list_lock;

	struct timer_list account_timer;
	struct timer_list monitor_timer;

	unsigned int total_weight;
	unsigned int credit_balance;
	int num_vif;
};

void add_active_vif(struct ancs_vm *vif);
void remove_active_vif(struct ancs_vm *vif);
static void credit_accounting(unsigned long data);
int get_quota(struct ancs_vm *vif);
void set_vcpu_quota(struct ancs_vm *vif, int quota);
void set_vhost_quota(struct ancs_vm *vif, int quota);

