<?php

/**
 * Socket客户端
 */

if(!isset($argv[1]) || !isset($argv[2]) || !isset($argv[3])){
    echo "usage: php client.php {localIp} {serverIp} {serverPort}  \n";
    exit;
}

$localIp        = $argv[1];
$serverIp       = $argv[2];
$serverPort     = $argv[3];

$sockets = array();

function buildConnect($lIp,$sIp,$sPort,$num){
    global $sockets;

    for($i = 1; $i <= $num; $i++){
        //稍稍停顿一下，避免把服务端的握手队列打满
        if(0 == $i % 1000){
            sleep(1); 
            echo $lIp." 连接 ".$sIp.":".$sPort." 成功了 ".$i." 条\n";
        }

        $socket = socket_create(AF_INET,SOCK_STREAM,SOL_TCP);
        if($socket == false) {
            echo "$lIp $sPort socket_create() 失败,".socket_strerror(socket_last_error($socket))."\n";
            sleep(5);
            continue;
        }

        if(false == socket_bind($socket, $lIp)){
            echo "$lIp $sPort socket_bind() 失败,".socket_strerror(socket_last_error($socket))."\n";
            sleep(5);
            continue;
        }

        if(false == socket_connect($socket, $sIp, $sPort)){
            echo "$lIp $sPort socket_connect() 失败,".socket_strerror(socket_last_error($socket))."\n";
            sleep(5);
            continue;
        }

        //将 socket 保存起来，避免连接被内核释放
        $sockets[] = $socket;

        //发送数据
        //$wmsg = "I am client\n";
        //socket_write($con, $wmsg, strlen($wmsg));

        //接收数据
        //$rmsg = socket_read($socket, 2048);
        //echo = "收到的信息:".$rmsg."\n";    }
    }
}

function closeAll(){
    global $sockets;
    foreach($sockets as $socket){
        socket_close($socket);
    }
}

//开始测试
buildConnect($localIp, $serverIp, $serverPort, 50000);

//连接完了 sleep 一会儿
//这样有时间在 linux 上用 netstat、ss、slabtop 等命令进行观察
sleep(600);

//关闭所有连接
echo "close all connection now...";
closeAll();
