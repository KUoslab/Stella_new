#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/proc_fs.h>
#include <linux/list.h>
#include <linux/string.h>
#include<linux/kvm_host.h>
#include<linux/sched.h>
#include<../kernel/sched/sched.h>
#include "common.h"
struct proc_dir_vif{
	char name[10];
	int id;
	struct proc_dir_entry *dir;
	struct proc_dir_entry *file[10];
};

extern struct list_head ancs_proc_list;

static struct proc_dir_entry *proc_root_dir;
static struct proc_dir_vif proc_vif[20];
static int idx;
int ancs =0;
int fileread = 0;

struct credit_allocator *credit_allocator;

#ifdef CPU_CONTROL
static struct work_struct ires_work;
void ires_work_func(void * data)
{
	printk("[WORK QUEUE] Enter workqueue function\n");  
}
static DECLARE_WORK(ires_work, ires_work_func);
static int calculate_vcpu_quota(int before)
{
	int after;
	
	if(before < 0)
		after = 70000;
	else
		after = before - 30000;
	
	if(after <= 0 && before > MIN_QUOTA)
		after = before - 1000;
	else	
		after = MIN_QUOTA;

	return after;
}
static void vcpu_control(struct ancs_vm *vif, unsigned long goal, unsigned long perf)
{
	struct ancs_vm *temp_vif, *next_vif;
	int before, after;

	if(vif==NULL)
		goto error;
	
	if(goal < perf){
		before = get_vcpu_quota(vif);
		after = calculate_vcpu_quota(before);
		set_vcpu_quota(vif, after);
		vif->vcpu_control = false;
		return;		
	}
#if 0	
	else{
		if(list_empty(&credit_allocator->victim_vif_list))
			goto error;

		list_for_each_entry_safe(temp_vif, next_vif, &credit_allocator->victim_vif_list, victim_list){
			if(temp_vif==NULL)
				goto error;
			
			before = get_vcpu_quota(temp_vif);
			after = calculate_vcpu_quota(before);
			set_vcpu_quota(vif, after);
			}
		vif->vcpu_control = false;
		}
	return;
#endif	
error:
	printk("ancs: vcpu control failed\n");
	return;
			
}

static void quota_control(unsigned long data){
	struct ancs_vm *temp_vif, *next_vif;
	unsigned long perf, goal;
	unsigned long now_sla;
	int i;
#ifdef PRO_SHARE
	unsigned long total_credit;
	int total_weight;
#endif
	int before, after, diff, dat;
	int cpu = smp_processor_id();
	WARN_ON(cpu != data);	

	if(list_empty(&credit_allocator->active_vif_list))
		goto out;

#ifdef PRO_SHARE
	if(credit_allocator->total_credit != 0){
		total_credit = credit_allocator->total_credit;
		credit_allocator->total_credit = 0;
		}
	total_weight = credit_allocator->total_weight;
#endif

#ifdef HYBRID
	if(total_credit < credit_allocator->quota_balance)
		total_credit=0;
	else
		total_credit-=credit_allocator->quota_balance;
	
	credit_allocator->quota_balance = 0;
#endif

	list_for_each_entry_safe(temp_vif, next_vif, &credit_allocator->active_vif_list, active_list){
		if(!temp_vif)
			goto out;

#if defined(HYBRID)
		credit_allocator->total_credit += temp_vif->pps;
		credit_allocator->quota_balance += temp_vif->max_credit;
		
		if(total_credit==0)
			goal = temp_vif->max_credit;
		else
			goal = temp_vif->max_credit + (((total_credit* temp_vif->weight) + (total_weight-1) )/ total_weight);

#elif !defined(PRO_SHARE)&&defined(MIN_RESERV)
		goal = temp_vif->max_credit;

#elif !defined(MIN_RESERV)&&defined(PRO_SHARE)
		credit_allocator->total_credit += temp_vif->pps;
		goal = ((total_credit * temp_vif->weight) + (total_weight-1) )/ total_weight;
#endif

#ifdef BW_CONTROL
		perf = temp_vif->used_credit;
#elif defined(PPS_CONTROL)
		perf = temp_vif->pps;
#endif
		for (i = 0; i < VM_NUM; i++) {
			if (gos_vm_list[i]->control_type == network &&
			    gos_vm_list[i]->vhost->pid == temp_vif->vhost->pid) {
				gos_vm_list[i]->prev_sla = gos_vm_list[i]->now_sla;
				gos_vm_list[i]->now_sla = perf * 10000 / goal;
				break;
			}
		}		

	skip:
#ifdef BW_CONTRL
		temp_vif->stat.nw_usage = temp_vif->used_credit;
		temp_vif->used_credit = 0;
#else /* PPS_CONTROL */
		temp_vif->stat.nw_usage = temp_vif->pps;
		temp_vif->pps = 0;
#endif
		}

	out:
	mod_timer(&credit_allocator->quota_timer, jiffies + msecs_to_jiffies(1000));
	return;

}
int get_vcpu_quota(struct ancs_vm *vif)
{
	struct task_struct *vcpu=vif->vcpu[0];
	int quota;

	if(vcpu==NULL)
		return 0;
	
	quota=tg_get_cfs_quota(vcpu->sched_task_group);
	
	//printk("ancs: quota of vhost is %d\n", quota);
	return quota;
}

int get_vhost_quota(struct ancs_vm *vif)
{
	struct task_struct *vhost=vif->vhost;
	int quota;
	
	quota=tg_get_cfs_quota(vhost->sched_task_group);
	
	//printk("ancs: quota of vhost is %d\n", quota);
	return quota;
}
void set_vcpu_quota(struct ancs_vm *vif, int quota)
{
	struct task_struct *ts;
	int i, err;

	for(i=0; i<MAX_NUMBER_VCPU; i++){
		ts=vif->vcpu[i];

		if(ts==NULL)
			continue;

		err=tg_set_cfs_quota(ts->sched_task_group, quota);
		if(err)
			printk("ancs: [VM%d] vcpu quota setting is failed -> %d \n", vif->id, quota);
		}
}

void set_vhost_quota(struct ancs_vm *vif, int quota)
{
	struct task_group *tg=vif->vhost->sched_task_group;
	int err;

	err=tg_set_cfs_quota(tg, quota);
	if(err)
		printk("ancs: [VM%d] vhost quota setting is failed -> %d\n", vif->id, quota);
	
	//printk("ancs: SETTING quota of vhost is %d\n", quota);
}
#endif
void add_active_vif(struct ancs_vm *vif)
{
	unsigned long flags;
#ifdef CPU_CONTROL
	long quota, i;
	struct task_struct *previous, *next;
#endif

	if(vif==NULL){
		printk(KERN_ERR "ancs: add_active_vif is failed! vif is NULL\n");
		return;
		}

	if(!list_empty(&vif->active_list))
		return;
	
	credit_allocator->total_weight+=vif->weight;
	credit_allocator->num_vif++;
	printk(KERN_ALERT "ancs: add_active_vif%d is called total weight %d remainig credit %u\n", vif->id, credit_allocator->total_weight, vif->remaining_credit);
	
	spin_lock_irqsave(&credit_allocator->active_vif_list_lock, flags);
	if (list_empty(&vif->active_list)){
		list_add_tail(&vif->active_list, &credit_allocator->active_vif_list);
		}
	spin_unlock_irqrestore(&credit_allocator->active_vif_list_lock, flags);

#ifdef CPU_CONTROL
	previous = (struct task_struct *)vif->parent;
	for(i=0;i<MAX_NUMBER_VCPU;i++){
		next = next_thread(previous);
		if(next == NULL){
			printk("ancs: vcpu task struct is NULL\n");
			break;
			}
		else{
			vif->vcpu[i] = next;
			printk("ancs: task struct %p of pid %d\n", next, next->pid);
			previous = next;
			}
	}
#endif

}
void remove_active_vif(struct ancs_vm *vif)
{
//	int i;

	if(vif==NULL){
		printk(KERN_ERR "ancs: remove_active_vif%d is failed! vif is NULL\n", vif->id);
		return;
		}
	if(list_empty(&vif->active_list))
		return;
	
	credit_allocator->total_weight -= vif->weight;
	credit_allocator->num_vif--;
	printk(KERN_ALERT "ancs: remove_active_vif%d is called total weight %d\n", vif->id, credit_allocator->total_weight);

	spin_lock_irq(&credit_allocator->active_vif_list_lock);
	if (!list_empty(&vif->active_list)) 
		list_del_init(&vif->active_list);
	spin_unlock_irq(&credit_allocator->active_vif_list_lock);
}
#ifdef CPU_CONTROL
static void ancs_monitoring(unsigned int data){
	struct list_head *next;
	struct ancs_vm *temp_vif, *next_vif;
	unsigned long cpu, nw, result;
	cpu = 0; 
	nw = 0;
	result = 0;
	if(list_empty(&credit_allocator->active_vif_list))
		goto out;

	list_for_each_entry_safe(temp_vif, next_vif, &credit_allocator->active_vif_list, active_list){
		if(!temp_vif)
			goto out;
#if 0		
		cpu = temp_vif->stat.cpu_usage; 
		nw = temp_vif->used_credit;
		
		if(temp_vif->stat.cpu_usage != 0)
			cpu = cpu /(MAX_NUMBER_VCPU*100);
		if(temp_vif->used_credit != 0)
			nw = nw / MAX_CREDIT;

		if(cpu > nw)
			temp_vif->stat.flag = CPU_intensive;
		else
			temp_vif->stat.flag = NW_intensive;

		temp_vif->used_credit = 0;
#endif
#ifdef PPS_CONTROL
		result += temp_vif->pps;
		temp_vif->pps = 0;
#endif		
		}
	credit_allocator->total_credit = result;
	out:
	mod_timer(&credit_allocator->monitor_timer, jiffies + msecs_to_jiffies(5000));
	return;
}
#endif
static void credit_accounting(unsigned long data){
	struct list_head *iter, *next;
	struct ancs_vm *temp_vif, *next_vif; 
	int total = credit_allocator->total_weight;
	unsigned int weight_left = total;
	unsigned int credit_left = 0;
	unsigned int credit_total = MAX_CREDIT ;
	unsigned int credit_fair, credit_used;
	int credit_xtra = 0 ;
	iter=next=NULL;
	temp_vif=next_vif=NULL;
	
	
	int cpu = smp_processor_id();
	WARN_ON(cpu != data);	

//	spin_lock(&credit_allocator->active_vif_list_lock);
	if(list_empty(&credit_allocator->active_vif_list) || total == 0)
		goto out;
	
	if(credit_allocator->credit_balance > 0)
		credit_total += credit_allocator->credit_balance ;

	ancs++;
	list_for_each_entry_safe(temp_vif, next_vif, &credit_allocator->active_vif_list, active_list){
		if(!temp_vif)
			goto out;
		
		weight_left -= temp_vif->weight;	
		credit_fair = ((credit_total * temp_vif->weight) + (total-1) )/ total;
//		temp_vif->remaining_credit += credit_fair;
		temp_vif->remaining_credit = credit_fair;
#ifdef CPU_CONTROL
		credit_used=temp_vif->used_credit;
			/* check required performance is satisfied */
		if(credit_used!=0 && credit_used < credit_fair)	
			schedule_work(&ires_work);
#endif
		if(temp_vif->min_credit!=0 || temp_vif->max_credit!=0){
			if(temp_vif->min_credit!=0 && temp_vif->remaining_credit < temp_vif->min_credit){
				credit_total-= (temp_vif->min_credit - temp_vif->remaining_credit);
				temp_vif->remaining_credit = temp_vif->min_credit;
				total-=temp_vif->weight;
				list_del(&temp_vif->active_list);
				list_add(&temp_vif->active_list, &credit_allocator->active_vif_list);
			}
			else if(temp_vif->max_credit!=0 && temp_vif->remaining_credit > temp_vif->max_credit){
				credit_total+= (temp_vif->remaining_credit - temp_vif->max_credit);
				temp_vif->remaining_credit = temp_vif->max_credit;
				total-=temp_vif->weight;
				list_del(&temp_vif->active_list);
				list_add(&temp_vif->active_list, &credit_allocator->active_vif_list);
				}
			goto skip;
		}
		
//		if(temp_vif->remaining_credit < MAX_CREDIT)
		if(temp_vif->used_credit != 0){
			credit_xtra = 1;
			temp_vif->used_credit = 0;
			}
		else
		{
			credit_left += (temp_vif->remaining_credit - MIN_CREDIT);

			if(weight_left != 0U){
				credit_total += ((credit_left*total)+(weight_left - 1))/weight_left;
				credit_left=0;
			}
			if(credit_xtra){
				list_del(&temp_vif->active_list);
				list_add(&temp_vif->active_list, &credit_allocator->active_vif_list);
			}
			
			if(credit_allocator->num_vif > 1)
				temp_vif->remaining_credit = credit_fair;
			else
				temp_vif->remaining_credit = MAX_CREDIT;
		}
skip:	
		if(temp_vif->need_reschedule == true)
			//xen_netbk_check_rx_xenvif(temp_vif);
			//vhost_poll_queue(temp_vif->poll);
			temp_vif->need_reschedule = false;

	}
	credit_allocator->credit_balance = credit_left;
	
out:
//	spin_unlock(&netbk->active_vif_list_lock);
	mod_timer(&credit_allocator->account_timer, jiffies + msecs_to_jiffies(100));
	return;
}
static ssize_t vif_write(struct file *file, const char __user* user_buffer, size_t count, loff_t *ppos)
{
        char* filename = file->f_path.dentry->d_name.name;
	char* input = strsep(&user_buffer,"\n");
       struct ancs_vm *vif;
	char *endptr;
        unsigned int value = simple_strtol(input, &endptr, 10);
        
        if(endptr == input && *endptr != '\0')
        {
                printk("invalid input!\n");
                return count;
        }

	vif = PDE_DATA(file_inode(file));	
	if(!(vif)){
		printk(KERN_INFO "NULL Data\n");
		return 0;
	}

	if(!strcmp(filename, "min_credit"))
        {
		vif->min_credit = value;
		goto out;
        }
	
	if(!strcmp(filename, "max_credit"))
        {
		vif->max_credit = value;
		goto out;
        }

	if(!strcmp(filename, "weight"))
        {
		vif->weight = value;
		goto out;
        }
#ifdef CPU_CONTROL	
	if(!strcmp(filename, "CPU_usage"))
        {
		vif->stat.cpu_usage= value;
		goto out;
        }
	if(!strcmp(filename, "kvm_virq"))
        {
		vif->stat.virq= value;
		goto out;
        }
#endif
out:
	return count;
	
}

static ssize_t vif_read(struct file *file, char *buf, size_t count, loff_t *ppos)
{
	char* filename = file->f_path.dentry->d_name.name;
	struct ancs_vm *vif;	
	unsigned int len;
	
	vif = PDE_DATA(file_inode(file));
	if(!(vif)){
                printk(KERN_INFO "NULL Data\n");
                return 0;
        }
	if(!strcmp(filename, "min_credit")){
		len = sprintf(buf, "%d\n", vif->min_credit);
		goto out;
		}
	else if(!strcmp(filename, "max_credit")){
		len = sprintf(buf, "%d\n", vif->max_credit);
		goto out;
		}
	else if(!strcmp(filename, "weight")){
		len = sprintf(buf, "%d\n", vif->weight);
		goto out;
		}
	else if(!strcmp(filename, "remaining_credit")){
		len = sprintf(buf, "%d\n", vif->remaining_credit);
		goto out;
		}
	else if(!strcmp(filename, "used_credit")){
       	len = sprintf(buf, "%d\n", vif->used_credit);
		goto out;
		}
	else if(!strcmp(filename, "pid")){
		len = sprintf(buf, "%d\n", vif->vhost->pid);
		goto out;
		}
#ifdef CPU_CONTROL	
	else if(!strcmp(filename, "CPU_usage")){
       	len = sprintf(buf, "%d\n", vif->stat.cpu_usage);
		goto out;
		}
	else if(!strcmp(filename, "kvm_virq")){
		len = sprintf(buf, "%d\n", vif->stat.virq);
		goto out;
		}
#endif	
	else{
		count = sprintf(buf, "%s", "ERROR");
		return count;
		}

out:
	if(fileread == 0){
                        fileread = 1;
                        return len;
                }
                else
                {
                        fileread = 0;
                        return 0;
                }
}

static const struct file_operations vif_opt ={
	.write = vif_write,
	.read = vif_read,
};


static int __init vif_init(void)
{
        struct list_head *p;
        struct ancs_vm *vif;
        idx = 0;
	 int cpu = smp_processor_id();
	
	 credit_allocator = kmalloc(sizeof(struct credit_allocator), GFP_KERNEL | __GFP_NOWARN | __GFP_REPEAT);
	 if (!credit_allocator)
	 	return -ENOMEM;	

	 credit_allocator->total_weight = 0;
	 credit_allocator->credit_balance = 0;
	 credit_allocator->num_vif =0;
	 
	 INIT_LIST_HEAD(&credit_allocator->active_vif_list);
	 spin_lock_init(&credit_allocator->active_vif_list_lock);

	 proc_root_dir = proc_mkdir("oslab", NULL);
	
        list_for_each(p, &ancs_proc_list){
                vif = list_entry(p, struct ancs_vm, proc_list);
		   add_active_vif(vif);
		proc_vif[idx].id = (int)vif->id;
		sprintf(proc_vif[idx].name, "vif%d", (int)vif->id);
		proc_vif[idx].dir = proc_mkdir(proc_vif[idx].name, proc_root_dir);
                proc_vif[idx].file[0] = proc_create_data
		("min_credit",0600, proc_vif[idx].dir, &vif_opt, vif);
		proc_vif[idx].file[1] = proc_create_data
		("max_credit",0600, proc_vif[idx].dir, &vif_opt, vif);
		proc_vif[idx].file[2] = proc_create_data
		("weight",0600, proc_vif[idx].dir, &vif_opt, vif);
		proc_vif[idx].file[3] = proc_create_data
		("remaining_credit",0600, proc_vif[idx].dir, &vif_opt, vif);
		proc_vif[idx].file[4] = proc_create_data
		("used_credit",0600, proc_vif[idx].dir, &vif_opt, vif);
		proc_vif[idx].file[5] = proc_create_data
		("pid",0600, proc_vif[idx].dir, &vif_opt, vif);
#ifdef CPU_CONTROL		
		proc_vif[idx].file[6] = proc_create_data
		("CPU_usage",0600, proc_vif[idx].dir, &vif_opt, vif);
		proc_vif[idx].file[7] = proc_create_data
		("vhost_usage",0600, proc_vif[idx].dir, &vif_opt, vif);
		proc_vif[idx].file[8] = proc_create_data
		("kvm_virq",0600, proc_vif[idx].dir, &vif_opt, vif);
		
#endif	
		idx++;
        }
	cpu	= smp_processor_id();

//	setup_timer(&credit_allocator->account_timer, credit_accounting, cpu);
//	mod_timer(&credit_allocator->account_timer, jiffies + msecs_to_jiffies(50));

#ifdef CPU_CONTROL
//	setup_timer(&credit_allocator->monitor_timer, ancs_monitoring, cpu);
//	mod_timer(&credit_allocator->monitor_timer, jiffies + msecs_to_jiffies(5000));
	setup_timer(&credit_allocator->quota_timer, quota_control, cpu);
	mod_timer(&credit_allocator->quota_timer, jiffies + msecs_to_jiffies(1000));

	INIT_LIST_HEAD(&credit_allocator->victim_vif_list);
	 spin_lock_init(&credit_allocator->victim_vif_list_lock);
#ifdef PRO_SHARE
	 credit_allocator->total_credit = ~0UL;
#endif
#ifdef HYBRID
	credit_allocator->quota_balance = 0;
#endif
#endif
	printk(KERN_INFO "ancs: credit allocator init!!\n");	
        return 0;
}

static void __exit vif_exit(void)
{
	struct list_head *p;
	struct ancs_vm *vif;
	int i;
        printk("EXIT!\n");

        for(i=0; i<idx; i++){
                remove_proc_entry("min_credit", proc_vif[i].dir);
		remove_proc_entry("max_credit", proc_vif[i].dir);
		remove_proc_entry("weight", proc_vif[i].dir);
		remove_proc_entry("remaining_credit", proc_vif[i].dir);
		remove_proc_entry("used_credit", proc_vif[i].dir);
		remove_proc_entry("pid", proc_vif[i].dir);
#ifdef CPU_CONTROL	
		remove_proc_entry("CPU_usage", proc_vif[i].dir);
		remove_proc_entry("vhost_usage", proc_vif[i].dir);
		remove_proc_entry("kvm_virq", proc_vif[i].dir);
#endif	
		remove_proc_entry(proc_vif[i].name, proc_root_dir);
        }
	
	list_for_each(p, &ancs_proc_list){
		vif = list_entry(p, struct ancs_vm, proc_list);
		remove_active_vif(vif);
		}
//	del_timer(&credit_allocator->monitor_timer);
//	del_timer(&credit_allocator->account_timer);
	del_timer(&credit_allocator->quota_timer);

	remove_proc_entry("oslab", NULL);
        return;
}

module_init(vif_init);
module_exit(vif_exit);

MODULE_AUTHOR("Korea University");
MODULE_DESCRIPTION("OSLAB");
MODULE_LICENSE("GPL v2");
MODULE_VERSION("ver 1.0");
