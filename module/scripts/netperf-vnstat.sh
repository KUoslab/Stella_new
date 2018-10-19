#!/bin/sh

I=1
FM=99
SEC=`expr $3 + 0`

echo -n "*** Script Start at "

echo `date +%F-%H-%M-%S`

WaitFor58sec()
{
echo "*** wait for 58 second..." #to wait until running this script in other VMs

S=`date +%S`

if [ "$S" = 58 ]
then
        return
fi

until [ "$S" = 58 ]
do
        sleep 1
        S=`date +%S`
done

#reached to 58 second
echo `date +%M-%S-%N`
}

WaitForTheTime()
{
#WaitFor58sec

M=`date +%M`
M=`expr $M + 0`

if [ $FM -eq 99 ]       #for the first time or not
then 
        FM=`date +%M`
        FM=`expr $FM + 0`
        TM=`expr $FM + 1`       #next target minute
        echo "First TM=$TM"

else
        TM=`expr $TM + $SEC`       #next target minute. First Min +5 +5 +5
        echo "HERE TM=$TM"
fi


#make sure TM does not exceed 60

#DM=`expr $TM - $M`
DM=`expr $TM - $M`

echo $DM

if [ $TM -gt 59 ]
then 
        TM=`expr $TM - 60`
fi

echo "*** wait for execution...next time: $TM"
echo `date +%M-%S-%N`

#re-check for wating

M=`date +%M`
M=`expr $M + 0`

if [ $M = $TM ]
then
        return
fi

M=`date +%M`
M=`expr $M + 0`
DM=`expr $TM - $M`

#long sleep for long wait

while [ $DM -gt 1 ]
do
        sleep 1
        M=`date +%M`
        M=`expr $M + 0`
        DM=`expr $TM - $M`
done


M=`date +%M`
M=`expr $M + 0`

if [ $M = $TM ]
then
        return
fi

WaitFor58sec

M=`date +%M`
M=`expr $M + 0`

#spin to the time less than 2 second
until [ $M = $TM ]
do
        M=`date +%M`
        M=`expr $M + 0`
done

#time reached
echo `date +%M-%S-%N`
}


#the body of the script


A=0

until [ "$A" = "$I" ]
do
        echo 
        echo -n "Execution #"
        echo $A
        echo

        WaitForTheTime
	netperf -H $1 -p 5002 -l 180 -- -m 1024 & netperf -H $1 -p 5003 -l 180 -- -m 1024 & netperf -H $1 -p 5004 -l 180 -- -m 1024 & netperf -H $1 -p 5005 -l 180 -- -m 1024 & netperf -H $1 -p 5006 -l 180 -- -m 1024 & netperf -H $1 -p 5007 -l 180 -- -m 1024 & sleep 1
	vnstat -i $2 -tr 120
	A=`expr $A + 1`
done

