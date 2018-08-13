mpstat 60 1 > mpstat.txt & iostat 60 2 > iostat.txt & vnstat -i $1 -tr 60 > vnstat.txt

