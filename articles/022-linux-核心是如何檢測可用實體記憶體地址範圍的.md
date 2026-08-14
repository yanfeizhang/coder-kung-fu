---
layout: default
title: "Linux 核心 (Linux Kernel) 是如何檢測可用實體記憶體 (Physical Memory) 位址範圍的？"
---

# Linux 核心 (Linux Kernel) 是如何檢測可用實體記憶體 (Physical Memory) 位址範圍的？

> 原文標題：Linux 内核是如何检测可用物理内存地址范围的？

> 原文連結：https://mp.weixin.qq.com/s/jZm9CbPayhAlKaptbkppnQ

---

大家好，我是飛哥！

記憶體 (Memory) 從硬體上，看到的是一根根有著金手指的硬體。但核心是如何能夠識別到主機板上安裝的記憶體的呢？我們有沒有辦法來檢視到核心的這個識別過程？

我們今天就來看下核心對實體記憶體條的檢測過程。

## 一、韌體程式介紹

記憶體從硬體上看到的是連線在主機板上一根根有著金手指的硬體。核心需要識別到這些記憶體才可以進行後面的使用。但其實作業系統 (Operating System) 在剛剛啟動的時候，對記憶體的可用位址範圍、NUMA 分組資訊都是一無所知。

好在在電腦的體系結構中，除了作業系統和硬體外，中間還存在著一層韌體（firmware）。

韌體是位於主機板上的使用 SPI Nor Flash 儲存著的軟體。起著在硬體和作業系統中間承上啟下的作用。它對外提供介面規範是高階設定和電源介面（ACPI，Advanced Configuration and Power Interface）。
其第一個版本 ACPI 1.0 是 1997 年的時候由英特爾、微軟和東芝公司共同推出的。截止書稿寫作時最新的版本是 2022 年 8 月釋出的 6.5 版本。在 UEFI 論壇裡可以下載到最新的規範文件 [uefi.org/sites/defau…](https://link.juejin.cn?target=https%3A%2F%2Fuefi.org%2Fsites%2Fdefault%2Ffiles%2Fresources%2FACPI_Spec_6_5_Aug29.pdf%25E3%2580%2582)

在這個規範中，定義了電腦硬體和作業系統之間的介面，包含的主要內容有電腦硬體設定描述、裝置通訊方式、電源功能管理等功能。在電腦啟動過程中，韌體負責著硬體自檢、初始化硬體裝置、載入作業系統載入程式，將控制權轉移到作業系統並提供介面供作業系統讀取硬體資訊。

作業系統所需要的記憶體等硬體資訊就是透過韌體來取得的。

## 二、從韌體讀取記憶體佈局資料

作業系統啟動時要做的一件重要的事情就是探測可用實體記憶體的位址範圍。在韌體 ACPI 介面規範中定義了探測記憶體的實體分布規範。

具體的過程是核心請求中斷號 15H，並設定操作碼為 E820 H。然後韌體就會向核心報告可用的實體記憶體位址範圍。因為操作碼是 E820，所以這個取得機制也被常稱為 E820。

下面是具體的核心程式碼。核心在啟動的時候也有個 main 函式。在 main 函式中會呼叫 `detect_memory`，實體記憶體安裝檢測就是在這個函式開始處理的。

```
//file:arch/x86/boot/main.c


void

main
(
void
)
{

detect_memory
();
...
}
```

```
// file:arch/x86/boot/memory.c


void

detect_memory
(
void
)
{

detect_memory_e820
();
...
}
```

真正的探測操作是在   `detect_memory_e820` 中完成的。`detect_memory_e820` 函式發出 15 中斷並處理所有結果，把記憶體位址範圍儲存到 boot_params.e820_table 物件中。

```
//file:arch/x86/boot/memory.c
static void detect_memory_e820(void)
{
 struct boot_e820_entry *
desc
 = boot_params.e820_table
;


 initregs(&ireg)
;


ireg.ax
  =
0
xe820
;

 ...

 do {
  intcall(0x15, &ireg, &oreg)
;

  ...
  *desc++ = buf
;

  count++
;

 }while (ireg.ebx && count < ARRAY_SIZE(boot_params.e820_table))
;



boot_params.e820_entries
 = count
;

}
```

## 三、儲存 e820 全域性資料並列印

在上面一節中，把記憶體位址範圍資料儲存到   `boot_params` 中了。但 `boot_params` 只是一箇中間啟動過程資料。記憶體這麼重要的資料還是應該單獨存起來。所以，還專門有一個 `e820_table` 全域性資料結構。

/
/file:`arch/x`
`86/kernel/e820.c`
```
static struct e820_table e820_table_init  __initdata;
struct e820_table *e820_table __refdata   = &e820_table_init;
```

```
//file
:arch/x86/include/asm/e820/types
.h
struct e820_table {
__u32 nr_entries;
struct e820_entry entries[
E820_MAX_ENTRIES
];
};
```

在核心啟動的後面的過程中，會把   boot_params.e820_table 中的資料複製到這個全域性的 `e820_table` 中。並把它列印出來。具體是在 e820__memory_setup 函式中處理的。

```
//file:arch\x86\kernel\e820.c

void __init
e820__memory_setup
(void)
{

// 儲存 boot_params.e820_table 儲存到全域性 e820_table 中

char *who;
who = x86_init
.resources
.memory_setup
();
...


// 列印記憶體檢測結果


pr_info
("BIOS-provided physical RAM map:\n");

e820__print_table
(who);
}
```

關於儲存過程我們就不細看了。我們重點來了解下記憶體檢測結果列印。

```
//file:arch\x86\kernel\e820.c
void __init e820__print_table(char *who)
{
 int i
;


 for (
i
 =
0
; i < e820_table->nr_entries; i++) {

  pr_info("%s:
[mem %#018Lx-%#018Lx]
 ",
   who,
   e820_table->entries
[i]
.addr,
   e820_table->entries
[i]
.addr + e820_table->entries
[i]
.size - 1)
;


  e820_print_type(e820_table->entries
[i]
.type)
;

  pr_cont("\n")
;

 }
}
```

核心啟動過程中的輸出的資訊透過 dmseg 命令來檢視。例如我的這臺機器啟動時輸入的日誌如下，詳細地展示了 BIOS 對實體記憶體的檢測結果。

Dec 23 04:53:10 kernel:
[    0.000000]
BIOS-provided physical RAM map:
Dec 23 04:53:10 kernel:
[    0.000000]
BIOS-e820:
[mem 0x0000000000000000-0x000000000009ffff]
usable
Dec 23 04:53:10 kernel:
[    0.000000]
BIOS-e820:
[mem 0x00000000000a0000-0x00000000000fffff]
reserved
Dec 23 04:53:10 kernel:
[    0.000000]
BIOS-e820:
[mem 0x0000000000100000-0x000000002fffffff]
usable
Dec 23 04:53:10 kernel:
[    0.000000]
BIOS-e820:
[mem 0x0000000030000000-0x0000000030041fff]
ACPI NVS
Dec 23 04:53:10 kernel:
[    0.000000]
BIOS-e820:
[mem 0x0000000030042000-0x0000000075daffff]
usable
Dec 23 04:53:10 kernel:
[    0.000000]
BIOS-e820:
[mem 0x0000000075db0000-0x0000000075ffffff]
reserved
Dec 23 04:53:10 kernel:
[    0.000000]
BIOS-e820:
[mem 0x0000000076000000-0x00000000a4d52fff]
usable
Dec 23 04:53:10 kernel:
[    0.000000]
BIOS-e820:
[mem 0x00000000a4d53000-0x00000000a6bf7fff]
reserved
Dec 23 04:53:10 kernel:
[    0.000000]
BIOS-e820:
[mem 0x00000000a6bf8000-0x00000000a6d49fff]
ACPI data
Dec 23 04:53:10 kernel:
[    0.000000]
BIOS-e820:
[mem 0x00000000a6d4a000-0x00000000a7241fff]
ACPI NVS
Dec 23 04:53:10 kernel:
[    0.000000]
BIOS-e820:
[mem 0x00000000a7242000-0x00000000a816cfff]
reserved
Dec 23 04:53:10 kernel:
[    0.000000]
BIOS-e820:
[mem 0x00000000a816d000-0x00000000abffffff]
usable
Dec 23 04:53:10 kernel:
[    0.000000]
BIOS-e820:
[mem 0x00000000ac000000-0x00000000afffffff]
reserved
Dec 23 04:53:10 kernel:
[    0.000000]
BIOS-e820:
[mem 0x00000000b4000000-0x00000000b5ffffff]
reserved
Dec 23 04:53:10 kernel:
[    0.000000]
BIOS-e820:
[mem 0x00000000be000000-0x00000000bfffffff]
reserved
Dec 23 04:53:10 kernel:
[    0.000000]
BIOS-e820:
[mem 0x00000000c8000000-0x00000000c9ffffff]
reserved
Dec 23 04:53:10 kernel:
[    0.000000]
BIOS-e820:
[mem 0x00000000f4000000-0x00000000f5ffffff]
reserved
Dec 23 04:53:10 kernel:
[    0.000000]
BIOS-e820:
[mem 0x00000000fe000000-0x00000000ffffffff]
reserved
Dec 23 04:53:10 kernel:
[    0.000000]
BIOS-e820:
[mem 0x0000000100000000-0x000000104fefffff]
usable
Dec 23 04:53:10 kernel:
[    0.000000]
BIOS-e820:
[mem 0x000000104ff00000-0x000000104fffffff]
reserved
Dec 23 04:53:10 kernel:
[    0.000000]
BIOS-e820:
[mem 0x0000001050000000-0x000000204fefffff]
usable
Dec 23 04:53:10 kernel:
[    0.000000]
BIOS-e820:
[mem 0x000000204ff00000-0x000000204fffffff]
reserved
Dec 23 04:53:10 kernel:
[    0.000000]
BIOS-e820:
[mem 0x0000002050000000-0x000000304fefffff]
usable
Dec 23 04:53:10 kernel:
[    0.000000]
BIOS-e820:
[mem 0x000000304ff00000-0x000000304fffffff]
reserved
Dec 23 04:53:10 kernel:
[    0.000000]
BIOS-e820:
[mem 0x0000003050000000-0x000000404f2fffff]
usable
Dec 23 04:53:10 kernel:
[    0.000000]
BIOS-e820:
[mem 0x000000404f300000-0x000000404fffffff]
reserved
Dec 23 04:53:10 kernel:
[    0.000000]
BIOS-e820:
[mem 0x0000004050000000-0x000000504fefffff]
usable
Dec 23 04:53:10 kernel:
[    0.000000]
BIOS-e820:
[mem 0x000000504ff00000-0x000000504fffffff]
reserved
Dec 23 04:53:10 kernel:
[    0.000000]
BIOS-e820:
[mem 0x0000005050000000-0x000000604fefffff]
usable
Dec 23 04:53:10 kernel:
[    0.000000]
BIOS-e820:
[mem 0x000000604ff00000-0x000000604fffffff]
reserved
Dec 23 04:53:10 kernel:
[    0.000000]
BIOS-e820:
[mem 0x0000006050000000-0x000000704fefffff]
usable
Dec 23 04:53:10 kernel:
[    0.000000]
BIOS-e820:
[mem 0x000000704ff00000-0x000000704fffffff]
reserved
Dec 23 04:53:10 kernel:
[    0.000000]
BIOS-e820:
[mem 0x0000007050000000-0x000000804fefffff]
usable
Dec 23 04:53:10 kernel:
[    0.000000]
BIOS-e820:
[mem 0x000000804ff00000-0x000000804fffffff]
reserved
......

## 總結

一臺伺服器上會被插入多條記憶體。但作業系統在剛開始的時候，對記憶體的可用位址範圍、NUMA 分組資訊都是一無所知每條需要被編排進某個位址範圍然後才能被 CPU 使用。

Linux 作業系統在啟動的時候會向韌體程式發起 15 號中斷，並設定操作碼為 E820 H。然後韌體上報記憶體位址範圍，核心會把它儲存並列印出來。

使用 dmseg 可以檢視到 Linux 在啟動時對記憶體探測過程的記錄。其中輸出的最後一列為 usable 是實際可用的實體記憶體位址範圍。被標記為 reserved 的記憶體不能被分配使用，可能是記憶體啟動時用來儲存核心的一些關鍵資料和程式碼，也可能沒有對映到實際的實體記憶體。

如果你感興趣的話，可以使用 dmesg 檢視下你的 Linux 對實體記憶體的探測結果。

有了這個 `e820_table` 表示的可用實體記憶體位址範圍後，後面核心會透過自己的初期分配器 memblock、以及夥伴系統兩種機制將所有可用的實體記憶體管理起來。

其中 memblock 管理方式比較粗放，僅僅用於核心啟動時。啟動好了後 memblock 會把可用實體頁交接給夥伴系統。夥伴系統管理的比較精細，會按 4KB、8KB、16KB 等各種不同大小的空閒塊來管理。

當我們的應用程式執行過程中，發現對應虛擬位址對應實體記憶體還沒有分配的時候，會觸發缺頁中斷 (Page Fault)。在缺頁中斷中向夥伴系統申請指定大小的實體記憶體頁面。

這樣我們就把從實體記憶體的可用位址探測到最後的應用程式實體記憶體申請，這中間的執行過程就都串起來理解了。
