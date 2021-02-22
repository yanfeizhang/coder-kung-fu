#!/bin/bash

#---------------------------  begin -------------------------
#注意：这一部分的内容需要根据你自己的实验环境来设置

#1.服务端的 IP 
#示例
#SERVERIP="192.168.1.100"
SERVERIP="192.168.1.100"

#2.服务端的端口
SERVERPORTS=(
    "8100"
    "8101"
    "8102"
    "8103"
    "8104"
    "8105"
    "8106"
    "8107"
    "8108"
    "8109"
    "8110"
    "8111"
    "8112"
    "8113"
    "8114"
    "8115"
    "8116"
    "8117"
    "8118"
    "8119"
)
#---------------------------   end  -------------------------

TYPE=$1

exec_runcli(){
    for i in "${!SERVERPORTS[@]}"; do
        echo  $CLIENT $SERVERIP ${SERVERPORTS[$i]} &
        $CLIENT $SERVERIP ${SERVERPORTS[$i]} &
    done
}

exec_stopcli(){
    ps -ef | grep $CLIENT | awk '{print $2}' | xargs kill -9 
}

exec_runsrv(){
    for i in "${!SERVERPORTS[@]}"; do
        echo $SERVER 0.0.0.0 ${SERVERPORTS[$i]} &
        $SERVER 0.0.0.0 ${SERVERPORTS[$i]} &
    done
}

exec_stopsrv(){
    ps -ef | grep $SERVER | awk '{print $2}' | xargs kill -9
}

case $TYPE in
    "runcli")  CLIENT=$2; exec_runcli;;
    "stopcli")  CLIENT=$2; exec_stopcli;;
    "runsrv")  SERVER=$2; exec_runsrv;;
    "stopsrv")  SERVER=$2; exec_stopsrv;;
    *)  echo "get unkown type $TYPE"; exit ;;
esac

