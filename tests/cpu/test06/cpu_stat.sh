#!/bin/bash

#获取宿主机的 CPU 使用情况
function get_host_cpu_usage(){
	#内核会在/proc/stat中输出整机CPU的使用情况, 例如：cat /proc/stat 输出如下
	#cpu  52635657 657000 57094567 7675992570 422057 0 545206 0 0 0
	#其中各列中的数值都是从启动到现在的累计和，单位是jiffies
	#除了第一列外，其余每列的含义分别是：
	#  1.user：用户态花费的cpu时间
	#  2.nice：用户态在低优先级花费的cpu时间
	#  3.system：系统态花费的cpu时间
	#  4.idel：在空闲任务上花费的cpu时间
	#  5.iowait：等待I/O花费的cpu时间
	#  6.irq：硬中断花费的cpu时间
	#  7.softirq：软中断花费的cpu时间
	#  8.steal：系统处在虚拟化环境中，你的虚拟机被其他虚拟机占用的 CPU 时间
	#  9.guest：运行虚拟机花费的cpu时间
	#  10.guest_nice：运行低优先级虚拟机花费的cpu时间

	#获取宿主机的 CPU 用量的原理，是选择两个时间点，
	#cpu总时间=user+system+nice+idle+iowait+irq+softirq
	#cpu_usage=100-(idle2-idle1)/(cpu总时间2-cpu总时1)*100

	T1_CPU_INFO=$(cat /proc/stat | grep -w cpu | awk '{print $2,$3,$4,$5,$6,$7,$8}')
	T1_IDLE=$(echo $T1_CPU_INFO | awk '{print $4}')
	T1_TOTAL=$(echo $T1_CPU_INFO | awk '{print $1+$2+$3+$4+$5+$6+$7}')

	sleep 10

	T2_CPU_INFO=$(cat /proc/stat | grep -w cpu | awk '{print $2,$3,$4,$5,$6,$7,$8}')
	T2_IDLE=$(echo $T2_CPU_INFO | awk '{print $4}')
	T2_TOTAL=$(echo $T2_CPU_INFO | awk '{print $1+$2+$3+$4+$5+$6+$7}')

    CPU_UTILIZATION=`echo ${T1_IDLE} ${T1_TOTAL} ${T2_IDLE} ${T2_TOTAL}| awk '{printf "%.2f", (1-($3-$1)/($4-$2))*100}'`
	echo "Host CPU Utiliztion:${CPU_UTILIZATION}%"
}

get_host_cpu_usage


