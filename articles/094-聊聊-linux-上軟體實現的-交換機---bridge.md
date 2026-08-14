---
layout: default
title: "聊聊 Linux 上軟體實作的「交換機」 - Bridge！"
---

# 聊聊 Linux 上軟體實作的「交換機」 - Bridge！

> 原文標題：聊聊 Linux 上软件实现的“交换机” - Bridge！

> 原文連結：https://mp.weixin.qq.com/s/JnKz1fUgZmGdvfxOm2ehZg

---

大家好，我是飛哥！

Linux 中的 veth 是一對能互相連線、互相通訊的虛擬網路卡 (Network Card)。透過使用它，我們可以讓 Docker 容器 (Container) 和宿主機通訊，或者是在兩個 Docker 容器中進行交流。參見 [《輕鬆理解 Docker 網路 (Network) 虛擬化基礎之 veth 裝置！》](https://mp.weixin.qq.com/s/sSQFINJ8RO8Nc4XtcyQIjQ)。

不過在實際中，我們會想在一臺實體主機上我們虛擬出來幾個、甚至幾十個的容器，以求得充分壓榨實體主機的硬體資源。但這樣帶來的問題是大量的容器之間的網路互聯。很明顯上面簡單的 veth 互聯方案是沒有辦法直接工作的，我們該怎麼辦？？？

回頭想一下，在實體主機的網路環境中，多臺不同的實體主機之間是如何連線一起互相通訊的呢？ 沒錯，那就是乙太網交換機。同一網路內的多臺實體主機透過交換機連在一起，然後它們就可以相互通訊了。

在我們的網路虛擬化環境裡，和實體網路中的交換機一樣，也需要這樣的一個軟體實作的裝置。它需要有很多個虛擬埠，能把更多的虛擬網路卡連線在一起，透過自己的轉發功能讓這些虛擬網路卡之間可以通訊。
在 Linux 下這個軟體實作交換機的技術就叫做 bridge（再強調下，這是純軟體實作的）。

各個 Docker 容器都透過 veth 連線到 bridge 上，bridge 負責在不同的「埠」之間轉發資料封包。這樣各個 Docker 之間就可以互相通訊了！

今天我們來展開聊聊 bridge 的詳細工作過程。

## 一、如何使用 bridge

在分析它的工作原理之前，很有必要先來看一看網橋是如何使用的。

為了方便大家理解，接下來我們透過動手實踐的方式，在一臺 Linux 上建立一個小型的虛擬網路出來，並讓它們之間互相通訊。

## 1.1 建立兩個不同的網路

Bridge 是用來連線兩個不同的虛擬網路的，所以在準備實驗 bridge 之前我們得先需要用 net namespace 構建出兩個不同的網路空間來。

具體的建立過程如下。我們透過 ip netns 命令建立 net namespace。首先建立一個 net1：

```
# ip netns add net1
```

接下來建立一對 veth 出來，裝置名分別是 veth1 和 `veth1_p`。並把其中的一頭 veth1 放到這個新的 netns 中。

```
# ip link add veth1 type veth peer name veth1_p
```

```
# ip link set veth1 netns net1
```

因為我們打算是用這個 veth1 來通訊，所以需要為其設定上 ip，並把它啟動起來。

```
# ip netns exec net1 ip addr add 192.168.0.101/24 dev veth1
```

```
# ip netns exec net1 ip link set veth1 up
```

檢視一下，上述的設定是否成功。

```
# ip netns exec net1 ip link list
```

```
# ip netns exec net1 ifconfig
```

重複上述步驟，在建立一個新的 netns 出來，命名分別為。

- netns: net2
- veth pair: veth2, `veth2_p`
- ip: 192.168.0.102

好了，這樣我們就在一臺 Linux 就建立出來了兩個虛擬的網路環境。

## 1.2 把兩個網路連線到一起

在上一個步驟中，我們只是建立出來了兩個獨立的網路環境而已。這個時候這兩個環境之間還不能互相通訊。我們需要建立一個虛擬交換機 - bridge， 來把這兩個網路環境連起來。

建立過程如下。建立一個 bridge 裝置, 把剛剛建立的兩對 veth 中剩下的兩頭「插」到 bridge 上來。

## brctl addbr br0
```
# ip link set dev veth1_p master br0
```

```
# ip link set dev veth2_p master br0
```

```
# ip addr add 192.168.0.100/24 dev br0
```

再為 bridge 設定上 IP，並把 bridge 以及插在其上的 veth 啟動起來。

```
# ip link set veth1_p up
```

```
# ip link set veth2_p up
```

```
# ip link set br0 up
```

檢視一下當前 bridge 的狀態，確認剛剛的操作是成功了的。

## brctl show
bridge name     bridge id               STP enabled     interfaces
br0             8000.4e931ecf02b1       no              `veth1_p`
`veth2_p`

## 1.3 網路連通測試

激動人心的時刻就要到了，我們在 net1 裡（透過指定 ip netns exec net1 以及 -I veth1），ping 一下 net2 裡的 IP（192.168.0.102）試試。

```
# ip netns exec net1 ping 192.168.0.102 -I veth1
PING 192.168.0.102 (192.168.0.102) from 192.168.0.101 veth1: 56(84) bytes of data.
64 bytes from 192.168.0.102: icmp_seq=1 ttl=64 time=0.037 ms
64 bytes from 192.168.0.102: icmp_seq=2 ttl=64 time=0.008 ms
64 bytes from 192.168.0.102: icmp_seq=3 ttl=64 time=0.005 ms
```

哇塞，通了通了！！

這樣，我們就在一臺 Linux 上虛擬出了 net1 和 net2 兩個不同的網路環境。我們還可以按照這種方式建立更多的網路，都可以透過一個 bridge 連線到一起。這就是 Docker 中網路系統工作的基本原理。

## 二、Bridge 是如何建立出來的

在核心中，bridge 是由兩個相鄰儲存的核心物件來表示的。

我們先看下它是如何被建立出來的。核心中建立 bridge 的關鍵程式碼在 `br_add_bridge` 這個函式里。

```
//file:net/bridge/br_if.c
int br_add_bridge(struct net *net, const char *name)
{
    //申請網橋裝置，並用 br_dev_setup 來啟動它
    dev = alloc_netdev(sizeof(struct net_bridge), name,
               br_dev_setup);

    dev_net_set(dev, net);
    dev->rtnl_link_ops = &br_link_ops;

    //註冊網橋裝置
    res = register_netdev(dev);
    if (res)
        free_netdev(dev);
    return res;
}
```

上述程式碼中註冊網橋的關鍵程式碼是   `alloc_netdev` 這一行。在這個函式里，將申請網橋的核心物件 `net_device`。在這個函式呼叫裡要注意兩點。

- 1. 第一個參數傳入了 struct   `net_bridge` 的大小
- 2. 第三個參數傳入的 `br_dev_setup` 是一個函式。

帶著這兩點注意事項，我們進入到 `alloc_netdev` 的實作中。

```
//file: include/linux/netdevice.h
#define alloc_netdev(sizeof_priv, name, setup) \
    alloc_netdev_mqs(sizeof_priv, name, setup, 1, 1)
```

好吧，竟然是個宏。那就得看 `alloc_netdev_mqs` 了。

```
//file: net/core/dev.c
struct net_device *alloc_netdev_mqs(int sizeof_priv, ...，void (*setup)(struct net_device *))
{
    //申請網橋裝置
    alloc_size = sizeof(struct net_device);
    if (sizeof_priv) {
        alloc_size = ALIGN(alloc_size, NETDEV_ALIGN);
        alloc_size += sizeof_priv;
    }

    p = kzalloc(alloc_size, GFP_KERNEL);
    dev = PTR_ALIGN(p, NETDEV_ALIGN);

    //網橋裝置初始化
    dev->... = ...;
    setup(dev); //setup是一個函式指標，實際使用的是 br_dev_setup

    ...
}
```

在上述程式碼中。kzalloc 是用來在核心態 (Kernel Mode) 申請核心記憶體 (Memory) 的。需要注意的是，申請的記憶體大小是一個 struct   `net_device` 再加上一個 struct `net_bridge`（第一個參數傳進來的）。一次性就申請了兩個核心物件，這說明 bridge 在核心中是由兩個核心資料結構來表示的，分別是 struct `net_device` 和 struct `net_bridge`。

申請完了一家緊接著呼叫 setup，這實際是外部傳入的 `br_dev_setup` 函式。在這個函式內部進行進一步的初始化。

```
//file: net/bridge/br_device.c
void br_dev_setup(struct net_device *dev)
{
    struct net_bridge *br = netdev_priv(dev);
    dev->... = ...;
    br->... = ...;
    ...
}
```

總之，brctl addbr br0 命令主要就是完成了 bridge 核心物件（struct   `net_device` 和 struct `net_bridge`）的申請以及初始化。

## 三、新增裝置

呼叫 brctl addif br0 veth0 給網橋新增裝置的時候，會將 veth 裝置以虛擬的方式連到網橋上。當新增了若干個 veth 以後，核心中物件的大概邏輯圖如下。

其中 veth 是由 struct   `net_device` 來表示，bridge 的虛擬插口是由 struct `net_bridge_port` 來表示。我們接下來看看原始碼，是如何達成上述的邏輯結果的。

新增裝置會呼叫到   `net/bridge/br_if.c` 下面的 `br_add_if`。

```
//file: net/bridge/br_if.c
int br_add_if(struct net_bridge *br, struct net_device *dev)
{
    // 申請一個 net_bridge_port
    struct net_bridge_port *p;
    p = new_nbp(br, dev);

    // 註冊裝置幀接收函式
    err = netdev_rx_handler_register(dev, br_handle_frame, p);

    // 新增到 bridge 的已用埠列表裡
    list_add_rcu(&p->list, &br->port_list);

    ......
}
```

這個函式中的第二個參數 dev 傳入的是要新增的裝置。在本文中，就可以認為是 veth 的其中一頭。比較關鍵的是   `net_bridge_port` 這個結構體，它模擬的是實體交換機上的一個插口。它起到一個連線的作用，把 veth 和 bridge 給連線了起來。見 `new_nbp` 原始碼如下：

```
//file: net/bridge/br_if.c
static struct net_bridge_port *new_nbp(struct net_bridge *br,
                       struct net_device *dev)
{
    //申請插口物件
    struct net_bridge_port *p;
    p = kzalloc(sizeof(*p), GFP_KERNEL);

    //初始化插口
    index = find_portno(br);
    p->br = br;
    p->dev = dev;
    p->port_no = index;
    ...
}
```

在   `new_nbp` 中，先是申請了代表插口的核心物件。`find_portno` 是在當前 bridge 下尋找一個可用的埠號。接下來插口物件透過 `p->br` = br 和 bridge 裝置關聯了起來，透過 `p->dev` = dev 和代表 veth 裝置的 dev 物件也建立了聯絡。

在   `br_add_if` 中還呼叫 `netdev_rx_handler_register` 註冊了裝置幀接收函式，設定 veth 上的 `rx_handler` 為 `br_handle_frame`。後面在接收封包的時候會回呼到它。

```
//file:
int netdev_rx_handler_register(struct net_device *dev,
                   rx_handler_func_t *rx_handler,
                   void *rx_handler_data)
{
    ...
    rcu_assign_pointer(dev->rx_handler_data, rx_handler_data);
    rcu_assign_pointer(dev->rx_handler, rx_handler);
}
```

## 四、資料封包處理過程

在 [圖解 Linux 網路封包 (Network Packet) 接收過程](https://mp.weixin.qq.com/s/GoYDsfy9m0wRoXi_NCfCmg) 中我們講到過接收封包的完整流程。資料封包會被網路卡先送到 RingBuffer 中，然後依次經過硬中斷 (Hardware Interrupt)、軟中斷 (Softirq) 處理。在軟中斷中再依次把封包送到裝置層、協定堆疊，最後喚醒應用程式。

不過，拿 veth 裝置來舉例，如果它連線到了網橋上的話，在裝置層的 \_\_netif_receive_skb_core 函式中和上述過程有所不同。連在 bridge 上的 veth 在收到資料封包的時候，不會進入協定堆疊，而是會進入網橋處理。網橋找到合適的轉發口（另一個 veth），透過這個 veth 把資料轉發出去。工作流程如下圖。

我們從 `veth1_p` 裝置的接收看起，所有的裝置的接收都一樣，都會進入 \_\_netif_receive_skb_core 裝置層的關鍵函式。

```
//file: net/core/dev.c
static int __netif_receive_skb_core(struct sk_buff *skb, bool pfmemalloc)
{
    ...

    // tcpdump 擷取封包點
    list_for_each_entry_rcu(...);

    // 執行裝置的 rx_handler（也就是 br_handle_frame）
    rx_handler = rcu_dereference(skb->dev->rx_handler);
    if (rx_handler) {
        switch (rx_handler(&skb)) {
        case RX_HANDLER_CONSUMED:
            ret = NET_RX_SUCCESS;
            goto unlock;
        }
    }

    // 送往協定堆疊
    //...

unlock:
    rcu_read_unlock();
out:
    return ret;
}
```

在 \_\_netif_receive_skb_core 中先是過了   `tcpdump` 的擷取封包點，然後查詢和執行了 `rx_handler`。在上面小節中我們看到，把 veth 連線到網橋上的時候，veth 對應的核心物件 dev 中的 `rx_handler` 被設定成了 `br_handle_frame`。所以連線到網橋上的 veth 在收到封包的時候，會將幀送入到網橋處理函式   `br_handle_frame` 中。

另外要注意的是網橋函式處理完的話，一般來說就 goto unlock 退出了。和普通的網路卡資料封包接收相比，並不會往下再送到協定堆疊了。

接著來看下網橋是怎麼工作的吧，進入到 `br_handle_frame` 中來搜尋。

```
//file: net/bridge/br_input.c
rx_handler_result_t br_handle_frame(struct sk_buff **pskb)
{
    ...

forward:
    NF_HOOK(NFPROTO_BRIDGE, NF_BR_PRE_ROUTING, skb, skb->dev, NULL,
            br_handle_frame_finish);
}
```

上面我對   `br_handle_frame` 的邏輯進行了充分的簡化，簡化後它的核心就是呼叫 `br_handle_frame_finish`。同樣 `br_handle_frame_finish` 也有點小複雜。本文中，我們主要想了解的 Docker 場景下 bridge 上的 veth 裝置轉發。所以根據這個場景，我又對該函式進行了充分的簡化。

```
//file: net/bridge/br_input.c
int br_handle_frame_finish(struct sk_buff *skb)
{
    // 取得 veth 所連線的網橋埠、以及網橋裝置
    struct net_bridge_port *p = br_port_get_rcu(skb->dev);
    br = p->br;

    // 更新和查詢轉發表
    struct net_bridge_fdb_entry *dst;
    br_fdb_update(br, p, eth_hdr(skb)->h_source, vid);
    dst = __br_fdb_get(br, dest, vid)

    // 轉發
    if (dst) {
        br_forward(dst->dst, skb, skb2);
    }
}
```

在硬體中，交換機和集線器的主要區別就是它會智慧地把資料送到正確的埠上去，而不會像集線器那樣給所有的埠都群發一遍。所以在上面的函式中，我們看到了更新和查詢轉發表的邏輯。這就是網橋在學習，它會根據它的自學習結果來工作。

在找到要送往的埠後，下一步就是呼叫 `br_forward` => \_\_br_forward 進入真正的轉發流程。

```
//file: net/bridge/br_forward.c
static void __br_forward(const struct net_bridge_port *to, struct sk_buff *skb)
{
    // 將 skb 中的 dev 改成新的目的 dev
    skb->dev = to->dev;

    NF_HOOK(NFPROTO_BRIDGE, NF_BR_FORWARD, skb, indev, skb->dev,
        br_forward_finish);
}
```

在 \_\_br_forward 中，將 `skb` 上的裝置 dev 改為了新的目的 dev。

然後呼叫   `br_forward_finish` 進入傳送流程。 在 `br_forward_finish` 裡會依次呼叫 `br_dev_queue_push_xmit`、 `dev_queue_xmit`。

```
//file: net/bridge/br_forward.c
int br_forward_finish(struct sk_buff *skb)
{
    return NF_HOOK(NFPROTO_BRIDGE, NF_BR_POST_ROUTING, skb, NULL, skb->dev,
               br_dev_queue_push_xmit);
}
int br_dev_queue_push_xmit(struct sk_buff *skb)
{
dev_queue_xmit(skb);
...
}
```

`dev_queue_xmit` 就是傳送函式，在上一篇 [《輕鬆理解 Docker 網路虛擬化基礎之 veth 裝置！》](https://mp.weixin.qq.com/s/sSQFINJ8RO8Nc4XtcyQIjQ) 中我們介紹過，後續的傳送過程就是 `dev_queue_xmit` => `dev_hard_start_xmit` => `veth_xmit`。 在   `veth_xmit` 中會取得到當前 veth 的對端，然後把資料給它傳送過去。

至此，bridge 上的轉發流程就算是完畢了。要注意到的是，整個 bridge 的工作的原始碼都是在 `net/core/dev.c` 或 `net/bridge` 目錄下。都是在裝置層工作的。這也就充分印證了我們經常說的 bridge（實體交換機也一樣） 是二層上的裝置。

接下來，收到網橋發過來資料的 veth 會把資料封包傳送給它的對端 veth2，veth2 再開始自己的資料封包接收流程。
)

## 五、總結

所謂網路虛擬化，其實用一句話來概括就是用軟體來模擬實作真實的實體網路連線。

Linux 核心 (Linux Kernel) 中的 bridge 模擬實作了實體網路中的交換機的角色。和實體網路類似，可以將虛擬裝置插入到 bridge 上。不過和實體網路有點不一樣的是，一對 veth 插入 bridge 的那端其實就不是裝置了，可以理解為退化成了一個網線插頭。

當 bridge 接入了多對 veth 以後，就可以透過自身實作的網路封包轉發的功能來讓不同的 veth 之間互相通訊了。

回到 Docker 的使用場景上來舉例，完整的 Docker 1 和 Docker 2 通訊的過程是這樣的：

大致步驟是：

- 1. Docker1 往 veth1 上傳送資料
- 2. 由於 `veth1_p` 是 veth1 的 pair， 所以這個虛擬裝置上可以收到封包
- 3. veth 收到封包以後發現自己是連在網橋上的，於是乎進入網橋處理。在網橋裝置上尋找要轉發到的埠，這時找到了   `veth2_p` 開始傳送。網橋完成了自己的轉發工作
- 4. veth2 作為 `veth2_p` 的對端，收到了資料封包
- 5. Docker2 裡的就可以從 veth2 裝置上收到資料了

覺得這個流程圖還不過癮？那我們再繼續拉大視野，從兩個 Docker 的使用者態 (User Mode) 來開始看一看。

Docker 1 在需要傳送資料的時候，先透過   `send` 系統呼叫 (System Call) 傳送，這個傳送會執行到協定堆疊進行協定頭的封裝等處理。經由鄰居子系統找到要使用的裝置（veth1）後，從這個裝置將資料傳送出去，veth1 的對端 `veth1_p` 會收到資料封包。

收到資料的   `veth1_p` 是一個連線在 bridge 上的裝置，這時候 bridge 會接管該 veth 的資料接收過程。從自己連線的所有裝置中查詢目的裝置。找到 `veth2_p` 以後，呼叫該裝置的傳送函式將資料傳送出去。同樣 `veth2_p` 的對端 veth2 即將收到資料。

其中 veth2 收到資料後，將和 lo、eth0 等裝置一樣，進入正常的資料接收處理過程。Docker 2 中的使用者態程序 (Process) 將能夠收到 Docker 1 傳送過來的資料了就。
