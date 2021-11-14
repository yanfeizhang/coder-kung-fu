
## 准备工作

创建一个虚拟网络环境

```sh
# make create-net1
```

创建一个 bridge，并和刚才的创建的 veth 建立连接

```sh
# make create-br
```

进行 nat 配置

```sh
# make create-nat
```


## 访问外部网络

选择一个母机能 ping 同的 ip，ping 之

```
# ip netns exec net1 ping 10.\*.\*.\*
```
访问外网，要保证这个 ip 用母机能 ping 

```sh
PING 10.\*.\*.\* (10.\*.\*.\*) 56(84) bytes of data.
64 bytes from 10.\*.\*.\*: icmp_seq=1 ttl=57 time=2.12 ms
64 bytes from 10.\*.\*.\*: icmp_seq=2 ttl=57 time=1.76 ms
```

## 提供服务给外网调用

使用 nc 命令在虚拟网络中监听一个服务
```
# ip netns exec net1 nc -lp 80
```

在另外一台机器上使用 telnent 连接这台机器上的 8088 端口。

**注意**， telnet 需要指定的是容器所在的母机的 ip，但 8088 这个端口上的服务是由虚拟容器网络提供的。

```c
# telnet 10.\*.\*.\* 8088
Trying 10.\*.\*.\*...
Connected to 10.\*.\*.\*.
Escape character is '^]'.
......
```


