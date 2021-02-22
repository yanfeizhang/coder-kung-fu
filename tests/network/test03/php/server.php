<?php
/**
 * Socket服务端
 */
set_time_limit(0);

if(!isset($argv[1]) || !isset($argv[2])){
	echo "usage: php server.php {ip} {port}\n";
	exit;
}
$ip = $argv[1];
$port = $argv[2];

if(($sock = socket_create(AF_INET, SOCK_STREAM, SOL_TCP)) < 0) {
    echo "socket_create() 失败的原因是:" . socket_strerror($sock) . "\n";
    exit();
}

if(($ret = socket_bind($sock, $ip, $port)) < 0) {
    echo "socket_bind() 失败的原因是:" . socket_strerror($ret) . "\n";
    exit();
}

if(($ret = socket_listen($sock,1024)) < 0) {
    echo "socket_listen() 失败的原因是:" . socket_strerror($ret) . "\n";
    exit();
}
echo "$ip $port listen ok!\n";

$sockets = array();
$count = 0;
do{
    //接收一个Socket连接
    $con = socket_accept($sock);
    if (false == $con) {
        echo "$ip $port socket_accept() failed: reason:".socket_strerror(socket_last_error($socket))."\n";
        continue;
    } else {
        echo "$ip $port accept success: ".$count++."\n";

        //发送数据
        //$wmsg = "I am server\n";
        //socket_write($con, $wmsg, strlen($wmsg));

        //接收数据
        //$rmsg = socket_read($msgsock[$count], 2048);
        //echo = "收到的信息:".$rmsg."\n";
    }
	$sockets[$count] = $con;
}while(true);
