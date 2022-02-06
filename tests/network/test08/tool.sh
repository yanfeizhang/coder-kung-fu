#!/bin/bash

exec_runsrv(){
    for((i=1;i<=5;i++)); do
        echo $SERVER 0.0.0.0 6000  &
        $SERVER 0.0.0.0 6000  &
    done

}

exec_stopsrv(){
    ps -ef | grep $SERVER | awk '{print $2}' | xargs kill -9 > /dev/null
}

TYPE=$1
case $TYPE in
    "runsrv")  SERVER=$2; exec_runsrv;;
    "stopsrv")  SERVER=$2; exec_stopsrv;;
    *)  echo "get unkown type $TYPE"; exit ;;
esac
