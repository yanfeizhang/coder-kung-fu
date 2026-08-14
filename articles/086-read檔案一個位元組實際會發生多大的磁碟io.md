---
layout: default
title: "`read` 檔案一個位元組實際會發生多大的磁碟 (Disk) IO？"
---

# `read` 檔案一個位元組實際會發生多大的磁碟 (Disk) IO？

> 原文標題：read文件一个字节实际会发生多大的磁盘IO？

> 原文連結：https://mp.weixin.qq.com/s/vekemOfUHBjZSy3uXb49Rw

---

?

先講一個作者大約 5-6 年前我在某當時很火的一個應用分發創業公司的面試小插曲，該公司安排了一個剛工作 1 年多的一個同學來面我，聊到我們專案中的設定檔裡寫的一個開關，這位同學就跳出來說，你這個讀檔案啦，每個使用者請求來了還得多一次的磁碟 IO，效能肯定差。藉由這個故事其實我發現了一個問題，雖然我們中的大部分人都是電腦科班出身，程式碼也寫的很遛。但是在一些看似司空見慣的問題上，我們中的絕大多數人並沒有真正理解，或者理解的不夠透徹。

不管你用的是什麼語言，  `C/PHP/GO`、還是 Java，相信大家都有過讀取檔案的經歷。我們來思考兩個問題，如果我們讀取檔案中的一個位元組：

- 是否會發生磁碟 IO？
- 發生的話，Linux 實際向磁碟讀取多少位元組了呢？

為了便於理解問題，我們把 c 的程式碼也列出來：

```
int main()
{
char    c;
int     in;

in = open("in.txt",O_RDONLY);
read(in,&c,1);
return 0;
}
```

如果不是從事   `C/C`++開發工作的同學，這個問題想深度理解起來確實不那麼容易。因為目前常用的主流語言，`PHP/Java/Go` 什麼的封裝層次都比較高，把核心的很多細節都給遮蔽的比較徹底。要想把上面的兩個問題釐清，需要剖開 Linux 的內部來理解 Linux 的 IO 堆疊。

1

Linux IO 堆疊簡介

廢話不多說，我們直接把 Linux IO 堆疊的一個簡化版本畫出來：（官方的 IO 堆疊參考這個：http://www.ilinuxkernel.com/files/Linux.IO.stack_v1.0.pdf）

?

圖 1 Linux 硬碟 IO 堆疊

我們在前面也分享了幾篇文章討論了上圖圖中的硬體層，還有檔案系統 (File System) 模組。但透過這個 IO 堆疊我們發現，我們對 Linux 檔案的 IO 的理解還是遠遠不夠，還有好幾個核心元件：IO 引擎、VFS、PageCache、通用塊管理層、IO 排程層等模組我們並沒有瞭解太多。彆著急，讓我們一一道來：

1. IO 引擎

我們開發同學想要讀寫檔案的話，在 lib 庫層有很多種函式可以選擇，例如   `read`，`write`， `mmap` 等。這事實上就是在選擇 Linux 提供的 IO 引擎。我們最常用的 `read`、  `write` 函式是屬於 sync 引擎，除了 sync，還有 map、psync、vsync、libaio、posixaio 等。sync，psync 都屬於同步方式，libaio 和 posixaio 屬於非同步 IO。

當然了 IO 引擎也需要 VFS、通用塊層等更底層的支援才能實作。在 sync 引擎的 `read` 函式里會進入 VFS 提供的 `read` 系統呼叫 (System Call)。

2. VFS 虛擬檔案系統

在核心層，第一個看到的是 VFS。VFS 誕生的思想是抽象一個通用的檔案系統模型，對我們開發人員或者是使用者提供一組通用的介面，讓我們不用 care 具體檔案系統的實作。VFS 提供的核心資料結構有四個，它們定義在核心原始碼的 `include/linux/fs.h` 和 `include/linux/dcache.h` 中。

- superblock：Linux 用來標註具體已安裝的檔案系統的有關資訊
- inode：Linux 中的每一個檔案都有一個 inode，你可以把 inode 理解為檔案的身份證
- file：記憶體 (Memory) 中的檔案物件，用來儲存程序 (Process) 和磁碟檔案的對應關係
- desty：目錄項，是路徑中的一部分，所有的目錄項物件串起來就是一棵 Linux 下的目錄樹。

圍繞這這四個核心資料結構，VFS 也都定義了一系列的操作方法。例如，inode 的操作方法定義 `inode_operations`(`include/linux/fs.h`)，在它的裡面定義了我們非常熟悉的 mkdir 和 rename 等。

```
struct inode_operations {
......
int (*link) (struct dentry *,struct inode *,struct dentry *);
int (*unlink) (struct inode *,struct dentry *);
int (*mkdir) (struct inode *,struct dentry *,  umode_t);
int (*rmdir) (struct inode *,struct dentry *);
int (*rename) (struct inode *, struct dentry *,
struct inode *, struct dentry *, unsigned int);
......
```

在 file 對應的操作方法 `file_operations` 裡面定義了我們經常使用的 `read` 和 `write`：

```
struct   file_operations {
......
ssize_t (* read) (struct file *, char __user *, size_t,   loff_t *);
ssize_t (* write) (struct file *, const char __user *, size_t,   loff_t *);
......
int (*mmap) (struct file *,struct vm_area_struct *);
int (*open) (struct inode *, struct file *);
int (*flush) (struct file *, fl_owner_t id);
```

3. Page Cache

在 VFS 層往下看，我們注意到了 Page Cache。它的中文譯名叫頁快取 (Cache) 記憶體，是 Linux 核心 (Linux Kernel) 使用的主要磁碟快取記憶體，是一個純記憶體的工作元件，其作用就是來給存取相對比較慢的磁碟來進行存取加速。如果要存取的檔案 block 正好存在於 Page Cache 內，那麼並不會有實際的磁碟 IO 發生。如果不存在，那麼會申請一個新頁，發出缺頁中斷 (Page Fault)，然後用磁碟讀取到的 block 內容來填充它 ，下次直接使用。Linux 核心使用搜尋樹來高效管理大量的頁面。

如果你有特殊的需求想要繞開 Page Cache，只要設定   `DIRECT_IO` 就可以了。有兩種情況需要繞開：

- 測試磁碟 IO 的真實效能
- 節約使用 Page Cache 時系統呼叫陷入到核心態 (Kernel Mode)，以及核心記憶體向使用者程序記憶體複製到開銷。

4. 檔案系統

在我在之前的文章《新建一個空檔案佔用多少磁碟空間？》、《理解格式化原理》裡討論的都是具體的檔案系統。檔案系統裡最重要的兩個概念就是 inode 和 block，這兩個我們在之前的文章裡也都見過了。一個 block 是多大呢，這是運維在格式化的時候決定的，一般預設是 4KB。

除了 inode 和 block，每個檔案系統還會定義自己的實際操作函式。例如在 ext4 中定義的 `ext4_file_operations` 和 `ext4_file_inode_operations` 如下：

```
const struct file_operations ext4_file_operations = {
.read_iter      = ext4_file_read_iter,
.write_iter     =ext4_file_write_iter,
.mmap           = ext4_file_mmap,
.open           = ext4_file_open,
......
};

const struct   inode_operations ext4_file_inode_operations = {
.setattr        =   ext4_setattr,
.getattr        = ext4_file_getattr,
......
};
```

5. 通用塊層

通用塊層是一個處理系統中所有塊裝置 IO 請求的核心模組。它定義了一個叫 bio 的資料結構來表示一次 IO 操作請求（`include/linux/bio.h`）。

那麼一次 bio 裡對應的 IO 大小單位是頁面，還是扇區呢？都不是，是段！每個 bio 可能會包含多個段。一個段是一個完整的頁面，或者是頁面的一部分，具體請參考這個（https://www.ilinuxkernel.com/files/Linux.Generic.Block.Layer.pdf）。

為什麼要引入「段」這麼讓人費解的東西呢？這是因為在磁碟中連續儲存的資料，到了記憶體 Page Cache 裡的時候可能記憶體並不連續了。這種狀況出現是正常的，不能說磁碟中連續的資料我在記憶體中就非得用連續的空間來快取。段就是為了能讓一次磁碟 IO 能 DMA 到多「段」位址並不連續的記憶體中的。

一個常見的扇區/段/頁的大小對例如下圖：

?

圖 2 Linux 的頁/段/扇區的關係範例

6. IO 排程層

當通用塊層把 IO 請求實際發出以後，並不一定會立即被執行。因為排程層會從全域性出發，儘量讓整體磁碟 IO 效能最大化。大致的工作方式是讓磁頭類似電梯那樣工作，先往一個方向走，到頭再回來，這樣磁碟效率會比較高一些。具體的演算法有 noop，deadline 和 cfg 等。

在你的機器上，透過 dmesg | `grep` -i scheduler 來檢視你的 Linux 支援的演算法，並在測試的時候可以選擇其中的一種。

2

讀檔案過程

我們已經把 Linux IO 堆疊裡的各個核心元件都介紹一邊了。現在我們再從頭整體過一下讀取檔案的過程

- lib 裡的   `read` 函式首先進入系統呼叫 `sys_read`
- 在   `sys_read` 再進入 VFS 裡的 `vfs_read`、`generic_file_read` 等函式
- 在 vfs 裡的 `generic_file_read` 會判斷是否快取命中，命中則回傳
- 若不命中核心在 Page Cache 裡分配一個新頁框，發出缺頁中斷，
- 核心向通用塊層發起塊   `I/O` 請求，塊裝置遮蔽了磁碟、隨身碟的差異
- 通用塊層把用 bio 代表的 `I/O` 請求放到 IO 請求佇列中
- IO 排程層透過電梯演算法來排程佇列中的請求
- 驅動程式向磁碟控制器發出讀取命令控制，DMA 方式直接填充到 Page Cache 中的新頁框
- 控制器發出中斷通知
- 核心將使用者需要的 1 個位元組填充到使用者記憶體中
- 然後你的程序被喚醒

可以看到，如果 Page Cache 命中的話，根本就沒有磁碟 IO 產生。所以，大家不要覺得程式碼裡出現幾個讀寫檔案的邏輯就覺得效能會慢的不行。作業系統 (Operating System) 已經替你最佳化了很多很多，記憶體級別的存取延遲大約是 ns 級別的，比機械磁碟 IO 快了 2-3 個數量級。如果你的記憶體足夠大，或者你的檔案被存取的足夠頻繁，其實這時候的 `read` 操作極少有真正的磁碟 IO 發生。

我們再看第二種情況，如果 Page Cache 不命中的話，Linux 實際進行了多少個位元組的磁碟 IO。整個 IO 過程中涉及到了好幾個核心元件。 而每個元件之間都是採用不同長度的塊來管理磁碟資料的。

- Page Cache 是以頁為單位的，Linux 頁大小一般是 4KB（避免有大神挑刺，這裡說下 Linux 能設定大記憶體頁）
- 檔案系統是以塊為單位來管理的。使用 dumpe2fs 可以檢視，一般一個塊預設是 4KB
- 通用塊層是以段為單位來處理磁碟 IO 請求的，一個段為一個頁或者是頁的一部分
- IO 排程程式透過 DMA 方式傳輸 N 個扇區到記憶體，扇區一般為 512 位元組
- 硬碟也是採用「扇區」的管理和傳輸資料的

可以看到，雖然我們從使用者角度確實是只讀了 1 個位元組（開篇的程式碼中我們只給這次磁碟 IO 留了一個位元組的快取區）。但是在整個核心工作流中，最小的工作單位是磁碟的扇區，為 512 位元組，比 1 個位元組要大的多。另外 block、page cache 等高層元件工作單位更大，所以實際一次磁碟讀取是很多位元組一起進行的。假設段就是一個記憶體頁的話，一次磁碟 IO 就是 4KB（8 個 512 位元組的扇區）一起進行讀取。

Linux 核心中我們沒有講到的是還有一套複雜的預讀取的策略。所以，在實踐中，可能比 8 更多的扇區來一起被傳輸到記憶體中。

3

最後

作業系統的本意是做到讓你簡單可依賴， 讓你儘量把它當成一個黑盒。你想要一個位元組，它就給你一個位元組，但是自己默默幹了許許多多的工作。我們雖然國內絕大多數開發都不是從事底層開發，但如果你十分關注你的應用程式的效能，你應該明白作業系統的什麼時候悄悄提高了你的效能，是怎麼來提高的。以便在將來某一個時候你的線上伺服器扛不住快要掛掉的時候，你能迅速找出問題所在。

我們再擴充套件一下，假如 Page Cache 沒有命中，那麼一定會有傳動到機械軸上的磁碟 IO 嗎？

其實也不一定，為什麼，因為現在的磁碟本身就會帶一塊快取。另外現在的伺服器都會組建磁碟陣列，在磁碟陣列裡的核心硬體 Raid 卡里也會整合 RAM 作為快取。只有所有的快取都不命中的時候，機械軸帶著磁頭才會真正工作。
