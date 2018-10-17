netperf -H $1 -p 5002 -l 120 -- -m 1024 & netperf -H $1 -p 5003 -l 120 -- -m 1024 & netperf -H $1 -p 5004 -l 120 -- -m 1024 & netperf -H $1 -p 5005 -l 120 -- -m 1024 & vnstat -i $2 -tr 120

