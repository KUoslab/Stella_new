mpstat 120 1 > mpstat.txt & iostat 120 2 > iostat.txt & vnstat -i $1 -tr 120 > vnstat.txt

