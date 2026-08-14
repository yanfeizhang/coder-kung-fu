---
layout: default
title: "徹底弄懂 Linux 網路 (Network) 命名空間 (Namespace)"
---

# 徹底弄懂 Linux 網路 (Network) 命名空間 (Namespace)

> 原文標題：彻底弄懂 Linux 网络命名空间

> 原文連結：https://mp.weixin.qq.com/s/lscMpc5BWAEzjgYw6H0wBw

---

大家好，我是飛哥！

在 Linux 上透過 [veth](https://mp.weixin.qq.com/s/sSQFINJ8RO8Nc4XtcyQIjQ) 我們可以建立出許多的虛擬裝置。透過 [Bridge](https://mp.weixin.qq.com/s/JnKz1fUgZmGdvfxOm2ehZg) 模擬乙太網交換機的方式可以讓這些網路裝置之間進行通訊。不過虛擬化中還有很重要的一步，那就是隔離。借用 Docker 的概念來說，那就是不能讓 A 容器 (Container) 用到 B 容器的裝置，甚至連看一眼都不可以。只有這樣才能保證不同的容器之間複用硬體資源的同時，還不會影響其他容器的正常執行。

在 Linux 上實作隔離的技術手段就是 namespace。透過 namespace 可以隔離容器的程序 (Process) PID、檔案系統 (File System) 掛載點、主機名等多種資源。不過我們今天重點要介紹的是網路 namespace，簡稱 netns。它可以為不同的命名空間從邏輯上提供獨立的網路協定堆疊，具體包括網路裝置、路由表、arp 表、iptables、以及套接字（`socket`）等。使得不同的網路空間就都好像執行在獨立的網路中一樣。

你是不是和飛哥一樣，也很好奇 Linux 底層到底是如何實作網路隔離的？我們今天來好好挖一挖 netns 的內部實作。

## 一、如何使用 netns

由於我們平時的開發工作很少涉及網路空間，所以我們先來看一下網路空間是如何使用的吧。我們來建立一個新的命名空間 net1。再建立一對 veth，將 veth 的一頭放到 net1 中。分別檢視一下宿主機和 net1 空間內的 iptable、裝置等。最後讓兩個命名空間之間進行通訊。

下面是詳細的使用過程。首先我們先來建立一個新的網路命名空間 (Network Namespace) - net1。

```
# ip netns add net1
```

來檢視一下它的 iptable、路由表、以及網路裝置

```
# ip netns exec net1 route
Kernel IP routing table
Destination     Gateway         Genmask         Flags Metric Ref    Use Iface
```

```
# ip netns exec net1 iptables -L
ip netns exec net1 iptables -L
Chain INPUT (policy ACCEPT)
target     prot opt source               destination
......
```

```
# ip netns exec net1 ip link list
lo: <LOOPBACK> mtu 65536 qdisc noop state DOWN mode DEFAULT qlen 1
link/loopback 00:00:00:00:00:00 brd 00:00:00:00:00:00
```

由於是新建立的 netns，所以上述的輸出中路由表、iptable 規則都是空的。不過這個命名空間中初始的情況下就存在一個 lo 本地環回裝置，只不過預設是 DOWN（未啟動）狀態。

接下來我們建立一對 veth，並把 veth 的一頭新增給它。

```
# ip link add veth1 type veth peer name veth1_p
```

```
# ip link set veth1 netns net1
```

在宿主機上檢視一下當前的裝置，發現已經看不到 veth1 這個網路卡 (Network Card) 裝置了，只能看到 `veth1_p`。

```
# ip link list
1: lo: <LOOPBACK,UP,LOWER_UP> mtu 65536 ...
2: eth0: <BROADCAST,MULTICAST,UP,LOWER_UP> mtu 1500 ...
3: eth1: <BROADCAST,MULTICAST> mtu 1500 ...
45: veth1_p@if46: <BROADCAST,MULTICAST> mtu 1500 qdisc noop state DOWN mode DEFAULT qlen 1000
link/ether 0e:13:18:0a:98:9c brd ff:ff:ff:ff:ff:ff link-netnsid 0
```

這個新裝置已經跑到 net1 這個網路空間裡了。

```
# ip netns exec net1 ip link list
1: lo: <LOOPBACK> mtu 65536 ...
46: veth1@if45: <BROADCAST,MULTICAST> mtu 1500 qdisc noop state DOWN mode DEFAULT qlen 1000
link/ether 7e:cd:ec:1c:5d:7a brd ff:ff:ff:ff:ff:ff link-netnsid 0
```

把這對 veth 分別設定上 ip，並把它們啟動起來

```
# ip addr add 192.168.0.100/24 dev veth1_p
```

```
# ip netns exec net1 ip addr add 192.168.0.101/24 dev veth1
```

```
# ip link set dev veth1_p up
```

```
# ip netns exec net1 ip link set dev veth1 up
```

在宿主機和 net1 中分別執行 ifconfig 檢視當前啟動的網路裝置。

## ifconfig
eth0: ...
lo: ...
`veth1_p`: flags=4163<UP,BROADCAST,RUNNING,MULTICAST>  mtu 1500
inet 192.168.0.100  netmask 255.255.255.0  broadcast 0.0.0.0
...

```
# ip netns exec net1 ifconfig
veth1: flags=4163<UP,BROADCAST,RUNNING,MULTICAST>  mtu 1500
inet 192.168.0.101  netmask 255.255.255.0  broadcast 0.0.0.0
...
```

我們來讓它和宿主機通訊一下試試。

```
# ip netns exec net1 ping 192.168.0.100 -I veth1
PING 192.168.0.100 (192.168.0.100) from 192.168.0.101 veth1: 56(84) bytes of data.
64 bytes from 192.168.0.100: icmp_seq=1 ttl=64 time=0.027 ms
64 bytes from 192.168.0.100: icmp_seq=2 ttl=64 time=0.010 ms
```

好了，現在一個新網路命名空間建立實驗就結束了。在這個空間裡，網路裝置、路由表、arp 表、iptables 都是獨立的，不會和宿主機上的衝突，也不會和其他空間裡的產生干擾。而且還可以透過 veth 來和其他空間下的網路進行通訊。

## 二、核心中 namespace 的定義

在核心中，很多元件都是和 namespace 有關係的，我們先來看看這個關聯關係是如何定義的。後面我們再看下 namespace 本身的詳細結構。

## 2.1 歸屬到 namespace 的東西

在 Linux 中，很多我們平常熟悉的概念都是歸屬到某一個特定的網路 namespace 中的，例如程序、網路卡裝置、  `socket` 等等。

Linux 中每個程序）都是用 `task_struct` 來表示的。每個 `task_struct` 都要關聯到一個 namespace 物件 nsproxy，而 nsproxy 又包含了 netns。對於網路卡裝置和 `socket` 來說，透過自己的成員來直接表明自己的歸屬。

拿網路裝置來舉例，只有歸屬到當前 netns 下的時候才能夠透過 ifconfig 看到，否則是不可見的。我們詳細來看看這幾個資料結構的定義，先來看程序。

```
//file:include/linux/sched.h
struct task_struct {
    /* namespaces */
    struct nsproxy *nsproxy;
    ......
}
```

命名空間的核心資料結構是上面的這個 struct nsproxy。所有型別的 namespace（包括 pid、檔案系統掛載點、網路堆疊等等）都是在這裡定義的。

```
//file: include/linux/nsproxy.h
struct nsproxy {
    struct uts_namespace *uts_ns;    // 主機名
    struct ipc_namespace *ipc_ns;    // IPC
    struct mnt_namespace *mnt_ns;    // 檔案系統掛載點
    struct pid_namespace *pid_ns;    // 程序標號
    struct net          *net_ns;        // 網路協定堆疊
};
```

其中 struct net *  `net_ns` 就是今天我們要討論的網路命名空間。它的詳細定義我們待會再說。我們接著再看錶示網路裝置的 struct `net_device`，它也是要歸屬到某一個網路空間下的。

```
//file: include/linux/netdevice.h
struct net_device{
    //裝置名
    char            name[IFNAMSIZ];

    //網路命名空間
    struct net        *nd_net;

    ...
}
```

所有的網路裝置剛建立出來都是在宿主機預設網路空間下的。可以透過 ip link set 裝置名 netns 網路空間名將裝置移動到另外一個空間裡去。前面的實驗裡，當 veth 1 移動到 net1 下的時候，該裝置在宿主機下「消失」了，在 net1 下就能看到了。

還有我們經常用的 `socket`，也是歸屬在某一個網路命名空間下的。

```
//file: include/net/sock.h
struct sock_common {
    struct net         *skc_net;
}
```

## 2.2 網路 namespace 定義

本小節中，我們來看網路 namespace 的主要資料結構 struct net 的定義。

可見每個 net 下都包含了自己的路由表、iptable 以及核心參數 (Kernel Parameter) 設定等等。我們來看具體的程式碼。

```
//file:include/net/net_namespace.h
struct net {
    //每個 net 中都有一個迴環裝置
    struct net_device       *loopback_dev;          /* The loopback */

    //路由表、netfilter都在這裡
    struct netns_ipv4    ipv4;
    ......

    unsigned int        proc_inum;
}
```

由上述定義可見，每一個 netns 中都有一個   `loopback_dev`，這就是為什麼我們在第一節中看到剛建立出來的空間裡就能看到一個 lo 裝置的底層原因。

網路 netspace 中最核心的資料結構是 struct `netns_ipv4` ipv4。在這個資料結構裡，定義了每一個網路空間專屬的路由表、ipfilter 以及各種核心參數。

```
//file: include/net/netns/ipv4.h
struct netns_ipv4 {
    //路由表
    struct fib_table    *fib_local;
    struct fib_table    *fib_main;
    struct fib_table    *fib_default;

    //ip表
    struct xt_table        *iptable_filter;
    struct xt_table        *iptable_raw;
    struct xt_table        *arptable_filter;

    //核心參數
    long sysctl_tcp_mem[3];
    ...
}
```

## 三、網路 namespace 的建立

回顧第一小節中，我們實驗步驟主要是建立了一個 netns，為其新增了一個 veth 裝置。在這節中我們來窺探一下剛才的實驗步驟內部到底是如何執行的。

## 3.1 程序與網路命名空間

Linux 上存在一個預設的網路命名空間，Linux 中的 1 號程序初始使用該預設空間。Linux 上其他所有程序都是由 1 號程序派生出來的，在派生   `clone` 的時候如果沒有額外特別指定，所有的程序都將共享這個預設網路空間。

在 `clone` 裡可以指定建立新程序時的 flag，都是 CLONE_ 開頭的。
和 namespace 有的的標誌位有   `CLONE_NEWIPC`、`CLONE_NEWNET`、 `CLONE_NEWNS`、 `CLONE_NEWPID` 等等。如果在建立程序時指定了   `CLONE_NEWNET` 標記位，那麼該程序將會建立並使用新的 netns。

其實核心提供了三種操作命名空間的方式，分別是 `clone`、setns 和 unshare。本文中我們只用 `clone` 來舉例，ip netns add 使用的是 unshare，原理和 `clone` 是類似的。

我們先來看下預設的網路命名空間的初始化過程。

```
//file: init/init_task.c
struct task_struct init_task = INIT_TASK(init_task);
```

```
//file: include/linux/init_task.h
#define INIT_TASK(tsk)  \
{
    ...
     .nsproxy    = &init_nsproxy,    \
}
```

上面的程式碼是在初始化第 1 號程序。可見 nsproxy 是已經建立好的   `init_nsproxy`。再看 `init_nsproxy` 是如何建立的。

```
//file: kernel/nsproxy.c
struct nsproxy init_nsproxy = {
    .uts_ns    = &init_uts_ns,
    .ipc_ns    = &init_ipc_ns,
    .mnt_ns    = NULL,
    .pid_ns    = &init_pid_ns,
    .net_ns    = &init_net,
};
```

初始的   `init_nsproxy` 裡將多個命名空間都進行了初始化，其中我們關注的網路命名空間，用的是預設網路空間 `init_net`。它是系統初始化的時候就建立好的。

```
//file: net/core/net_namespace.c
struct net init_net = {
    .dev_base_head = LIST_HEAD_INIT(init_net.dev_base_head),
};
EXPORT_SYMBOL(init_net);
```

```
//file: net/core/net_namespace.c
static int __init net_ns_init(void)
{
    ...
    setup_net(&init_net, &init_user_ns);
    ...
    register_pernet_subsys(&net_ns_ops);
    return 0;
}
```

上面的   `setup_net` 方法中對這個預設網路命名空間進行初始化。

看到這裡我們清楚了 1 號程序的命名空間初始化過程。Linux 中所有的程序都是由這個 1 號程序建立的。如果建立子程序過程中沒有指定 `CLONE_NEWNET` 這個 flag 的話，就直接還使用這個預設的網路空間。

如果建立程序過程中指定了   `CLONE_NEWNET`，那麼就會重新申請一個網路命名空間出來。見如下的關鍵函式 `copy_net_ns`（它的呼叫鏈是 `do_fork` => `copy_process` =>   `copy_namespaces` => `create_new_namespaces` => `copy_net_ns`）。

```
//file: net/core/net_namespace.c
struct net *copy_net_ns(unsigned long flags,
            struct user_namespace *user_ns, struct net *old_net)
{
    struct net *net;

    // 重要！！！
    // 不指定 CLONE_NEWNET 就不會建立新的網路命名空間
    if (!(flags & CLONE_NEWNET))
        return get_net(old_net);

    //申請新網路命名空間並初始化
    net = net_alloc();
    rv = setup_net(net, user_ns);
    ...
}
```

記住   `setup_net` 是初始化網路命名空間的，這個函式接下來我們還會提到。

## 3.2 命名空間內的網路子系統初始化

命名空間內的各個元件都是在 `setup_net` 時初始化的，包括路由表、tcp 的 proc 偽檔案系統、iptable 規則讀取等等，所以這個小節也是蠻重要的。

由於核心網路模組的複雜性，在核心中將網路模組劃分成了各個子系統。每個子系統都定義了一個

```
//file: include/net/net_namespace.h
struct pernet_operations {
    // 連結串列指標
    struct list_head list;

    // 子系統的初始化函式
    int (*init)(struct net *net);

    // 網路命名空間每個子系統的退出函式
    void (*exit)(struct net *net);
    void (*exit_batch)(struct list_head *net_exit_list);
    int *id;
    size_t size;
};
```

各個子系統透過呼叫   `register_pernet_subsys` 或 `register_pernet_device` 將其初始化函式註冊到網路命名空間系統的全域性連結串列 `pernet_list` 中。你在原始碼目錄下用這兩個函式搜尋的話，會看到各個子系統的註冊過程。

拿 `register_pernet_subsys` 來舉例，我們來簡單看下它是如何將子系統都註冊到 `pernet_list` 中的。

```
//file: net/core/net_namespace.c
static struct list_head *first_device = &pernet_list;
int register_pernet_subsys(struct pernet_operations *ops)
{
    error =  register_pernet_operations(first_device, ops);
    ...
}
```

`register_pernet_operations` 又會呼叫 \_\_register_pernet_operations。

```
//file: include/net/net_namespace.h
#define for_each_net(VAR)                \
    list_for_each_entry(VAR, &net_namespace_list, list)
```

```
//file: net/core/net_namespace.c
static int __register_pernet_operations(struct list_head *list,
                    struct pernet_operations *ops)
{
    struct net *net;

    list_add_tail(&ops->list, list);
    if (ops->init || (ops->id && ops->size)) {
        for_each_net(net) {
            error = ops_init(ops, net);
            ...
}
```

在上面   `list_add_tail` 這一行，完成了將子系統傳入的 struct pernet\_operations *ops 鏈入到 `pernet_list` 中。並注意一下， `for_each_net` 是遍歷了所有的網路命名空間，然後在這個空間內執行了 `ops_init` 初始化。

這個初始化是網路子系統在註冊的時候呼叫的。同樣當新的命名空間建立時，會遍歷該全域性變數   `pernet_list`，執行每個子模組註冊上來的初始化函式。再回到我們 3.1.1 裡提到的 `setup_net` 函式。

```
//file: net/core/net_namespace.c
static __net_init int setup_net(struct net *net, struct user_namespace *user_ns)
{
    const struct pernet_operations *ops;
    list_for_each_entry(ops, &pernet_list, list) {
        error = ops_init(ops, net);
    ...
}
```

```
//file: net/core/net_namespace.c
static int ops_init(const struct pernet_operations *ops, struct net *net)
{
    if (ops->init)
        err = ops->init(net);
}
```

在建立新命名空間呼叫到   `setup_net` 時，會透過 `pernet_list` 找到所有的網路子系統，把它們都 init 一遍。

我們拿路由表來舉例，路由表子系統透過   `register_pernet_subsys` 將 `fib_net_ops` 註冊進來了。

```
//file: net/ipv4/fib_frontend.c
static struct pernet_operations fib_net_ops = {
    .init = fib_net_init,
    .exit = fib_net_exit,
};

void __init ip_fib_init(void)
{
    register_pernet_subsys(&fib_net_ops);
    ...
}
```

這樣每當建立一個新的命名空間的時候，就會呼叫   `fib_net_init` 來建立一套獨立的路由規則。

再例如拿 iptable 中的 nat 表來說，也是一樣。每當建立新命名空間的時候，就會呼叫 `iptable_nat_net_init` 建立一套新的表。

```
//file: net/ipv4/netfilter/iptable_nat.c
static struct pernet_operations iptable_nat_net_ops = {
    .init    = iptable_nat_net_init,
    .exit    = iptable_nat_net_exit,
};
static int __init iptable_nat_init(void)
{
    err = register_pernet_subsys(&iptable_nat_net_ops);
    ...
```

## 3.3 新增裝置

在一個裝置剛剛建立出來的時候，它是屬於預設網路命名空間   `init_net` 的，包括 veth 裝置。不過可以在建立完後修改裝置到新的網路命名空間。

拿 veth 裝置來舉例，它是在建立時的原始碼 `alloc_netdev_mqs` 中設定到 `init_net` 上的。(執行程式碼路徑：veth\_newlink => `rtnl_create_link` => `alloc_netdev_mqs`)

```
//file: core/dev.c
struct net_device *alloc_netdev_mqs(...)
{
    dev_net_set(dev, &init_net);
}
```

```
//file: include/linux/netdevice.h
void dev_net_set(struct net_device *dev,struct net *net)
{
    release_net(dev->nd_net);
    dev->nd_net = hold_net(net);
}

在執行修改裝置所屬的 namespace 的時候，會將 dev->nd_net 再指向新的 netns。對於 veth 來說，它包含了兩個裝置。這兩個裝置可以放在不同的 namespace 中。這就是 Docker 容器和其宿主機或者其他容器通訊的基礎。
```

```
//file: core/dev.c
int dev_change_net_namespace(struct net_device *dev, struct net *net, ...)
{
    ...
    dev_net_set(dev, net)
}
```

## 四、在 namespace 下的網路收發

在前面一節中，我們知道了核心是如何建立 netns 出來，也瞭解了網路裝置是如何新增到其他命名空間裡的。 在這一小節，我們聊聊，當考慮到網路命名空間的時候，網路封包 (Network Packet) 的收發又是怎麼樣的呢？

## 4.1   `socket` 與網路命名空間

首先來考慮的就是我們熟悉的 `socket`。其實每個 `socket` 都是歸屬於某一個網路命名空間的，這個關聯關係在上面的 2.1 小節提到過。

到底歸屬那個 netns，這是由建立這個 `socket` 的程序所屬的 netns 來決定。當在某個程序裡建立 `socket` 的時候，核心就會把當前程序的 nsproxy-> `net_ns` 找出來，並把它賦值給   `socket` 上的網路命名空間成員 `skc_net`。

> 在預設下，我們建立的   `socket` 都屬於預設的網路命名空間 `init_net`

我們來展開看下   `socket` 是如何被放到某個網路命名空間中的。在 `socket` 中，用來儲存和網路命名空間歸屬關係的變數是 `skc_net`，如下。

```
//file: include/net/sock.h
struct sock_common {
    ...
    struct net         *skc_net;
}

接下來就是   socket 建立的時候，核心中可以透過 current->nsproxy->net_ns 把當前程序所屬的 netns 找出來，最終把 socket 中的 sk_net 成員和該命名空間建立好了聯絡。
```

```
//file: net/socket.c
int sock_create(int family, int type, int protocol, struct socket **res)
{
    return __sock_create(current->nsproxy->net_ns, family, type, protocol, res, 0);
}

在   socket_create 中，看到 current->nsproxy->net_ns 了吧，它取得到了程序的 netns。再依次經過\_\_sock_create => inet_create => sk_alloc，呼叫到   sock_net_set 的時候，成功設定了新 socket 和 netns 的關聯關係。
```

```
//file: include/net/sock.h
static inline
void sock_net_set(struct sock *sk, struct net *net)
{
    write_pnet(&sk->sk_net, net);
}
```

## 4.2 網路封包的收發過程

網路封包的接收和傳送過程我們在這兩篇文章裡詳細介紹過,[圖解 Linux 網路封包接收過程](https://mp.weixin.qq.com/s/GoYDsfy9m0wRoXi_NCfCmg) 和 [25 張圖，一萬字，拆解 Linux 網路封包傳送過程](https://mp.weixin.qq.com/s/wThfD9th9e_-YGHJJ3HXNQ)。

本小節的不再重複贅述這個收發過程，我們就以網路封包傳送過程中的路由功能為例，來看一下網路在傳輸的時候是如何使用到 netns 的。其他收發過程中的各個步驟也都是類似的。

大致的原理就是   `socket` 上記錄了其歸屬的網路命名空間。需要查詢路由表之前先找到該命名空間，再找到命名空間裡的路由表，然後再開始執行查詢。這樣，各個命名空間中的路由過程就都隔離開了。

我們來看詳細的路由查詢原始碼。在 [25 張圖，一萬字，拆解 Linux 網路封包傳送過程](https://mp.weixin.qq.com/s/wThfD9th9e_-YGHJJ3HXNQ) 中我們提到過在傳送過程中在 IP 層的傳送函式 `ip_queue_xmit` 中呼叫 `ip_route_output_ports` 來查詢路由項。

```
//file: net/ipv4/ip_output.c
int ip_queue_xmit(struct sk_buff *skb, struct flowi *fl)
{
    rt = ip_route_output_ports(sock_net(sk), fl4, sk,
                    daddr, inet->inet_saddr,
                    ...);
}
```

注意上面的   `sock_net`(sk) 這一步，在這裡將 `socket` 上記錄的命名空間 struct net * `sk_net` 給找了出來。

```
//file: include/net/sock.h
static inline struct net *sock_net(const struct sock *sk)
{
    return read_pnet(&sk->sk_net);
}
```

找到命名空間以後，就會將它 (以 struct net * 指標的形式) 一路透傳到後面的各個函式中。 在 [127.0.0.1 之本機網路通訊過程知多少 ?！](https://mp.weixin.qq.com/s/6_OfoeD3ZpyQisY2F-4_bw) 中我們介紹了路由查詢最後會執行到   `fib_lookup`，我們來看下這個函式的原始碼。

> 路由查詢的呼叫鏈條有點長，是 `ip_route_output_ports` => -> `ip_route_output_flow` => \_\_ip_route_output_key() => `ip_route_output_key_hash` => `ip_route_output_key_hash_rcu`）

```
//file: include/net/ip_fib.h
static inline int fib_lookup(struct net *net, ...)
{
    struct fib_table *table;
    table = fib_get_table(net, RT_TABLE_LOCAL);
    table = fib_get_table(net, RT_TABLE_MAIN);
    ...
}

static inline struct fib_table *fib_get_table(struct net *net, u32 id)
{
    ptr = id == RT_TABLE_LOCAL ?
        &net->ipv4.fib_table_hash[TABLE_LOCAL_INDEX] :
        &net->ipv4.fib_table_hash[TABLE_MAIN_INDEX];
    return hlist_entry(ptr->first, struct fib_table, tb_hlist);
}
```

由上述程式碼可見，在路由過程中是根據前面步驟中確定好的命名空間 struct net *net 來查詢路由項的。不同的命名空間有不同的 net 變數，所以不同的 netns 中自然也就可以設定不同的路由表了。

網路收發過程中其他步驟也是類似的，涉及到需要隔離的地方，都是透過命名空間去查詢的。

## 五、結論

Linux 的網路 namespace 實作了獨立協定堆疊的隔離。這個說法其實不是很準確，核心網路程式碼只有一套，並沒有隔離。

它是透過為不同空間建立不同的 struct net 物件。每個 struct net 中都有獨立的路由表、iptable 等資料結構。每個裝置、每個 `socket` 上也都有指標指明自己歸屬那個 netns。透過這種方法從邏輯上看起來好像是真的有多個協定堆疊一樣。

這樣，就為一臺實際上建立出多個邏輯上的協定堆疊，為 Docker 容器的誕生提供了可能。

在上面的範例中，Docker1 和 Docker2 都可以分別擁有自己獨立的網路卡裝置，設定自己的路由規則、iptable。從而使得他們的網路功能不會相互影響。
