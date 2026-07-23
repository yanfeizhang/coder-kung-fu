---
layout: default
title: "天天講路由，那 Linux 路由到底咋實現的！？"
---

# 天天講路由，那 Linux 路由到底咋實現的！？

> 原文標題：天天讲路由，那 Linux 路由到底咋实现的！？

> 原文連結：http://kfngxl.cn/index.php/archives/488/

---

大家好，我是飛哥。

容器是一種新的虛擬化技術，每一個容器都是一個[邏輯上獨立的網路環境](https://mp.weixin.qq.com/s/lscMpc5BWAEzjgYw6H0wBw)。Linux 上提供了軟體虛擬出來的二層交換機[Bridge](https://mp.weixin.qq.com/s/JnKz1fUgZmGdvfxOm2ehZg)可以解決同一個宿主機上多個容器之間互連的問題，但這是不夠的。二層交換無法解決容器和宿主機外部網路的互通。

容器肯定是需要和宿主機以外的外部網路互通才具備實用價值的。比如在 Kubernets 中，就要求所有的 pod 之間都可以互通。相當於在原先物理機所組成的網路之上，要再建一個互通的虛擬網路出來。這就是 Overlay 網路的概念，用一個簡單的示例圖表示如下。

回想在傳統物理物理網路中，不同子網之間的伺服器是如何互聯起來的呢，沒錯，就是在三層工作的路由器，也叫閘道器。路由器使得資料包可以從一個子網中傳輸到另一個子網中，進而實現更大範圍的網路互通。如下圖所示，一臺路由器將 192.168.0.x 和 192.168.1.x 兩個子網連線了起來。

在容器虛擬化網路中，自然也需要這麼一個角色，將容器和宿主機以外的網路連線起來。其實 Linux 天生就具備路由的功能，只是在雲原生時代，它的路由功能再一次找到了用武之地。在容器和外部網路通訊的過程中，Linux 就又承擔起路由器的角色，實現容器資料包的正確轉發和投遞。

在各種基於容器的雲原生技術盛行的今天，再次回頭深刻理解路由工作原理顯得非常有必要，而且也非常的有價值。今天，我們就再來強化一下 Linux 上的路由知識！

### 一、什麼時候需要路由

先來聊聊 Linux 在什麼情況下需要路由過程。其實在傳送資料時和接收資料時都會涉及到路由選擇，為什麼？我們挨個來看。

#### 1.1 傳送資料時選路

Linux 之所以在傳送資料包的時候需要進行路由選擇，這是因為伺服器上是可能會有多張網絡卡裝置存在的。資料包在傳送的時候，一路透過使用者態、TCP 層到了 IP 層的時候，就要進行路由選擇，以決定使用哪張網絡卡裝置把資料包送出去。詳細過程參見[25 張圖，一萬字，拆解 Linux 網路包傳送過程](https://mp.weixin.qq.com/s/wThfD9th9e_-YGHJJ3HXNQ)

來大致過一下路由相關原始碼原始碼。網路層傳送的入口函式是 ip_queue_xmit。

```
//file: net/ipv4/ip_output.c
int ip_queue_xmit(struct sk_buff *skb, struct flowi *fl)
{
    // 路由選擇過程
    // 選擇完後記錄路由資訊到 skb 上
    rt = (struct rtable *)__sk_dst_check(sk, 0);
    if (rt == NULL) {
        // 沒有快取則查詢路由項
        rt = ip_route_output_ports(...);
        sk_setup_caps(sk, &rt->dst);
    }
    skb_dst_set_noref(skb, &rt->dst);
    ...
    //傳送
    ip_local_out(skb);
}
```

在 ip_queue_xmit 裡我們開頭就看到了路由項查詢， ip_route_output_ports 這個函式中完成路由選擇。路由選擇就是到路由表中進行匹配，然後決定使用哪個網絡卡傳送出去。

Linux 中最多可以有 255 張路由表，其中預設情況下有 local 和 main 兩張。使用 ip 命令可以檢視路由表的具體配置。拿 local 路由表來舉例。

```
#ip route list table local
local 10.143.x.y dev eth0 proto kernel scope host src 10.143.x.y
local 127.0.0.1 dev lo proto kernel scope host src 127.0.0.1
```

#### 1.2 接收資料時選路

沒錯，接收資料包的時候也需要進行路由選擇。這是因為 Linux 可能會像路由器一樣工作，將收到的資料包透過合適的網絡卡將其轉發出去。

Linux 在 IP 層的接收入口 ip_rcv 執行後呼叫到 ip_rcv_finish。在這裡展開路由選擇。如果發現確實就是本裝置的網路包，那麼就透過 ip_local_deliver 送到更上層的 TCP 層進行處理。

如果路由後發現非本裝置的網路包，那就進入到 ip_forward 進行轉發，最後透過 ip_output 傳送出去。

具體的程式碼如下。

```
//file: net/ipv4/ip_input.c
static int ip_rcv_finish(struct sk_buff *skb){
    ...
    if (!skb_dst(skb)) {
        int err = ip_route_input_noref(skb, iph->daddr, iph->saddr,
                           iph->tos, skb->dev);
        ...
    }
    ...
    return dst_input(skb);
}
```

其中 ip_route_input_noref 就是在進行路由查詢。

```
//file: net/ipv4/route.c
int ip_route_input_noref(struct sk_buff *skb, __be32 daddr, __be32 saddr,
             u8 tos, struct net_device *dev)
{
    ...
    res = ip_route_input_slow(skb, daddr, saddr, tos, dev);
    return res;
}
```

這裡記著 ip_route_input_slow 就行了，後面我們再看。

#### 1.3 linux 路由小結

路由在核心協議棧中的位置可以用如下一張圖來表示。

網路包在傳送的時候，需要從本機的多個網絡卡裝置中選擇一個合適的傳送出去。網路包在接收的時候，也需要進行路由選擇，如果是屬於本裝置的包就往上層送到網路層、傳輸層直到 socket 的接收快取區中。 如果不是本裝置上的包，就選擇合適的裝置將其轉發出去。

### 二、Linux 的路由實現

#### 2.1 路由表

路由表（routing table）在核心原始碼中的另外一個叫法是轉發資訊庫（Forwarding Information Base，FIB）。所以你在原始碼中看到的 fib 開頭的定義基本上就是和路由表相關的功能。

其中路由表本身是用 struct fib_table 來表示的。

```
//file: include/net/ip_fib.h
struct fib_table {
    struct hlist_node    tb_hlist;
    u32            tb_id;
    int            tb_default;
    int            tb_num_default;
    unsigned long        tb_data[0];
};
```

所有的路由表都透過一個 hash - fib_table_hash 來組織和管理。它是放在網路名稱空間 net 下的。這也就說明每個名稱空間都有自己獨立的路由表。

```
//file:include/net/net_namespace.h
struct net {
    struct netns_ipv4    ipv4;
    ...
}

//file: include/net/netns/ipv4.h
struct netns_ipv4 {
    // 所有路由表
    struct hlist_head    *fib_table_hash;

    // netfilter
    ...
}
```

在預設情況下，Linux 只有 local 和 main 兩個路由表。如果核心編譯時支援策略路由，那麼管理員最多可以配置 255 個獨立的路由表。

如果你的伺服器上建立了多個網路名稱空間的話，那麼就會存在多套路由表。以除了預設命名網路空間外，又創了了一個新網路名稱空間的情況為例，路由表在整個核心資料結構中的關聯關係總結如下圖所示。

#### 2.2 路由查詢

在上面的小節中我們看到，傳送過程呼叫 ip_route_output_ports 來查詢路由，接收過程呼叫 ip_route_input_slow 來查詢。 但其實這兩個函式都又最終會呼叫到 fib_lookup 這個核心函式，原始碼如下。

```
//file: net/ipv4/route.c
struct rtable *__ip_route_output_key(struct net *net, struct flowi4 *fl4)
{
    ...
    // 進入 fib_lookup
    if (fib_lookup(net, fl4, &res)) {
    }
}

//file: net/ipv4/route.c
static int ip_route_input_slow(struct sk_buff *skb, __be32 daddr, __be32 saddr,
                   u8 tos, struct net_device *dev)
{
    ...
    // 進入 fib_lookup
    err = fib_lookup(net, &fl4, &res);
}
```

我們來看下 fib_loopup 都幹了啥。為了容易理解，我們只看一下不支援多路由表版本的 fib_lookup。

```
//file: include/net/ip_fib.h
static inline int fib_lookup(struct net *net, const struct flowi4 *flp,
                 struct fib_result *res)
{
    struct fib_table *table;

    table = fib_get_table(net, RT_TABLE_LOCAL);
    if (!fib_table_lookup(table, flp, res, FIB_LOOKUP_NOREF))
        return 0;

    table = fib_get_table(net, RT_TABLE_MAIN);
    if (!fib_table_lookup(table, flp, res, FIB_LOOKUP_NOREF))
        return 0;
    return -ENETUNREACH;
}
```

這個函式就是依次到 local 和 main 表中進行匹配，匹配到後就返回，不會繼續往下匹配。從上面可以看到local 表的優先順序要高於 main 表，如果 local 表中找到了規則，則路由過程就結束了。

這也就是很多同學說為什麼 ping 本機的時候在 eth0 上抓不到包的根本原因。所有命中 local 表的包都會被送往 loopback 設定，不會過 eth0。

### 三、路由的使用方法

#### 3.1 開啟轉發路由

在預設情況下，Linux 上的轉發功能是關閉的，這時候 Linux 發現收到的網路包不屬於自己就會將其丟棄。

但在某些場景下，例如對於容器網路來說，Linux 需要轉發本機上其它網路名稱空間中過來的資料包，需要手工開啟轉發。如下這兩種方法都可以。

```
# sysctl -w net.ipv4.ip_forward=1
# sysctl net.ipv4.conf.all.forwarding=1
```

開啟後，Linux 就能像路由器一樣對不屬於本機（嚴格地說是本網路名稱空間）的 IP 資料包進行路由轉發了。

#### 3.2 檢視路由表

在預設情況下，Linux 只有 local 和 main 兩個路由表。如果核心編譯時支援策略路由，那麼管理員最多可以配置 255 個獨立的路由表。 在 centos 上可以透過以下方式檢視是否開啟了 CONFIG_IP_MULTIPLE_TABLES 多路由表支援。

```
# cat /boot/config-3.10.0-693.el7.x86_64
CONFIG_IP_MULTIPLE_TABLES=y
...
```

所有的路由表按照從 0 - 255 進行編號，每個編號都有一個別名。編號和別名的對應關係在 /etc/iproute2/rt_tables 這個檔案裡可以查到。

```
# cat /etc/iproute2/rt_tables
255     local
254     main
253     default
0       unspec
200     eth0_table
```

檢視某個路由表的配置，透過使用 ip route list table {表名} 來檢視。

```
#ip route list table local
local 10.143.x.y dev eth0 proto kernel scope host src 10.143.x.y
local 127.0.0.1 dev lo proto kernel scope host src 127.0.0.1
```

如果是檢視 main 路由表，也可以直接使用 route 命令

```
# route -n
Kernel IP routing table
Destination     Gateway         Genmask         Flags Metric Ref    Use Iface
10.0.0.0        10.*.*.254      255.0.0.0       UG    0      0        0 eth0
10.*.*.0        0.0.0.0         255.255.248.0   U     0      0        0 eth0
```

上面欄位中的含義如下

- Destination： 目的地址，可以是一個具體的 IP，也可以是一個網段，和 Genmask 一起表示。

- Gateway：閘道器地址，如果是 0.0.0.0 表示不需要經過閘道器。

- Flags: U 表示有效，G 表示連線路由，H 這條規則是主機路由，而不是網路路由。

- Iface：網絡卡裝置，使用哪個網絡卡將包送過去。

上述結果中輸出的第一條路由規則表示這臺機器下，確切地說這個網路環境下，所有目標為 10.0.0.0/8（Genmask 255.0.0.0 表示前 8 位為子網掩碼） 網段的網路包都要透過 eth0
裝置送到 10...254 這個閘道器，由它再幫助轉發。

第二條路由規則表示，如果目的地址是 10...0/21（Genmask 255.255.248.0 表示前 21 位為子網掩碼）則直接透過 eth0 發出即可，不需要經過閘道器就可通訊。

#### 3.3 修改路由表

預設的 local 路由表是核心根據當前機器的網絡卡裝置配置自動生成的，不需要手工維護。對於main 的路由表配置我們一般只需要使用 route add 命令就可以了，刪除使用 route del。

修改主機路由

```
# route add -host 192.168.0.100 dev eth0 //直連不用閘道器
# route add -host 192.168.1.100 dev eth0 gw 192.168.0.254 //下一跳閘道器
```

修改網路路由

```
# route add -net 192.168.1.0/24 dev eth0 //直連不用閘道器
# route add -net 192.168.1.0/24 dev eth0 gw 10.162.132.110 //下一跳閘道器
```

也可以指定一條預設規則，不命中其它規則的時候會執行到這條。

```
# route add default gw 192.168.0.1 eth0
```

對於其它編號的路由表想要修改的話，就需要使用 ip route 命令了。這裡不過多展開，只用 main 表舉一個例子，有更多使用需求的同學請自行搜尋。

```
# ip route add 192.168.5.0/24 via 10.*.*.110 dev eth0 table main
```

#### 3.4 路由規則測試

在配置了一系列路由規則後，為了快速校驗是否符合預期，可以透過 ip route get 命令來確認。

```
# ip route get 192.168.2.25
192.168.2.25 via 10.*.*.110 dev eth0 src 10.*.*.161
    cache
```

### 本文總結

在現如今各種網路虛擬化技術裡，到處都能看著對路由功能的靈活應用。所以我們今天專門深入研究了一下 Linux 路由工作原理。

在 Linux 核心中，對於傳送過程和接收過程都會涉及路由選擇，其中接收過程的路由選擇是為了判斷是該本地接收還是將它轉發出去。

預設有 local 和 main 兩個路由表，不過如果安裝的 linux 開啟了 CONFIG_IP_MULTIPLE_TABLES 選項的話，最多能支援 255 張路由表。

路由選擇過程其實不復雜，就是根據各個路由表的配置找到合適的網絡卡裝置，以及下一跳的地址，然後把包轉發出去就算是完事。

透過合適地配置路由規則，容器中的網路環境和外部的通訊不再是難事。透過大量地干預路由規則就可以實現虛擬網路互通。

Github：[https://github.com/yanfeizhang/coder-kung-fu](https://github.com/yanfeizhang/coder-kung-fu)
關注公眾號：微信掃描下方二維碼

我打造了一套更為完整的影片課程。總共包括了硬體原理、記憶體管理、程序管理、網路管理、檔案系統、容器原理、效能觀測、效能最佳化、呼叫原理、等幾個大部分的內容。

其中有 5 個大部分已經更新完畢，下圖中彩色部分為已更新內容，總時長 1900 多分鐘。

剩下各個大部分還在持續更新中。

除了影片內容，星球裡我也給大家開放各種電子 PDF、PPT 的下載。

目前我給大家仍然還開放 200 元的大額優惠券。加入後一年是 299 元。後期再續費目前是 6 折，也是 299。直接使用下方優惠券加入即可看上面的這些影片內容。
