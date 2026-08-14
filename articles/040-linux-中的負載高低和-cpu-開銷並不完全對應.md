---
layout: default
title: "Linux 中的負載高低和 CPU 開銷並不完全對應"
---

# Linux 中的負載高低和 CPU 開銷並不完全對應

> 原文標題：Linux 中的负载高低和 CPU 开销并不完全对应

> 原文連結：https://mp.weixin.qq.com/s/1Pl4tT_Nq-fEZrtRpILiig

---

大家好，我是飛哥！

負載是檢視 Linux 伺服器執行狀態時很常用的一個效能指標。在觀察線上伺服器執行狀況的時候，我們也是經常把負載找出來看一看。線上上請求壓力過大的時候，經常是也伴隨著負載的飆高。

但是負載的原理你真的理解了嗎？我來列舉幾個問題，看看你對負載的理解是否足夠的深刻。

- 負載是如何計算出來的？
- 負載高低和 CPU 消耗正相關嗎？
- 核心是如何暴露負載資料給應用層的？

如果你對以上問題的理解還拿捏不是很準，那麼飛哥今天就帶你來深入地瞭解一下 Linux 中的負載！

## 一、理解負載檢視過程

我們經常用   `top` 命令檢視 Linux 系統的負載情況。一個典型的 `top` 命令輸出的負載如下所示。

```
# top
Load Avg: 1.25, 1.30, 1.95  .....
......
```

輸出中的 Load Avg 就是我們常說的負載，也叫系統平均負載。因為單純某一個瞬時的負載值並沒有太大意義。所以 Linux 是計算了過去一段時間內的平均值，這三個數分別代表的是過去 1 分鐘、過去 5 分鐘和過去 15 分鐘的平均負載值。

那麼   `top` 命令展示的資料數是如何來的呢？事實上，`top` 命令裡的負載值是從 / `proc/loadavg` 這個偽檔案裡來的。透過 `strace` 命令跟蹤 `top` 命令的系統呼叫 (System Call) 可以看的到這個過程。

```
# strace top
...
openat(AT_FDCWD, "/proc/loadavg", O_RDONLY) = 7
```

核心中定義了 loadavg 這個偽檔案的 open 函式。當使用者態 (User Mode) 存取 `/proc/loadavg` 會觸發核心定義的函式，在這裡會讀取核心中的平均負載變數，簡單計算後便可展示出來。整體流程如下圖所示。

我們根據上述流程圖再展開了看下。偽檔案 `/proc/loadavg` 在 kernel 中定義是在 `/fs/proc/loadavg.c` 中。在該檔案中會建立 `/proc/loadavg`，併為其指定操作方法 `loadavg_proc_fops`。

```
//file: fs/proc/loadavg.c
static int __init proc_loadavg_init(void)
{
    proc_create("loadavg", 0, NULL, &loadavg_proc_fops);
    return 0;
}
```

在 `loadavg_proc_fops` 中包含了開啟該檔案時對應的操作方法。

```
//file: fs/proc/loadavg.c
static const struct file_operations loadavg_proc_fops = {
    .open        = loadavg_proc_open,
    ......
};
```

當在使用者態開啟 `/proc/loadavg` 檔案時，都會呼叫   `loadavg_proc_fops` 中的 open 函式指標 -loadavg_proc_open。 `loadavg_proc_open` 接下來會呼叫 `loadavg_proc_show` 進行處理，核心的計算是在這裡完成的。

```
//file: fs/proc/loadavg.c
static int loadavg_proc_show(struct seq_file *m, void *v)
{
    unsigned long avnrun[3];

    //取得平均負載值
    get_avenrun(avnrun, FIXED_1/200, 0);

    //列印輸出平均負載
    seq_printf(m, "%lu.%02lu %lu.%02lu %lu.%02lu %ld/%d %d\n",
        LOAD_INT(avnrun[0]), LOAD_FRAC(avnrun[0]),
        LOAD_INT(avnrun[1]), LOAD_FRAC(avnrun[1]),
        LOAD_INT(avnrun[2]), LOAD_FRAC(avnrun[2]),
        nr_running(), nr_threads,
        task_active_pid_ns(current)->last_pid);
    return 0;
}
```

在   `loadavg_proc_show` 函式中做了兩件事。

- 呼叫 `get_avenrun` 讀取當前負載值
- 將平均負載值按照一定的格式列印輸出

> 在上面的原始碼中，大家看到了   `FIXED_1/200`、`LOAD_INT`、`LOAD_FRAC` 等奇奇怪怪的定義，程式碼寫的這麼猥瑣是因為核心中並沒有 float、double 等浮點數型別，而是用整數來模擬的。這些程式碼都是為了在整數和小數之間轉化使的。知道這個背景就行了，不用過度展開剖析。

這樣使用者透過存取 `/proc/loadavg` 檔案就可以讀取到核心計算的負載資料了。其中取得 `get_avenrun` 只是在存取 avenrun 這個全域性陣列而已。

```
//file:kernel/sched/core.c
void get_avenrun(unsigned long *loads, unsigned long offset, int shift)
{
    loads[0] = (avenrun[0] + offset) << shift;
    loads[1] = (avenrun[1] + offset) << shift;
    loads[2] = (avenrun[2] + offset) << shift;
}
```

現在可以總結一下我們開篇中的一個問題:核心是如何暴露負載資料給應用層的？

核心定義了一個偽檔案 `/proc/loadavg`，每當使用者開啟這個檔案的時候，核心中的   `loadavg_proc_show` 函式就會被呼叫到，接著存取 avenrun 全域性陣列變數並將平均負載從整數轉化為小數，並列印出來。

好了，另外一個新問題又來了，avenrun 全域性陣列變數中儲存的資料是何時，又是被如何計算出來的呢？

## 二、核心中負載的計算過程

接上小節，我們繼續檢視 avenrun 全域性陣列變數的資料來源。這個陣列的計算過程分為如下兩步：

1. PerCPU 定期彙總瞬時負載：定時重新整理每個 CPU 當前任務數到 `calc_load_tasks`，將每個 CPU 的負載資料彙總起來，得到系統當前的瞬時負載。
2. 定時計算系統平均負載：定時器根據當前系統整體瞬時負載，使用指數加權移動平均法（一種高效計算平均數的演算法）計算過去 1 分鐘、過去 5 分鐘、過去 15 分鐘的平均負載。

接下來我們分成兩個小節來分別介紹。

## 2.1 PerCPU 定期彙總負載

在 Linux 核心 (Linux Kernel) 中，有一個子系統叫做時間子系統。在時間子系統裡，初始化了一個叫高解析度的定時器。在該定時器中會定時將每個 CPU 上的負載資料（running 程序 (Process) 數 + uninterruptible 程序數）彙總到系統全域性的瞬時負載變數 `calc_load_tasks` 中。整體流程如下圖所示。

我們把上述流程圖展開看一下，我們找到了高解析度定時器的原始碼如下：

```
//file:kernel/time/tick-sched.c
void tick_setup_sched_timer(void)
{
    //初始化高解析度定時器 sched_timer
    hrtimer_init(&ts->sched_timer, CLOCK_MONOTONIC, HRTIMER_MODE_ABS);

    //將定時器的到期函式設定成 tick_sched_timer
    ts->sched_timer.function = tick_sched_timer;
    ...
}
```

在高解析度初始化的時候，將到期函式設定成了   `tick_sched_timer`。透過這個函式讓每個 CPU 都會週期性地執行一些任務。其中重新整理當前系統負載就是在這個時機進行的。這裡有一點要注意一個前提是每個 CPU 都有自己獨立的執行佇列，。

我們根據 `tick_sched_timer` 的原始碼進行追蹤，它依次透過呼叫 `tick_sched_handle`=> `update_process_times`=>  `scheduler_tick`。最終在 `scheduler_tick` 中會重新整理當前 CPU 上的負載值到 `calc_load_tasks` 上。因為每個 CPU 都在定時刷，所以 `calc_load_tasks` 上記錄的就是整個系統的瞬時負載值。

我們來看下負責重新整理的 `scheduler_tick` 這個核心函式:

```
//file:kernel/sched/core.c
void scheduler_tick(void)
{
    int cpu = smp_processor_id();
    struct rq *rq = cpu_rq(cpu);
    update_cpu_load_active(rq);
    ...
}
```

在這個函式中，取得當前 cpu 以及其對應的執行佇列 rq（run queue），呼叫 `update_cpu_load_active` 重新整理當前 CPU 的負載資料到全域性陣列中。

```
//file:kernel/sched/core.c
static void update_cpu_load_active(struct rq *this_rq)
{
    ...
    calc_load_account_active(this_rq);
}
```

```
//file:kernel/sched/core.c
static void calc_load_account_active(struct rq *this_rq)
{
    //取得當前執行佇列的負載相對值
    delta  = calc_load_fold_active(this_rq);
    if (delta)
        //新增到全域性瞬時負載值
        atomic_long_add(delta, &calc_load_tasks);
    ...
}
```

在   `calc_load_account_active` 中看到，透過 `calc_load_fold_active` 取得當前執行佇列的負載相對值，並把它加到全域性瞬時負載值 `calc_load_tasks` 上。至此， `calc_load_tasks` 上就有了當前系統當前時間下的整體瞬時負載總數了。

我們再展開看看是如何根據執行佇列計算負載值的：

```
//file:kernel/sched/core.c
static long calc_load_fold_active(struct rq *this_rq)
{
    long nr_active, delta = 0;

    // R 和 D 狀態的使用者 task
    nr_active = this_rq->nr_running;
    nr_active += (long) this_rq->nr_uninterruptible;

    // 只回傳變化的量
    if (nr_active != this_rq->calc_load_active) {
        delta = nr_active - this_rq->calc_load_active;
        this_rq->calc_load_active = nr_active;
    }

    return delta;
}
```

哦，原來是同時計算了   `nr_running` 和 `nr_uninterruptible` 兩種狀態的程序的數量。對應於使用者空間中的 R 和 D 兩種狀態的 task 數（程序 OR 執行緒 (Thread)）。

> 由於   `calc_load_tasks` 是一個長期存在的資料。所以在重新整理 rq 裡的程序數到其上的時候，只需要刷變化的量就行，不用全部重算。因此上述函式回傳的是一個 delta。

## 2.2 定時計算系統平均負載

上一小節中我們找到了系統當前瞬時負載 `calc_load_tasks` 變數的更新過程。現在我們還缺一個計算過去 1 分鐘、過去 5 分鐘、過去 15 分鐘平均負載的機制。

傳統意義上，我們在計算平均數的時候採取的方法都是把過去一段時間的數字都加起來然後平均一下。把過去 N 個時間點的所有瞬時負載都加起來取一個平均數不完事了。這其實是我們傳統意義上理解的平均數，假如有 n 個數字，分別是 x1, x2, ..., xn。那麼這個資料集合的平均數就是 (x1 + x2 + ... + xn) / N。

但是如果用這種簡單的演算法來計算平均負載的話，存在以下幾個問題：

1. 需要儲存過去每一個取樣週期的資料
假設我們每 10 毫秒都採集一次，那麼就需要使用一個比較大的陣列將每一次取樣的資料全部都存起來，那麼統計過去 15 分鐘的平均數就得存 1500 個資料 (15 分鐘 * 每分鐘 100 次) 。而且每出現一個新的觀察值，就要從移動平均中減去一個最早的觀察值，再加上一個最新的觀察值，記憶體 (Memory) 陣列會頻繁地修改和更新。

2. 計算過程較為複雜
計算的時候再把整個陣列全加起來，再除以樣本總數。雖然加法很簡單，但是成百上千個數字的累加仍然很是繁瑣。

3. 不能準確表示當前變化趨勢
傳統的平均數計算過程中，所有數字的權重是一樣的。但對於平均負載這種實時應用來說，其實越靠近當前時刻的數值權重應該越要大一些才好。因為這樣能更好反應近期變化的趨勢。

所以，在 Linux 裡使用的並不是我們所以為的傳統的平均數的計算方法，而是採用的一種指數加權移動平均（Exponential Weighted Moving Average，EMWA）的平均數計演算法。

這種指數加權移動平均數計演算法在深度學習中有很廣泛的應用。另外股票市場裡的 EMA 均線也是使用的是類似的方法求均值的方法。該演算法的數學表示式是：a1 = a0 * factor + a * (1 - factor)。這個演算法想理解起來有點小複雜，感興趣的同學可以 Google 自行搜尋。

我們只需要知道這種方法在實際計算的時候只需要上一個時間的平均數即可，不需要儲存所有瞬時負載值。另外就是越靠近現在的時間點權重越高，能夠很好地表示近期變化趨勢。

這其實也是在時間子系統中定時完成的，透過一種叫做指數加權移動平均計算的方法，計算這三個平均數。

我們來詳細看下上圖中的執行過程。時間子系統將在時鐘中斷中會註冊時鐘中斷的處理函式為 `timer_interrupt`。

```
//file:arch/ia64/kernel/time.c
void __init
time_init (void)
{
    register_percpu_irq(IA64_TIMER_VECTOR, &timer_irqaction);
    ia64_init_itm();
}

static struct irqaction timer_irqaction = {
.handler =    timer_interrupt,
.flags =    IRQF_DISABLED | IRQF_IRQPOLL,
.name =        "timer"
};
```

當每次時鐘節拍到來時會呼叫到   `timer_interrupt`，依次會呼叫到 `do_timer` 函式。

```
//file:kernel/time/timekeeping.c
void do_timer(unsigned long ticks)
{
    ...
    calc_global_load(ticks);
}
```

其中   `calc_global_load` 是平均負載計算的核心。它會取得系統當前瞬時負載值 `calc_load_tasks`，然後來計算過去 1 分鐘、過去 5 分鐘、過去 15 分鐘的平均負載，並儲存到 avenrun 中，供使用者程序讀取。

```
//file:kernel/sched/core.c
void calc_global_load(unsigned long ticks)
{
    ...
    // 1.取得當前瞬時負載值
    active = atomic_long_read(&calc_load_tasks);

    // 2.平均負載的計算
    avenrun[0] = calc_load(avenrun[0], EXP_1, active);
    avenrun[1] = calc_load(avenrun[1], EXP_5, active);
    avenrun[2] = calc_load(avenrun[2], EXP_15, active);
    ...
}
```

取得瞬時負載比較簡單，就是讀取一個記憶體變數而已。在 `calc_load` 中就是採用了我們前面說的指數加權移動平均法來計算過去 1 分鐘、過去 5 分鐘、過去 15 分鐘的平均負載的。具體實作的程式碼如下：

```
//file:kernel/sched/core.c
/*
```

* a1 = a0 * e + a * (1 - e)
* /
```
static unsigned long
calc_load(unsigned long load, unsigned long exp, unsigned long active)
{
load *= exp;
load += active * (FIXED_1 - exp);
load += 1UL << (FSHIFT - 1);
return load >> FSHIFT;
}
```

雖然這個演算法理解起來挺複雜，但是程式碼看起來確實要簡單不少，計算量看起來很少。而且看不懂也沒有關係，只需要知道核心並不是採用的原始的平均數計算方法，而是採用了一種計算快，且能更好表達變化趨勢的演算法就行。

至此，我們開篇提到的「負載是如何計算出來的？」這個問題也有結論了。

Linux 定時將每個 CPU 上的執行佇列中 running 和 uninterruptible 的狀態的程序數量彙總到一個全域性系統瞬時負載值中，然後再定時使用指數加權移動平均法來統計過去 1 分鐘、過去 5 分鐘、過去 15 分鐘的平均負載。

## 三、平均負載和 CPU 消耗的關係

現在很多同學都將平均負載和 CPU 給聯絡到了一起。認為負載高、CPU 消耗就會高，負載低，CPU 消耗就會低。

在很老的 Linux 的版本裡，統計負載的時候確實是只計算了 runnable 的任務數量，這些程序只對 CPU 有需求。在那個年代裡，負載和 CPU 消耗量確實是正相關的。負載越高就表示正在 CPU 上執行，或等待 CPU 執行的程序越多，CPU 消耗量也會越高。

但是前面我們看到了，本文使用的 3.10 版本的 Linux 負載平均數不僅跟蹤 runnable 的任務，而且還跟蹤處於 uninterruptible sleep 狀態的任務。而 uninterruptible 狀態的程序其實是不佔 CPU 的。

所以說，負載高並一定是 CPU 處理不過來，也有可能會是因為磁碟 (Disk) 等其他資源排程不過來而使得程序進入 uninterruptible 狀態的程序導致的！

為什麼要這麼修改。我從網上搜到了遠在 1993 年的一封郵件裡找到了原因，以下是郵件原文。

From: Matthias Urlichs <urlichs@smurf.sub.org>
Subject: Load average broken ?
Date: Fri, 29 Oct 1993 11:37:23 +0200


The kernel only counts "runnable" processes when computing the load average.
I don't like that; the problem is that processes which are swapping or
waiting on "fast", i.e. noninterruptible, `I/O`, also consume resources.

It seems somewhat nonintuitive that the load average goes down when you
replace your fast swap disk with a slow swap disk...

Anyway, the following patch seems to make the load average much more
consistent WRT the subjective speed of the system. And, most important, the
load is still zero when nobody is doing anything. ;-)


- -- `kernel/sched.c.orig` Fri Oct 29 10:31:11 1993
+ ++ `kernel/sched.c`  Fri Oct 29 10:32:51 1993
@@ -414,7 +414,9 @@
unsigned long nr = 0;

for(p = &`LAST_TASK`; p > &`FIRST_TASK`; --p)
- if (*p && (*p)->state == `TASK_RUNNING`)
+ if (*p && ((*p)->state == `TASK_RUNNING`) ||
+ (*p)->state == `TASK_UNINTERRUPTIBLE`) ||
+ (*p)->state == `TASK_SWAPPING`))
nr += `FIXED_1`;
return nr;
}

可見這個修改是在 1993 年就引入了。在這封郵件所示的 Linux 原始碼變化中可以看到，負載正式把   `TASK_UNINTERRUPTIBLE` 和 `TASK_SWAPPING` 狀態（交換狀態後來從 Linux 中刪除）的程序也給新增了進來。在這封郵件中的正文中，作者也清楚地表達了為什麼要把 `TASK_UNINTERRUPTIBLE` 狀態的程序新增進來的原因。我把他的說明翻譯一下，如下：

「核心在計算平均負載時只計算「可執行」程序。我不喜歡那樣；問題是正在「快速」交換或等待的程序，即不可中斷的 `I/O`，也會消耗資源。 當您用慢速交換磁碟替換快速交換磁碟時，平均負載下降似乎有點不直觀...... 無論如何，下面的補丁似乎使負載平均值更加一致 WRT 系統的主觀速度。而且，最重要的是，當沒有人做任何事情時，負載仍然為零。 ;-)」

這一補丁提交者的主要思想是平均負載應該表現對系統所有資源的需求情況，而不應該只表現對 CPU 資源的需求。

假設某個   `TASK_UNINTERRUPTIBLE` 狀態的程序因為等待磁碟 IO 而排隊的話，此時它並不消耗 CPU，但是正在等磁碟等硬體資源。那麼它是應該體現在平均負載的計算裡的。所以作者把 `TASK_UNINTERRUPTIBLE` 狀態的程序都表現到平均負載裡了。

所以，負載高低表明的是當前系統上對系統資源整體需求更情況。如果負載變高，可能是 CPU 資源不夠了，也可能是磁碟 IO 資源不夠了，所以還需要配合其他觀測命令具體分情況分析。

## 四、總結

今天我帶大家深入地學習了一下 Linux 中的負載。我們根據一幅圖來總結一下今天學到的內容。

我把負載工作原理分成了如下三步。

- 1. 核心定時彙總每 CPU 負載到系統瞬時負載
- 2. 核心使用指數加權移動平均快速計算過去 1、5、15 分鐘的平均數
- 3. 使用者程序透過開啟 loadavg 讀取核心中的平均負載

我們再回頭來總結一下開篇提到的幾個問題。

1. 負載是如何計算出來的？
是定時將每個 CPU 上的執行佇列中 running 和 uninterruptible 的狀態的程序數量彙總到一個全域性系統瞬時負載值中，然後再定時使用指數加權移動平均法來統計過去 1 分鐘、過去 5 分鐘、過去 15 分鐘的平均負載。

2. 負載高低和 CPU 消耗正相關嗎？
負載高低表明的是當前系統上對系統資源整體需求更情況。如果負載變高，可能是 CPU 資源不夠了，也可能是磁碟 IO 資源不夠了。所以不能說看著負載變高，就覺得是 CPU 資源不夠用了。

3. 核心是如何暴露負載資料給應用層的？
核心定義了一個偽檔案 /  `proc/loadavg`，每當使用者開啟這個檔案的時候，核心中的 `loadavg_proc_show` 函式就會被呼叫到，該函式中存取 avenrun 全域性陣列變數，並將平均負載從整數轉化為小數，然後列印出來。
