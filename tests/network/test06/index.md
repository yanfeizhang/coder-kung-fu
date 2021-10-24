创建网络环境1

```sh
# make create-net1
```

创建一对儿 veth，并把一头放到 net1中。分别在母机和 net1 中查看设备信息

```sh
# make create-veth
```

配置 IP 并启动 veth

```sh
# make start-veth
```

ping一下试试

```sh
# make ping
```

实验结果后，清理实验环境

```sh
# make clean
```