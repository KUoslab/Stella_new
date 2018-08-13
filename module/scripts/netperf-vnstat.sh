netperf -H $1 -p 5002 -l 60 -- -m 1024 & netperf -H $1 -p 5003 -l 60 -- -m 1024 & netperf -H $1 -p 5004 -l 60 -- -m 1024 & netperf -H $1 -p 5005 -l 60 -- -m 1024 & vnstat -i $2 -tr 60

