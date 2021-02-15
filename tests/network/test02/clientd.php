<?php

/**
 * Socket客户端
 */

require("ports.php");

//这里填写你的服务器ip
$serverIp = "";

$cmd = "ps -ef | grep client.php | awk '{print $2}' | xargs kill -9";
shell_exec($cmd);
shell_exec("echo '' > clientd.log");

if(isset($argv[1]) && "stop"==$argv[1]){
    exit;
}

foreach($ports as $port){
    $tCommand = "php client.php $serverIp $port >> clientd.log &";
    echo $tCommand."\n";
    shell_exec($tCommand);  
    sleep(1);
}



