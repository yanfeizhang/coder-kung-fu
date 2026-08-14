---
layout: default
title: "Linux 核心 (Linux Kernel)「偷吃」了你的記憶體 (Memory)！"
---

# Linux 核心 (Linux Kernel)「偷吃」了你的記憶體 (Memory)！

> 原文標題：Linux 内核“偷吃”了你的内存！

> 原文連結：https://mp.weixin.qq.com/s/MSlvaSmX2NQIzK10jjLXZg

---

大家好，我是飛哥！

今天我來拋個問題出來，那就是 Linux 核心給我們使用的記憶體「並不足量」。拿我手頭的一臺虛擬機器來舉例（實體主機原理一樣），透過 `dmidecode` 命令檢視到這臺伺服器是一條 16384 MB 的記憶體。

## `dmidecode`

```text
Memory
Device
Total Width:
Unknown
Data Width:
Unknown
Size:
16384
MB
Manufacturer:
QEMU
......
Device
Total Width: Unknown
Data Width: Unknown
Size: 16384 MB
Manufacturer: QEMU
......
```

但是使用 free 檢視的時候，Linux 卻告訴我們只有 15773 MB 可用。

```
# free -m


total

used

free

shared

buff/cache

available


Mem:

15773

794

13708

54

1270

14688


Swap:

0

0

0
```

那問題來了，16384 和 15773 中間差的這 611 MB 跑哪裡去了？

其實答案也並不玄乎，就是核心自己用掉了。但要想理解核心是如何使用掉這些記憶體的需要對核心的實體記憶體 (Physical Memory) 管理機制有足夠深入的瞭解才行。

本節就讓我們帶著這個問題來學習下核心對實體記憶體的管理方式。

## 一、Linux 初期的 memblock 記憶體分配器

提到實體記憶體的管理，很多同學想到的都是夥伴系統。但其實核心中採用了兩種記憶體管理機制來管理實體記憶體。

第一種是初期記憶體分配器。在核心剛啟動的時候，採用的是較為簡單的記憶體分配器。這種記憶體分配器僅僅只為了滿足系統啟動時間內對記憶體頁的簡單管理。第二種就是我們熟知的夥伴系統，這是在 Linux 系統正常執行時期採用的主要分配器。

在 Linux 的早期版本中，初期分配器採用的是 bootmem。但在 2010 年之後，就慢慢替換成了 memblock 記憶體分配器。可以參見這個歷史 commit （[git.kernel.org/pub/scm/lin…](https://link.juejin.cn?target=https%3A%2F%2Fgit.kernel.org%2Fpub%2Fscm%2Flinux%2Fkernel%2Fgit%2Ftorvalds%2Flinux.git%2Fcommit%2Fmm%2Fmemblock.c%3Fid%3D95f72d1ed41a66f1c1c29c24d479de81a0bea36f%25EF%25BC%2589)

## 1.1 建立 memblock 記憶體分配器

我們來看下 memblock 記憶體分配器是如何實作的。

在 [《Linux 核心是如何檢測可用實體記憶體位址範圍的？》](https://link.juejin.cn?target=https%3A%2F%2Fmp.weixin.qq.com%2Fs%3F__biz%3DMjM5Njg5NDgwNA%3D%3D%26mid%3D2247493061%26idx%3D1%26sn%3Da6829fe1b8e6c24042c240152c37cf2c%26scene%3D21%23wechat_redirect) 一文中我們看到了 Linux 核心檢測實體記憶體的工作原理。在檢測完成後呼叫 e820__memory_setup 來把檢測結果儲存到了 `e820_table` 全域性資料結構中。

```
//file:arch/x86/kernel/setup.c

void __init
setup_arch
(char **cmdline_p)
{
...

// 儲存實體記憶體檢測結果


e820__memory_setup
();
...


// membloc 記憶體分配器初始化


e820__memblock_setup
();
}
```

在儲存完記憶體檢測結果後，Linux 核心就會呼叫 e820__memblock_setup 來建立 memblock 記憶體分配器。在看建立 memblock 之前我們先來看看這種記憶體分配器是長什麼樣子的。memblock 的實作非常簡單，就是按照檢測到的記憶體位址範圍是 usable 還是 reserved 分成兩個物件，然後分別用兩個陣列給存起來。

它的相關程式碼如下。

```
//file:mm/memblock.c
struct memblock memblock
__initdata_memblock
 = {
 .
memory.regions
  = memblock_memory_init_regions,
 .
memory.cnt
  =
1
, /* empty dummy entry */
 .
memory.max
  = INIT_MEMBLOCK_MEMORY_REGIONS,
 .
memory.name
  =
"memory"
,

 .
reserved.regions
 = memblock_reserved_init_regions,
 .
reserved.cnt
  =
1
, /* empty dummy entry */
 .
reserved.max
  = INIT_MEMBLOCK_RESERVED_REGIONS,
 .
reserved.name
  =
"reserved"
,

 .
bottom_up
  =
false
,
 .
current_limit
  = MEMBLOCK_ALLOC_ANYWHERE,
}
```

## define `INIT_MEMBLOCK_REGIONS`   128


## define `INIT_MEMBLOCK_RESERVED_REGIONS`  `INIT_MEMBLOCK_REGIONS`


## define `INIT_MEMBLOCK_MEMORY_REGIONS`  `INIT_MEMBLOCK_REGIONS`


```
static struct memblock_region memblock_memory_init_regions
[INIT_MEMBLOCK_MEMORY_REGIONS]
__initdata_memblock
;

staticstruct memblock_region memblock_reserved_init_regions
[INIT_MEMBLOCK_RESERVED_REGIONS]
__initdata_memblock
;
```

說完了 memblock 記憶體分配器器的實作，我們再來看它的構造過程。在探測到記憶體佈局後，呼叫 e820__memblock_setup 來對 memblock 記憶體分配器進行建立。

```
//file:arch/x86/kernel/e820.c

void __init
e820__memblock_setup
(void)
{
...
for (i =
0
; i < e820_table->nr_entries; i++) {
struct e820_entry *entry = &e820_table->entries
[i]
;
...
if (entry->type == E820_TYPE_SOFT_RESERVED)

memblock_reserve
(entry->addr, entry->size);


memblock_add
(entry->addr, entry->size);
}


// 列印 memblock 建立結果


memblock_dump_all
();
}
```

memblock 記憶體分配器的建立過程是遍歷 e820 table 中的每一段記憶體區域。

判斷如果是預留記憶體就呼叫   `memblock_reserve` 新增到 reserved 成員中，也就是預留記憶體列表。新增過程是會修改 reserved 中的區域數量 cnt，然後在設定 regions 中的一個元素。如果是可用記憶體就呼叫 `memblock_add` 新增到 memory 成員中，也就是可用記憶體列表。新增過程同上。

在 memblock 建立完成後，緊接著還呼叫   `memblock_dump_all`() 進行了一次列印輸出。關於這個輸出，我們在下面的小節來檢視。

## 1.2 檢視 memblock 的日誌資訊

Linux 核心在啟動的時候可以記錄 memblock 建立日誌。但需要在 Linux 啟動參數中新增 memblock=debug 參數並重啟機器才可以。

我在我的機器上是修改了 `/boot/grub/grub.cfg` 檔案，在啟動參數中新增了 memblock=debug。

linux   `/boot/vmlinuz-5.4.143.bsk.8-amd64`
root
=UUID=b4116c79-
9698
-
42
a6-
8379
-
47
befe6f963a ...... memblock=debug

然後重啟後，透過檢視 `/proc/cmdline` 確認開啟生效。

```
# cat /proc/cmdline


BOOT_IMAGE
=/boot/vmlinuz-
5.4
.
143
.bsk.
8
```

- amd64 ...... memblock=debug

然後就可以透過 dmseg 可以看到 memblock 記憶體分配器建立輸出的各種日誌資訊了。

......

[    0.010238]
MEMBLOCK configuration:

[    0.010239]
memory
size
=
0
x00000003fff78c00 reserved size =
0
x0000000003c6d144

[    0.010240]

memory.cnt
=
0
x3

[    0.010241]
memory
[0x0]

[0x0000000000001000-0x000000000009efff]
, 0x000000000009e000 bytes flags: 0x0

[    0.010243]
memory
[0x1]

[0x0000000000100000-0x00000000bffd9fff]
, 0x00000000bfeda000 bytes flags: 0x0

[    0.010244]
memory
[0x2]

[0x0000000100000000-0x000000043fffffff]
, 0x0000000340000000 bytes flags: 0x0

[    0.010245]

reserved.cnt
=
0
x4

[    0.010246]
reserved
[0x0]

[0x0000000000000000-0x0000000000000fff]
, 0x0000000000001000 bytes flags: 0x0

[    0.010247]
reserved
[0x1]

[0x00000000000f5a40-0x00000000000f5b83]
, 0x0000000000000144 bytes flags: 0x0

[    0.010248]
reserved
[0x2]

[0x0000000001000000-0x000000000340cfff]
, 0x000000000240d000 bytes flags: 0x0

[    0.010249]
reserved
[0x3]

[0x0000000034f31000-0x000000003678ffff]
, 0x000000000185f000 bytes flags: 0x0
......

在上面的輸出範例中，可以看到總實體記憶體是 0x3fff78c00 (約 1.596 TB)，保留記憶體：0x3c6d144 (約 61 MB)。包含 3 個記憶體塊，每個記憶體塊的位址範圍等等資訊。

## 二、向 memblock 記憶體分配器申請記憶體

核心在啟動的過程中，夥伴系統沒有建立。這時所有的記憶體都是透過 memblock 記憶體分配器來分配的。比較重要的兩個使用的場景是 crash kernel 和頁管理初始化。

## 2.1 crash kernel

一般我們認為在伺服器上是有一套核心。但實際上，伺服器是啟動了兩個核心，第一個是正常使用的核心，第二個是崩潰發生時 kdump 機制使用的應急核心。

核心為了在崩潰時能記錄崩潰的現場，方便以後排查分析，設計實作了一套 kdump 機制。

有了 kdump 機制，發生系統崩潰的時候 kdump 使用 kexec 啟動到第二個核心中執行。這樣第一個核心中的記憶體就得以保留下來了。然後可以把崩潰時的所有執行狀態都收集到 dump core 中。

這裡我們不對 kdump 機制過多展開，我們想重點說的是這套機制是需要額外的記憶體才能工作的。透過   `reserve_crashkernel_low` 和 `reserve_crashkernel` 兩個函式向 memblock 記憶體分配器申請記憶體。

```
//file:arch/x86/kernel/setup.c

static int __init
reserve_crashkernel_low
(void)
{
...

// 申請記憶體

low_base =
memblock_phys_alloc_range
(low_size, CRASH_ALIGN,
0
, CRASH_ADDR_LOW_MAX);

pr_info
("Reserving %ldMB of low memory at %ldMB for crashkernel (low RAM limit: %ldMB)\n",
(unsignedlong)(low_size >>
20
),
(unsignedlong)(low_base >>
20
),
(unsignedlong)(low_mem_limit >>
20
));
...
}

static void __init
reserve_crashkernel
(void)
{
...

// 申請記憶體

low_base =
memblock_phys_alloc_range
(low_size, CRASH_ALIGN,
0
, CRASH_ADDR_LOW_MAX);

pr_info
("Reserving %ldMB of memory at %ldMB for crashkernel (System RAM: %ldMB)\n",
(unsignedlong)(crash_size >>
20
),
(unsignedlong)(crash_base >>
20
),
(unsignedlong)(total_mem >>
20
));
}
```

這兩個記憶體都在申請完記憶體後把資訊透過日誌的方式列印出來了，在 dmesg 的輸出中可以看到。

......

[    0.010832]

Reserving

128
MB

of

low

memory

at

2928
MB

for

crashkernel
(System low
RAM
:
3071
MB)

[    0.010835]

Reserving

128
MB

of

memory

at

17264
MB

for

crashkernel
(System
RAM
:
16383
MB)

在我的這臺虛擬機器中，總共為 crash kernel 預留了兩個 128 MB，總共 512 MB 的記憶體。這些記憶體會一直被佔用，我們自己的使用者程式無法使用。

## 2.2 頁管理初始化

我們都知道，Linux 是透過頁的方式來管理所有的實體記憶體的。頁的大小是 4KB。

然而每一個頁都需要使用一個 struct page 物件來表示。這個物件也是需要消耗記憶體的。在不同的版本中，struct page 的大小不一樣，一般比較常見的大小是 64 位元組。

```
//file:include/linux/mm_types.h


struct

page
{

unsigned

long
flags;
...
}
```

頁管理機制的初始化具體函式是   `paging_init`，具體的執行路徑是在 `start_kernel` -> `setup_arch` -> x86_init.paging.pagetable_init -> `paging_init`。

```text
start_kernel
  -> setup_arch
  -> e820__memory_setup
     // 核心把實體記憶體檢測結果從 boot_params.e820_table 儲存到 e820_table 中，並列印出來
  -> e820__memblock_setup
     // 根據 e820 資訊建立 memblock 記憶體分配器，開啟除錯時可以列印資訊
  -> x86_init.paging.pagetable_init (native_pagetable_init)
  -> paging_init
     // 頁管理機制的初始化
  -> mm_init
  -> mem_init
  -> memblock_free_all
     // 向夥伴系統移交控制權
```

在 `paging_init` 這個函式中為所有的頁面都申請一個 struct page 物件。將來透過這個物件來對頁面進行管理。

記憶體頁管理模型也經過了幾代的變化，在最早的時候，採用的是 FLAT 模型、中間還經歷了 DISCONTIG 模型，現在都預設採用了 SPARSEMEM 模型。 SPARSEMEM 模型在記憶體中就是一個二維陣列。

```c
// file: mm/sparse.c
#ifdef CONFIG_SPARSEMEM_EXTREME
struct mem_section *mem_section;
#else
struct mem_section mem_section[NR_SECTION_ROOTS][SECTIONS_PER_ROOT]
    ____cacheline_internodealigned_in_smp;
#endif
EXPORT_SYMBOL(mem_section);
```

在這個二維陣列中，透過層層包裝，最後包含的最小單元就是表示記憶體的 struct page 物件。

假設 struct page 結構體大小是 64 位元組。那麼平均每 4 KB 就額外需要消耗 64 位元組記憶體用來儲存這個物件。   `64/4096` 約等於 1.56%，那麼管理 16 GB 的記憶體大約需要 (16*1024 MB) * 1.56%，約 256 MB 的記憶體。

## 三、向夥伴系統釋放可用記憶體

在經過記憶體檢測、memblock 記憶體分配器構建、頁管理機制初始化等步驟後，夥伴管理系統就要登場了。在 `mm_init` -> `mem_init` -> `memblock_free_all` 中， memblock 開啟了它給夥伴系統交接記憶體的交接儀式。

```text
start_kernel
  -> setup_arch
  -> e820__memory_setup
     // 核心把實體記憶體檢測結果從 boot_params.e820_table 儲存到 e820_table 中，並列印出來
  -> e820__memblock_setup
     // 根據 e820 資訊建立 memblock 記憶體分配器，開啟除錯時可以列印資訊
  -> x86_init.paging.pagetable_init (native_pagetable_init)
  -> paging_init
     // 頁管理機制的初始化
  -> mm_init
  -> mem_init
  -> memblock_free_all
     // 向夥伴系統移交控制權
```

我們來看下記憶體的交接過程。

```
//file:mm/memblock.c

void __init
memblock_free_all
(void)
{
unsigned long pages;
......
pages =
free_low_memory_core_early
();

totalram_pages_add
(pages);
}
```

具體的釋放是在 `free_low_memory_core_early` 中進行的。值得注意的是，memblock 是把 reserved 和可用記憶體是分開來交接的。這樣保證 reserved 記憶體即使交接給了夥伴系統，夥伴系統也不會把它分配出去給使用者程式使用。

```c
// mm/memblock.c
static unsigned long __init free_low_memory_core_early(void)
{
    // reserved 記憶體交接
    memmap_init_reserved_pages();

    // 可用記憶體交接
    for_each_free_mem_range(i, NUMA_NO_NODE, MEMBLOCK_NONE, &start, &end, NULL)
        count += __free_memory_core(start, end);

    return count;
}
```

在交接完畢後，回傳交接的記憶體頁數。這個頁數透過 `totalram_pages_add` 函式新增到一個名為 _totalram_pages 的全域性變數中了。

```
//file:mm/page_alloc.c

atomic_long_t _totalram_pages __read_mostly;

EXPORT_SYMBOL
(_totalram_pages);
```

## 四、總結

Linux 並不會把全部的實體記憶體都提供給我們使用。所以如果你透過 free -m 檢視到可用記憶體比實際的實體記憶體小也絲毫不用感到奇怪。

Linux 為了維護自身的執行，會需要消耗一些記憶體。在本文中我們介紹了 kdump 機制對記憶體的消耗，也介紹了記憶體的頁管理機制對記憶體的佔用。但實際上還以一些其他的消耗，例如 NUMA 機制中的 node 、zone 的管理等等也都需要記憶體。

另外在本文中，我們也給大家介紹了 memblock 記憶體分配器。核心在啟動檢測到記憶體的位址佈局後，會用這個佈局來初始化 memblock 記憶體分配器。後面核心的 kdump 機制、頁管理機制、NUMA 初始化等再需要使用記憶體的時候，都是向 memblock 分配器來申請記憶體的。其中頁管理機制中 struct page 的開銷大約就需要 1.56%。

當核心啟動工作執行的差不多的時候，memblock 會將記憶體管理交接給夥伴系統。memblock 是把 reserved 和可用記憶體是分開來交接的。這樣保證 reserved 記憶體即使交接給了夥伴系統，夥伴系統也不會把它分配出去給使用者程式使用。

以上就是核心在啟動時對實體記憶體的管理和使用原理。

最後，除了核心之外，其實應用層面在記憶體的使用上也會有一定程度的管理開銷。拿 golang 記憶體分配器 TCMalloc 來舉例，其核心資料結構 mspan 定義如下。

```go
// src/runtime/mheap.go
type mspan struct {
    next *mspan // 下一個 span 節點
    prev *mspan // 上一個 span 節點
    allocBits *gcBits // 標記記憶體的佔用
    gcmarkBits *gcBits // 標記記憶體的 GC 回收情況
    ...
}
```

在 mspan 中會有一些必須的元陣列，例如組成 span 連結串列的指標，以及標記 span 中物件分配的點陣圖、標記記憶體 GC 掃描時的點陣圖等等。這些都是無法被應用程式使用的。
