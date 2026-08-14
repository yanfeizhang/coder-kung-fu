---
layout: default
title: "理解 iptables 原理"
---

# 理解 iptables 原理

> 原文標題：理解 iptables 原理

> 原文連結：https://mp.weixin.qq.com/s/O084fYzUFk7jAzJ2DDeADg

---

大家好，我是飛哥！
現在 iptables 這個工具的應用似乎是越來越廣了。不僅僅是在傳統的防火牆、NAT 等功能出現，在今天流行的的 Docker、Kubernets、Istio 專案中也經常能見著對它的身影。正因為如此，所以深入理解 iptables 工作原理是非常有價值的事情。

Linux 核心 (Linux Kernel) 網路 (Network) 堆疊是一個純核心態 (Kernel Mode) 的東西，和使用者層功能是天然隔離。但為了迎合各種各樣使用者層不同的需求，核心開放了一些口子出來供使用者干預。使得使用者層可以透過一些設定，改變核心的工作方式，從而實作特殊的需求。

Linux 在核心網路元件中很多關鍵位置佈置了 netfilter 過濾器。Iptables 就是根據 netfilter 來實作的。所以本文中 iptables 和 netfilter 這兩個名詞有時候就混著用了。

飛哥也在網上看過很多關於 netfilter 技術文章，但是我覺得都寫的不夠清晰。所以我們擼起袖子，自己寫一篇。Netfilter 的實作可以簡單地歸納為四表五鏈。我們來詳細看看四表、五鏈究竟是什麼意思。

## 一、Iptables 中的五鏈

Linux 下的 netfilter 在核心協定堆疊的各個重要關卡埋下了五個鉤子。每一個鉤子都對應是一系列規則，以連結串列的形式存在，所以俗稱五鏈。當網路封包 (Network Packet) 在協定堆疊中流轉到這些關卡的時候，就會依次執行在這些鉤子上註冊的各種規則，進而實作對網路封包的各種處理。

要想把五鏈理解好，飛哥認為最關鍵是要把核心接收、傳送、轉發三個過程分開來看。

## 1.1 接收過程

Linux 在網路封包接收在 IP 層的入口函式是   `ip_rcv`。網路在這裡包碰到的第一個 HOOK 就是 PREROUTING。當該鉤子上的規則都處理完後，會進行路由選擇。如果發現是本裝置的網路封包，進入 `ip_local_deliver` 中，在這裡又會遇到 INPUT 鉤子。

我們來看下詳細的程式碼，先看 `ip_rcv`。

```
//file: net/ipv4/ip_input.c
int ip_rcv(struct sk_buff *skb, ......){
    ......
    return NF_HOOK(NFPROTO_IPV4, NF_INET_PRE_ROUTING, skb, dev, NULL,
               ip_rcv_finish);

}
```

`NF_HOOK` 這個函式會執行到 iptables 中 pre\_routing 裡的各種表註冊的各種規則。當處理完後，進入 `ip_rcv_finish`。在這裡函式里將進行路由選擇。這也就是 PREROUTING 這一鏈名字得來的原因，因為是在路由前執行的。

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

如果發現是本地裝置上的接收，會進入   `ip_local_deliver` 函式。接著是又會執行到 `LOCAL_IN` 鉤子，這也就是我們說的 INPUT 鏈。

```
//file: net/ipv4/ip_input.c
int ip_local_deliver(struct sk_buff *skb){
    ......
    return NF_HOOK(NFPROTO_IPV4, NF_INET_LOCAL_IN, skb, skb->dev, NULL,
               ip_local_deliver_finish);

}

簡單總結接收資料的處理流程是：PREROUTING 鏈 -> 路由判斷（是本機）-> INPUT 鏈-> ...
```

## 1.2 傳送過程

Linux 在網路封包傳送的過程中，首先是傳送的路由選擇，然後碰到的第一個 HOOK 就是 OUTPUT，然後接著進入 POSTROUTING 鏈。

來大致過一下原始碼，網路層傳送的入口函式是 `ip_queue_xmit`。

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

在這裡先進行了傳送時的路由選擇，然後進入傳送時的 IP 層函式 \_\_ip_local_out。

```
//file: net/ipv4/ip_output.c
int __ip_local_out(struct sk_buff *skb)
{
    struct iphdr *iph = ip_hdr(skb);

    iph->tot_len = htons(skb->len);
    ip_send_check(iph);
    return nf_hook(NFPROTO_IPV4, NF_INET_LOCAL_OUT, skb, NULL,
               skb_dst(skb)->dev, dst_output);
}
```

上面的   `NF_HOOK` 將傳送資料封包送入到 `NF_INET_LOCAL_OUT`(OUTPUT) 鏈。執行完後，進入 `dst_output`。

```
//file: include/net/dst.h
static inline int dst_output(struct sk_buff *skb)
{
    return skb_dst(skb)->output(skb);
}
```

在這裡取得到之前的選路，並呼叫選到的 output 傳送。將進入 `ip_output`。

```
//file: net/ipv4/ip_output.c
int ip_output(struct sk_buff *skb)
{
    ...

    //再次交給 netfilter，完畢後回呼 ip_finish_output
    return NF_HOOK_COND(NFPROTO_IPV4, NF_INET_POST_ROUTING, skb, NULL, dev,
        ip_finish_output,
        !(IPCB(skb)->flags & IPSKB_REROUTED));
}

總結下傳送資料封包流程是：路由選擇 -> OUTPUT 鏈 -> POSTROUTING 鏈-> ...
```

## 1.3 轉發過程

其實除了接收和傳送過程以外，Linux 核心還可以像路由器一樣來工作。它將接收到網路封包，然後根據路由表選到合適的網路卡 (Network Card) 裝置將其轉發出去。

這個過程中，先是經歷接收資料的前半段。在   `ip_rcv` 中經過 PREROUTING 鏈，然後路由後發現不是本裝置的封包，那就進入 `ip_forward` 函式進行轉發，在這裡又會遇到 FORWARD 鏈。最後還會進入 `ip_output` 進行真正的傳送，遇到 POSTROUTING 鏈。

我們來過一下原始碼，先是進入 IP 層入口 `ip_rcv`，在這裡遇到 PREROUTING 鏈。

```
//file: net/ipv4/ip_input.c
int ip_rcv(struct sk_buff *skb, ......){
    ......
    return NF_HOOK(NFPROTO_IPV4, NF_INET_PRE_ROUTING, skb, dev, NULL,
               ip_rcv_finish);

}
```

PREROUTING 鏈條上的規則都處理完後，進入   `ip_rcv_finish`，在這裡路由選擇，然後進入 `dst_input`。

```
//file: include/net/dst.h
static inline int dst_input(struct sk_buff *skb)
{
    return skb_dst(skb)->input(skb);
}
```

轉發過程的這幾步和接收過程一模一樣的。不過核心路徑就要從上面的 input 方法呼叫開始分道揚鑣了。非本裝置的不會進入   `ip_local_deliver`，而是會進入到 `ip_forward`。

```
//file: net/ipv4/ip_forward.c
int ip_forward(struct sk_buff *skb)
{
    ......
    return NF_HOOK(NFPROTO_IPV4, NF_INET_FORWARD, skb, skb->dev,
               rt->dst.dev, ip_forward_finish);
}
```

在   `ip_forward_finish` 裡會送到 IP 層的傳送函式 `ip_output`。

```
//file: net/ipv4/ip_output.c
int ip_output(struct sk_buff *skb)
{
    ...
    //再次交給 netfilter，完畢後回呼 ip_finish_output
    return NF_HOOK_COND(NFPROTO_IPV4, NF_INET_POST_ROUTING, skb, NULL, dev,
        ip_finish_output,
        !(IPCB(skb)->flags & IPSKB_REROUTED));
}
```

在   `ip_output` 裡會遇到 POSTROUTING 鏈。再後面的流程就和傳送過程的下半段一樣了。

總結下轉發資料過程：PREROUTING -> 路由判斷（不是本裝置，找到下一跳） -> FORWARD -> POSTROUTING -> ...

## 1.4 iptables 彙總

理解了接收、傳送和轉發三個過程以後，讓我們把上面三個流程彙總起來。

資料接收過程走的是 1 和 2，傳送過程走的是 4 、5，轉發過程是 1、3、5。有了這張圖，我們能更清楚地理解 iptables 和核心的關係。

## 二、Iptables 的四表

在上一節中，我們介紹了 iptables 中的五個鏈。在每一個鏈上都可能是由許多個規則組成的。在 `NF_HOOK` 執行到這個鏈的時候，就會把規則按照優先順序挨個過一遍。如果有符合條件的規則，則執行規則對應的動作。

而這些規則根據用途的不同，又可以 raw、mangle、nat 和 filter。

- raw 表的作用是將命中規則的包，跳過其他表的處理，它的優先順序最高。
- mangle 表的作用是根據規則修改資料封包的一些標誌位，例如 TTL
- nat 表的作用是實作網路位址轉換
- filter 表的作用是過濾某些包，這是防火牆工作的基礎

在 PREROUTING 鏈中的規則中，分別可以執行 raw、mangle 和 nat 三種功能。

為什麼不是全部四個表呢，這是由於功能的不同，不是所有功能都會完全使用到五個鏈。

Raw 表目的是跳過其他表，所以只需要在接收和傳送兩大過程的最開頭處把關，所以只需要用到 PREROUTING 和 OUTPUT 兩個鉤子。

Mangle 表有可能會在任意位置都有可能會修改網路封包，所以它是用到了全部的鉤子位置。

NAT 分為 SNAT（Source NAT）和 DNAT（Destination NAT）兩種，可能會工作在 PREROUTING、INPUT、OUTPUT、POSTROUTING 四個位置。

Filter 只在 INPUT、OUTPUT 和 FORWARD 這三步中工作就夠了。

從整體上看，四鏈五表的關係如下圖。

這裡再多說一點，每個命名空間 (Namespace) 都是有自己獨立的 iptables 規則的。我們拿 NAT 來舉例，核心在遍歷 NAT 規則的時候，是從 net（命名空間變數）的 ipv4.  `nat_table` 上取下來的。`NF_HOOK` 最終會執行到 `nf_nat_rule_find` 函式。

```
//file: net/ipv4/netfilter/iptable_nat.c
static unsigned int nf_nat_rule_find(...)
{
    struct net *net = nf_ct_net(ct);
    unsigned int ret;

    //重要!!!!!! nat_table 是在 namespace 中儲存著的
    ret = ipt_do_table(skb, hooknum, in, out, net->ipv4.nat_table);
    if (ret == NF_ACCEPT) {
        if (!nf_nat_initialized(ct, HOOK2MANIP(hooknum)))
            ret = alloc_null_binding(ct, hooknum);
    }
    return ret;
}
```

Docker 容器 (Container) 就是根據命名空間來工作的，所以每個 Docker 容器中都可以設定自己獨立的 iptables 規則

## 三、Iptables 使用舉例

看完前面兩小節，大家已經理解了四表五鏈是如何實作的了。那我們接下來透過幾個實際的功能來看下實踐中是如何使用 iptables 的。

## 3.1 SNAT 源位址替換

假如說我們有一臺 Linux，它的 eth0 的 IP 是 10.162.0.100，透過這個 IP 可以存取另外其他伺服器。現在我們在這臺機器上建立了個 Docker 虛擬網路環境 net1 出來，它的網路卡 veth1 的 IP 是 192.168.0.2。

如果想讓 192.168.0.2 能存取外部網路，則需要宿主網路命名空間 (Network Namespace) 下的裝置工作幫其進行網路封包轉發。由於這是個私有的位址，只有這臺 Linux 認識，所以它是無法存取外部的伺服器的。這個時候如果想要讓 net1 正常存取 10.162.0.101，就必須在轉發時執行 SNAT - 源位址替換。

SNAT 工作在路由之後，網路封包傳送之前，也就是 POSTROUTING 鏈。我們在宿主機的命名空間裡增加如下這條 iptables 規則。這條規則判斷如果源是 192.168.0 網段，且目的不是 br0 的，統統執行源 IP 替換判斷。

```
# iptables -t nat -A POSTROUTING -s 192.168.0.0/24 ! -o br0 -j MASQUERADE
```

有了這條規則，我們來看下整個傳送封包過程。

當資料封包發出來的時候，先從 veth 傳送到 br0。由於 br0 在宿主機的命名空間中，這樣會執行到 POSTROUTING 鏈。在這個鏈有我們剛設定的 snat 規則。 根據這條規則，核心將網路封包中 192.168.0.2（外界不認識） 替換成宿主機的 IP 10.162.0.100（外界都認識）。同時還要跟蹤記錄連結狀態。

然後宿主機根據自己的路由表進行判斷，選擇預設傳送裝置將封包從 eth0 網路卡傳送出去，直到送到 10.162.0.101。

接下來在 10.162.0.100 上會收到來自 10.162.0.101 的響應包。由於上一步記錄過連結跟蹤，所以宿主機能知道這個回包是給 192.168.0.2 的。再反替換並透過 br0 將回傳送達正確的 veth 上。

## 3.2 DNAT 目的位址替換

接著上面小節裡的例子，假設我們想在 192.168.0.2 上提供 80 埠的服務。同樣，外面的伺服器是無法存取這個位址的。這個時候要用到 DNAT 目的位址替換。需要在資料封包進來的時候，將其目的位址替換成 192.168.0.2:80 才行。

DNAT 工作在核心接收到網路封包的第一個鏈中，也就是 PREROUTING。我們增加一條 DNAT 規則， 具體的設定如下。

```
# iptables -t nat -A PREROUTING  ! -i br0 -p tcp -m tcp --dport 8088 -j DNAT --to-destination 192.168.0.2:80
```

當有外界來的網路封包到達 eth0 的時候。由於 eth0 在宿主機的命名空間中，所以會執行到 PREROUTING 鏈。

該規則判斷如果埠是 8088 的 TCP 請求，則將目的位址替換為 192.168.0.2:80。再透過 br0（192.168.0.1）轉發資料封包，資料封包將到達真正提供服務的 192.168.0.2:80 上。

同樣在 DNAT 中也會有連結跟蹤記錄，所以 192.168.0.2 給 10.162.0.101 的回傳封包中的源位址會被替換成 10.162.0.100:8088。之後 10.162.0.101 收到封包，它一直都以為自己是真的和 10.162.0.100:8088 通訊。

這樣 net1 環境中的 veth1 既可以實作存取外網，也可以提供服務給外網使用了。事實上，單機的 Docker 就是透過這兩小節介紹的 SNAT 和 DNAT 設定來進行網路通訊的。

## 3.3 filter

Filter 表主要實作網路封包的過濾。假如我們發現了一個惡意 IP 瘋狂請求我們的伺服器，對服務造成了影響。那麼我們就可以用 filter 把它禁掉。其工作原理就是在接收封包的 INPUT 鏈位置處進行判斷，發現是惡意請求就儘早終止不處理。避免進入到更上層繼續浪費 CPU 開銷。

具體的設定細節如下：

```
# iptables -I INPUT -s 1.2.3.4 -j DROP //封禁
```

```
# iptables -D INPUT -s 1.2.3.4 -j DROP //解封
```

當然也可以封禁某個 IP 段。

```
# iptables -I INPUT -s 121.0.0.0/8 -j DROP //封禁
```

```
# iptables -D INPUT -s 121.0.0.0/8 -j DROP //解封
```

再例如說假設你不想讓別人任意 ssh 登入你的伺服器，只允許你的 IP 存取。那就只放開你自己的 IP，其他的都禁用掉就好了。

```
# iptables -t filter -I INPUT -s 1.2.3.4 -p tcp --dport 22 -j ACCEPT
```

```
# iptables -t filter -I INPUT -p tcp --dport 22 -j DROP
```

## 3.4 raw

Raw 表中的規則可以繞開其他表的處理。 在 nat 表中，為了保證雙向的流量都能正常完成位址替換，會跟蹤並且記錄連結狀態。每一條連線都會有對應的記錄產生。使用以下兩個命令可以檢視。

```
# conntrack -L
```

```
# cat /proc/net/ip_conntrack
```

但在高流量的情況下，可能會有連線跟蹤記錄滿的問題發生。我就遇到過一次在測試單機百萬併發連線的時候，發生因連線數超過了 `nf_conntrack_max` 而導致新連線無法建立的問題。

## `ip_conntrack`: table full, dropping packet

但其實如果不使用 NAT 功能的話，連結跟蹤功能是可以關閉的，例如。

```
# iptables -t raw -A PREROUTING -d 1.2.3.4 -p tcp --dport 80 -j NOTRACK
```

```
# iptables -A FORWARD -m state --state UNTRACKED -j ACCEPT
```

## 3.5 mangle

路由器在轉發網路封包的時候，ttl 值會減 1 ，該值為 0 時，最後一個路由就會停止再轉發這個資料封包。如若不想讓本次路由影響 ttl，便可以在 mangel 表中加個 1，把它給補回來。

## ptables -t mangle -A PREROUTING -i eth0 -j TTL --ttl-inc 1

所有從 eth0 介面進來的資料封包的 ttl 值加 1，以抵消路由轉發預設減的 1。

## 總結

Iptables 是一個非常常用，也非常重要的工具。 Linux 上的防火牆、nat 等基礎功能都是根據它實作的。還有現如今流行的的 Docker、Kubernets、Istio 專案中也經常能見著對它的身影。正因為如此，所以深入理解 iptables 工作原理是非常有價值的事情。

今天我們先是在第一節裡從核心接收、傳送、轉發三個不同的過程理解了五鏈的位置。

接著又根據描述了 iptables 從功能上看的另外一個維度，表。每個表都是在多個鉤子位置處註冊自己的規則。當處理封包的時候觸發規則，並執行。從整體上看，四鏈五表的關係如下圖。

最後我們又分別在 raw、mangle、nat、filter 幾個表上舉了簡單的應用例子。希望透過今天的學習，你能將 iptables 徹底融會貫通。相信這一定會對你的工作有很大的幫助的！
