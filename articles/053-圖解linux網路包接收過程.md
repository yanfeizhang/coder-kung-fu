---
layout: default
title: "圖解 Linux 網路封包 (Network Packet) 接收過程"
---

# 圖解 Linux 網路封包 (Network Packet) 接收過程

> 原文標題：🔥图解Linux网络包接收过程

> 原文連結：https://mp.weixin.qq.com/s/GoYDsfy9m0wRoXi_NCfCmg

---

前面和大家分享了我在 CPU、記憶體 (Memory)、磁碟 (Disk) 上的一點淺薄的思考。今天開始我們討論 Linux 裡最重要的一個模組-網路 (Network) 模組。還是按照慣例來，讓我們從一段最簡單的程式碼開始思考。為了簡單起見，我們用 udp 來舉例，如下：

```
int main(){
    int serverSocketFd = socket(AF_INET, SOCK_DGRAM, 0);
    bind(serverSocketFd, ...);

    char buff[BUFFSIZE];
    int readCount = recvfrom(serverSocketFd, buff, BUFFSIZE, 0, ...);
    buff[readCount] = '\0';
    printf("Receive from client:%s\n", buff);
}
```

上面程式碼是非常簡單的一段 udp server 接收收據的邏輯。 當在開發視角看的時候，只要用戶端有對應的資料傳送過來，伺服器端執行   `recv_from` 後就能收到它，並把它列印出來。我們現在想知道的是，當網路封包達到網路卡 (Network Card)，直到我們的 recvfrom 收到資料，這中間，究竟都發生過什麼？

我們為什麼要了解這麼底層呢？如果你負責的應用不是高併發的，流量也不大，確實沒有必要往下看。如果你負責的是為百萬，千萬甚至過億使用者提供的服務，深入理解 Linux 系統內部是如何實作的，以及各個部分之間是如何互動對你的工作將會有非常大的幫助。本文根據 Linux 3.10，原始碼參見 [https://mirrors.edge.kernel.org/pub/linux/kernel/v3.x/](https://mirrors.edge.kernel.org/pub/linux/kernel/v3.x/)，網路卡驅動採用 Intel 的 igb 網路卡舉例。

## 一、Linux 網路封包接收總覽

在  `TCP/IP` 網路分層模型裡，整個協定堆疊被分成了實體層、鏈路層、網路層，傳輸層和應用層。實體層對應的是網路卡和網線，應用層對應的是我們常見的 Nginx，FTP 等等各種應用。Linux 實作的是鏈路層、網路層和傳輸層這三層。

在 Linux 核心 (Linux Kernel) 實作中，鏈路層協定靠網路卡驅動來實作，核心協定堆疊來實作網路層和傳輸層。核心對更上層的應用層提供  `socket` 介面來供使用者程序 (Process) 存取。我們用 Linux 的視角來看到的 `TCP/IP` 網路分層模型應該是下面這個樣子的。

在 Linux 的原始碼中，網路裝置驅動對應的邏輯位於  `driver/net/ethernet`, 其中 intel 系列網路卡的驅動在 `driver/net/ethernet/intel` 目錄下。協定堆疊模組程式碼位於 kernel 和  net 目錄。

核心和網路裝置驅動是透過中斷的方式來處理的。當裝置上有資料到達的時候，會給 CPU 的相關引腳上觸發一個電壓變化，以通知 CPU 來處理資料。對於網路模組來說，由於處理過程比較複雜和耗時，如果在中斷函式中完成所有的處理，將會導致中斷處理函式（優先順序過高）將過度佔據 CPU，將導致 CPU 無法響應其他裝置，例如滑鼠和鍵盤的訊息。因此 Linux 中斷處理函式是分上半部和下半部的。上半部是隻進行最簡單的工作，快速處理然後釋放 CPU，接著 CPU 就可以允許其他中斷進來。剩下將絕大部分的工作都放到下半部中，可以慢慢從容處理。2.4 以後的核心版本採用的下半部實作方式是軟中斷 (Softirq)，由 ksoftirqd 核心執行緒 (Thread) 全權處理。和硬中斷 (Hardware Interrupt) 不同的是，硬中斷是透過給 CPU 實體引腳施加電壓變化，而軟中斷是透過給記憶體中的一個變數的二進位值以通知軟中斷處理程式。

好了，大概瞭解了網路卡驅動、硬中斷、軟中斷和 ksoftirqd 執行緒之後，我們在這幾個概念的基礎上給出一個核心接收封包的路徑示意:

當網路卡上收到資料以後，Linux 中第一個工作的模組是網路驅動。 網路驅動會以 DMA 的方式把網路卡上收到的幀寫到記憶體裡。再向 CPU 發起一箇中斷，以通知 CPU 有資料到達。第二，當 CPU 收到中斷請求後，會去呼叫網路驅動註冊的中斷處理函式。 網路卡的中斷處理函式並不做過多工作，發出軟中斷請求，然後儘快釋放 CPU。ksoftirqd 檢測到有軟中斷請求到達，呼叫  `poll` 開始輪詢接收封包，收到後交由各級協定堆疊處理。對於 udp 封包來說，會被放到使用者 `socket` 的接收佇列中。

我們從上面這張圖中已經從整體上把握到了 Linux 對資料封包的處理過程。但是要想了解更多網路模組工作的細節，我們還得往下看。

## 二、Linux 啟動

Linux 驅動，核心協定堆疊等等模組在具備接收網路卡資料封包之前，要做很多的準備工作才行。例如要提前建立好 ksoftirqd 核心執行緒，要註冊好各個協定對應的處理函式，網路卡裝置子系統要提前初始化好，網路卡要啟動好。只有這些都 Ready 之後，我們才能真正開始接收資料封包。那麼我們現在來看看這些準備工作都是怎麼做的。

## 2.1 建立 ksoftirqd 核心程序

Linux 的軟中斷都是在專門的核心執行緒中進行的，因此我們非常有必要看一下這些程序是怎麼初始化的，這樣我們才能在後面更準確地瞭解接收封包過程。該程序數量不是 1 個，而是 N 個，其中 N 等於你的機器的核數。

系統初始化的時候在  `kernel/smpboot.c` 中呼叫了 `smpboot_register_percpu_thread`， 該函式進一步會執行到 `spawn_ksoftirqd`（位於 `kernel/softirq.c`）來建立出 softirqd 程序。

相關程式碼如下：

```
//file: kernel/softirq.c
static struct smp_hotplug_thread softirq_threads = {
    .store          = &ksoftirqd,
    .thread_should_run  = ksoftirqd_should_run,
    .thread_fn      = run_ksoftirqd,
    .thread_comm        = "ksoftirqd/%u",
};

static __init int spawn_ksoftirqd(void)
{
register_cpu_notifier(&cpu_nfb);

BUG_ON(smpboot_register_percpu_thread(&softirq_threads));

return 0;
}
early_initcall(spawn_ksoftirqd);
```

當 ksoftirqd 被建立出來以後，它就會進入自己的執行緒迴圈函式   `ksoftirqd_should_run` 和 `run_ksoftirqd` 了。不停地判斷有沒有軟中斷需要被處理。這裡需要注意的一點是，軟中斷不僅僅只有網路軟中斷，還有其他型別。

```
//file: include/linux/interrupt.h
enum
{
    HI_SOFTIRQ=0,
    TIMER_SOFTIRQ,
    NET_TX_SOFTIRQ,
    NET_RX_SOFTIRQ,
    BLOCK_SOFTIRQ,
    BLOCK_IOPOLL_SOFTIRQ,
    TASKLET_SOFTIRQ,
    SCHED_SOFTIRQ,
    HRTIMER_SOFTIRQ,
    RCU_SOFTIRQ,    /* Preferable RCU should always be the last softirq */

    NR_SOFTIRQS
};
```

## 2.2 網路子系統初始化

linux 核心透過呼叫   `subsys_initcall` 來初始化各個子系統，在原始碼目錄裡你可以 `grep` 出許多對這個函式的呼叫。這裡我們要說的是網路子系統的初始化，會執行到 `net_dev_init` 函式。

```
//file: net/core/dev.c
static int __init net_dev_init(void)
{
    ......

    for_each_possible_cpu(i) {
        struct softnet_data *sd = &per_cpu(softnet_data, i);

        memset(sd, 0, sizeof(*sd));
        skb_queue_head_init(&sd->input_pkt_queue);
        skb_queue_head_init(&sd->process_queue);
        sd->completion_queue = NULL;
        INIT_LIST_HEAD(&sd->poll_list);

        ......
    }

    ......

    open_softirq(NET_TX_SOFTIRQ, net_tx_action);
    open_softirq(NET_RX_SOFTIRQ, net_rx_action);
}
subsys_initcall(net_dev_init);
```

在這個函式里，會為每個 CPU 都申請一個   `softnet_data` 資料結構，在這個資料結構裡的 `poll_list` 是等待驅動程式將其 `poll` 函式註冊進來，稍後網路卡驅動初始化的時候我們可以看到這一過程。

另外 `open_softirq` 註冊了每一種軟中斷都註冊一個處理函式。 `NET_TX_SOFTIRQ` 的處理函式為 `net_tx_action`，  `NET_RX_SOFTIRQ` 的為 `net_rx_action`。繼續跟蹤 `open_softirq` 後發現這個註冊的方式是記錄在 `softirq_vec` 變數裡的。後面 ksoftirqd 執行緒收到軟中斷的時候，也會使用這個變數來找到每一種軟中斷對應的處理函式。

```
//file: kernel/softirq.c
void open_softirq(int nr, void (*action)(struct softirq_action *))
{
    softirq_vec[nr].action = action;
}
```

## 2.3 協定堆疊註冊

核心實作了網路層的 ip 協定，也實作了傳輸層的 tcp 協定和 udp 協定。 這些協定對應的實作函式分別是   `ip_rcv`(),`tcp_v4_rcv`() 和 `udp_rcv`()。和我們平時寫程式碼的方式不一樣的是，核心是透過註冊的方式來實作的。 Linux 核心中的 `fs_initcall` 和   `subsys_initcall` 類似，也是初始化模組的入口。`fs_initcall` 呼叫 `inet_init` 後開始網路協定堆疊註冊。 透過 `inet_init`，將這些函式註冊到了   `inet_protos` 和 `ptype_base` 資料結構中了。如下圖:

相關程式碼如下:

```
//file: net/ipv4/af_inet.c
static struct packet_type ip_packet_type __read_mostly = {
    .type = cpu_to_be16(ETH_P_IP),
    .func = ip_rcv,
};

static const struct net_protocol udp_protocol = {
.handler =  udp_rcv,
.err_handler =  udp_err,
.no_policy =    1,
.netns_ok = 1,
};

static const struct net_protocol tcp_protocol = {
.early_demux    =   tcp_v4_early_demux,
.handler    =   tcp_v4_rcv,
.err_handler    =   tcp_v4_err,
.no_policy  =   1,
.netns_ok   =   1,
};

static int __init inet_init(void)
{
......

if (inet_add_protocol(&icmp_protocol, IPPROTO_ICMP) < 0)
pr_crit("%s: Cannot add ICMP protocol\n", __func__);
if (inet_add_protocol(&udp_protocol, IPPROTO_UDP) < 0)
pr_crit("%s: Cannot add UDP protocol\n", __func__);
if (inet_add_protocol(&tcp_protocol, IPPROTO_TCP) < 0)
pr_crit("%s: Cannot add TCP protocol\n", __func__);

......

dev_add_pack(&ip_packet_type);
}
```

上面的程式碼中我們可以看到，  `udp_protocol` 結構體中的 handler 是 `udp_rcv`， `tcp_protocol` 結構體中的 handler 是 `tcp_v4_rcv`，透過 `inet_add_protocol` 被初始化了進來。

```c
int inet_add_protocol(const struct net_protocol *prot, unsigned char protocol)
{
if (!prot->netns_ok) {
pr_err("Protocol %u is not namespace aware, cannot register.\n",
protocol);
return -EINVAL;
}

return !cmpxchg((const struct net_protocol **)&inet_protos[protocol],
NULL, prot) ? 0 : -1;
}
```

`inet_add_protocol` 函式將 tcp 和 udp 對應的處理函式都註冊到了 `inet_protos` 陣列中了。再看 `dev_add_pack`(& `ip_packet_type`);這一行，  `ip_packet_type` 結構體中的 type 是協定名，func 是 `ip_rcv` 函式，在 `dev_add_pack` 中會被註冊到 `ptype_base` 雜湊表中。

```
//file: net/core/dev.c
void dev_add_pack(struct packet_type *pt)
{
    struct list_head *head = ptype_head(pt);
    ......
}


static inline struct list_head *ptype_head(const struct packet_type *pt)
{
if (pt->type == htons(ETH_P_ALL))
return &ptype_all;
else
return &ptype_base[ntohs(pt->type) & PTYPE_HASH_MASK];
}
```

這裡我們需要記住   `inet_protos` 記錄著 udp、tcp 的處理函式位址，`ptype_base` 儲存著 `ip_rcv`() 函式的處理位址。後面我們會看到軟中斷中會透過 `ptype_base` 找到   `ip_rcv` 函式位址，進而將 IP 封包正確地送到 `ip_rcv`() 中執行。在 `ip_rcv` 中將會透過 `inet_protos` 找到 tcp 或 udp 的處理函式，再而把封包轉發給   `udp_rcv`() 或 `tcp_v4_rcv`() 函式。

擴充套件一下，如果看一下   `ip_rcv` 和 `udp_rcv` 等函式的程式碼能看到很多協定的處理過程。例如， `ip_rcv` 中會處理 netfilter 和 iptable 過濾，如果你有很多或者很複雜的 netfilter 或 iptables 規則，這些規則都是在軟中斷的上下文中執行的，會加大網路延遲。再例如， `udp_rcv` 中會判斷   `socket` 接收佇列是否滿了。對應的相關核心參數 (Kernel Parameter) 是 net.core.rmem_max 和 net.core.rmem_default。如果有興趣，建議大家好好讀一下 `inet_init` 這個函式的程式碼。

## 2.4 網路卡驅動初始化

每一個驅動程式（不僅僅只是網路卡驅動）會使用   `module_init` 向核心註冊一個初始化函式，當驅動被載入時，核心會呼叫這個函式。例如 igb 網路卡驅動的程式碼位於 `drivers/net/ethernet/intel/igb/igb_main.c`

```
//file: drivers/net/ethernet/intel/igb/igb_main.c
static struct pci_driver igb_driver = {
    .name     = igb_driver_name,
    .id_table = igb_pci_tbl,
    .probe    = igb_probe,
    .remove   = igb_remove,
    ......
};

static int __init igb_init_module(void)
{
......
ret = pci_register_driver(&igb_driver);
return ret;
}
```

驅動的   `pci_register_driver` 呼叫完成後，Linux 核心就知道了該驅動的相關資訊，例如 igb 網路卡驅動的 `igb_driver_name` 和 `igb_probe` 函式位址等等。當網路卡裝置被識別以後，核心會呼叫其驅動的 probe 方法（`igb_driver` 的 probe 方法是   `igb_probe`）。驅動 probe 方法執行的目的就是讓裝置 ready ，對於 igb 網路卡，其 `igb_probe` 位於 `drivers/net/ethernet/intel/igb/igb_main.c` 下。主要執行的操作如下：

第 5 步中我們看到，網路卡驅動實作了 `ethtool` 所需要的介面，也在這裡註冊完成函式位址的註冊。當 `ethtool` 發起一個系統呼叫 (System Call) 之後，核心會找到對應操作的回呼函式。對於 igb 網路卡來說，其實作函式都在 `drivers/net/ethernet/intel/igb/igb_ethtool.c` 下。 相信你這次能徹底理解   `ethtool` 的工作原理了吧？ 這個命令之所以能檢視網路卡收發封包統計、能修改網路卡自適應模式、能調整 RX 佇列的數量和大小，是因為 `ethtool` 命令最終呼叫到了網路卡驅動的相應方法，而不是 `ethtool` 本身有這個超能力。

第 6 步註冊的 `igb_netdev_ops` 中包含的是 `igb_open` 等函式，該函式在網路卡被啟動的時候會被呼叫。

```
//file: drivers/net/ethernet/intel/igb/igb_main.c
......
static const struct net_device_ops igb_netdev_ops = {
  .ndo_open               = igb_open,
  .ndo_stop               = igb_close,
  .ndo_start_xmit         = igb_xmit_frame,
  .ndo_get_stats64        = igb_get_stats64,
  .ndo_set_rx_mode        = igb_set_rx_mode,
  .ndo_set_mac_address    = igb_set_mac,
  .ndo_change_mtu         = igb_change_mtu,
  .ndo_do_ioctl           = igb_ioctl,......
```

第 7 步中，在   `igb_probe` 初始化過程中，還呼叫到了 `igb_alloc_q_vector`。他註冊了一個 NAPI 機制所必須的 `poll` 函式，對於 igb 網路卡驅動來說，這個函式就是 `igb_poll`,如下程式碼所示。

```
static int igb_alloc_q_vector(struct igb_adapter *adapter,
int v_count, int v_idx,
int txr_count, int txr_idx,
int rxr_count, int rxr_idx)
{

......
/* initialize NAPI */
netif_napi_add(adapter->netdev, &q_vector->napi,
igb_poll, 64);

}
```

## 2.5 啟動網路卡

當上面的初始化都完成以後，就可以啟動網路卡了。回憶前面網路卡驅動初始化時，我們提到了驅動向核心註冊了 structure   `net_device_ops` 變數，它包含著網路卡啟用、傳送封包、設定 mac 位址等回呼函式（函式指標）。當啟用一個網路卡時（例如，透過 ifconfig eth0 up），`net_device_ops` 中的 `igb_open` 方法會被呼叫。它通常會做以下事情：

```
//file: drivers/net/ethernet/intel/igb/igb_main.c
static int __igb_open(struct net_device *netdev, bool resuming)
{
    /* allocate transmit descriptors */
    err = igb_setup_all_tx_resources(adapter);

    /* allocate receive descriptors */
    err = igb_setup_all_rx_resources(adapter);

    /* 註冊中斷處理函式 */
    err = igb_request_irq(adapter);
    if (err)
        goto err_req_irq;

    /* 啟用NAPI */
    for (i = 0; i < adapter->num_q_vectors; i++)
        napi_enable(&(adapter->q_vector[i]->napi));

    ......
}
```

在上面 __igb_open 函式呼叫了  `igb_setup_all_tx_resources`,和 `igb_setup_all_rx_resources`。在 `igb_setup_all_rx_resources` 這一步操作中，分配了 RingBuffer，並建立記憶體和 Rx 佇列的對映關係。（Rx Tx 佇列的數量和大小可以透過 `ethtool` 進行設定）。我們再接著看中斷函式註冊 `igb_request_irq`:

```
static int igb_request_irq(struct igb_adapter *adapter)
{
if (adapter->msix_entries) {
err = igb_request_msix(adapter);
if (!err)
goto request_done;
......
}
}

static int igb_request_msix(struct igb_adapter *adapter)
{
......
for (i = 0; i < adapter->num_q_vectors; i++) {
...
err = request_irq(adapter->msix_entries[vector].vector,
igb_msix_ring, 0, q_vector->name,
}
```

在上面的程式碼中跟蹤函式呼叫，__igb_open=>`igb_request_irq`=>  `igb_request_msix`, 在 `igb_request_msix` 中我們看到了，對於多佇列的網路卡，為每一個佇列都註冊了中斷，其對應的中斷處理函式是 `igb_msix_ring`（該函式也在 `drivers/net/ethernet/intel/igb/igb_main.c` 下）。 我們也可以看到，msix 方式下，每個 RX 佇列有獨立的  MSI-X 中斷，從網路卡硬體中斷的層面就可以設定讓收到的封包被不同的 CPU 處理。（可以透過 irqbalance ，或者修改 / `proc/irq`/ `IRQ_NUMBER` /`smp_affinity` 能夠修改和 CPU 的繫結行為）。

當做好以上準備工作以後，就可以開門迎客（資料封包）了！

## 三、迎接資料的到來

## 3.1 硬中斷處理

首先當資料幀從網線到達網路卡上的時候，第一站是網路卡的接收佇列。網路卡在分配給自己的 RingBuffer 中尋找可用的記憶體位置，找到後 DMA 引擎會把資料 DMA 到網路卡之前關聯的記憶體裡，這個時候 CPU 都是無感的。當 DMA 操作完成以後，網路卡會向 CPU 發起一個硬中斷，通知 CPU 有資料到達。

> 注意：當 RingBuffer 滿的時候，新來的資料封包將給丟棄。ifconfig 檢視網路卡的時候，可以裡面有個 overruns，表示因為環形佇列滿被丟棄的封包。如果發現有丟包，可能需要透過  `ethtool` 命令來加大環形佇列的長度。

在啟動網路卡一節，我們說到了網路卡的硬中斷註冊的處理函式是 `igb_msix_ring`。

```
//file: drivers/net/ethernet/intel/igb/igb_main.c
static irqreturn_t igb_msix_ring(int irq, void *data)
{
    struct igb_q_vector *q_vector = data;

    /* Write the ITR value calculated from the previous interrupt. */
    igb_write_itr(q_vector);

    napi_schedule(&q_vector->napi);

    return IRQ_HANDLED;
}

igb_write_itr 只是記錄一下硬體中斷頻率（據說目的是在減少對 CPU 的中斷頻率時用到）。順著 napi_schedule 呼叫一路跟蹤下去， __napi_schedule=>____napi_schedule

/* Called with irq disabled */
static inline void ____napi_schedule(struct softnet_data *sd,
                     struct napi_struct *napi)
{
    list_add_tail(&napi->poll_list, &sd->poll_list);
    __raise_softirq_irqoff(NET_RX_SOFTIRQ);
}
```

這裡我們看到，  `list_add_tail` 修改了 CPU 變數 `softnet_data` 裡的 `poll_list`，將驅動 `napi_struct` 傳過來的   `poll_list` 新增了進來。
其中 `softnet_data` 中的 `poll_list` 是一個雙向列表，其中的裝置都帶有輸入幀等著被處理。緊接著__raise_softirq_irqoff 觸發了一個軟中斷 `NET_RX_SOFTIRQ`， 這個所謂的觸發過程只是對一個變數進行了一次或運算而已。

```
//file:kernel/softirq.c
void __raise_softirq_irqoff(unsigned int nr)
{
    trace_softirq_raise(nr);
    or_softirq_pending(1UL << nr);
}
```

```
//file: include/linux/interrupt.h
#define or_softirq_pending(x)  (local_softirq_pending() |= (x))
```

```
//file: include/linux/irq_cpustat.h
#define local_softirq_pending() \
    __IRQ_STAT(smp_processor_id(), __softirq_pending)
```

我們說過，Linux 在硬中斷裡只完成簡單必要的工作，剩下的大部分的處理都是轉交給軟中斷的。透過上面程式碼可以看到，硬中斷處理過程真的是非常短。只是記錄了一個暫存器，修改了一下下 CPU 的   `poll_list`，然後發出個軟中斷。就這麼簡單，硬中斷工作就算是完成了。

## 3.2 ksoftirqd 核心執行緒處理軟中斷

核心執行緒初始化的時候，我們介紹了 ksoftirqd 中兩個執行緒函式 `ksoftirqd_should_run` 和 `run_ksoftirqd`。其中 `ksoftirqd_should_run` 程式碼如下：

```
static int ksoftirqd_should_run(unsigned int cpu)
{
return local_softirq_pending();
}
```

## define `local_softirq_pending`() \
__IRQ_STAT(`smp_processor_id`(), __softirq_pending)

這裡看到和硬中斷中呼叫了同一個函式   `local_softirq_pending`。使用方式不同的是硬中斷位置是為了寫入標記，這裡僅僅只是讀取。如果硬中斷中設定了 `NET_RX_SOFTIRQ`,這裡自然能讀取的到。接下來會真正進入執行緒函式中 `run_ksoftirqd` 處理：

```
static void run_ksoftirqd(unsigned int cpu)
{
local_irq_disable();
if (local_softirq_pending()) {
__do_softirq();
rcu_note_context_switch(cpu);
local_irq_enable();
cond_resched();
return;
}
local_irq_enable();
}
```

在 __do_softirq 中，判斷根據當前 CPU 的軟中斷型別，呼叫其註冊的 action 方法。

asmlinkage void __do_softirq(void)
{
do {
if (pending & 1) {
unsigned int `vec_nr` = h - `softirq_vec`;
```
int prev_count = preempt_count();

```
...
`trace_softirq_entry`(`vec_nr`);
`h->action`(h);
`trace_softirq_exit`(`vec_nr`);
...
}
h++;
pending >>= 1;
} while (pending);
}

在網路子系統初始化小節， 我們看到我們為   `NET_RX_SOFTIRQ` 註冊了處理函式 `net_rx_action`。所以 `net_rx_action` 函式就會被執行到了。

這裡需要注意一個細節，硬中斷中設定軟中斷標記，和 ksoftirq 的判斷是否有軟中斷到達，都是根據 `smp_processor_id`() 的。這意味著只要硬中斷在哪個 CPU 上被響應，那麼軟中斷也是在這個 CPU 上處理的。所以說，如果你發現你的 Linux 軟中斷 CPU 消耗都集中在一個核上的話，做法是要把調整硬中斷的 CPU 親和性，來將硬中斷打散到不同的 CPU 核上去。

我們再來把精力集中到這個核心函式 `net_rx_action` 上來。

```
//file:net/core/dev.c
static void net_rx_action(struct softirq_action *h)
{
    struct softnet_data *sd = &__get_cpu_var(softnet_data);
    unsigned long time_limit = jiffies + 2;
    int budget = netdev_budget;
    void *have;

    local_irq_disable();

    while (!list_empty(&sd->poll_list)) {
        ......
        n = list_first_entry(&sd->poll_list, struct napi_struct, poll_list);

        work = 0;
        if (test_bit(NAPI_STATE_SCHED, &n->state)) {
            work = n->poll(n, weight);
            trace_napi_poll(n);
        }

        budget -= work;
    }
}
```

> 有同學問在硬中斷中新增裝置到   `poll_list`，會不會重複新增呢？答案是不會的，在軟中斷處理函式 `net_rx_action` 這裡一進來就呼叫 `local_irq_disable` 把所有的硬中斷都給關了，不會讓硬中斷重複新增 `poll_list` 的機會。在硬中斷的處理函式中本身也有類似的判斷機制，打磨了幾十年的核心考慮在細節考慮上還是很完善的。

函式開頭的   `time_limit` 和 budget 是用來控制 `net_rx_action` 函式主動退出的，目的是保證網路封包的接收不霸佔 CPU 不放。 等下次網路卡再有硬中斷過來的時候再處理剩下的接收資料封包。其中 budget 可以透過核心參數調整。 這個函式中剩下的核心邏輯是取得到當前 CPU 變數 `softnet_data`，對其 `poll_list` 進行遍歷, 然後執行到網路卡驅動註冊到的   `poll` 函式。對於 igb 網路卡來說，就是 igb 驅動裡的 `igb_poll` 函式了。

/**
* `igb_poll` - NAPI Rx polling callback
* @napi: napi polling structure
* @budget: count of how many packets we should handle
* */
```
static int igb_poll(struct napi_struct *napi, int budget)
{
...
if (q_vector->tx.ring)
clean_complete = igb_clean_tx_irq(q_vector);

if (q_vector->rx.ring)
clean_complete &= igb_clean_rx_irq(q_vector, budget);
...
}
```

在讀取操作中，  `igb_poll` 的重點工作是對 `igb_clean_rx_irq` 的呼叫。

```
static bool igb_clean_rx_irq(struct igb_q_vector *q_vector, const int budget)
{
...

do {

/* retrieve a buffer from the ring */
skb = igb_fetch_rx_buffer(rx_ring, rx_desc, skb);

/* fetch next buffer in frame if non-eop */
if (igb_is_non_eop(rx_ring, rx_desc))
continue;
}

/* verify the packet layout is correct */
if (igb_cleanup_headers(rx_ring, rx_desc, skb)) {
skb = NULL;
continue;
}

/* populate checksum, timestamp, VLAN, and protocol */
igb_process_skb_fields(rx_ring, rx_desc, skb);

napi_gro_receive(&q_vector->napi, skb);
}
```

`igb_fetch_rx_buffer` 和 `igb_is_non_eop` 的作用就是把資料幀從 RingBuffer 上取下來。為什麼需要兩個函式呢？因為有可能幀要佔多個 RingBuffer，所以是在一個迴圈中取得的，直到幀尾部。取得下來的一個資料幀用一個 `sk_buff` 來表示。收取完資料以後，對其進行一些校驗，然後開始設定 sbk 變數的 timestamp, VLAN id, protocol 等欄位。接下來進入到 `napi_gro_receive` 中:

```
//file: net/core/dev.c
gro_result_t napi_gro_receive(struct napi_struct *napi, struct sk_buff *skb)
{
    skb_gro_reset_offset(skb);

    return napi_skb_finish(dev_gro_receive(napi, skb), skb);
}
```

`dev_gro_receive` 這個函式代表的是網路卡 GRO 特性，可以簡單理解成把相關的小封包合併成一個大封包就行，目的是減少傳送給網路堆疊的封包數，這有助於減少 CPU 的使用量。我們暫且忽略，直接看 `napi_skb_finish`, 這個函式主要就是呼叫了 `netif_receive_skb`。

```
//file: net/core/dev.c
static gro_result_t napi_skb_finish(gro_result_t ret, struct sk_buff *skb)
{
    switch (ret) {
    case GRO_NORMAL:
        if (netif_receive_skb(skb))
            ret = GRO_DROP;
        break;
    ......
}
```

在   `netif_receive_skb` 中，資料封包將被送到協定堆疊中。

## 3.3 網路協定堆疊處理

`netif_receive_skb` 函式會根據包的協定，假如是 udp 封包，會將封包依次送到 `ip_rcv`(), `udp_rcv`() 協定處理函式中進行處理。

```
//file: net/core/dev.c
int netif_receive_skb(struct sk_buff *skb)
{
    //RPS處理邏輯，先忽略
    ......

    return __netif_receive_skb(skb);
}

static int __netif_receive_skb(struct sk_buff *skb)
{
    ......
    ret = __netif_receive_skb_core(skb, false);
}

static int __netif_receive_skb_core(struct sk_buff *skb, bool pfmemalloc)
{
    ......

    //pcap邏輯，這裡會將資料送入擷取封包點。tcpdump就是從這個入口取得包的
    list_for_each_entry_rcu(ptype, &ptype_all, list) {
        if (!ptype->dev || ptype->dev == skb->dev) {
            if (pt_prev)
                ret = deliver_skb(skb, pt_prev, orig_dev);
            pt_prev = ptype;
        }
    }

    ......

    list_for_each_entry_rcu(ptype,
            &ptype_base[ntohs(type) & PTYPE_HASH_MASK], list) {
        if (ptype->type == type &&
            (ptype->dev == null_or_dev || ptype->dev == skb->dev ||
             ptype->dev == orig_dev)) {
            if (pt_prev)
                ret = deliver_skb(skb, pt_prev, orig_dev);
            pt_prev = ptype;
        }
    }
}
```

在 __netif_receive_skb_core 中，我看著了原來經常使用的  `tcpdump` 的擷取封包點，很是激動，看來讀一遍原始碼時間真的沒白浪費。接著 __netif_receive_skb_core 取出 protocol，它會從資料封包中取出協定資訊，然後遍歷註冊在這個協定上的回呼函式列表。 `ptype_base` 是一個 hash table，在協定註冊小節我們提到過。 `ip_rcv` 函式位址就是存在這個 hash table 中的。

```
//file: net/core/dev.c
static inline int deliver_skb(struct sk_buff *skb,
                  struct packet_type *pt_prev,
                  struct net_device *orig_dev)
{
    ......
    return pt_prev->func(skb, skb->dev, pt_prev, orig_dev);
}

pt_prev->func 這一行就呼叫到了協定層註冊的處理函式了。對於 IP 封包來講，就會進入到  ip_rcv（如果是 arp 包的話，會進入到 arp_rcv）。
```

## 3.4 IP 協定層處理

我們再來大致看一下 linux 在 ip 協定層都做了什麼，包又是怎麼樣進一步被送到 udp 或 tcp 協定處理函式中的。

```
//file: net/ipv4/ip_input.c
int ip_rcv(struct sk_buff *skb, struct net_device *dev, struct packet_type *pt, struct net_device *orig_dev)
{
    ......

    return NF_HOOK(NFPROTO_IPV4, NF_INET_PRE_ROUTING, skb, dev, NULL,
               ip_rcv_finish);
}
```

這裡   `NF_HOOK` 是一個鉤子函式，當執行完註冊的鉤子後就會執行到最後一個參數指向的函式 `ip_rcv_finish`。

```
static int ip_rcv_finish(struct sk_buff *skb)
{
......

if (!skb_dst(skb)) {
int err = ip_route_input_noref(skb, iph->daddr, iph->saddr,
iph->tos, skb->dev);
...
}

......

return dst_input(skb);
}
```

跟蹤   `ip_route_input_noref` 後看到它又呼叫了 `ip_route_input_mc`。 在 `ip_route_input_mc` 中，函式 `ip_local_deliver` 被賦值給了 dst.input, 如下：

```
//file: net/ipv4/route.c
static int ip_route_input_mc(struct sk_buff *skb, __be32 daddr, __be32 saddr,
                u8 tos, struct net_device *dev, int our)
{
    if (our) {
        rth->dst.input= ip_local_deliver;
        rth->rt_flags |= RTCF_LOCAL;
    }
}
```

所以回到   `ip_rcv_finish` 中的 return `dst_input`(`skb`)。

/* Input packet from network to transport.  */
```
static inline int dst_input(struct sk_buff *skb)
{
return skb_dst(skb)->input(skb);
}
```

`skb_dst`(`skb`)->input 呼叫的 input 方法就是路由子系統賦的 `ip_local_deliver`。

```
//file: net/ipv4/ip_input.c
int ip_local_deliver(struct sk_buff *skb)
{
    /*
```

* Reassemble IP fragments.
* /

if (`ip_is_fragment`(`ip_hdr`(`skb`))) {
if (`ip_defrag`(`skb`, `IP_DEFRAG_LOCAL_DELIVER`))
return 0;
}

return `NF_HOOK`(`NFPROTO_IPV4`, `NF_INET_LOCAL_IN`, `skb`, `skb->dev`, NULL,
`ip_local_deliver_finish`);
}

```
static int ip_local_deliver_finish(struct sk_buff *skb)
{
......

int protocol = ip_hdr(skb)->protocol;
    const struct net_protocol *ipprot;

    ipprot = rcu_dereference(inet_protos[protocol]);
    if (ipprot != NULL) {
        ret = ipprot->handler(skb);
    }
}
```

如協定註冊小節看到   `inet_protos` 中儲存著 `tcp_rcv`() 和 `udp_rcv`() 的函式位址。這裡將會根據封包中的協定型別選擇進行分發,在這裡 `skb` 封包將會進一步被派送到更上層的協定中， udp 和 tcp。

## 3.5 UDP 協定層處理

在協定註冊小節的時候我們說過，udp 協定的處理函式是 `udp_rcv`。

```
//file: net/ipv4/udp.c
int udp_rcv(struct sk_buff *skb)
{
    return __udp4_lib_rcv(skb, &udp_table, IPPROTO_UDP);
}

int __udp4_lib_rcv(struct sk_buff *skb, struct udp_table *udptable,
           int proto)
{
    sk = __udp4_lib_lookup_skb(skb, uh->source, uh->dest, udptable);

    if (sk != NULL) {
        int ret = udp_queue_rcv_skb(sk, skb
    }

    icmp_send(skb, ICMP_DEST_UNREACH, ICMP_PORT_UNREACH, 0);
}
```

__udp4_lib_lookup_skb 是根據 `skb` 來尋找對應的  `socket`，當找到以後將資料封包放到 `socket` 的快取 (Cache) 佇列裡。如果沒有找到，則傳送一個目標不可達的 ICMP 封包。

```
//file: net/ipv4/udp.c
int udp_queue_rcv_skb(struct sock *sk, struct sk_buff *skb)
{
    ......

    if (sk_rcvqueues_full(sk, skb, sk->sk_rcvbuf))
        goto drop;

        rc = 0;

    ipv4_pktinfo_prepare(skb);
    bh_lock_sock(sk);
    if (!sock_owned_by_user(sk))
        rc = __udp_queue_rcv_skb(sk, skb);
    else if (sk_add_backlog(sk, skb, sk->sk_rcvbuf)) {
        bh_unlock_sock(sk);
        goto drop;
    }
    bh_unlock_sock(sk);

    return rc;
}
```

`sock_owned_by_user` 判斷的是使用者是不是正在這個 `socket` 上進行系統呼叫，如果沒有，那就可以直接放到 `socket` 的接收佇列中。如果有，那就透過   `sk_add_backlog` 把資料封包新增到 backlog 佇列。 當使用者釋放的 `socket` 的時候，核心會檢查 backlog 佇列，如果有資料再移動到接收佇列中。

`sk_rcvqueues_full` 接收佇列如果滿了的話，將直接把封包丟棄。接收佇列大小受核心參數 net.core.rmem_max 和 net.core.rmem_default 影響。

## 四、recvfrom 系統呼叫

花開兩朵，各表一枝。 上面我們說完了整個 Linux 核心對資料封包的接收和處理過程，最後把資料封包放到 `socket` 的接收佇列中了。那麼我們再回頭看使用者程序呼叫 recvfrom 後是發生了什麼。 我們在程式碼裡呼叫的 recvfrom 是一個 glibc 的庫函式，該函式在執行後會將使用者進行陷入到核心態 (Kernel Mode)，進入到 Linux 實作的系統呼叫 `sys_recvfrom`。在理解 Linux 對 `sys_recvfrom` 之前，我們先來簡單看一下  `socket` 這個核心資料結構。這個資料結構太大了，我們只把對和我們今天主題相關的內容畫出來，如下：

`socket` 資料結構中的 const struct `proto_ops` 對應的是協定的方法集合。每個協定都會實作不同的方法集，對於 IPv4 Internet 協定族來說,每種協定都有對應的處理方法，如下。對於 udp 來說，是透過 `inet_dgram_ops` 來定義的，其中註冊了 `inet_recvmsg` 方法。

```
//file: net/ipv4/af_inet.c
const struct proto_ops inet_stream_ops = {
    ......
    .recvmsg       = inet_recvmsg,
    .mmap          = sock_no_mmap,
    ......
}
const struct proto_ops inet_dgram_ops = {
    ......
    .sendmsg       = inet_sendmsg,
    .recvmsg       = inet_recvmsg,
    ......
}
```

`socket` 資料結構中的另一個資料結構 struct sock * sk 是一個非常大，非常重要的子結構體。其中的 `sk_prot` 又定義了二級處理函式。對於 udp 協定來說，會被設定成 udp 協定實作的方法集 `udp_prot`。

```
//file: net/ipv4/udp.c
struct proto udp_prot = {
    .name          = "UDP",
    .owner         = THIS_MODULE,
    .close         = udp_lib_close,
    .connect       = ip4_datagram_connect,
    ......
    .sendmsg       = udp_sendmsg,
    .recvmsg       = udp_recvmsg,
    .sendpage      = udp_sendpage,
    ......
}
```

看完了   `socket` 變數之後，我們再來看 `sys_recvfrom` 的實作過程。

在  `inet_recvmsg` 呼叫了 `sk->sk_prot->recvmsg`。

```
//file: net/ipv4/af_inet.c
int inet_recvmsg(struct kiocb *iocb, struct socket *sock, struct msghdr *msg,
         size_t size, int flags)
{
    ......
    err = sk->sk_prot->recvmsg(iocb, sk, msg, size, flags & MSG_DONTWAIT,
                   flags & ~MSG_DONTWAIT, &addr_len);
    if (err >= 0)
        msg->msg_namelen = addr_len;
    return err;
}
```

上面我們說過這個對於 udp 協定的   `socket` 來說，這個 `sk_prot` 就是 `net/ipv4/udp.c` 下的 struct proto `udp_prot`。由此我們找到了 `udp_recvmsg` 方法。

```
//file: net/core/datagram.c:EXPORT_SYMBOL(__skb_recv_datagram);
struct sk_buff *__skb_recv_datagram(struct sock *sk, unsigned int flags,
                    int *peeked, int *off, int *err)
{
    ......
    do {
        struct sk_buff_head *queue = &sk->sk_receive_queue;
        skb_queue_walk(queue, skb) {
            ......
        }

        /* User doesn't want to wait */
        error = -EAGAIN;
        if (!timeo)
            goto no_packet;
    } while (!wait_for_more_packets(sk, err, &timeo, last));
}
}

終於我們找到了我們想要看的重點，在上面我們看到了所謂的讀取過程，就是存取  sk->sk_receive_queue。如果沒有資料，且使用者也允許等待，則將呼叫  wait_for_more_packets() 執行等待操作，它加入會讓使用者程序進入睡眠狀態。
```

## 五、總結

網路模組是 Linux 核心中最複雜的模組了，看起來一個簡簡單單的接收封包過程就涉及到許多核心元件之間的互動，如網路卡驅動、協定堆疊，核心 ksoftirqd 執行緒等。
看起來很複雜，本文想透過原始碼 + 圖示的方式，儘量以容易理解的方式來將核心接收封包過程講清楚。現在讓我們再串一串整個接收封包過程。

當使用者執行完  recvfrom 呼叫後，使用者程序就透過系統呼叫進行到核心態工作了。如果接收佇列沒有資料，程序就進入睡眠狀態被作業系統 (Operating System) 掛起。這塊相對比較簡單，剩下大部分的戲份都是由 Linux 核心其他模組來表演了。

首先在開始接收封包之前，Linux 要做許多的準備工作：

- 建立 ksoftirqd 執行緒，為它設定好它自己的執行緒函式，後面就指望著它來處理軟中斷呢。
- 協定堆疊註冊，linux 要實作許多協定，例如 arp，icmp，ip，udp，tcp，每一個協定都會將自己的處理函式註冊一下，方便包來了迅速找到對應的處理函式
- 網路卡驅動初始化，每個驅動都有一個初始化函式，核心會讓驅動也初始化一下。在這個初始化過程中，把自己的 DMA 準備好，把 NAPI 的 `poll` 函式位址告訴核心
- 啟動網路卡，分配 RX，TX 佇列，註冊中斷對應的處理函式

以上是核心準備接收封包之前的重要工作，當上面都 ready 之後，就可以開啟硬中斷，等待資料封包的到來了。

當資料到到來了以後，第一個迎接它的是網路卡：

- 網路卡將資料幀 DMA 到記憶體的 RingBuffer 中，然後向 CPU 發起中斷通知
- CPU 響應中斷請求，呼叫網路卡啟動時註冊的中斷處理函式
- 中斷處理函式幾乎沒做什麼，就發起了軟中斷請求
- 核心執行緒 ksoftirqd 執行緒發現有軟中斷請求到來，先關閉硬中斷
- ksoftirqd 執行緒開始呼叫驅動的   `poll` 函式接收封包
- `poll` 函式將受到的包送到協定堆疊註冊的 `ip_rcv` 函式中
- `ip_rcv` 函式再講包送到 `udp_rcv` 函式中（對於 tcp 封包就送到 `tcp_rcv`）

現在我們可以回到開篇的問題了，我們在使用者層看到的簡單一行 recvfrom,Linux 核心要替我們做如此之多的工作，才能讓我們順利收到資料。這還是簡簡單單的 UDP ，如果是 TCP ，核心要做的工作更多，不由得感嘆核心的開發者們真的是用心良苦。

理解了整個接收封包過程以後，我們就能明確知道 Linux 收一個包的 CPU 開銷了。首先第一塊是使用者程序呼叫系統呼叫陷入核心態的開銷。第二塊是 CPU 響應包的硬中斷的 CPU 開銷。第三塊是 ksoftirqd 核心執行緒的軟中斷上下文花費的。後面我們再專門發一篇文章實際觀察一下這些開銷。

另外網路收發中有很多末只細節我們並沒有展開了說，例如說 no NAPI， GRO，RPS 等。因為我覺得說的太對了反而會影響大家對整個流程的把握，所以儘量只保留主框架了，少即是多！
