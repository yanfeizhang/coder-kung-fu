---
layout: default
title: "使用者態 (User Mode) `tcpdump` 如何實作擷取核心網路封包 (Network Packet) 的？"
---

# 使用者態 (User Mode) `tcpdump` 如何實作擷取核心網路封包 (Network Packet) 的？

> 原文標題：用户态 tcpdump 如何实现抓到内核网络包的?

> 原文連結：https://mp.weixin.qq.com/s/ZX8Jluh-RgJXcVh3OvycRQ

---

大家好，我是飛哥！

今天聊聊大家工作中經常用到的   `tcpdump`。

在網路封包的傳送和接收過程中，絕大部分的工作都是在核心態 (Kernel Mode) 完成的。那麼問題來了，我們常用的執行在使用者態的程式 `tcpdump` 是如何擷取核心態封包的呢？有的同學知道 `tcpdump` 是根據 libpcap 的，那麼 libpcap 的工作原理又是什麼樣的呢。如果讓你直接撰寫一個封包擷取程式，你有沒有思路？

按照飛哥的風格，不深入探究到最底層的原理我們不會罷休的。所以我對相關的原始碼進行了深入分析。徹底釐清了以下這幾個問題。

- `tcpdump` 是如何工作的？
- netfilter 過濾的封包   `tcpdump` 是否可以擷取到？
- 讓你自己寫一個封包擷取程式的話該如何下手？

藉助如下這幾個問題，我們來展開今天的探索之旅！

## 一、網路封包接收過程

在 [圖解 Linux 網路封包接收過程](https://mp.weixin.qq.com/s/GoYDsfy9m0wRoXi_NCfCmg) 一文中我們詳細介紹了網路封包是如何從網路卡 (Network Card) 到達使用者程序 (Process) 中的。這個過程我們可以簡單用如下這個圖來表示。

## 找到 `tcpdump` 擷取封包點

我們在網路 (Network) 裝置層的程式碼裡找到了   `tcpdump` 的擷取封包入口。在 \_\_netif_receive_skb_core 這個函式里會遍歷 `ptype_all` 上的協定。還記得上文中我們提到 `tcpdump` 在 `ptype_all` 上註冊了虛擬協定。這時就能執行的到了。來看函式：

```
//file: net/core/dev.c
static int __netif_receive_skb_core(struct sk_buff *skb, bool pfmemalloc)
{
    ......
    //遍歷 ptype_all （tcpdump 在這裡掛了虛擬協定）
    list_for_each_entry_rcu(ptype, &ptype_all, list) {
        if (!ptype->dev || ptype->dev == skb->dev) {
            if (pt_prev)
                ret = deliver_skb(skb, pt_prev, orig_dev);
            pt_prev = ptype;
        }
    }
}
```

在上面函式中遍歷   `ptype_all`，並使用 `deliver_skb` 來呼叫協定中的回呼函式。

```
//file: net/core/dev.c
static inline int deliver_skb(...)
{
    return pt_prev->func(skb, skb->dev, pt_prev, orig_dev);
}
```

對於   `tcpdump` 來說，就會進入 `packet_rcv` 了（後面我們再說為什麼是進入這個函式）。這個函式在 `net/packet/af_packet.c` 檔案中。

```
//file: net/packet/af_packet.c
static int packet_rcv(struct sk_buff *skb, ...)
{
    __skb_queue_tail(&sk->sk_receive_queue, skb);
    ......
}
```

可見   `packet_rcv` 把收到的 `skb` 放到了當前 packet `socket` 的接收佇列裡了。這樣後面呼叫 recvfrom 的時候就可以取得到所擷取到的封包！！

## 再找 netfilter 過濾點

為了解釋我們開篇中提到的問題，這裡我們再稍微到協定層中多看一些。在 `ip_rcv` 中我們找到了一個 netfilter 相關的執行邏輯。

```
//file: net/ipv4/ip_input.c
int ip_rcv(...)
{
    ......
    return NF_HOOK(NFPROTO_IPV4, NF_INET_PRE_ROUTING, skb, dev, NULL,
               ip_rcv_finish);
}
```

如果你用   `NF_HOOK` 作為關鍵詞來搜尋，還能搜到不少 netfilter 的過濾點。不過所有的過濾點都是位於 IP 協定層的。

在接收封包的過程中，資料封包是先經過網路裝置層然後才到協定層的。

那麼我們開篇中的一個問題就有了答案了。假如我們設定了 netfilter 規則，在接收封包的過程中，工作在網路裝置層的 `tcpdump` 先開始工作。還沒等 netfilter 過濾，`tcpdump` 就擷取到封包了！

所以，在接收封包的過程中，netfilter 過濾並不會影響 `tcpdump` 的擷取封包！

## 二、網路封包傳送過程

我們接著再來看網路封包傳送過程。在 [25 張圖，一萬字，拆解 Linux 網路封包傳送過程](https://mp.weixin.qq.com/s/wThfD9th9e_-YGHJJ3HXNQ) 一文中，我們詳細描述過網路封包的傳送過程。傳送過程可以彙總成簡單的一張圖。

## 找到 netfilter 過濾點

在傳送的過程中，同樣是在 IP 層進入各種 netfilter 規則的過濾。

```
//file: net/ipv4/ip_output.c
int ip_local_out(struct sk_buff *skb)
{
    //執行 netfilter 過濾
    err = __ip_local_out(skb);
}

int __ip_local_out(struct sk_buff *skb)
{
......
return nf_hook(NFPROTO_IPV4, NF_INET_LOCAL_OUT, skb, NULL,
skb_dst(skb)->dev, dst_output);
}
```

在這個檔案中，還能看到若干處 netfilter 過濾邏輯。

## 找到   `tcpdump` 擷取封包點

傳送過程在協定層處理完畢到達網路裝置層的時候，也有 `tcpdump` 的擷取封包點。

```
//file: net/core/dev.c
int dev_hard_start_xmit(struct sk_buff *skb, struct net_device *dev,
            struct netdev_queue *txq)
{
    ...
    if (!list_empty(&ptype_all))
        dev_queue_xmit_nit(skb, dev);
}

static void dev_queue_xmit_nit(struct sk_buff *skb, struct net_device *dev)
{
list_for_each_entry_rcu(ptype, &ptype_all, list) {
if ((ptype->dev == dev || !ptype->dev) &&
(!skb_loop_sk(ptype, skb))) {
if (pt_prev) {
deliver_skb(skb2, pt_prev, skb->dev);
pt_prev = ptype;
continue;
}
......
}
}
}
```

在上述程式碼中我們看到，在   `dev_queue_xmit_nit` 中遍歷 `ptype_all` 中的協定，並依次呼叫 `deliver_skb`。這就會執行到 `tcpdump` 掛在上面的虛擬協定。

在網路封包的傳送過程中，和接收過程恰好相反，是協定層先處理、網路裝置層後處理。

如果 netfilter 設定了過濾規則，那麼在協定層就直接過濾掉了。在下層網路裝置層工作的   `tcpdump` 將無法再捕獲到該網路封包。

## 三、`TCPDUMP` 啟動

前面兩小節我們說到了核心收發封包都透過遍歷   `ptype_all` 來執行擷取封包的。那麼我們現在來看看使用者態的 `tcpdump` 是如何掛載協定到內 `ptype_all` 上的。

我們透過 `strace` 命令我們抓一下 `tcpdump` 命令的系統呼叫 (System Call)，顯示結果中有一行 `socket` 系統呼叫。  `Tcpdump` 秘密的源頭就藏在這行對 `socket` 函式的呼叫裡。

```
# strace tcpdump -i eth0
socket(AF_PACKET, SOCK_RAW, 768)
......
```

`socket` 系統呼叫的第一個參數列示建立的 `socket` 所屬的位址簇或者協定簇，取值以 AF 或者 PF 開頭。在 Linux 裡，支援很多種協定族，在 `include/linux/socket.h` 中可以找到所有的定義。這裡建立的是 packet 型別的 `socket`。

> 協定族和位址族：每一種協定族都有其對應的位址族。例如 IPV4 的協定族定義叫   `PF_INET`，其位址族的定義是 `AF_INET`。它們是一一對應的，而且值也完全一樣，所以經常混用。

```
//file: include/linux/socket.h
#define AF_UNSPEC    0
#define AF_UNIX        1    /* Unix domain sockets         */
#define AF_LOCAL    1    /* POSIX name for AF_UNIX    */
#define AF_INET        2    /* Internet IP Protocol     */
#define AF_INET6    10    /* IP version 6            */
#define AF_PACKET    17    /* Packet family        */
......
```

另外上面第三個參數 768 代表的是   `ETH_P_ALL`，socket.htons(`ETH_P_ALL`) = 768。

我們來展開看這個 packet 型別的 `socket` 建立的過程中都做了什麼，找到 `socket` 建立原始碼。

```
//file: net/socket.c
SYSCALL_DEFINE3(socket, int, family, int, type, int, protocol)
{
    ......
    retval = sock_create(family, type, protocol, &sock);
}

int __sock_create(struct net *net, int family, int type, ...)
{
......
pf = rcu_dereference(net_families[family]);
err = pf->create(net, sock, protocol, kern);
}
```

在 \_\_sock_create 中，從   `net_families` 中取得了指定協定。並呼叫了它的 create 方法來完成建立。

`net_families` 是一個陣列，除了我們常用的 `PF_INET`（ipv4） 外，還支援很多種協定族。例如 `PF_UNIX`、  `PF_INET6`（ipv6）、`PF_PACKET` 等等。每一種協定族在 `net_families` 陣列的特定位置都可以找到其 family 型別。在這個 family 型別裡，成員函式 create 指向該協定族的對應建立函式。

根據上圖，我們看到對於 packet 型別的 `socket`，`pf->create` 實際呼叫到的是 `packet_create` 函式。我們進入到這個函式中來一探究竟，這是理解 `tcpdump` 工作原理的關鍵！

```
//file: packet/af_packet.c
static int packet_create(struct net *net, struct socket *sock, int protocol,
             int kern)
{
    ...
    po = pkt_sk(sk);
    po->prot_hook.func = packet_rcv;

    //註冊鉤子
    if (proto) {
        po->prot_hook.type = proto;
        register_prot_hook(sk);
    }
}

static void register_prot_hook(struct sock *sk)
{
struct packet_sock *po = pkt_sk(sk);
dev_add_pack(&po->prot_hook);
}
```

在   `packet_create` 中設定回呼函式為 `packet_rcv`，再透過 `register_prot_hook`=> `dev_add_pack` 完成註冊。註冊完後，是在全域性協定   `ptype_all` 連結串列中新增了一個虛擬的協定進來。

我們再來看下 `dev_add_pack` 是如何註冊協定到 `ptype_all` 中的。回顧我們開頭看到的 `socket` 函式呼叫，第三個參數 proto 傳入的是   `ETH_P_ALL`。那 `dev_add_pack` 其實最後是把 hook 函式新增到了 `ptype_all` 裡了，程式碼如下。

```
//file: net/core/dev.c
void dev_add_pack(struct packet_type *pt)
{
    struct list_head *head = ptype_head(pt);
    list_add_rcu(&pt->list, head);
}

static inline struct list_head *ptype_head(const struct packet_type *pt)
{
if (pt->type == htons(ETH_P_ALL))
return &ptype_all;
else
return &ptype_base[ntohs(pt->type) & PTYPE_HASH_MASK];
}
```

> 我們整篇文章都以   `ETH_P_ALL` 為例，但其實有的時候也會有其他情況。在別的情況下可能會註冊協定到 `ptype_base` 裡了，而不是 `ptype_all`。同樣， `ptype_base` 中的協定也會在傳送和接收的過程中被執行到。

總結：  `tcpdump` 啟動的時候內部邏輯其實很簡單，就是在 `ptype_all` 中註冊了一個虛擬協定而已。

## 四、總結

現在我們再回頭看開篇提到的幾個問題。

1. `tcpdump` 是如何工作的

使用者態 `tcpdump` 命令是透過 `socket` 系統呼叫，在核心原始碼中用到的 `ptype_all` 中掛載了函式鉤子上去。無論是在網路封包接收過程中，還是在傳送過程中，都會在網路裝置層遍歷   `ptype_all` 中的協定，並執行其中的回呼。`tcpdump` 命令就是根據這個底層原理來工作的。

2. netfilter 過濾的封包   `tcpdump` 是否可以擷取到
關於這個問題，得分接收和傳送過程分別來看。在網路封包接收的過程中，由於 `tcpdump` 近水樓臺先得月，所以完全可以捕獲到命中 netfilter 過濾規則的包。

但是在傳送的過程中，恰恰相反。網路封包先經過協定層，這時候被 netfilter 過濾掉的話，底層工作的   `tcpdump` 還沒等看見就什麼也沒了。

3. 讓你自己寫一個封包擷取程式的話該如何下手
如果你想自己寫一段類似 `tcpdump` 的封包擷取程式的話，使用 packet `socket` 就可以了。我用 c 寫了一段擷取封包，並且解析源 IP 和目的 IP 的簡單 demo。

原始碼位址：[https://github.com/yanfeizhang/coder-kung-fu/blob/main/tests/network/test04/main.c](https://github.com/yanfeizhang/coder-kung-fu/blob/main/tests/network/test04/main.c)

編譯一下，注意執行需要 root 許可權。

```
# gcc -o main main.c
```

## `./main`

執行結果預覽如下：
