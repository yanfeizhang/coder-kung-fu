---
layout: default
title: "輕鬆理解 Docker 網路 (Network) 虛擬化基礎之 veth 裝置！"
---

# 輕鬆理解 Docker 網路 (Network) 虛擬化基礎之 veth 裝置！

> 原文標題：轻松理解 Docker 网络虚拟化基础之 veth 设备！

> 原文連結：https://mp.weixin.qq.com/s/sSQFINJ8RO8Nc4XtcyQIjQ

---

大家好，我是飛哥！

正如我在先前提到的，最近我又對網路虛擬化技術產生了濃厚的興趣。迫切想理解在 Docker 等虛擬技術下，網路底層是如何執行的。

不得不說，網路虛擬化技術是我給自己刨的又一個大坑。雖然我自認為把原生 Linux 網路實作過程理解的還算不錯了。但在看網路虛擬化相關的技術的時候，還是覺得不是很容易。

不過，飛哥有絕招，那就是先挑個軟柿子來捏。這不，今天我給大家帶來的就是 Docker 網路虛擬化中的一個比較好理解的技術 - veth。

回想下在實體主機組成的網路裡，最基礎，最簡單的網路連線方式是什麼？沒錯，那就是直接用一根交叉網線把兩臺電腦的網路卡 (Network Card) 連起來。這樣，一臺機器傳送資料，另外一臺就能收到了。

那麼，網路虛擬化實作的第一步，就是用軟體來模擬這個簡單的網路連線實作過程。實作的技術就是我們今天的主角 veth，它模擬了在實體世界裡的兩塊網路卡，以及一條網線。透過它可以將兩個虛擬的裝置連線起來，讓他們之間相互通訊。平時工作中在 Docker 映象裡我們看到的 eth0 裝置，其實就是 veth。

事實上，這種軟體模擬硬體方式我們一點兒也不陌生，我們本機網路 IO 裡的 lo 迴環裝置也是這樣一個用軟體虛擬出來裝置。 Veth 和 lo 的一點區別就是 veth 總是成雙成對地出現。

我們今天就來深入地看看 veth 這個東西是怎麼工作的。

## 一、veth 如何使用

不像迴環裝置，絕大多數同學在日常工作中可能都沒接觸過 veth。所以本文我們專門拉一小節出來介紹 veth 是如何使用的。

在 Linux 下，我們可以透過使用 ip 命令建立一對 veth。其中 link 表示 link layer 的意思，即鏈路層。這個命令可以用於管理和檢視網路介面，包括實體網路介面，也包括虛擬介面。

```
# ip link add veth0 type veth peer name veth1
```

使用 ip link show 來進行檢視。

```
# ip link add veth0 type veth peer name veth1
```

```
# ip link show
1: lo: <LOOPBACK,UP,LOWER_UP> mtu 65536 qdisc noqueue state UNKNOWN mode DEFAULT
link/loopback 00:00:00:00:00:00 brd 00:00:00:00:00:00
2: eth0: <BROADCAST,MULTICAST,UP,LOWER_UP> mtu 1500 qdisc mq state UP mode DEFAULT qlen 1000
link/ether 6c:0b:84:d5:88:d1 brd ff:ff:ff:ff:ff:ff
3: eth1: <BROADCAST,MULTICAST> mtu 1500 qdisc noop state DOWN mode DEFAULT qlen 1000
link/ether 6c:0b:84:d5:88:d2 brd ff:ff:ff:ff:ff:ff
4: veth1@veth0: <BROADCAST,MULTICAST,M-DOWN> mtu 1500 qdisc noop state DOWN mode DEFAULT qlen 1000
link/ether 4e:ac:33:e5:eb:16 brd ff:ff:ff:ff:ff:ff
5: veth0@veth1: <BROADCAST,MULTICAST,M-DOWN> mtu 1500 qdisc noop state DOWN mode DEFAULT qlen 1000
link/ether 2a:6d:65:74:30:fb brd ff:ff:ff:ff:ff:ff
```

和 eth0、lo 等網路裝置一樣，veth 也需要為其設定上 ip 後才能夠正常工作。我們為這對 veth 分別來設定上 IP。

```
# ip addr add 192.168.1.1/24 dev veth0
```

```
# ip addr add 192.168.1.2/24 dev veth1
```

接下來，我們把這兩個裝置啟動起來。

```
# ip link set veth0 up
```

```
# ip link set veth1 up
```

當裝置啟動起來以後，我們透過我們熟悉的 ifconfig 就可以檢視到它們了。

## ifconfig
eth0: ......
lo: ......
veth0: flags=4163<UP,BROADCAST,RUNNING,MULTICAST>  mtu 1500
inet 192.168.1.1  netmask 255.255.255.0  broadcast 0.0.0.0
......
veth1: flags=4163<UP,BROADCAST,RUNNING,MULTICAST>  mtu 1500
inet 192.168.1.2  netmask 255.255.255.0  broadcast 0.0.0.0
......

現在，一對虛擬裝置已經建立起來了。不過我們需要做一點準備工作，它們之間才可以進行互相通訊。首先要關閉反向過濾   `rp_filter`，該模組會檢查 IP 包是否符合要求，否則可能會過濾掉。然後再開啟 `accept_local`，接收本機 IP 資料封包。詳細準備過程如下：

```
# echo 0 > /proc/sys/net/ipv4/conf/all/rp_filter
```

```
# echo 0 > /proc/sys/net/ipv4/conf/veth0/rp_filter
```

```
# echo 0 > /proc/sys/net/ipv4/conf/veth1/rp_filter
```

```
# echo 1 > /proc/sys/net/ipv4/conf/veth1/accept_local
```

```
# echo 1 > /proc/sys/net/ipv4/conf/veth0/accept_local
```

好了，我們在 veth0 上來 ping 一下 veth1。這兩個 veth 之間可以通訊了，歐耶！

## ping 192.168.1.2 -I veth0
PING 192.168.1.2 (192.168.1.2) from 192.168.1.1 veth0: 56(84) bytes of data.
64 bytes from 192.168.1.2: `icmp_seq`=1 ttl=64 time=0.019 ms
64 bytes from 192.168.1.2: `icmp_seq`=2 ttl=64 time=0.010 ms
64 bytes from 192.168.1.2: `icmp_seq`=3 ttl=64 time=0.010 ms
...

我在另外一個控制檯上，還啟動了 `tcpdump` 擷取封包，抓到的結果如下。

```
# tcpdump -i veth0
09:59:39.449247 ARP, Request who-has *** tell ***, length 28
09:59:39.449259 ARP, Reply *** is-at 4e:ac:33:e5:eb:16 (oui Unknown), length 28
09:59:39.449262 IP *** > ***: ICMP echo request, id 15841, seq 1, length 64
09:59:40.448689 IP *** > ***: ICMP echo request, id 15841, seq 2, length 64
09:59:41.448684 IP *** > ***: ICMP echo request, id 15841, seq 3, length 64
09:59:42.448687 IP *** > ***: ICMP echo request, id 15841, seq 4, length 64
09:59:43.448686 IP *** > ***: ICMP echo request, id 15841, seq 5, length 64
```

由於兩個裝置之間是首次通訊的，所以 veth0 首先先發出了一個 arp request，veth1 收到後回覆了一個 arp reply。然後接下來就是正常的 ping 命令下的 IP 包了。

## 二、veth 底層建立過程

在上一小節中，我們親手建立了一對 veth 裝置，並透過簡單的設定就可以讓他們之間互相進行通訊了。那麼在本小節中，我們看看在核心裡，veth 到底是如何建立的。

Veth 相關原始碼位於   `drivers/net/veth.c`，其中初始化入口是 `veth_init`。

```
//file: drivers/net/veth.c
static __init int veth_init(void)
{
    return rtnl_link_register(&veth_link_ops);
}
```

在   `veth_init` 中註冊了 `veth_link_ops`（veth 裝置的操作方法），它包含了 veth 裝置的建立、啟動和刪除等回呼函式。

```
//file: drivers/net/veth.c
static struct rtnl_link_ops veth_link_ops = {
    .kind        = DRV_NAME,
    .priv_size    = sizeof(struct veth_priv),
    .setup        = veth_setup,
    .validate    = veth_validate,
    .newlink    = veth_newlink,
    .dellink    = veth_dellink,
    .policy        = veth_policy,
    .maxtype    = VETH_INFO_MAX,
};
```

我們先來看下 veth 裝置的建立函式 `veth_newlink`，這是理解 veth 的關鍵之處。

```
//file: drivers/net/veth.c
static int veth_newlink(struct net *src_net, struct net_device *dev,
             struct nlattr *tb[], struct nlattr *data[])
{
    ...
    //建立
    peer = rtnl_create_link(net, ifname, &veth_link_ops, tbp);

    //註冊
    err = register_netdevice(peer);
    err = register_netdevice(dev);
    ...

    //把兩個裝置關聯到一起
    priv = netdev_priv(dev);
    rcu_assign_pointer(priv->peer, peer);

    priv = netdev_priv(peer);
    rcu_assign_pointer(priv->peer, dev);
}
```

在   `veth_newlink` 中，我們看到它透過 `register_netdevice` 建立了 peer 和 dev 兩個網路虛擬裝置。接下來的 `netdev_priv` 函式回傳的是網路裝置的 private 資料，`priv->peer` 就是一個指標而已。

```
//file: drivers/net/veth.c
struct veth_priv {
    struct net_device __rcu    *peer;
    atomic64_t        dropped;
};
```

兩個新建立出來的裝置 dev 和 peer 透過 `priv->peer` 指標來完成結對。其中 dev 裝置裡的 `priv->peer` 指標指向 peer 裝置， peer 裝置裡的 `priv->peer` 指向 dev。

接著我們再看下 veth 裝置的啟動過程。

```
//file: drivers/net/veth.c
static void veth_setup(struct net_device *dev)
{
    //veth的操作列表，其中包括veth的傳送函式veth_xmit
    dev->netdev_ops = &veth_netdev_ops;
    dev->ethtool_ops = &veth_ethtool_ops;
    ......
}
```

其中 `dev->netdev_ops` = &  `veth_netdev_ops` 這行也比較關鍵。 `veth_netdev_ops` 是 veth 裝置的操作函式。例如傳送過程中呼叫的函式指標 `ndo_start_xmit`，對於 veth 裝置來說就會呼叫到 `veth_xmit`。這個在下一個小節裡我們會用到。

```
//file: drivers/net/veth.c
static const struct net_device_ops veth_netdev_ops = {
    .ndo_init            = veth_dev_init,
    .ndo_open            = veth_open,
    .ndo_stop            = veth_close,
    .ndo_start_xmit      = veth_xmit,
    .ndo_change_mtu      = veth_change_mtu,
    .ndo_get_stats64     = veth_get_stats64,
    .ndo_set_mac_address = eth_mac_addr,
};
```

## 三、veth 網路通訊過程

在 [《25 張圖，一萬字，拆解 Linux 網路封包 (Network Packet) 傳送過程》](https://mp.weixin.qq.com/s/wThfD9th9e_-YGHJJ3HXNQ) 和 [《圖解 Linux 網路封包接收過程》](https://mp.weixin.qq.com/s/GoYDsfy9m0wRoXi_NCfCmg) 兩篇文章中，我們系統介紹了 Linux 網路封包的收發過程。 在 [《127.0.0.1 之本機網路通訊過程知多少 ?》](https://mp.weixin.qq.com/s/6_OfoeD3ZpyQisY2F-4_bw) 中我們又詳細討論了根據迴環裝置 lo 的本機網路 IO 過程。

我們回顧一下根據迴環裝置 lo 的本機網路過程。在傳送階段裡，流程分別是   `send` 系統呼叫 (System Call) => 協定堆疊 => 鄰居子系統 => 網路裝置層 => 驅動。在接收階段裡，流程分別是軟中斷 (Softirq) => 驅動 => 網路裝置層 => 協定堆疊 => 系統呼叫回傳。過程圖示如下：



根據 veth 的網路 IO 過程和上面這個過程圖幾乎完全一樣。和 lo 裝置所不同的就是使用的驅動程式不一樣，馬上我們就能看到。

網路裝置層最後會透過 `ops->ndo_start_xmit` 來呼叫驅動進行真正的傳送。

```
//file: net/core/dev.c
int dev_hard_start_xmit(struct sk_buff *skb, struct net_device *dev,
   struct netdev_queue *txq)
{
    //取得裝置驅動的回呼函式集合 ops
    const struct net_device_ops *ops = dev->netdev_ops;

    //呼叫驅動的 ndo_start_xmit 來進行傳送
    rc = ops->ndo_start_xmit(skb, dev);
    ...
}
```

在 [《127.0.0.1 之本機網路通訊過程知多少 ?》](https://mp.weixin.qq.com/s/6_OfoeD3ZpyQisY2F-4_bw) 一文中，我們提到過對於迴環裝置 lo 來說   `netdev_ops` 是 `loopback_ops`。那麼上面傳送過程中呼叫的 ops-> `ndo_start_xmit` 對應的就是 `loopback_xmit`。

```
//file:drivers/net/loopback.c
static const struct net_device_ops loopback_ops = {
    .ndo_init      = loopback_dev_init,
    .ndo_start_xmit= loopback_xmit,
    .ndo_get_stats64 = loopback_get_stats64,
};
```

回顧本文上一小節中，對於 veth 裝置來說，它在啟動的時候將   `netdev_ops` 設定成了 `veth_netdev_ops`。那 ops-> `ndo_start_xmit` 對應的具體傳送函式就是 `veth_xmit`。這就是在整個傳送的過程中，唯一和 lo 裝置不同的地方所在。我們來簡單看一下這個傳送函式的程式碼。

```
//file: drivers/net/veth.c
static netdev_tx_t veth_xmit(struct sk_buff *skb, struct net_device *dev)
{
    struct veth_priv *priv = netdev_priv(dev);
    struct net_device *rcv;

    //取得 veth 裝置的對端
    rcv = rcu_dereference(priv->peer);

    //呼叫 dev_forward_skb 向對端傳送封包
    if (likely(dev_forward_skb(rcv, skb) == NET_RX_SUCCESS)) {
    }
```

在   `veth_xmit` 中主要就是取得一下當前 veth 裝置，然後向對端把資料傳送過去就行了。傳送到對端裝置的工作是由 `dev_forward_skb` 函式來處理的。

```
//file: net/core/dev.c
int dev_forward_skb(struct net_device *dev, struct sk_buff *skb)
{
    skb->protocol = eth_type_trans(skb, dev);
    ...
    return netif_rx(skb);
}
```

先呼叫了 `eth_type_trans` 將 `skb` 的所屬裝置改為了剛剛取到的 veth 的對端裝置 rcv。

```
//file: net/ethernet/eth.c
__be16 eth_type_trans(struct sk_buff *skb, struct net_device *dev)
{
    skb->dev = dev;
    ...
}

接著呼叫   netif_rx，這塊又和 lo 裝置的操作一樣了。在該方法中最終會執行到 enqueue_to_backlog 中（netif_rx-> netif_rx_internal-> enqueue_to_backlog）。在這裡將要傳送的 skb 插入 softnet_data-> input_pkt_queue 佇列中並呼叫 ____napi_schedule 來觸發軟中斷，見下面的程式碼。
```

```
//file: net/core/dev.c
static int enqueue_to_backlog(struct sk_buff *skb, int cpu,
         unsigned int *qtail)
{
    sd = &per_cpu(softnet_data, cpu);
    __skb_queue_tail(&sd->input_pkt_queue, skb);

    ...
    ____napi_schedule(sd, &sd->backlog);
}
```

```
//file:net/core/dev.c

static inline void ____napi_schedule(struct softnet_data *sd,
                     struct napi_struct *napi)
{
    list_add_tail(&napi->poll_list, &sd->poll_list);
    __raise_softirq_irqoff(NET_RX_SOFTIRQ);
}
```

當資料傳送完喚起軟中斷後，veth 對端的裝置開始接收。 和傳送過程不同的是，所有的虛擬裝置的接收封包   `poll` 函式都是一樣的，都是在裝置層被初始化成了 `process_backlog`。

```
//file:net/core/dev.c
static int __init net_dev_init(void)
{
    for_each_possible_cpu(i) {
        sd->backlog.poll = process_backlog;
    }
}
```

所以 veth 裝置的接收過程和 lo 裝置完全一樣。想再看看這塊過程的同學就請參考 [《127.0.0.1 之本機網路通訊過程知多少 ?》](https://mp.weixin.qq.com/s/6_OfoeD3ZpyQisY2F-4_bw) 一文中的第三節吧。大致流程是   `net_rx_action` 執行到 `deliver_skb`，然後送到協定堆疊中。

|--->`net_rx_action`()
|--->`process_backlog`()
|--->__netif_receive_skb()
|--->__netif_receive_skb_core()
|---> `deliver_skb`

## 總結

由於大部分的同學在日常工作中一般不會接觸到 veth，所以在看到 Docker 相關的技術文中提到這個技術時總會以為它是多麼的高深。

其實從實作上來看，虛擬裝置 veth 和我們日常接觸的 lo 裝置非常非常的像。連根據 veth 的本機網路 IO 通訊圖其實都是我直接從 127.0.0.1 的那篇文章裡複製過來的。只要你看過飛哥的 [127.0.0.1 之本機網路通訊過程知多少 ?！](https://mp.weixin.qq.com/s/6_OfoeD3ZpyQisY2F-4_bw) 這篇，理解起來 veth 簡直不要太容易。

只不過和 lo 裝置相比，veth 是為了虛擬化技術而生的，所以它多了個結對的概念。在建立函式 `veth_newlink` 中，一次性就建立了兩個網路裝置出來，並把對方分別設定成了各自的 peer。在傳送資料的過程中，找到傳送裝置的 peer，然後發起軟中斷讓對方收取就算完事了。

怎麼樣，是不是 So easy!
