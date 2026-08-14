---
layout: default
title: "挑戰 Redis 單一執行個體記憶體 (Memory) 最大極限，「遭遇」NUMA 陷阱！"
---

# 挑戰 Redis 單一執行個體記憶體 (Memory) 最大極限，「遭遇」NUMA 陷阱！

> 原文標題：挑战Redis单实例内存最大极限，“遭遇”NUMA陷阱！

> 原文連結：https://mp.weixin.qq.com/s/dag1Zp1h4lQfqeUUGz4Ogw

---

我們公司的基礎架構部有個雲 Redis 平臺，其中 Redis 執行個體在申請的時候可以自由選擇需要的記憶體的大小。然後就引發了我的一個思考，Redis 單一執行個體記憶體最大申請到多大比較合適？假設宿主機是 64GB 記憶體的實體主機，如果不考慮 CPU 資源的的浪費，我是否可以開一個 50G 的 Redis 執行個體？

於是我在 Google 上各種搜尋，討論這個問題的人似乎不多。找到唯一感覺靠譜點的答案，那就是單程序 (Process) 分配的記憶體最好不要超過一個 node 裡的記憶體總量，否則 linux 當該 node 裡的記憶體分配光了的時候，會在自己 node 裡動用硬碟 swap，而不是其他 node 裡申請。這即使所謂的 numa 陷阱，當 Redis 進入這種狀態後會導致效能急劇下降（不只是 redis，所有的記憶體密集型應用如 mysql，mongo 等都會有類似問題）。

看起來這個解釋非常有說服力。於是乎，我就想親手捕捉一次 NUMA 陷阱，看看這個傢伙究竟什麼樣。

## 先聊聊 QPI 與 NUMA

最早在 CPU 都是單核的時候，用的匯流排都是 FSB 匯流排。經典結構如下圖：

到來後來 CPU 的開發者們發現 CPU 的頻率已經接近實體極限了，沒法再有更大程度的提高了。在 2003 年的時候，CPU 的頻率就已經達到 2 個多 GB，甚至 3 個 G 了。現在你再來看今天的 CPU，基本也還是這個頻率，沒進步多少。摩爾定律失效了，或者說是向另外一個方向發展了。那就是多核化、多 CPU 化。

剛開始核不多的時候，FSB 匯流排勉強還可以支撐。但是隨著 CPU 越來越多，所有的資料 IO 都透過這一條匯流排和記憶體叫喚資料，這條 FSB 就成為了整個電腦系統的瓶頸。舉個北京的例子，這就好比進回龍觀的京藏高速，剛開始回龍觀人口不多的時候，這條高速承載沒問題。但是現在回龍觀聚集了幾十萬人了，「匯流排」還僅有這一條，未免效率太低。

CPU 的設計者們很快改變了自己的設計，引入了 QPI 匯流排，相應的 CPU 的結構就叫 NMUA 架構。下圖直觀理解

## 話說 NUMA 陷阱

NUMA 陷阱指的是引入 QPI 匯流排後，在電腦系統裡可能會存在的一個坑。大致的意思就是如果你的機器開啟了 numa，那麼你的記憶體即使在充足的情況下，也會使用磁碟 (Disk) 上的 swap，導致效能低下。原因就是 NUMA 為了高效，會僅僅只從你的當前 node 裡分配記憶體，只要當前 node 裡用光了（即使其他 node 還有），也仍然會啟用硬碟 swap。

當我第一次聽說到這個概念的時候，不禁感嘆我運氣好，我的 Redis 執行個體貌似從來沒有掉進這個陷阱裡過。那為了以後也別栽坑，趕緊去了解了下我的機器的 numa 狀態：

```
# numactl --hardware
available: 2 nodes (0-1)
node 0 cpus: 0 1 2 3 4 5 12 13 14 15 16 17
node 0 size: 32756 MB
node 0 free: 19642 MB
node 1 cpus: 6 7 8 9 10 11 18 19 20 21 22 23
node 1 size: 32768 MB
node 1 free: 18652 MB
node distances:
node   0   1
  0:  10  21
  1:  21  10
```

上面結果說明我們有兩個 node，node0 和 node1，分別有 12 個核心，各有 32GB 的記憶體。
再看   `zone_reclaim_mode`，它用來管理當一個記憶體區域 (zone) 內部的記憶體耗盡時，是從其內部進行記憶體回收還是可以從其他 zone 進行回收的選項：

- 0 關閉 `zone_reclaim` 模式，可以從其他 zone 或 NUMA 節點回收記憶體
- 1 開啟 `zone_reclaim` 模式，這樣記憶體回收只會發生在本地節點內
- 2 在本地回收記憶體時，可以將 cache 中的髒資料寫回硬碟，以回收記憶體
- 4 在本地回收記憶體時，表示可以用 Swap 方式回收記憶體

```
# cat /proc/sys/vm/zone_reclaim_mode
1
```

額，好吧。我的這臺機器上的 `zone_reclaim_mode` 還真是 1，只會在本地節點回收記憶體。

## 實踐捕捉 numa 陷阱未遂

那我的好奇心就來了，既然我的單個 node 節點只有 32G，那我部署一個 50G 的 Redis，給它填滿資料試試到底會不會發生 swap。

實驗開始，我先檢視了本地總記憶體，以及各個 node 的記憶體剩餘狀況。

```
# top
......
Mem:  65961428k total, 26748124k used, 39213304k free,   632832k buffers
Swap:  8388600k total,        0k used,  8388600k free,  1408376k cached

# cat /proc/zoneinfo"
  ......
Node 0, zone   Normal
  pages free     4651908
Node 1, zone   Normal
  pages free     4773314
```

總記憶體不用解釋，`/proc/zoneinfo` 裡包含了 node 可供應用程式申請的 free pages。node1 有 4651908 個頁面，4651908*4K=18G 的可用記憶體。

接下來讓我們啟動 redis 執行個體，把其記憶體上限設定到超過單個 node 裡的記憶體大小。我這裡單 node 記憶體大小是 32G，我把 redis 設定成了 50G。開始灌入資料。最終資料全部灌完之後，

```
# top
Mem:  65961428k total, 53140400k used, 12821028k free,   637112k buffers
Swap:  8388600k total,        0k used,  8388600k free,  1072524k cached
  PID USER      PR  NI  VIRT  RES  SHR S %CPU %MEM    TIME+  COMMAND
 8356 root      20   0 62.8g  46g 1292 S  0.0 74.5   3:45.34 redis-server

# cat /proc/zoneinfo | grep "pages free"
  pages free     3935
  pages free     347180
  pages free     1402744
  pages free     1501670
```

實驗證明，在   `zone_reclaim_mode` 為 1 的情況下，Redis 是平均在兩個 node 裡申請節點的，並沒有固定在某一個 cpu 裡。

莫非是大佬們的忠告錯了嗎？其實不是，如果不繫結親和性的話，分配記憶體是當程序在哪個 node 上的 CPU 發起記憶體申請，就優先在哪個 node 裡分配記憶體。之所以是平均分配在兩個 node 裡，是因為 redis-server 程序實驗中經常會進入主動睡眠狀態，醒來後可能 CPU 就換了。所以基本上，最後看起來記憶體是平均分配的。如下圖，CPU 進行了 500 萬次的上下文切換 (Context Switch)，用 `top` 命令看到 cpu 也是在 node0 和 node1 跳來跳去。

```
# grep ctxt /proc/8356/status
voluntary_ctxt_switches:        5259503
nonvoluntary_ctxt_switches:     1449
```

## 改進方法，成功抓獲 numa 陷阱

殺死程序，記憶體歸位

```
# cat /proc/zoneinfo
Node 0, zone   Normal
  pages free     7597369
Node 1, zone   Normal
  pages free     7686732
```

繫結 CPU 和記憶體的親和性，然後再啟動。

```
numactl --cpunodebind=0 --membind=0 /search/odin/daemon/redis/bin/redis-server /search/odin/daemon/redis/conf/redis.conf
```

`top` 命令觀察到 CPU 確實一直在 node0 的節點裡。node 裡的記憶體也在快速消耗。

```
# cat /proc/zoneinfo
Node 0, zone   Normal
  pages free     10697
Node 1, zone   Normal
  pages free     7686732
```

看，記憶體很快就消耗光了。我們再看 `top` 命令觀察到的 swap，很激動地發現，我終於陷入到傳說中的 numa 陷阱了。

```
Tasks: 603 total,   2 running, 601 sleeping,   0 stopped,   0 zombie
Cpu(s):  0.7%us,  5.4%sy,  0.0%ni, 85.6%id,  8.2%wa,  0.0%hi,  0.1%si,  0.0%st
Mem:  65961428k total, 34530000k used, 31431428k free,   319156k buffers
Swap:  8388600k total,  6000792k used,  2387808k free,   777584k cached

  PID USER      PR  NI  VIRT  RES  SHR S %CPU %MEM    TIME+  COMMAND
  258 root      20   0     0    0    0 R 72.3  0.0   0:17.18 kswapd0
25934 root      20   0 37.5g  30g 1224 D 71.6 48.7   1:06.09 redis-server
```

這時候，Redis 實際使用的實體記憶體 (Physical Memory) RES 定格到了 30g 不再上漲，而是開始消耗 Swap。

又過了一會兒，Redis 被 oom 給 kill 了。

## 結論

透過今天的實驗，我們可以發現確實有 NUMA 陷阱這種東西存在。不過那是我手工透過   `numactl` 指令繫結 cpu 和 mem 的親和性後才遭遇的。相信國內絕大部分的線上 Redis 沒有進行這個繫結，所以理論上來單 Redis 單一執行個體可以使用到整個機器的實體記憶體。（實踐中最好不要這麼做，你的大部分記憶體都繫結到一個 redis 程序裡的話，那其他 CPU 核就沒什麼事幹了，浪費了 CPU 的多核計算能力）

## 擴充套件

當透過 `numactl` 繫結 CPU 和 mem 都在一個 node 裡的時候，記憶體 IO 不需要經過匯流排，效能會比較高，你 Redis 的 QPS 能力也會上漲。和跨 node 的記憶體 IO 效能對比，可以下面的執行個體，就是 10:21 的區別。

```
# numactl --hardware
......
node distances:
node   0   1
  0:  10  21
  1:  21  10
```

你要是對效能有極致的追求，可以試著繫結 numa 的親和性玩玩。不過，no 作 no die，掉到 numa 陷阱裡可別賴我，嘎嘎！
