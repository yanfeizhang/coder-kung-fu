---
layout: default
title: "`write` 檔案一個位元組後何時發起寫磁碟 (Disk) IO？"
---

# `write` 檔案一個位元組後何時發起寫磁碟 (Disk) IO？

> 原文標題：write文件一个字节后何时发起写磁盘IO？

> 原文連結：https://zhuanlan.zhihu.com/p/142441899

---

?

在前文《  `read` 檔案一個位元組實際會發生多大的磁碟 IO？》寫完之後，本來想著偷個懶，只透過讀操作來讓大家瞭解下 Linux IO 堆疊的各個模組就行了。但很多同學表示再讓我寫一篇關於寫操作的。既然不少同學都有這個需求，那我就寫一下吧。

Linux 核心 (Linux Kernel) 真的是太複雜了，原始碼的行數已經從 1.0 版本時的幾萬行，到現在已經是千萬行的一個龐然大物了。直接鑽進去的話，很容易在各種眼花繚亂的各種呼叫中迷失了自己，再也鑽不出來了。我會介紹一個研究核心的方法。一般我自己先想一個自己很想釐清的問題。不管在程式碼裡怎麼跳來跳去，時刻都要記得自己的問題，無關的部分儘量少去發散，只要把自己的問題釐清了就行了。

現在我想理解的問題是，在最常用的方式下，不開 `O_DIRECT`、不開 `O_SYNC`（寫檔案的方法有很多，有 sync 模式、direct 模式、 `mmap` 記憶體 (Memory) 對映模式），  `write` 是怎麼寫的。c 的程式碼範例如下:

```
#include <fcntl.h>
int main()
{
char c = 'a';
int out;

out = open("  out.txt", O_WRONLY | O_CREAT | O_TRUNC);
write(out,&c,1);
...
}
```

進一步細化我的問題，我們對開啟的檔案寫入一個位元組後

- `write` 函式在核心裡是怎麼執行的?
- 資料在什麼時機真正能寫入到磁碟上？

我們在討論的過程中不可避免地要涉及到核心程式碼，我使用的核心版本是 3.10.1。如果有需要，你可以到這裡來下載。  mirrors.edge.kernel.org。

1

`write` 函式實作剖析

我花了不短的時候跟蹤   `write` 寫到 ext4 檔案系統 (File System) 時的各種呼叫和回傳，大致理出來了一個互動圖。當然為了突出重點，我拋棄了不少細節，例如 DIRECT IO、ext4 日誌記錄什麼的都沒有體現出來，只抽取出來了一些我認為關鍵的呼叫。

?

圖 1 `write` 函式內部實作

在上面的流程圖裡，所有的寫操作最終到哪裡了呢？在最後面的_block_commit_write 中，只是 make dirty。然後大部分情況下你的函式呼叫就回傳了（稍後再說   `balancedirty_pages_ratelimited`）。資料現在還在記憶體中的 PageCache 裡，並沒有真正寫到硬碟。

為什麼要這樣實作，不直接寫硬碟呢？原因就在於硬碟尤其是機械硬碟 (Hard Disk Drive, HDD)，效能實在是太慢了。一塊伺服器級別的萬轉盤，最壞隨機存取平均延遲都是毫秒級別的，換算成 IOPS 只有 100 多不到 200。設想一下，假如你的後端介面裡每個使用者來存取都需要一次隨機磁碟 IO，不管你多牛的伺服器，每秒 200 的 qps 都將直接打爆你的硬碟，相信作為為百萬/千萬/過億使用者提供介面的你，這個是你絕對不能忍的。

Linux 這樣做也是有副作用的，如果接下來伺服器發生掉電，記憶體裡東西全丟。所以 Linux 還有另外一個「補丁」-延遲寫，幫我們緩解這個問題。注意下，我說的是緩解，並沒有徹底解決。

再說下 `balance_dirty_pages_ratelimited`，雖然絕大部分情況下，都是直接寫到 Page Cache 裡就回傳了。但在一種情況下，使用者程序 (Process) 必須得等待寫入完成才可以回傳，那就是對 `balance_dirty_pages_ratelimited` 的判斷如果超出限制了。該函式判斷當前髒頁是否已經超過髒頁上限 `dirty_bytes`、  `dirty_ratio`，超過了就必須得等待。這兩個參數只有一個會生效，另外 1 個是 0。拿 `dirty_ratio` 來說，如果設定的是 30，就說明如果髒頁比例超過記憶體的 30%，則 `write` 函式呼叫就必須等待寫入完成才能回傳。可以在你的機器上的 `/proc/sys/vm`/ 目錄來檢視這兩個設定。

```
# cat /proc/sys/vm/dirty_bytes
0
```

```
# cat /proc/sys/vm/dirty_ratio
30

2
```

核心延遲寫

核心是什麼時候真正把資料寫到硬碟中呢？為了快速摸清楚全貌，我想到的辦法是用 systemtap 工具，找到核心寫 IO 過程中的一個關鍵函式，然後在其中把函式呼叫堆疊打出來。查了半天資料以後，我決定用   `do_writepages` 這個函式。

```
#!/usr/bin/stap
probe   kernel.function("do_writepages")
{
printf("--------------------------------------------------------\n");
print_backtrace();
printf("--------------------------------------------------------\n");
}
```

systemtab 跟蹤以後，列印資訊如下:

0xffffffff8118efe0 : `do_writepages`+`0x0/0x40` [kernel]
0xffffffff8122d7d0 : __writeback_single_inode+`0x40/0x220` [kernel]
0xffffffff8122e414 :   `writeback_sb_inodes`+`0x1c4/0x490` [kernel]
0xffffffff8122e77f : __writeback_inodes_wb+  `0x9f/0xd0` [kernel]
0xffffffff8122efb3 : `wb_writeback`+`0x263/0x2f0` [kernel]
0xffffffff8122f35c : `bdi_writeback_workfn`+`0x1cc/0x460` [kernel]
0xffffffff810a881a : `process_one_work`+`0x17a/0x440` [kernel]
0xffffffff810a94e6 : `worker_thread`+`0x126/0x3c0` [kernel]
0xffffffff810b098f : kthread+`0xcf/0xe0` [kernel]
0xffffffff816b4f18 :   `ret_from_fork`+`0x58/0x90` [kernel]

從上面的輸出我們可以看出，真正的寫檔案過程操作是由 worker 核心執行緒 (Thread) 發出來的（和我們自己的應用程式程序沒有半毛錢關係，此時我們的應用程式的   `write` 函式呼叫早就回傳了）。這個 worker 執行緒寫回是週期性執行的，它的週期取決於核心參數 (Kernel Parameter) `dirty_writeback_centisecs` 的設定，根據參數名也大概能看出來，它的單位是百分之一秒。

```
# cat /proc/sys/vm/dirty_writeback_centisecs
500
```

我檢視到我的設定是 500，就是說每 5 秒會週期性地來執行一遍。回顧我們的問題，我們最關心的問題的什麼時候寫入的，圍繞這個思路不過多發散。於是沿著這個呼叫堆疊不斷地跟蹤，跳轉，終於找到了下面的程式碼。如下程式碼裡我們看到，如果是   `for_background` 模式，且 `over_bground_thresh` 判斷成功，就會開始回寫了。

```
static long   wb_writeback(struct bdi_writeback *wb,
struct   wb_writeback_work *work)
{
work->older_than_this = &oldest_jif;
...
if (work->for_background && !over_bground_thresh(wb->bdi))
break;
...

if (work->for_kupdate) {
oldest_jif = jiffies -
msecs_to_jiffies(dirty_expire_interval * 10);
} else ...
}
static long wb_check_background_flush(struct bdi_writeback *wb)
{
if (over_bground_thresh(wb->bdi)) {
...
return   wb_writeback(wb, &work);
}
}
```

那麼 `over_bground_thresh` 函式判斷的是什麼呢？其實就是判斷當前的髒頁是不是超過核心參數裡 `dirty_background_ratio` 或 `dirty_background_bytes` 的設定，沒超過的話就不寫了（程式碼位於   `fs/fs-writeback.c`：1440，限於篇幅我就不貼了）。這兩個參數只有一個會真正生效，其中 `dirty_background_ratio` 設定的是比例、`dirty_background_bytes` 設定的是位元組。

在我的機器上的這兩個參數設定如下，表示髒頁比例超過 10% 就開始回寫。

```
# cat /proc/sys/vm/dirty_background_bytes
0
```

```
# cat /proc/sys/vm/dirty_background_ratio
10
```

那如果髒頁一直都不超過這個比例怎麼辦呢，就不寫了嗎？ 不是的。在上面的 `wb_writeback` 函式中我們看到了，如果是 `for_kupdate` 模式，會記錄一個過期標記到 work-> `older_than_this`，再往後面的程式碼中把符合這個過期條件的頁面也寫回了。  `dirty_expire_interval` 這個變數是從哪裡來的呢？ 在 `kernel/sysctl.c` 裡，我們發現了蛛絲馬跡。哦，原來它是來自 `/proc/sys/vm/dirty_expire_centisecs` 這個設定。

```
1158         {
1159                 .procname       = "dirty_expire_centisecs",
1160                 .data           = &  dirty_expire_interval,
1161                 .maxlen         = sizeof(dirty_expire_interval),
1162                 .mode           = 0644,
1163                 .  proc_handler   = proc_dointvec_minmax,
1164                 .extra1         = &zero,
1165         },
```

在我的機器上，它的值是 3000。單位是百分之一秒，所以就是髒頁過了 30 秒就會被核心執行緒認為需要寫回到磁碟了。

```
# cat /proc/sys/vm/dirty_expire_centisecs
3000

3
```

結論

我們 demo 程式碼中的寫入，其實絕大部分情況都是寫入到 PageCache 中就回傳了，這時並沒有真正寫入磁碟。我們的資料會在如下三個時機下被真正發起寫磁碟 IO 請求：

- 第一種情況，如果   `write` 系統呼叫 (System Call) 時，如果發現 PageCache 中髒頁佔比太多，超過了 `dirty_ratio` 或 `dirty_bytes`， `write` 就必須等待了。
- 第二種情況，  `write` 寫到 PageCache 就已經回傳了。worker 核心執行緒非同步執行的時候，再次判斷髒頁佔比，如果超過了 `dirty_background_ratio` 或 `dirty_background_bytes`，也發起寫回請求。
- 第三種情況，這時同樣 `write` 呼叫已經回傳了。worker 核心執行緒非同步執行的時候，雖然系統內臟頁一直沒有超過 `dirty_background_ratio` 或 `dirty_background_bytes`，但是髒頁在記憶體中呆的時間超過   `dirty_expire_centisecs` 了，也會發起回寫。

如果對以上設定不滿意，你可以自己透過修改 `/etc/sysctl.conf` 來調整，修改完了別忘了執行 `sysctl` -p。

最後我們要認識到，這套 `write` pagecache+回寫的機制第一目標是效能，不是保證不丟失我們寫入的資料的。如果這時候掉電，髒頁時間未超過 `dirty_expire_centisecs` 的就真的丟了。如果你做的是和錢相關非常重要的業務，必須保證落盤完成才能回傳，那麼你就可能需要考慮使用 fsync。
