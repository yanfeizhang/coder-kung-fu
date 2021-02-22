#!/bin/bash

#---------------------------  begin -------------------------
#注意：这一部分的内容需要根据你自己的实验环境来设置

#1.客户端 IP 列表：选择20个，且不能在局域网中存在
# 示例
# IPS=(
#    "192.168.1.200"
#    "192.168.1.201"
# )

IPS=(
    "192.168.1.200"
    "192.168.1.201"
)

#2.客户端 IP 对应的子网掩码
#示例：
#NETMASK="255.255.248.0"
NETMASK="255.255.248.0"

#3.服务端的 IP 和 端口
#示例
#SERVERIP="192.168.1.100"
#SERVERPORT="8090"
SERVERIP="192.168.1.100"
SERVERPORT="8090"
#---------------------------   end  -------------------------

TYPE=$1

exec_ping(){
    for i in "${!IPS[@]}"; do
        ping -c 1 -W 1 ${IPS[$i]} > /dev/null
        if [[ $? != 0 ]];then 
            echo ${IPS[$i]}" false"
        else
            echo ${IPS[$i]}" true"
        fi
    done 
}

exec_ifup(){
    for i in "${!IPS[@]}"; do
        echo ifconfig eth0:$i ${IPS[$i]} netmask $NETMASK up
        ifconfig eth0:$i ${IPS[$i]} netmask $NETMASK up
    done
}

exec_ifdown(){
    for i in "${!IPS[@]}"; do
        echo ifconfig eth0:$i down
        ifconfig eth0:$i down
    done
}

exec_runcli(){
    for i in "${!IPS[@]}"; do
        echo  $CLIENT ${IPS[$i]} $SERVERIP $SERVERPORT
        $CLIENT ${IPS[$i]} $SERVERIP $SERVERPORT  &
    done
}

exec_stopcli(){
    ps -ef | grep $CLIENT | awk '{print $2}' | xargs kill -9 > /dev/null
}

exec_runsrv(){
    echo $SERVER 0.0.0.0 $SERVERPORT
    $SERVER 0.0.0.0 $SERVERPORT
}

case $TYPE in
    "ping")  exec_ping;;
    "ifup")  exec_ifup;;
    "ifdown")  exec_ifdown;;
    "runcli")  CLIENT=$2; exec_runcli;;
    "stopcli")  CLIENT=$2; exec_stopcli;;
    "runsrv")  SERVER=$2; exec_runsrv;;
    *)  echo "get unkown type $TYPE"; exit ;;
esac

