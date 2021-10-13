
本实验配套的 Makefile 位于本文件的同目录，其使用方法如下：


**创建网络环境1**

```sh
# make create-net1
```

**创建网络环境2**

```sh
# make create-net2
```

**创建 bridge**，并把 net1、net2中的“网线插头”给接上

```sh
# make create-br 
```

从 net1 的网络环境中 ping 一下 net2 试试

```sh
# ip netns exec net1 ping 192.168.0.102 -I veth1
PING 192.168.0.102 (192.168.0.102) from 192.168.0.101 veth1: 56(84) bytes of data.
64 bytes from 192.168.0.102: icmp_seq=1 ttl=64 time=0.037 ms
64 bytes from 192.168.0.102: icmp_seq=2 ttl=64 time=0.008 ms
64 bytes from 192.168.0.102: icmp_seq=3 ttl=64 time=0.005 ms
```

最后，清理实验所创建的各种虚拟网络设备

```sh
# make clean
```