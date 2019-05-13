#!/bin/sh

sh ./insert_sla.sh VM1 n_maxcredit 6000
sh ./insert_sla.sh VM1 c_usage 60
sh ./insert_sla.sh VM1 b_bw 60
sh ./insert_sla.sh VM2 n_maxcredit 4000
sh ./insert_sla.sh VM2 c_usage 40
sh ./insert_sla.sh VM2 b_bw 40
cat /proc/gos_vm_info
