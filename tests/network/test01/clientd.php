<?php

/**
 * Socket服务端
 */

//-------------- start -------------
//这一部分需要根据你自己的实验环境来修改
//1.需要你从自己的网络环境中选出 20 个可用的 ip，
$ips = array(
        "客户端 IP1",
        "客户端 IP2",
        "客户端 IP3",
        "......",
        "服务端 IP20",
    );

//2.需要设置你的子网掩码
$netmask = "子网掩码";

//3.指定要连接的server ip 和端口
$serverIp = "服务端 IP";
$serverPort = "服务端端口";
//--------------- end --------------    

if(!isset($argv[1])){
    echo "usage: php clientd.php ifup/ifdown/stop/start\n";
    exit;
}

function pingAddress($address) {  
    $outcome = "";
    $status = -1;  
    $pingresult = exec("ping -c 1 -W 1 {$address}", $outcome, $status);  

    if (0 == $status) {  
        return "true";
    } else {  
        return "false";
    }  
}  

switch($argv[1]){
    case "ping":
        foreach($ips as $k=>$ip){
            echo $ip.":".pingAddress($ip)."\n";
        }
        break;

    case "ifup":
        foreach($ips as $k=>$ip){  
            shell_exec("ifconfig eth0:$k $ip netmask ".$netmask." up");
        }   
        break;

    case "ifdown":
        foreach($ips as $k=>$ip){
            shell_exec("ifconfig eth0:$k down");
        }
        break;

    case "start":
        $cmd = "ps -ef | grep client.php | awk '{print $2}' | xargs kill -9";
        shell_exec($cmd);
        shell_exec("echo '' > clientd.log");

        foreach($ips as $ip){
            $cmd = "php client.php $ip $serverIp $serverPort >> clientd.log &";
            echo $cmd."\n";
            shell_exec($cmd);
        }
        break;

    case "stop":
        $cmd = "ps -ef | grep client.php | awk '{print $2}' | xargs kill -9";
        shell_exec($cmd);
        break;  

    default:
        break;
}
    
