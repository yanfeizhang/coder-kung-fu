---
layout: default
title: "Linux 網路封包 (Network Packet) 接收過程的監控與調校"
---

# Linux 網路封包 (Network Packet) 接收過程的監控與調校

> 原文標題：Linux网络包接收过程的监控与调优

> 原文連結：https://mp.weixin.qq.com/s/6iQ-OhEbQJbEcgi9kakSjg

---

上一篇文章中《圖解 Linux 網路封包接收過程》，我們梳理了在 Linux 系統下一個資料封包被接收的整個過程。Linux 核心 (Linux Kernel) 對網路封包的接收過程大致可以分為接收到 RingBuffer、硬中斷 (Hardware Interrupt) 處理、ksoftirqd 軟中斷 (Softirq) 處理幾個過程。其中在 ksoftirqd 軟中斷處理中，把資料封包從 RingBuffer 中摘下來，送到協定堆疊的處理，再之後送到使用者程序 (Process)  `socket` 的接收佇列中。

理解了 Linux 工作原理之後，還有更重要的兩件事情。第一是動手監控，會實際檢視網路封包接收的整體情況。第二是調校，當你的伺服器有問題的時候，你能找到瓶頸所在，並會利用核心開放的參數進行調節。

## 先說幾個工具

在正式內容開始之前，我們先來了解幾個 Linux 下監控網路卡 (Network Card) 時可用的工具。

```
# ethtool

```
首先第一個工具就是我們在上文中提到的   `ethtool`，它用來檢視和設定網路卡參數。這個工具其實本身只是提供幾個通用介面，真正的實作是都是在網路卡驅動中的。正因為該工具是由驅動直接實作的，所以個人覺得它最重要。

該命令比較複雜，我們選幾個今天能用到的說

- -i 顯示網路卡驅動的資訊，如驅動的名稱、版本等
- -S  檢視網路卡收發封包的統計情況
- `-g/-G` 檢視或者修改 RingBuffer 的大小
- -l /-L 檢視或者修改網路卡佇列數
- -c /-C 檢視或者修改硬中斷合併策略

實際檢視一下網路卡驅動：

```
# ethtool -i eth0
driver: ixgbe
......
```

這裡看到我的機器上網路卡驅動程式是 ixgbe。有了驅動名稱，就可以在原始碼中找到對應的程式碼了。對於  ixgbe 來說，其驅動的原始碼位於  `drivers/net/ethernet/intel/ixgbe` 目錄下。 ixgbe_ethtool.c 下都是實作的供 `ethtool` 使用的相關函式，如果 `ethtool` 哪裡有不理解的，就可以透過這種方式查詢到原始碼來讀。另外我們前文《圖解 Linux 網路封包接收過程》裡提到的 NAPI 接收封包時的 `poll` 回呼函式，啟動網路卡時的 open 函式都是在這裡實作的。

## ifconfig

網路 (Network) 管理工具 ifconfig 不只是可以為網路卡設定 ip，啟動或者禁用網路卡，也包含了一些網路卡的統計資訊。

```
eth0: flags=4163<UP,BROADCAST,RUNNING,MULTICAST>  mtu 1500
        inet 10.162.42.51  netmask 255.255.248.0  broadcast 10.162.47.255
        inet6 fe80::6e0b:84ff:fed5:88d1  prefixlen 64  scopeid 0x20<link>
        ether 6c:0b:84:d5:88:d1  txqueuelen 1000  (Ethernet)
        RX packets 2953454  bytes 414212810 (395.0 MiB)
        RX errors 0  dropped 4636605  overruns 0  frame 0
        TX packets 127887  bytes 82943405 (79.1 MiB)
        TX errors 0  dropped 0 overruns 0  carrier 0  collisions 0
```

- RX packets：接收的總封包數
- RX bytes：接收的位元組數
- RX errors：表示總的接收封包的錯誤數量
- RX dropped：資料封包已經進入了 Ring Buffer，但是由於其他原因導致的丟包
- RX overruns：表示了 fifo 的 overruns，這是由於 Ring Buffer 不足導致的丟包

## 偽檔案系統 (File System)/proc

Linux 核心提供了 /proc 偽檔案系統，透過/proc 可以檢視核心內部資料結構、改變核心設定。我們先跑一下題，看一下這個偽檔案系統裡都有什麼：

- `/proc/sys` 目錄可以檢視或修改核心參數 (Kernel Parameter)
- `/proc/cpuinfo` 可以檢視 CPU 資訊
- `/proc/meminfo` 可以檢視記憶體 (Memory) 資訊
- `/proc/interrupts` 統計所有的硬中斷
- `/proc/softirqs` 統計的所有的軟中斷資訊
- `/proc/slabinfo` 統計了核心資料結構的 slab 記憶體使用情況
- `/proc/net/dev` 可以看到一些網路卡統計資料

詳細聊下偽檔案 `/proc/net/dev`，透過它可以看到核心中對網路卡的一些相關統計。包含了以下資訊：

- bytes: 傳送或接收的資料的總位元組數
- packets: 介面傳送或接收的資料封包總數
- errs: 由裝置驅動程式檢測到的傳送或接收錯誤的總數
- drop: 裝置驅動程式丟棄的資料封包總數
- fifo: FIFO 緩衝區錯誤的數量
- frame: The number of packet framing errors.（分組幀錯誤的數量）
- colls: 介面上檢測到的衝突數

所以，偽檔案 `/proc/net/dev` 也可以作為我們檢視網路卡工作統計資料的工具之一。

## 偽檔案系統 sysfs

sysfs 和/proc 類似，也是一個偽檔案系統，但是比 proc 更新，結構更清晰。 其中的 `/sys/class/net/eth0/statistics`/ 也包含了網路卡的統計資訊。

```
# cd /sys/class/net/eth0/statistics/
# grep . * | grep tx
tx_aborted_errors:0
tx_bytes:170699510
tx_carrier_errors:0
tx_compressed:0
tx_dropped:0
tx_errors:0
tx_fifo_errors:0
tx_heartbeat_errors:0
tx_packets:262330
tx_window_errors:0
```

好了，簡單瞭解過這幾個工具以後，讓我們正式開始今天的行程。

## RingBuffer 監控與調校

前面我們看到，當網線中的資料幀到達網路卡後，第一站就是 RingBuffer（網路卡透過 DMA 機制將資料幀送到 RingBuffer 中）。因此我們第一個要監控和調校的就是網路卡的 RingBuffer，我們使用 `ethtool` 來來檢視一下 Ringbuffer。

```
# ethtool -g eth0
Ring parameters for eth0:
Pre-set maximums:
RX:     4096
RX Mini:    0
RX Jumbo:   0
TX:     4096
Current hardware settings:
RX:     512
RX Mini:    0
RX Jumbo:   0
TX:     512
```

這裡看到我手頭的網路卡設定 RingBuffer 最大允許設定到 4096，目前的實際設定是 512。

> 這裡有一個小細節，  `ethtool` 檢視到的是實際是 Rx bd 的大小。Rx bd 位於網路卡中，相當於一個指標。RingBuffer 在記憶體中，Rx bd 指向 RingBuffer。Rx bd 和 RingBuffer 中的元素是一一對應的關係。在網路卡啟動的時候，核心會為網路卡的 Rx bd 在記憶體中分配 RingBuffer，並設定好對應關係。

在 Linux 的整個網路堆疊中，RingBuffer 起到一個任務的收發中轉站的角色。對於接收過程來講，網路卡負責往 RingBuffer 中寫入收到的資料幀，ksoftirqd 核心執行緒 (Thread) 負責從中取走處理。只要 ksoftirqd 執行緒工作的足夠快，RingBuffer 這個中轉站就不會出現問題。但是我們設想一下，假如某一時刻，瞬間來了特別多的封包，而 ksoftirqd 處理不過來了，會發生什麼？這時 RingBuffer 可能瞬間就被填滿了，後面再來的封包網路卡直接就會丟棄，不做任何處理！

那我們怎麼樣能看一下，我們的伺服器上是否有因為這個原因導致的丟包呢？ 前面我們介紹的四個工具都可以檢視這個丟包統計，拿 `ethtool` 來舉例：

```
# ethtool -S eth0
......
rx_fifo_errors: 0
tx_fifo_errors: 0
```

`rx_fifo_errors` 如果不為 0 的話（在 ifconfig 中體現為 overruns 指標增長），就表示有包因為 RingBuffer 裝不下而被丟棄了。那麼怎麼解決這個問題呢？很自然首先我們想到的是，加大 RingBuffer 這個「中轉倉庫」的大小。透過 `ethtool` 就可以修改。

```
# ethtool -G eth1 rx 4096 tx 4096
```

這樣網路卡會被分配更大一點的」中轉站「，可以解決偶發的瞬時的丟包。不過這種方法有個小副作用，那就是排隊的封包過多會增加處理網路封包的延遲。所以另外一種解決思考更好，。那就是讓核心處理網路封包的速度更快一些，而不是讓網路封包傻傻地在 RingBuffer 中排隊。怎麼加快核心消費 RingBuffer 中任務的速度呢，彆著急，我們繼續往下看...

## 硬中斷監控與調校

在資料被接收到 RingBuffer 之後，下一個執行就是就是硬中斷的發起。我們先來檢視硬中斷，然後再聊下怎麼最佳化。

## 監控

硬中斷的情況可以透過核心提供的偽檔案 `/proc/interrupts` 來進行檢視。

```
$ cat  /proc/interrupts
           CPU0       CPU1       CPU2       CPU3
  0:         34          0          0          0   IO-APIC-edge      timer
 ......
 27:        351          0          0 1109986815   PCI-MSI-edge      virtio1-input.0
 28:       2571          0          0          0   PCI-MSI-edge      virtio1-output.0
 29:          0          0          0          0   PCI-MSI-edge      virtio2-config
 30:    4233459 1986139461     244872     474097   PCI-MSI-edge      virtio2-input.0
 31:          3          0          2          0   PCI-MSI-edge      virtio2-output.0
```

上述結果是我手頭的一臺虛機的輸出結果。上面包含了非常豐富的資訊，讓我們一一道來：

- 網路卡的輸入佇列 virtio1-input.0 的中斷號是 27
- 27 號中斷都是由 CPU3 來處理的
- 總的中斷次數是 1109986815。

這裡有兩個細節我們需要關注一下。
1. 為什麼輸入佇列的中斷都在 CPU3 上呢？
這是因為核心的一個設定，在偽檔案系統中可以檢視到。

```
#cat /proc/irq/27/smp_affinity
8
```

`smp_affinity` 裡是 CPU 的親和性的繫結，8 是二進位的 1000,第 4 位為 1，代表的就是第 4 個 CPU 核心-CPU3。

2. 對於接收封包來過程來講，硬中斷的總次數表示的是 Linux 接收封包總數嗎？
不是，硬體中斷次數不代表總的網路封包數。第一網路卡可以設定中斷合併，多個網路幀可以只發起一次中斷。第二 NAPI 執行的時候會關閉硬中斷，透過 `poll` 來接收封包。

## 多佇列網路卡調校

現在的主流網路卡基本上都是支援多佇列的，我們可以透過將不同的佇列分給不同的 CPU 核心來處理，從而加快 Linux 核心處理網路封包的速度。這是最為有用的一個最佳化手段。

每一個佇列都有一箇中斷號，可以獨立向某個 CPU 核心發起硬中斷請求，讓 CPU 來   `poll` 封包。透過將接收進來的包被放到不同的記憶體佇列裡，多個 CPU 就可以同時分別向不同的佇列發起消費了。這個特性叫做 RSS（Receive Side Scaling，接收端擴充套件）。透過 `ethtool` 工具可以檢視網路卡的佇列情況。

```
# ethtool -l eth0
Channel parameters for eth0:
Pre-set maximums:
RX:        0
TX:        0
Other:        1
Combined:    63
Current hardware settings:
RX:        0
TX:        0
Other:        1
Combined:    8
```

上述結果表示當前網路卡支援的最大佇列數是 63，當前開啟的佇列數是 8。對於這個設定來講，最多同時可以有 8 個核心來參與網路接收封包。如果你想提高核心接收封包的能力，直接簡單加大佇列數就可以了，這比加大 RingBuffer 更為有用。因為加大 RingBuffer 只是給個更大的空間讓網路幀能繼續排隊，而加大佇列數則能讓包更早地被核心處理。`ethtool` 修改佇列數量方法如下：

```
#ethtool -L eth0 combined 32
```

我們前文說過，硬中斷髮生在哪一個核上，它發出的軟中斷就由哪個核來處理。所有透過加大網路卡佇列數，這樣硬中斷工作、軟中斷工作都會有更多的核心參與進來。

> 每一個佇列都有一箇中斷號，每一箇中斷號都是繫結在一個特定的 CPU 上的。如果你不滿意某一箇中斷的 CPU 繫結，可以透過修改 `/proc/irq`/{中斷號}/`smp_affinity` 來實作。

一般處理到這裡，網路封包的接收就沒有大問題了。但如果你有更高的追求，或者是說你並沒有更多的 CPU 核心可以參與進來了，那怎麼辦？放心，我們也還有方法提高單核的處理網路封包的接收速度。

## 硬中斷合併

先來講一個實際中的例子，假如你是一位開發同學，和你對口的產品經理一天有 10 個小需求需要讓你幫忙來處理。她對你有兩種中斷方式：

- 第一種：產品經理想到一個需求，就過來找你，和你描述需求細節，然後讓你幫你來改
- 第二種：產品經理想到需求後，不來打擾你，等攢夠 5 個來找你一次，你集中處理

我們現在不考慮及時性，只考慮你的工作整體效率，你覺得那種方案下你的工作效率會高呢？或者換句話說，你更喜歡哪一種工作狀態呢？很明顯，只要你是一個正常的開發，都會覺得第二種方案更好。對人腦來講，頻繁的中斷會打亂你的計劃，你腦子裡剛才剛想到一半技術方案可能也就廢了。當產品經理走了以後，你再想撿起來剛被中斷之的工作的時候，很可能得花點時間回憶一會兒才能繼續工作。

對於 CPU 來講也是一樣，CPU 要做一件新的事情之前，要載入該程序的位址空間，load 程序程式碼，讀取程序資料，各級別 cache 要慢慢熱身。因此如果能適當降低中斷的頻率，多攢幾個包一起發出中斷，對提升 CPU 的工作效率是有幫助的。所以，網路卡允許我們對硬中斷進行合併。

現在我們來看一下網路卡的硬中斷合併設定。

```
# ethtool -c eth0
Coalesce parameters for eth0:
Adaptive RX: off  TX: off
......

rx-usecs: 1
rx-frames: 0
rx-usecs-irq: 0
rx-frames-irq: 0
......
```

我們來說一下上述結果的大致含義

- Adaptive RX: 自適應中斷合併，網路卡驅動自己判斷什麼時候該合併什麼時候不合並
- rx-usecs： 當過這麼長時間過後，一個 RX interrupt 就會被產生
- rx-frames：當累計接收到這麼多個幀後，一個 RX interrupt 就會被產生

如果你想好了修改其中的某一個參數了的話，直接使用 `ethtool` -C 就可以，例如：

```
ethtool -C eth0 adaptive-rx on
```

不過需要注意的是，減少中斷數量雖然能使得 Linux 整體吞吐更高，不過一些包的延遲也會增大，所以用的時候得適當注意。

## 軟中斷監控與調校

在硬中斷之後，再接下來的處理過程就是 ksoftirqd 核心執行緒中處理的軟中斷了。之前我們說過，軟中斷和它對應的硬中斷是在同一個核心上處理的。 因此，前面硬中斷分散到多核上處理的時候，軟中斷的最佳化其實也就跟著做了，也會被多核處理。不過軟中斷也還有自己的可最佳化選項。

## 監控

軟中斷的資訊可以從 `/proc/softirqs` 讀取：

```
$ cat /proc/softirqs
                    CPU0       CPU1       CPU2       CPU3
          HI:          0          2          2          0
       TIMER:  704301348 1013086839  831487473 2202821058
      NET_TX:      33628      31329      32891     105243
      NET_RX:  418082154 2418421545  429443219 1504510793
       BLOCK:         37          0          0   25728280
BLOCK_IOPOLL:          0          0          0          0
     TASKLET:     271783     273780     276790     341003
       SCHED: 1544746947 1374552718 1287098690 2221303707
     HRTIMER:          0          0          0          0
         RCU: 3200539884 3336543147 3228730912 3584743459
```

## 軟中斷 budget 調整

不知道你有沒有聽說過番茄工作法，它的大致意思就是你要有一整段的不被打擾的時間，集中精力處理某一項作業。這一整段時間時長被建議是 25 分鐘。
對於我們的 Linux 的處理軟中斷的 ksoftirqd 來說，它也和番茄工作法思路類似。一旦它被硬中斷觸發開始了工作，它會集中精力處理一波兒網路封包，然後再去做別的事情。

我們說的處理一波兒是多少呢，策略略複雜。我們只說其中一個比較容易理解的，那就是 net.core.netdev_budget 核心參數。

```
#sysctl -a | grep
net.core.netdev_budget = 300
```

這個的意思說的是，ksoftirqd 一次最多處理 300 個包，處理夠了就會把 CPU 主動讓出來，以便 Linux 上其他的任務可以得到處理。 那麼假如說，我們現在就是想提高核心處理網路封包的效率。那就可以讓 ksoftirqd 程序多幹一會兒網路封包的接收，再讓出 CPU。至於怎麼提高，直接修改不這個參數的值就好了。

```
#sysctl -w net.core.netdev_budget=600
```

如果要保證重啟仍然生效，需要將這個設定寫到 `/etc/sysctl.conf`

## 軟中斷 GRO 合併

GRO 和硬中斷合併的思想很類似，不過階段不同。硬中斷合併是在中斷髮起之前，而 GRO 已經到了軟中斷上下文中了。

如果應用中是大檔案的傳輸，大部分包都是一段資料，不用 GRO 的話，會每次都將一個小包傳送到協定堆疊（IP 接收函式、TCP 接收）函式中進行處理。開啟 GRO 的話，Linux 就會智慧進行包的合併，之後將一個大包傳給協定處理函式。這樣 CPU 的效率也是就提高了。

```
ethtool -k eth0 | grep generic-receive-offload
generic-receive-offload: on
```

如果你的網路卡驅動沒有開啟 GRO 的話，可以透過如下方式開啟。

```
# ethtool -K eth0 gro on
```

> GRO 說的僅僅只是包的接收階段的最佳化方式，對於傳送來說是 GSO。

## 總結

在網路技術這一領域裡，有太多的知識內容都停留在理論階段了。你可能覺得你的網路學的滾瓜爛熟了，可是當你的線上服務出現問題的時候，你還是不知道該怎麼排查，怎麼最佳化。這就是因為只懂了理論，而不清楚 Linux 是透過哪些核心機制將網路技術落地的，各個核心元件之間怎麼配合，每個元件有哪些參數可以做調整。我們用兩篇文章詳細討論了 Linux 網路封包的接收過程，以及這個過程中的一些統計資料如何檢視，如何調校。相信消化完這兩篇文章之後，你的網路的理解直接能提升 1 個 Level，你對線上服務的把控能力也會更加如魚得水。
