<?php

/**
 * Socket服务端
 */

require("ports.php");

$cmd = "ps -ef | grep server.php | awk '{print $2}' | xargs kill -9";
shell_exec($cmd);
shell_exec("echo '' > serverd.log");

if(isset($argv[1]) && "stop"==$argv[1]){
    exit;
}

foreach($ports as $port){
    $tCommand = "php server.php 0.0.0.0 $port >> serverd.log &";
    echo $tCommand."\n";
    shell_exec($tCommand);
    sleep(1);
}

