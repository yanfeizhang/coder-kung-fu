<?php

/**
 * Socket客户端
 */

if(!isset($argv[1]) || !isset($argv[2])){
    echo "usage: php client.php {serverIp} {serverPort}  \n";
    exit;
}
$serverIp       = $argv[1];
$serverPort     = $argv[2];

$sockets = array();

function buildConnect($sIp,$sPort,$num){
    global $sockets;

    for($i = 1; $i <= $num; $i++){
        if(0 == $i % 1000){
            echo " 连接 ".$sIp.":".$sPort." 成功了 ".$i." 条\n";
            sleep(1); 
        }
        
        $socket = socket_create(AF_INET, SOCK_STREAM, SOL_TCP);
        if($socket == false) {
            echo "$sPort socket_create() 失败,".socket_strerror(socket_last_error($socket))."\n";
            sleep(5);
            continue;
        }

        if(false == socket_connect($socket, $sIp, $sPort)){
            echo "$sPort socket_connect() 失败的原因是:".socket_strerror(socket_last_error($socket))."\n";
            sleep(5);
            continue;
        }

        //将 socket 保存起来，避免连接被内核释放
        $sockets[] = $socket;
        
        //$wmsg = "I am client!";
        //socket_write($socket, $wmsg, strlen($wmsg));

        //$rmsg = socket_read($socket, 1024);
        //echo "接收到的数据长度:".strlen($rmsg)."\n";

    }
}


function closeAll(){
    global $sockets;
    foreach($sockets as $socket){
        socket_close($socket);
    }
}

echo "\nconnect start:";
buildConnect($serverIp, $serverPort, 50000);

sleep(600);

echo "\nclose all sockets now";
closeAll();
