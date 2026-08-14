---
layout: default
title: "深入剖析 Golang 程式啟動原理 - 從 ELF 入口點到 GMP 初始化到執行 main！"
---

# 深入剖析 Golang 程式啟動原理 - 從 ELF 入口點到 GMP 初始化到執行 main！

> 原文標題：深入剖析 Golang 程序启动原理 - 从 ELF 入口点到GMP初始化到执行 main！

> 原文連結：https://mp.weixin.qq.com/s/0EZCmABsMEV3TFVmDZmzZA

---

大家好，我是飛哥！

在過去的開發工作中，大家都是透過建立程序 (Process) 或者執行緒 (Thread) 來工作的。[Linux 程序是如何建立出來的？](https://heapdump.cn/article/4636337)、[聊聊 Linux 中執行緒和程序的聯絡與區別！](https://heapdump.cn/article/4683828) 和 [你的新程序是如何被核心排程執行到的？](https://mp.weixin.qq.com/s?__biz=MjM5Njg5NDgwNA==&mid=2247490622&idx=1&sn=cc98b3eb0c2802199a3d0a9bb50a0c50&scene=21#wechat_redirect) 這幾篇文章就是幫大家深入理解程序執行緒原理的。

但是，時至今日光了解程序和執行緒已經不夠了。因為現在協程 (Coroutine) 程式設計模型大行其道。很多同學知道程序和執行緒，但就是不理解協程是如何工作的。雖然能寫出來程式碼，但不理解底層執行原理。

今天就讓我以 golang 版本的 hello world 程式為例，給大家拆解一下協程程式設計模型的工作過程。

在本文中我會從 ELF 可執行檔案的入口講起，講到 GMP 排程器的初始化，到主協程的建立，到主協程進入 runtime.main 最後執行到使用者定義的 main 函式。

## 一、 hello world 程式的執行入口

```go
package main

import "fmt"

func main() {
    fmt.Println("Hello World!")
}
```

執行起來也是一樣非常的簡單。

```console
#
go build main.go
#
./main
Hello World!
```

程式是跑起來了，但是問題來了。傳說中的協程究竟長什麼樣子，是何時被建立的，又是如何被載入執行並列印出 「Hello World!」 的。

不管是什麼語言編譯出來的可執行檔案，都有一個執行入口點。shell 在將程式載入完後會跳轉到程式入口點開始執行。

但值得提前說的一點是一般程式語言的入口點都不會是我們在程式碼中寫的那個 main。c 語言中如此，golang 中更是這樣。這是因為各個語言都需要在程序啟動過程中做一些啟動邏輯的。在 golang 中，其底層執行的 GMP、垃圾回收等機制都需要在進入使用者的 main 函式之前啟動起來。

接下來我們需要藉助 readelf 和 nm 命令來找到上述編譯出來的可執行檔案 main 的執行入口。首先使用 readelf 找到 main 的入口點是在 0x45c220 位置處，如下圖所示。

$ readelf --file-header main
ELF Header:
......
Entry point address:               0x45c220

那麼 0x45c220 這個位置對應的是哪個函式呢？藉助 nm 命令我們可以看到它是 _rt0_amd64_linux。

nm -n main | `grep` 45c220
000000000045c220 T _rt0_amd64_linux

這其實是一個彙編函式。

```
// file:a**_amd64.s
// _rt0_amd64 is common startup code for most amd64 systems when using
// internal linking.
TEXT _rt0_amd64(SB),NOSPLIT,$-8
	MOVQ	0(SP), DI	// argc
	LEAQ	8(SP), SI	// argv
	JMP	runtime·rt0_go(SB)
```

這個函式的開頭也有明確的註釋 「_rt0_amd64 is common startup code for most amd64 systems when using internal linking」。這說明我們找對了。

接下來的 golang 執行就是順著這個彙編函式開始執行，最後一步步地執行到我們所熟悉的 main 函式的。

## 二、入口執行分析

這一小節我們來看看 golang 程式在啟動的時候都做了哪些事情。相信理解這些底層的工作機制對從事 golang 開發的同學會非常的大有裨益。

在上一小節我們看到了的 golang 入口函式 _rt0_amd64。要注意的是，當程式碼執行到這裡的時候，作業系統 (Operating System) 已經為當前可執行檔案建立好了一個主執行緒了。_rt0_amd64 只是將參數簡單地儲存一下後就 JMP （彙編中的函式呼叫）到 runtime·`rt0_go` 中了。

這個函式很長，我們只挑有重要的講！

```
// file:runtime/a**_amd64.s
TEXT runtime·rt0_go(SB),NOSPLIT|TOPFRAME,$0
	......
	// 2.1 Golang 核心初始化過程
	CALL	runtime·osinit(SB)
	CALL	runtime·schedinit(SB)
	//2.2 呼叫 runtime·newproc 建立一個協程
	//    並將 runtime.main 函式作為入口
	MOVQ	$runtime·mainPC(SB), AX		// entry
	PUSHQ	AX
	CALL	runtime·newproc(SB)
	POPQ	AX
	//2.3 啟動執行緒，啟動排程系統
	CALL	runtime·mstart(SB)
```

洋洋灑灑好幾百行彙編程式碼，其實縮略完後，關鍵的核心邏輯就是上面幾個關鍵點。

第一、透過 runtime 中的 osinit、schedinit 等函式對 golang 執行時進行關鍵的初始化。在這裡我們將看到 GMP 的初始化，與排程邏輯。

第二、建立一個主協程，並指明 runtime.main 函式是其入口函式。因為作業系統載入的時候只建立好了主執行緒，協程這種東西還是得使用者態 (User Mode) 的 golang 自己來管理。golang 在這裡建立出了自己的第一個協程。

第三、呼叫 runtime·mstart 真正開啟執行。

接下來我們分三個小節來詳細瞭解下這三塊的邏輯。

## 2.1 golang 核心初始化

golang 的核心初始化包括 runtime·osinit 和 runtime·schedinit 這兩個函式。

在 runtime·osinit 中主要是取得 CPU 數量，頁大小和作業系統初始化工作。

```
// file:os_linux.go
func

osinit
()
{
ncpu = getproccount()
physHugePageSize = getHugePageSize()
osArchInit()
}
```

接下來是   runtime.schedinit 的初始化，這裡主要是對排程系統的初始化。

在這個函式的註釋中，也貼心地告訴了我們，golang 的 bootstrap（啟動）流程步驟分別是 call osinit、call schedinit、make & queue new G 和 call runtime·mstart 四個步驟。這和我們前面說的一致。

Golang 中排程的核心就是 GMP 原理。這裡我們不展開對 GMP 進行過多的說明，留著將來再細說。這裡只提一下，在 runtime.schedinit 這個函式中，會將所有的 P 都給初始化好，並用一個 allp slice 維護管理起來。

```
// file:runtime/proc.go
// The bootstrap sequence is:
//
// call osinit
// call schedinit
// make & queue new G
// call runtime·mstart
//
// The new G calls runtime·main.
func

schedinit
()
{
......

// 預設情況下 procs 等於 cpu 個數

// 如果設定了 GOMAXPROCS 則以這個為準
procs := ncpu

if
n, ok := atoi32(gogetenv(
"GOMAXPROCS"
)); ok && n >
0
{
procs = n
}

// 分配 procs 個 P

if
procresize(procs) !=
nil
{
throw(
"unknown runnable goroutine during bootstrap"
)
}
......
}
```

從上述原始碼中我們可以看到，P 的數量取決於當前 cpu 的數量，或者是   runtime.GOMAXPROCS 的設定。

不少 golang 的同學都有一種錯誤的認知，認為 runtime.GOMAXPROCS 限制的是 golang 中的執行緒數。這個認知是錯誤的。 runtime.GOMAXPROCS 真正制約的是 GMP 中的 P，而不是 M。

再來簡單看下 procresize，這個函式其實就是在維護 allp 變數，在這裡儲存著所有的 P。

```
// file:runtime/proc.go
// Change number of processors
// Returns list of Ps with local work, they need to be scheduled by the caller
func

procresize
(nprocs
int32
)
*
p
{

// 申請儲存 P 的陣列

if
nprocs >
int32
(
len
(allp)) {
allp = ...
}

// 對新 P 進行記憶體 (Memory) 分配和初始化，並儲存到 allp 陣列中

for
i := old; i < nprocs; i++ {
pp := allp[i]

if
pp ==
nil
{
pp =
new
(p)
}
pp.init(i)
atomicstorep(unsafe.Pointer(&allp[i]), unsafe.Pointer(pp))
}
...
}
```

## 2.2 golang 主協程的建立

彙編程式碼呼叫 runtime·newproc 建立一個協程，並將 runtime.main 函式作為入口。我們來看下第一個主協程是如何建立出來的。

```
//file:runtime/proc.go
func
newproc
(fn *funcval)

{
...
systemstack(func() {
newg := newproc1(fn, gp, pc)
_p_ := getg().m.p.ptr()
runqput(_p_, newg,
true
)

if
mainStarted {
wakep()
}
})
}
```

systemstack 這個函式是 golang 內部經常使用的，runtime 程式碼經常透過呼叫 systemstack 臨時性的切換到系統堆疊去執行一些特殊的任務。這裡所謂的系統堆疊，就是作業系統視角建立出來的執行緒和執行緒堆疊。如果不理解，先不管這個也問題不大。

接著呼叫 newproc1 來建立一個協程出來，runqput 達標的是將協程新增到執行佇列。最後的 wakep 是去喚醒一個執行緒去執行執行佇列中的協程。

## 協程建立

我們一個一個分別來看。先看 newproc1 是如何建立協程的。

```
// file:runtime/proc.go
func

newproc1
(fn *funcval, callergp *g, callerpc
uintptr
)
*
g
{
...

//從快取 (Cache) 中取得或者建立 G 物件
newg := gfget(_p_)

if
newg ==
nil
{
newg = malg(_StackMin)
...
}
newg.sched.sp = sp
newg.stktopsp = sp
...
newg.startpc = fn.fn
...

return
newg
}
```

在 gfget 中是嘗試從快取 (Cache) 中取得一個 G 物件出來。我們忽略這個邏輯，直接看 malg，因為它是建立一個 G。對我們理解更有幫助。在 malg 建立完後，對新的 gorutine 物件進行一些設定後就回傳了。

在呼叫 malg 時傳入了一個 _StackMin，這表示預設的堆疊大小，在 Golang 中的預設值是 2048。

> 這也就是很多人所說的 Golang 中協程很輕量，只需要消耗 2 KB 記憶體 (Memory) 的緣由。但其實這個說法並不是很準確。首先這裡分配的並不是 2KB，下面我們會看到還有有一些預留。另外當發生缺頁中斷 (Page Fault) 的時候，Linux 是以 4 KB 為單位分配的。

```
// file:runtime/proc.go
func

malg
(stacksize
int32
)
*
g
{
newg :=
new
(g)

if
stacksize >=
0
{

//這裡會在 stacksize 的基礎上為每個堆疊預留系統呼叫 (System Call) 所需的記憶體大小 \_StackSystem

//在 Linux/Darwin 上（ \_StackSystem == 0 ）本行不改變 stacksize 的大小
stacksize = round2(_StackSystem + stacksize)
}

// 切換到 G0 為 newg 初始化堆疊記憶體
systemstack(
func
()
{
newg.stack = stackalloc(
uint32
(stacksize))
})

// 設定 stackguard0 ，用來判斷是否要進行堆疊擴容
newg.stackguard0 = newg.stack.lo + _StackGuard
newg.stackguard1 = ^
uintptr
(
0
)
}
```

在呼叫 malg 的時候會將傳入的記憶體大小加上一個 _StackSystem 值預留給系統呼叫 (System Call) 使用，round2 函式會將傳入的值舍入為 2 的指數。然後會切換到 G0 執行 stackalloc 函式進行堆疊記憶體分配。分配完畢之後會設定 stackguard0 為 stack.lo + _StackGuard，作為將來判斷是否需要進行堆疊擴容使用。

```
//file:runtime/stack.go
func
stackalloc
(n uint32)

stack

{
thisg := getg()
...

//對齊到整數頁
n = uint32(alignUp(uintptr(n), physPageSize))
v := sysAlloc(uintptr(n), &memstats.stacks_sys)

return

stack
{uintptr(v), uintptr(v) + uintptr(n)}
}
```

其中堆疊是這樣一個結構體

```
//file:runtime/runtime2.go
type
 stack
struct
 {
 lo
uintptr
 hi
uintptr
}
```

sysAlloc 使用 `mmap` 系統呼叫來真正為協程堆疊申請指定大小的位址空間。

```
// file:runtime/mem_darwin.go
func

sysAlloc
(n
uintptr
, sysStat *sysMemStat)

unsafe
.
Pointer
{
v, err := mmap(
nil
, n, _PROT_READ|_PROT_WRITE, _MAP_ANON|_MAP_PRIVATE,
    -1,
    0)

if err != 0 {
    return nil
}
sysStat.add(int64(n))

return v
}
```

## 將協程新增到執行佇列

在協程建立出來後，會呼叫 runqput 將它新增到執行佇列中。在講這塊的邏輯之前，我們得首先講講 Golang 中的執行佇列。

Golang 為什麼會抽象出一個 P 來呢。這是因為 Golang 在 1.0 版本的多執行緒排程器的實作中，排程器和鎖都是全域性資源，鎖的競爭和開銷非常的大，導致效能比較差。

其實這個問題在 Linux 中早已很好地解決了。Golang 就把它學來了。在 Linux 中每個 CPU 核都有一個 runqueue，來儲存著將來要在該核上排程執行的程序或執行緒。這樣排程的時候，只需要看當前的 CPU 上的資源就行，把鎖的開銷就砍掉了。

所以，Golang 中的 P 可以認為是對 Linux 中 CPU 的一個虛擬，目的是和 Linux 一樣，找一個無競爭地保管執行佇列資源的方法。在 Golang 中，每個 P 都有它的執行佇列。

```
// file:runtime/proc.go
// runqput tries to put g on the local runnable queue.
// If next is false, runqput adds g to the tail of the runnable queue.
// If next is true, runqput puts g in the _p_.runnext slot.
// If the run queue is full, runnext puts g on the global queue.
// Executed only by the owner P.
func

runqput
(_p_ *p, gp *g, next
bool
)
{
...

//將新 goroutine 新增到 P 的 runnext 中

if
next {
retryNext:
oldnext := _p_.runnext

if
!_p_.runnext.cas(oldnext, guintptr(unsafe.Pointer(gp))) {

goto
retryNext
}

if
oldnext ==
0
{

return
}

// 將原來的 runnext 新增到執行佇列中
gp = oldnext.ptr()
}

//將新協程或者被從 runnext 上踢下來的協程新增到執行佇列中
retry:
h := atomic.LoadAcq(&_p_.runqhead)
// load-acquire, synchronize with consumers
t := _p_.runqtail

//如果 P 的執行佇列沒滿，那就新增到尾部

if
t-h <
uint32
(
len
(_p_.runq)) {
_p_.runq[t%
uint32
(
len
(_p_.runq))].set(gp)
atomic.StoreRel(&_p_.runqtail, t+
1
)
// store-release, makes the item available for consumption

return
}

//如果滿了，就新增到全域性執行佇列中

if
runqputslow(_p_, gp, h, t) {

return
}
}
```

在 runqput 中首先嚐試將新協程放到 runnext 中，這個有優先執行權。然後會將新協程，或者被新協程從 runnext 上踢下來的協程加入到當前 P（執行佇列）的尾部去。但還有可能當前這個執行佇列已經任務過多了，那就需要呼叫 runqputslow 分一部分執行佇列中的協程到全域性佇列中去。以便於減輕當前執行佇列的執行壓力。

## 喚醒一個執行緒去

前面只是將新建立的 goroutine 新增到了 P 的執行佇列中。現在 GMP 中的 G 有了，P 也有了，就還差 M 了。真正的執行還是需要作業系統的執行緒去執行的。

```
// file:runtime/proc.go
func

wakep
()
{
...
startm(
nil
,
true
)
}
```

wakep 核心是呼叫 startm。這個函式將會排程執行緒去執行 P 中的執行佇列。如果有必要的話，可能也需要建立新執行緒出來。

```
// file:runtime/proc.go
// Schedules some M to run the p (creates an M if necessary).
func

startm
(_p_ *p, spinning
bool
)
{
mp := acquirem()

//如果沒有傳入 p，就取得一個 idel p

if
_p_ ==
nil
{
_p_ = pidleget()
}

//再取得一個空閒的 m
nmp := mget()

if
nmp ==
nil
{

//如果取得不到，就建立一個出來
newm(fn, _p_, id)
...

return
}
...
}
```

## 2.3 啟動排程系統

現在 GMP 中的三元素全具備了，而且主協程中的執行函式 fn 也指定為了 runtime.main。接下來就是呼叫 mstart 來啟動執行緒，啟動排程系統。

彙編中的 mstart 函式呼叫的是 golang 原始碼中的 mstart0

```
// file:runtime/proc.go
func

mstart0
()
{
...
mstart1()
}
```

```
// file:runtime/proc.go
func

mstart1
()
{
...

// 進入排程迴圈
schedule()
}
```

其中，schedule 是整個 golang 程式的執行核心。所有的協程都是透過它來開始執行的。

schedule 的主要工作邏輯有這麼幾點

- 每隔 61 次排程輪迴從全域性佇列找，避免全域性佇列中的 g 被餓死。
- 從 p.runnext 取得 g，從 p 的本地佇列中取得。
- 呼叫 findrunnable 找 g，找不到的話就將 m 休眠，等待喚醒。
- 當找到一個 g 後，就會呼叫 execute 去執行 g

然後再來看原始碼就很容易理解了。

```
// file:runtime/proc.go
func

schedule
()
{
_g_ := getg()
...
top:
pp := _g_.m.p.ptr()

//每 61 次從全域性執行佇列中取得可執行的協程

if
gp ==
nil
{

if
_g_.m.p.ptr().schedtick%
61
==
0
&& sched.runqsize >
0
{
lock(&sched.lock)
gp = globrunqget(_g_.m.p.ptr(),
1
)
unlock(&sched.lock)
}
}

if
gp ==
nil
{

//從當前 P 的執行佇列中取得可執行
gp, inheritTime = runqget(_g_.m.p.ptr())
}

if
gp ==
nil
{

//當前 P 或者全域性佇列中取得可執行協程

//嘗試從其他 P 中 steal 任務來處理

//如果取得不到，就阻塞
gp, inheritTime = findrunnable()
// blocks until work is available
}
//執行協程
execute(gp, inheritTime)
}
```

其中 findrunnable 如果從當前 P 的執行佇列和全域性執行佇列取得 G 都沒有任務後，還會嘗試從其他的 P 中取得一些任務過來執行。程式碼就不過多展示了。

## 三、main 函式的真正執行

至此，整個 golang 的排程系統就算是跑起來了。因為前面我們建立了主協程，而且還給它設定了   runtime.main 函式作為入口。所以對於主協程的排程，就會進入這個入口進行執行。終於，能看到 runtime 快執行到我們自己寫的 main 函式中了。

runtime.main 在執行 main 套件中的 main 之前，還是做了一些不少其他工作，包括：

- 新建一個執行緒來執行 sy**on。sy**on 的工作是系統後臺監控（定期垃圾回收和排程搶佔）。
- 執行 runtime init 函式。runtime 包中也有不少的 init 函式，會在這個時機執行
- 啟動 gc 清掃的 goroutine。
- 執行 main init 函式。包括使用者定義的所有的 init 函式。
- 執行使用者 main 函式。

```
// file:runtime/proc.go
// The main goroutine.
func

main
()
{
g := getg()

// 在系統堆疊上執行 sy**on
systemstack(
func
()
{
newm(sy**on,
nil
,
```

- 1
)
})

// runtime 內部 init 函式的執行，編譯器動態產生的。
doInit(&`runtime_inittask`)
// Must be before defer.

// gc 啟動一個 goroutine 進行 gc 清掃
gcenable()

// 執行 main init
doInit(&`main_inittask`)

// 執行使用者 main
fn := `main_main`
// make an indirect call, as the linker doesn't know the address of the main package when laying down the runtime
fn()

// 退出程式
exit(
0
)
}

好了，終於，我們定義的 main 函式能被執行到。可以輸出 「Hello World！」了。

## 四、總結

Golang 程式的執行入口是 runtime 定義的一個彙編函式。這個函式核心有三個邏輯：

第一、透過 runtime 中的 osinit、schedinit 等函式對 golang 執行時進行關鍵的初始化。在這裡我們將看到 GMP 的初始化，與排程邏輯。

第二、建立一個主協程，並指明   runtime.main 函式是其入口函式。因為作業系統載入的時候只建立好了主執行緒，協程這種東西還是得使用者態的 golang 自己來管理。golang 在這裡建立出了自己的第一個協程。

第三、呼叫 runtime·mstart 真正開啟排程器進行執行。

當排程器開始執行後，其中主協程會進入 runtime.main 函式中執行。在這個函式中進行幾件初始化後，最後後真正進入使用者的 main 中執行。

第一、新建一個執行緒來執行 sy**on。sy**on 的工作是系統後臺監控（定期垃圾回收和排程搶佔）。
第二、啟動 gc 清掃的 goroutine。
第三、執行 runtime init，使用者 init。
第四、執行使用者 main 函式。

看似簡簡單單的一個 Golang 的 Hello World 程式，只要你願意深挖，裡面真的有極其豐富的營養的！
