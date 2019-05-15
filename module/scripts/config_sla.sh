#!/bin/sh


sh ./insert_sla.sh vm01 c_usage 10
sh ./insert_sla.sh vm02 c_usage 20
sh ./insert_sla.sh vm01 n_maxcredit 1000
sh ./insert_sla.sh vm02 n_maxcredit 2000
sh ./insert_sla.sh vm01 b_bw 10
sh ./insert_sla.sh vm02 b_bw 20
