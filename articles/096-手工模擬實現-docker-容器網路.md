---
layout: default
title: "手工模擬實作 Docker 容器 (Container) 網路 (Network)！"
---

# 手工模擬實作 Docker 容器 (Container) 網路 (Network)！

> 原文標題：手工模拟实现 Docker 容器网络！

> 原文連結：https://mp.weixin.qq.com/s/Arcz3RWe_o0Ijw6uPWKdVw

---

大家好，我是飛哥！

如今伺服器虛擬化技術已經發展到了深水區。現在業界已經有很多公司都遷移到容器上了。我們的開發寫出來的程式碼大機率是要執行在容器上的。因此深刻理解容器網路的工作原理非常的重要。這有這樣將來遇到問題的時候才知道該如何下手處理。

網路虛擬化，其實用一句話來概括就是用軟體來模擬實作真實的實體網路連線。例如 Docker 就是用純軟體的方式在宿主機上模擬出來的獨立網路環境。我們今天來徒手打造一個虛擬網路，
實作在這個網路裡存取外網資源，同時監聽埠提供對外服務的功能。

看完這一篇後，相信你對 Docker 虛擬網路能有進一步的理解。好了，我們開始！

## 一、基礎知識回顧

## 1.1 veth、bridge 與 namespace

Linux 下的 veth 是一對虛擬網路卡 (Network Card) 裝置，和我們常見的 lo 很類似。在這裡裝置裡，從一端傳送資料後，核心會尋找該裝置的另一半，所以在另外一端就能收到。不過 veth 只能解決一對一通訊的問題。詳情參見 [輕鬆理解 Docker 網路虛擬化基礎之 veth 裝置！](https://mp.weixin.qq.com/s/sSQFINJ8RO8Nc4XtcyQIjQ)

如果有很多對 veth 需要互相通訊的話，就需要引入 bridge 這個虛擬交換機。各個 veth 配對可以把一頭連線在 bridge 的介面上，bridge 可以和交換機一樣在埠之間轉發資料，使得各個埠上的 veth 都可以互相通訊。

Namespace 解決的是隔離性的問題。每個虛擬網路卡裝置、程序 (Process)、  `socket`、路由表等等網路堆疊相關的物件預設都是歸屬在 `init_net` 這個預設的 namespace 中的。不過我們希望不同的虛擬化環境之間是隔離的，用 Docker 來舉例，那就是不能讓 A 容器用到 B 容器的裝置、路由表、 `socket` 等資源，甚至連看一眼都不可以。只有這樣才能保證不同的容器之間複用資源的同時，還不會影響其他容器的正常執行。

透過 veth、namespace 和 bridge 我們在一臺 Linux 上就能虛擬多個網路環境出來。而且它們之間、和宿主機之間都可以互相通訊。

關於這三個技術的詳情，可以參考下面這三篇文章：

- [輕鬆理解 Docker 網路虛擬化基礎之 veth 裝置！](https://mp.weixin.qq.com/s/sSQFINJ8RO8Nc4XtcyQIjQ)
- [聊聊 Linux 上軟體實作的「交換機」 - Bridge！](https://mp.weixin.qq.com/s/JnKz1fUgZmGdvfxOm2ehZg)
- [徹底弄懂 Linux 網路命名空間 (Network Namespace)](https://mp.weixin.qq.com/s/lscMpc5BWAEzjgYw6H0wBw)

但是這三篇文章過後，我們還剩下一個問題沒有解決，那就是虛擬網路環境和外部網路的通訊。還拿 Docker 容器來舉例，你啟動的容器裡的服務肯定是需要存取外部的資料庫的。還有就是可能需要暴露例如 80 埠對外提供服務。例如在 Docker 中我們透過下面的命令將容器的 80 埠上的 web 服務要能被外網存取的到。

我們今天的文章主要就是解決這兩個問題的，一是從虛擬網路中存取外網，二是在虛擬網路中提供服務供外網使用。解決它們需要用到路由和 nat 技術。

## 1.2 路由選擇

Linux 是在傳送資料封包的時候，會涉及到路由過程。這個傳送資料封包既包括本機傳送資料封包，也包括途徑當前機器的資料封包的轉發。

先來看本機傳送資料封包。其中本機傳送在
[25 張圖，一萬字，拆解 Linux 網路封包 (Network Packet) 傳送過程](https://mp.weixin.qq.com/s/wThfD9th9e_-YGHJJ3HXNQ) 這一篇我們討論過。

所謂路由其實很簡單，就是該選擇哪張網路卡將資料寫進去。到底該選擇哪張網路卡呢，規則都是在路由表中指定的。Linux 中可以有多張路由表，最重要和常用的是 local 和 main。

local 路由表中統一記錄本地，確切的說是本網路命名空間中的網路卡裝置 IP 的路由規則。

```
# ip route list table local
local 10.143.x.y dev eth0 proto kernel scope host src 10.143.x.y
local 127.0.0.1 dev lo proto kernel scope host src 127.0.0.1
```

其他的路由規則，一般都是在 main 路由表中記錄著的。可以用 ip route list table local 檢視，也可以用更簡短的  route-n

再看途徑當前機器的資料封包的轉發。除了本機傳送以外，轉發也會涉及路由過程。如果 Linux 收到資料封包以後發現目的位址並不是本地的位址的話，就可以選擇把這個資料封包從自己的某個網路卡裝置上轉發出去。這個時候和本機傳送一樣，也需要讀取路由表。根據路由表的設定來選擇從哪個裝置將封包轉走。

不過值得注意的是，Linux 上轉發功能預設是關閉的。也就是發現目的位址不是本機 IP 位址預設是將封包直接丟棄。需要做一些簡單的設定，然後 Linux 才可以幹像路由器一樣的工作，實作資料封包的轉發。

## 1.3 iptables 與 NAT

Linux 核心 (Linux Kernel) 網路堆疊在執行上基本上是一個純核心態 (Kernel Mode) 的東西，但為了迎合各種各樣使用者層不同的需求，核心開放了一些口子出來供使用者層來干預。其中 iptables 就是一個非常常用的干預核心行為的工具，它在核心裡埋下了五個鉤子入口，這就是俗稱的五鏈。

Linux 在接收資料的時候，在 IP 層進入
`ip_rcv` 中處理。再執行路由判斷，發現是本機的話就進入 `ip_local_deliver` 進行本機接收，最後送往 TCP 協定層。在這個過程中，埋了兩個 HOOK，第一個是 `PRE_ROUTING`。這段程式碼會執行到 iptables 中 pre\_routing 裡的各種表。發現是本地接收後接著又會執行到  `LOCAL_IN`，這會執行到 iptables 中設定的 input 規則。

在傳送資料的時候，查詢路由表找到出口裝置後，依次透過 \_\_ip_local_out、  `ip_output` 等函式將封包送到裝置層。在這兩個函式中分別過了 OUTPUT 和 PREROUTING 開的各種規則。

如果是轉發過程，Linux 收到資料封包發現不是本機的封包可以透過查詢自己的路由表找到合適的裝置把它轉發出去。那就先是在  `ip_rcv` 中將封包送到 `ip_forward` 函式中處理，最後在 `ip_output` 函式中將封包轉發出去。在這個過程中分別過了 PREROUTING、FORWARD 和 POSTROUTING 三個規則。

綜上所述，iptables 裡的五個鏈在核心網路模組中的位置就可以歸納成如下這幅圖。

資料接收過程走的是 1 和 2，傳送過程走的是 4 、5，轉發過程是 1、3、5。有了這張圖，我們能更清楚地理解 iptable 和核心的關係。

在 iptables 中，根據實作的功能的不同，又分成了四張表。分別是 raw、mangle、nat 和 filter。其中 nat 表實作我們常說的 NAT（Network AddressTranslation） 功能。 其中 nat 又分成 SNAT（Source NAT）和 DNAT（Destination NAT）兩種。

SNAT 解決的是內網位址存取外部網路的問題。它是透過在 POSTROUTING 裡修改來源 IP 來實作的。

DNAT 解決的是內網的服務要能夠被外部存取到的問題。它在透過 PREROUTING 修改目標 IP 實作的。

## 二、 實作虛擬網路外網通訊

根據以上的基礎知識，我們用純手工的方式搭建一個可以和 Docker 類似的虛擬網路。而且要實作和外網通訊的功能。

## 1. 實驗環境準備

我們先來建立一個虛擬的網路環境出來，其命名空間 (Namespace) 為 net1。宿主機的 IP 是 10.162 的網段，可以存取外部機器。虛擬網路為其分配 192.168.0 的網段，這個網段是私有的，外部機器無法識別。

這個虛擬網路的搭建過程如下。先建立一個 netns 出來，命名為 net1。

```
# ip netns add net1
```

建立一個 veth 配對（veth1 - `veth1_p`），把其中的一頭 veth1 放在 net1 中，給它設定上 IP，並把它啟動起來。

```
# ip link add veth1 type veth peer name veth1_p
```

```
# ip link set veth1 netns net1
```

```
# ip netns exec net1 ip addr add 192.168.0.2/24 dev veth1  # IP
```

```
# ip netns exec net1 ip link set veth1 up
```

建立一個 bridge，給它也設定上 ip。接下來把 veth 的另外一端   `veth1_p` 插到 bridge 上面。最後把網橋和 `veth1_p` 都啟動起來。

## brctl addbr br0
```
# ip addr add 192.168.0.1/24 dev br0
```

```
# ip link set dev veth1_p master br0
```

```
# ip link set veth1_p up
```

```
# ip link set br0 up
```

這樣我們就在 Linux 上建立出了一個虛擬的網路。建立過程和 [聊聊 Linux 上軟體實作的「交換機」 - Bridge！](https://mp.weixin.qq.com/s/JnKz1fUgZmGdvfxOm2ehZg) 中一樣，只不過今天為了省事，只建立了一個網路出來，上一篇中建立出來了兩個。

## 2. 請求外網資源

現在假設我們上面的 net1 這個網路環境中想存取外網。這裡的外網是指的虛擬網路宿主機外部的網路。

我們假設它要存取的另外一臺機器 IP 是 10.153.*.* ，這個 10.153.*.* 後面兩段由於是我的內部網路，所以隱藏起來了。你在實驗的過程中，用自己的 IP 代替即可。

我們直接來存取一下試試

```
# ip netns exec net1 ping 10.153.*.*
connect: Network is unreachable
```

提示網路不通，這是怎麼回事？ 用這段報錯關鍵字在核心原始碼裡搜尋一下：

```
//file: arch/parisc/include/uapi/asm/errno.h
#define    ENETUNREACH    229    /* Network is unreachable */
```

```
//file: net/ipv4/ping.c
static int ping_sendmsg(struct kiocb *iocb, struct sock *sk, struct msghdr *msg,
            size_t len)
{
    ...
    rt = ip_route_output_flow(net, &fl4, sk);
    if (IS_ERR(rt)) {
        err = PTR_ERR(rt);
        rt = NULL;
        if (err == -ENETUNREACH)
            IP_INC_STATS_BH(net, IPSTATS_MIB_OUTNOROUTES);
        goto out;
    }
    ...
out:
    return err;
}
```

在   `ip_route_output_flow` 這裡的回傳值判斷如果是 ENETUNREACH 就退出了。這個宏定義註釋上來看報錯的資訊就是 「Network is unreachable」。

這個 `ip_route_output_flow` 主要是執行路由選路。所以我們推斷可能是路由出問題了，看一下這個命名空間的路由表。

```
# ip netns exec net1 route -n
Kernel IP routing table
Destination     Gateway         Genmask         Flags Metric Ref    Use Iface
192.168.0.0     0.0.0.0         255.255.255.0   U     0      0        0 veth1
```

怪不得，原來 net1 這個 namespace 下預設只有 192.168.0.* 這個網段的路由規則。我們 ping 的 IP 是 10.153.*.* ，根據這個路由表裡找不到出口。自然就傳送失敗了。

我們來給 net 新增上預設路由規則，只要匹配不到其他規則就預設送到 veth1 上，同時指定下一條是它所連線的 bridge（192.168.0.1）。

```
# ip netns exec net1 route add default gw 192.168.0.1 veth1
```

再 ping 一下試試。

```
# ip netns exec net1 ping 10.153.*.* -c 2
PING 10.153.*.* (10.153.*.*) 56(84) bytes of data.
```

- -- 10.153.*.* ping statistics ---
2 packets transmitted, 0 received, 100% packet loss, time 999ms

額好吧，仍然不通。上面路由幫我們把資料封包從 veth 正確送到了 bridge 這個網橋上。接下來網橋還需要 bridge 轉發到 eth0 網路卡上。所以我們得開啟下面這兩個轉發相關的設定

```
# sysctl net.ipv4.conf.all.forwarding=1
```

```
# iptables -P FORWARD ACCEPT
```

不過這個時候，還存在一個問題。那就是外部的機器並不認識 192.168.0.* 這個網段的 ip。它們之間都是透過 10.153.*.* 來進行通訊的。設想下我們工作中的電腦上沒有外網 IP 的時候是如何正常上網的呢？外部的網路只認識外網 IP。沒錯，那就是我們上面說的 NAT 技術。

我們這次的需求是實作內部虛擬網路存取外網，所以需要使用的是 SNAT。它將 namespace 請求中的 IP（192.168.0.2）換成外部網路認識的 10.153.*.*，進而達到正常存取外部網路的效果。

```
# iptables -t nat -A POSTROUTING -s 192.168.0.0/24 ! -o br0 -j MASQUERADE
```

來再 ping 一下試試，歐耶，通了！

```
# ip netns exec net1 ping 10.153.*.*
PING 10.153.*.* (10.153.*.*) 56(84) bytes of data.
64 bytes from 10.153.*.*: icmp_seq=1 ttl=57 time=1.70 ms
64 bytes from 10.153.*.*: icmp_seq=2 ttl=57 time=1.68 ms
```

這時候我們可以開啟 `tcpdump` 擷取封包檢視一下，在 bridge 上擷取到的封包我們能看到還是原始的源 IP 和目的 IP。

再到 eth0 上檢視的話，源 IP 已經被替換成可和外網通訊的 eth0 上的 IP 了。

至此，容器就可以透過宿主機的網路卡來存取外部網路上的資源了。我們來總結一下這個傳送過程

## 3. 開放容器埠

我們再考慮另外一個需求，那就是把在這個命名空間內的服務提供給外部網路來使用。

和上面的問題一樣，我們的虛擬網路環境中 192.168.0.2 這個 IP 外界是不認識它的。只有這個宿主機知道它是誰。所以我們同樣還需要 NAT 功能。

這次我們是要實作外部網路存取內部位址，所以需要的是 DNAT 設定。
DNAT 和 SNAT 設定中有一個不一樣的地方就是需要明確指定容器中的埠在宿主機上是對應哪個。例如在 docker 的使用中，是透過 -p 來指定埠的對應關係。

```
# docker run -p 8000:80 ...
```

我們透過如下這個命令來設定 DNAT 規則

```
# iptables -t nat -A PREROUTING  ! -i br0 -p tcp -m tcp --dport 8088 -j DNAT --to-destination 192.168.0.2:80
```

這裡表示的是宿主機在路由之前判斷一下如果流量不是來自 br0，並且是存取 tcp 的 8088 的話，那就轉發到 192.168.0.2:80 。

在 net1 環境中啟動一個 Server

```
# ip netns exec net1 nc -lp 80
```

外部選一個 ip，例如 10.143..， telnet 連一下 10.162..8088 試試，通了！

```
# telnet 10.162.*.* 8088
Trying 10.162.*.*...
Connected to 10.162.*.*.
Escape character is '^]'.
```

開啟擷取封包，`tcpdump -i eth0 host 10.143.*.*`。可見在請求的時候，目的是宿主機的 IP 的埠。

但資料封包到宿主機協定堆疊以後命中了我們設定的 DNAT 規則，宿主機把它轉發到了 br0 上。在 bridge 上由於沒有那麼多的網路流量封包，所以不用過濾直接擷取封包就行，`tcpdump -i br0`。

在 br0 上抓到的目的 IP 和埠是已經替換過的了。

bridge 當然知道 192.168.0.2 是 veth 1。 於是，在 veth1 上監聽 80 的服務就能收到來自外界的請求了！我們來總結一下這個接收過程

## 三、總結

本文開頭我們先是簡單介紹了 veth、bridge、namespace、路由、iptables 等基礎知識。Veth 實作連線，bridge 實作轉發，namespace 實作隔離，路由表控制傳送時的裝置選擇，iptables 實作 nat 等功能。

接著根據以上基礎知識，我們採用純手工的方式搭建了一個虛擬網路環境。

這個虛擬網路可以存取外網資源，也可以提供埠服務供外網來呼叫。這就是 Docker 容器網路工作的基本原理。

整個實驗我打包寫成一個 Makefile，放到了這裡：[https://github.com/yanfeizhang/coder-kung-fu/tree/main/tests/network/test07](https://github.com/yanfeizhang/coder-kung-fu/tree/main/tests/network/test07)

最後，我們再擴充套件一下。今天我們討論的問題是 Docker 網路通訊的問題。Docker 容器透過埠對映的方式提供對外服務。外部機器存取容器服務的時候，仍然需要透過容器的宿主機 IP 來存取。

在 Kubernets 中，對跨主網路通訊有更高的要求，要不同宿主機之間的容器可以直接互聯互通。所以 Kubernets 的網路模型也更為複雜。
