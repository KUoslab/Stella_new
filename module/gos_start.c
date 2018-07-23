#define GOS_SOURCE
#define DISK_SOURCE
#define CPU_SOURCE

#include "gos.h"

struct gos_vm_info *gos_vm_list[VM_NUM];
const unsigned long gos_interval = 3000E6L;

EXPORT_SYMBOL(gos_vm_list);
EXPORT_SYMBOL(gos_interval);
