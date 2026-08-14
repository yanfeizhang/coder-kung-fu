---
layout: default
title: "從程序 (Process) 堆疊記憶體 (Memory) 底層原理到 Segmentation fault 報錯"
---

# 從程序 (Process) 堆疊記憶體 (Memory) 底層原理到 Segmentation fault 報錯

> 原文標題：从进程栈内存底层原理到Segmentation fault报错

> 原文連結：https://mp.weixin.qq.com/s/pTP7ELOm4-JsAl1o_nyS-Q

---

大家好，我是飛哥！

堆疊是程式設計中使用記憶體最簡單的方式。例如，下面的簡單程式碼中的區域性變數 n 就是在堆疊中分配記憶體的。

```
#include <stdio.h>
void main()
{
    int n = 0;
    printf("0x%x\n",&v);
}
```

那麼我有幾個問題想問問大家，看看大家對於堆疊記憶體是否真的瞭解。

- 堆疊的實體記憶體 (Physical Memory) 是什麼時候分配的？
- 堆疊的大小限制是多大？這個限制可以調整嗎？
- 當堆疊發生溢位後應用程式會發生什麼？

如果你對以上問題還理解不是特別深刻，飛哥今天來帶你好好修煉程序堆疊記憶體這塊的內功！

## 一、程序堆疊的初始化

前面我們在 [《你寫的程式碼是如何跑起來的？》](https://mp.weixin.qq.com/s/1bdktqYF7VyAMadRlcRrSg) 這篇文章中介紹了程序的啟動過程。程序啟動呼叫 exec 載入可執行檔案過程的時候，會給程序堆疊申請一個 4 KB 的初始記憶體。我們今天來專門抽取並看一下這段邏輯。

載入系統呼叫 (System Call)  `execve` 依次呼叫 `do_execve`、 `do_execve_common` 來完成實際的可執行程式載入。

```
//file:fs/exec.c
static int do_execve_common(const char *filename, ...)
{
    bprm_mm_init(bprm);
    ...
}
```

在   `bprm_mm_init` 中會申請一個全新的位址空間 `mm_struct` 物件，準備留著給新程序使用。

```
//file:fs/exec.c
static int bprm_mm_init(struct linux_binprm *bprm)
{
    //申請個全新的位址空間 mm_struct 物件
    bprm->mm = mm = mm_alloc();
    __bprm_mm_init(bprm);
}
```

還會給新程序的堆疊申請一頁大小的虛擬記憶體 (Virtual Memory) 空間，作為給新程序準備的堆疊記憶體。申請完後把堆疊的指標儲存到 `bprm->p` 中記錄起來。

```
//file:fs/exec.c
static int __bprm_mm_init(struct linux_binprm *bprm)
{
    bprm->vma = vma = kmem_cache_zalloc(vm_area_cachep, GFP_KERNEL);
    vma->vm_end = STACK_TOP_MAX;
    vma->vm_start = vma->vm_end - PAGE_SIZE;
    ...

    bprm->p = vma->vm_end - sizeof(void *);
}
```

我們平時所說的程序虛擬位址空間在 Linux 是透過一個個的   `vm_area_struct` 物件來表示的。

每一個 `vm_area_struct`（就是上面 \_\_bprm_mm_init 函式中的 vma）物件表示程序虛擬位址空間裡的一段範圍，其 `vm_start` 和 `vm_end` 表示啟用的虛擬位址範圍的開始和結束。

```
//file:include/linux/mm_types.h
struct vm_area_struct {
    unsigned long vm_start;
    unsigned long vm_end;
    ...
}
```

要注意的是這只是位址範圍，而不是真正的實體記憶體分配。

在上面 \_\_bprm_mm_init 函式中透過   `kmem_cache_zalloc` 申請了一個 vma 核心物件。`vm_end` 指向了 `STACK_TOP_MAX`（位址空間的頂部附近的位置）， `vm_start` 和   `vm_end` 之間留了一個 Page 大小。也就是說預設給堆疊準備了 4KB 的大小。最後把堆疊的指標記錄到 `bprm->p` 中。

接下來程序載入過程會使用 `load_elf_binary` 真正開始載入可執行二進位程式。在載入時，會把前面準備的程序堆疊的位址空間指標設定到了新程序 mm 物件上。

```
//file:fs/binfmt_elf.c
static int load_elf_binary(struct linux_binprm *bprm)
{    //ELF 檔案頭解析
    //Program Header 讀取
    //清空父程序繼承來的資源
    retval = flush_old_exec(bprm);
    ...

    current->mm->start_stack = bprm->p;
}
```

這樣新程序將來就可以使用堆疊進行函式呼叫，以及區域性變數的申請了。

前面我們說了，這裡只是給堆疊申請了位址空間物件，並沒有真正申請實體記憶體。我們接著再來看一下，實體記憶體頁究竟是什麼時候分配的。

## 二、實體頁的申請

當程序在執行的過程中在堆疊上開始分配和存取變數的時候，如果實體頁還沒有分配，會觸發缺頁中斷 (Page Fault)。在缺頁中斷種來真正地分配實體記憶體。

為了避免篇幅過長，觸發缺頁中斷的過程就先不展開了。我們直接看一下缺頁中斷的核心處理入口 \_\_do_page_fault，它位於 `arch/x86/mm/fault.c` 檔案下。

```
//file:arch/x86/mm/fault.c
static void __kprobes
__do_page_fault(struct pt_regs *regs, unsigned long error_code)
{
    ...
    //根據新的 address 查詢對應的 vma
    vma = find_vma(mm, address);

    //如果找到的 vma 的開始位址比 address 小
    //那麼就不呼叫expand_stack了，直接呼叫
    if (likely(vma->vm_start <= address))
        goto good_area;
    ...
    if (unlikely(expand_stack(vma, address))) {
        bad_area(regs, error_code, address);
        return;
    }
good_area:
    //呼叫handle_mm_fault來完成真正的記憶體申請
    fault = handle_mm_fault(mm, vma, address, flags);

}
```

當存取堆疊上變數的記憶體的時候，首先會呼叫   `find_vma` 根據變數位址 address 找到其所在的 vma 物件。接下來呼叫的 if (`vma->vm_start` <= address) 是在判斷位址空間還夠不夠用。

如果堆疊記憶體 vma 的 start 比要存取的 address 小，則證明位址空間夠用，只需要分配實體記憶體頁就行了。 如果堆疊記憶體 vma 的 start 比要存取的 address 大，則需要呼叫  `expand_stack` 先擴充套件一下堆疊的虛擬位址空間 vma。擴充套件虛擬位址空間的具體細節我們在第三節再講。

這裡先假設要存取的變數位址 address 處於堆疊記憶體 vma 物件的  `vm_start` 和 `vm_end` 之間。那麼缺頁中斷處理就會跳轉到 `good_area` 處執行。在這裡呼叫 `handle_mm_fault` 來完成真正實體記憶體的申請。

```
//file:mm/memory.c
int handle_mm_fault(struct mm_struct *mm, struct vm_area_struct *vma,
        unsigned long address, unsigned int flags)
{
    ...

    //依次檢視每一級頁表項
    pgd = pgd_offset(mm, address);
    pud = pud_alloc(mm, pgd, address);
    pmd = pmd_alloc(mm, pud, address);
    pte = pte_offset_map(pmd, address);

    return handle_pte_fault(mm, vma, address, pte, pmd, flags);
}
```

Linux 是用四級頁表 (Page Table) 來管理虛擬位址空間到實體記憶體之間的對映管理的。所以在實際申請實體頁面之前，需要先 check 一遍需要的每一級頁表項是否存在，不存在的話需要申請。

為了好區分，Linux 還給每一級頁表都起了一個名字。

- 一級頁表：Page Global Dir，簡稱 pgd
- 二級頁表：Page Upper Dir，簡稱 pud
- 三級頁表：Page Mid Dir，簡稱 pmd
- 四級頁表：Page Table，簡稱 pte

看一下下面這個圖就比較好理解了

```
//file:mm/memory.c
int handle_pte_fault(struct mm_struct *mm,
             struct vm_area_struct *vma, unsigned long address,
             pte_t *pte, pmd_t *pmd, unsigned int flags)
{
    ...

    //匿名對映頁處理
    return do_anonymous_page(mm, vma, address,
                         pte, pmd, flags);
}
```

在   `handle_pte_fault` 會處理很多種的記憶體缺頁處理，例如檔案對映缺頁處理、swap 缺頁處理、寫時複製缺頁處理、匿名對映頁處理等等幾種情況。我們今天討論的主題是堆疊記憶體，這個對應的是匿名對映頁處理，會進入到 `do_anonymous_page` 函式中。

```
//file:mm/memory.c
static int do_anonymous_page(struct mm_struct *mm, struct vm_area_struct *vma,
        unsigned long address, pte_t *page_table, pmd_t *pmd,
        unsigned int flags)
{
    // 分配可移動的匿名頁面，底層透過 alloc_page
    page = alloc_zeroed_user_highpage_movable(vma, address);
    ...
}
```

在   `do_anonymous_page` 呼叫 `alloc_zeroed_user_highpage_movable` 分配一個可移動的匿名實體頁出來。在底層會呼叫到夥伴系統的 `alloc_pages` 進行實際實體頁面的分配。

核心是用夥伴系統來管理所有的實體記憶體頁的。其他模組需要實體頁的時候都會呼叫夥伴系統對外提供的函式來申請實體記憶體。

關於夥伴系統我們之前在 [核心記憶體管理](https://mp.weixin.qq.com/s/OR2XB4J76haGc1THeq7WQg) 這篇文章中詳細介紹過，感興趣的同學可以移步到該文中詳細瞭解。

到了這裡，開篇的問題一就有答案了，堆疊的實體記憶體是什麼時候分配的？程序在載入的時候只是會給新程序的堆疊記憶體分配一段位址空間範圍。而真正的實體記憶體是等到存取的時候觸發缺頁中斷，再從夥伴系統中申請的。

## 三、堆疊的自動增長

前面我們看到了，程序在被載入啟動的時候，堆疊記憶體預設只分配了 4 KB 的空間。那麼隨著程式的執行，當堆疊中儲存的呼叫鏈，區域性變數越來越多的時候，必然會超過 4 KB。

我回頭看下缺頁處理函式 \_\_do_page_fault。如果堆疊記憶體 vma 的 start 比要存取的 address 大，則需要呼叫 `expand_stack` 先擴充套件一下堆疊的虛擬位址空間 vma。

回顧 \_\_do_page_fault 原始碼，看到擴充堆疊空間的是由 `expand_stack` 函式來完成的。

```
//file:arch/x86/mm/fault.c
static void __kprobes
__do_page_fault(struct pt_regs *regs, unsigned long error_code)
{
    ...
    if (likely(vma->vm_start <= address))
        goto good_area;

    //如果堆疊 vma 的開始位址比 address 大，需要擴大堆疊
    if (unlikely(expand_stack(vma, address))) {
        bad_area(regs, error_code, address);
        return;
    }
good_area:
    ...
}
```

我們來看下 `expand_stack` 的內部細節。

> 其實在 Linux 堆疊位址空間增長是分兩種方向的，一種是從高位址往低位址增長，一種是反過來。大部分情況都是由高往低增長的。本文只以向下增長為例。

```
//file:mm/mmap.c
int expand_stack(struct vm_area_struct *vma, unsigned long address)
{
    ...
    return expand_downwards(vma, address);
}

int expand_downwards(struct vm_area_struct *vma, unsigned long address)
{
...
//計算堆疊擴大後的最後大小
size = vma->vm_end - address;

//計算需要擴充幾個頁面
grow = (vma->vm_start - address) >> PAGE_SHIFT;

//判斷是否允許擴充
acct_stack_growth(vma, size, grow);

//如果允許則開始擴充
vma->vm_start = address;

return ...
}
```

在   `expand_downwards` 中先進行了幾個計算。

- 計算出新的堆疊大小。計算公式是 size = vma->vm_end- address;
- 計算需要增長的頁數。計算公式是 grow = (vma-> vm_start- address) >>`PAGE_SHIFT`;

然後會判斷此次堆疊空間是否被允許擴充， 判斷是在   `acct_stack_growth` 中完成的。如果允許擴充套件，則簡單修改一下 `vma->vm_start` 就可以了！

我們再來看 `acct_stack_growth` 都進行了哪些限制判斷。

```
//file:mm/mmap.c
static int acct_stack_growth(struct vm_area_struct *vma, unsigned long size, unsigned long grow)
{
    ...
    //檢查位址空間是否超出限制
    if (!may_expand_vm(mm, grow))
        return -ENOMEM;

    //檢查是否超出堆疊的大小限制
    if (size > ACCESS_ONCE(rlim[RLIMIT_STACK].rlim_cur))
        return -ENOMEM;
    ...
    return 0;
}
```

在   `acct_stack_growth` 中只是進行一系列的判斷。`may_expand_vm` 判斷的是增長完這幾個頁後是否超出整體虛擬位址空間大小的限制。 rlim[ `RLIMIT_STACK`].rlim_cur 中記錄的是堆疊空間大小的限制。這些限制都可以透過 ulimit 命令檢視到。

```
# ulimit -a
......
max memory size         (kbytes, -m) unlimited
stack size              (kbytes, -s) 8192
virtual memory          (kbytes, -v) unlimited
```

上面的這個輸出表示虛擬位址空間大小沒有限制，堆疊空間的限制是 8 MB。如果程序堆疊大小超過了這個限制，會回傳 -ENOMEM。如果覺得系統預設的大小不合適可以透過 ulimit 命令修改。

```
# ulimit -s 10240
```

```
# ulimit -a
stack size              (kbytes, -s) 10240
```

到這裡開篇的第二個問題也有答案了，堆疊的大小限制是多大？這個限制可以調整嗎？
程序堆疊大小的限制在每個機器上都是不一樣的，可以透過 ulimit 命令來檢視，也同樣可以使用該命令修改。

至於開篇的問題 3，當堆疊發生溢位後應用程式會發生什麼？寫個簡單的無限遞迴呼叫就知道了，估計你也遇到過。報錯結果就是

'Segmentation fault (core dumped)

## 本文總結

來總結下本文的內容，本文討論了程序堆疊記憶體的工作原理。

第一，程序在載入的時候給程序堆疊申請了一塊虛擬位址空間 vma 核心物件。  `vm_start` 和 `vm_end` 之間留了一個 Page ，也就是說預設給堆疊準備了 4KB 的空間。
第二，當程序在執行的過程中在堆疊上開始分配和存取變數的時候，如果實體頁還沒有分配，會觸發缺頁中斷。在缺頁中斷中呼叫核心的夥伴系統真正地分配實體記憶體。
第三，當堆疊中的儲存超過 4KB 的時候會自動進行擴大。不過大小要受到限制，其大小限制可以透過 ulimit -s 來檢視和設定。

注意，今天我們討論的都是程序堆疊。執行緒 (Thread) 堆疊和程序堆疊有些不一樣。等後面有空我們再單獨看執行緒堆疊。

在回顧和總結下開篇我們丟擲的三個問題：
問題一：堆疊的實體記憶體是什麼時候分配的？
程序在載入的時候只是會給新程序的堆疊記憶體分配一段位址空間範圍。而真正的實體記憶體是等到存取的時候觸發缺頁中斷，再從夥伴系統中申請的。
問題二：堆疊的大小限制是多大？這個限制可以調整嗎？
程序堆疊大小的限制在每個機器上都是不一樣的，可以透過 ulimit 命令來檢視，也同樣可以使用該命令修改。
問題 3：當堆疊發生溢位後應用程式會發生什麼？
當堆疊溢位的時候，我們會收到報錯 「Segmentation fault (core dumped)」

最後，拋個問題大家一起思考吧。為什麼要對堆疊的位址空間進行限制，我能想到的是及時發現出錯的無限函式遞迴呼叫。不知道還有沒有什麼其他更好的理由要限制程序堆疊的大小。
