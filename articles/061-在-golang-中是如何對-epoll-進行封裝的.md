---
layout: default
title: "在 golang 中是如何對 epoll 進行封裝的？"
---

# 在 golang 中是如何對 epoll 進行封裝的？

> 原文標題：在 golang 中是如何对 epoll 进行封装的？

> 原文連結：http://kfngxl.cn/index.php/archives/254/

---

在 golang 中是如何對 epoll 進行封裝的？

在協程沒有流行以前，傳統的網路程式設計中，同步阻塞是效能低下的代名詞，一次切換就得是 3 us 左右的 CPU 開銷。各種基於 epoll 的非同步非阻塞的模型雖然提高了效能，但是基於回撥函式的程式設計方式卻非常不符合人的的直線思維模式。開發出來的程式碼的也不那麼容易被人理解。

Golang 的出現，可以說是將協程程式設計模式推向了一個高潮。這種新的程式設計方式既兼顧了同步程式設計方式的簡單易用，也在底層透過協程和 epoll 的配合避免了執行緒切換的效能高損耗。換句話說就是既簡單易用，效能又還不挺錯。

飛哥當年也是相中的 golang 的這個特點，開始帶領團隊轉型 golang 開發的。那麼今天我們來深刻地和大家分享一下 golang 官方提供的 net 包，來看看它是如何達成上面所說的這樣的效果的。

一、Golang net的使用方式

考慮到不少讀者沒有使用過 golang，那麼開頭我先把一個基於官方 net 包的 golang 服務的簡單使用程式碼給大家列出來。為了方便大家理解，我只保留骨幹程式碼。

在這個示例服務程式中，先是使用 net.Listen 來監聽了本地的 9008 這個埠。然後呼叫 Accept 進行接收連線處理。如果接收到了連線請求，透過 go process() 來啟動一個協程進行處理。在連線的處理中我展示了讀寫操作（Read 和 Write）。

整個服務程式看起來，妥妥的就是一個同步模型，包括 Accept、Read 和 Write 都會將當前協程給“阻塞”掉。比如 Read 函式這裡，如果伺服器呼叫時客戶端資料還沒有到達，那麼 Read 是不帶返回的，會將當前的協程 park 住。直到有了資料 Read 才會返回，處理協程繼續執行。

你如果在其它語言，例如 C 和 Java 中寫出這樣類似的伺服器程式碼，估計會被打死的。因為每一次同步的 Accept、Read、Write 都會導致你當前的執行緒被阻塞掉，會浪費大量的 CPU 進行執行緒上下文的切換。

但是在 golang 中這樣的程式碼執行效能卻是非常的不錯，為啥呢？我們繼續看本文接下來的內容。

二、Listen 底層過程

在傳統的 C、Java 等傳統語言中，listen 所做的事情就是直接呼叫核心的 listen 系統呼叫。參見 《為什麼服務端程式都需要先 listen 一下？》 。但是如果你也這麼同等地理解 golang net 包裡的 Listen， 那可就大錯特錯了。

和其它語言不同，在 golang net 的 listen 中，會完成如下幾件事：

建立 socket 並設定非阻塞，

bind 繫結並監聽本地的一個埠

呼叫 listen 開始監聽

epoll_create 建立一個 epoll 物件

epoll_etl 將 listen 的 socket 新增到 epoll 中等待連線到來

一次 Golang 的 Listen 呼叫，相當於在 C 語言中的 socket、bind、listen、epoll_create、epoll_etl 等多次函式呼叫的效果。封裝度非常的高，更大程度地對程式設計師遮蔽了底層的實現細節。

口說無憑，我們挖開 Golang 的內部原始碼瞅一瞅，這樣更真實。

Listen 的入口在 golang 原始碼的 net/dial.go 檔案中，讓我們展開來看更細節的邏輯。

2.1 Listen 入口執行流程

原始碼不用細看，看懂大概流程就可以。

可見，這個 Listen 只是一個入口。接下來會進入到 ListenConfig 下的 Listen 方法中。在 ListenConfig 的 Listen 中判斷這是一個 TCP 型別的話，會進入到 sysListener 下的 listenTCP 方法裡（src/net/tcpsock_posix.go）。然後再經過兩三次的函式呼叫跳轉，會進入到 net/sock_posix.go 檔案下的 socket 函式中。我們直接看它。

接下來我們分別在 2.2 和 2.3 小節來介紹 sysSocket 和 listenStream 這兩個函式。

2.2 建立 socket

sysSocket 這個函式和其它語言中的 socket 函式有很大的不同。在這個一個函式內就完成了三件事，建立 socket、bind 和 listen 監聽。我們來看 sysSocket的具體程式碼。

在 sysSocket 中，呼叫的 socketFunc 其實就是 socket 系統呼叫。見如下程式碼。

建立完 socket 之後，再呼叫 syscall.SetNonblock 將其設定為非阻塞模式。

2.3 繫結和監聽

我們接著再來看 listenStream。這個函式一進來就呼叫了系統呼叫 bind 和 listen 來完成了繫結和監聽。

其中 listenFunc 是一個宏，指向的就是 syscall.Listen 系統呼叫

2.4 epoll建立和初始化

接下來在 fd.init 這一行，經過多次的函式呼叫展開以後會執行到 epoll 物件的建立，並還把在 listen 狀態的 socket 控制代碼新增到了 epoll 物件中來管理其網路事件。

我們來看它是如何完成的。

serverInit.Do 這個是用來保證引數內的函式只執行一次的。不過多展開介紹。其引數 runtime_pollServerInit 是對 runtime 包的函式 poll_runtime_pollServerInit 的呼叫，其原始碼位於 runtime/netpoll.go 下。

該函式會執行到 netpollGenericInit， epoll 就是在它的內部建立的。

再來看 runtime_pollOpen。它的引數就是前面 listen 好了的 socket 的檔案描述符。在這個函式里，它將被放到 epoll 物件中。

三、Accept 過程

服務端在 Listen 完了之後，就是對 Accept 的呼叫了。該函式主要做了三件事

呼叫 accept 系統呼叫接收一個連線

如果沒有連線到達，把當前協程阻塞掉

新連線到來的話，將其新增到 epoll 中管理，然後返回

透過 Golang 裡的單步除錯可以看到它進入到了 TCPListener 下的 Accept 裡了。

我們上面說的三步都是在 netFD 的 accept 函式里處理的。

接下來我們詳細看每一步的細節。

3.1 接收一個連線

經過單步跟蹤後發現 Accept 進入到了 FD 物件的 Accept 方法下。在這裡將呼叫作業系統的 accept 系統呼叫。

其中 accept 方法內部會觸發 linux 作業系統的 accept 系統呼叫，我們就不過度展開了。 呼叫 accept 目的是獲取一個來自客戶端的連線。如果接收到了，就把他返回回去。

3.2 阻塞當前協程

我們來說說如果沒 accept 呼叫的時候，客戶端的連線請求還一個都沒有過來怎麼辦。

這時候，accept 系統呼叫會返回 syscall.EAGAIN。Golang 在對這個狀態的處理中，會把當前協程給阻塞起來。關鍵程式碼在這裡

runtime_pollWait 的原始碼在 runtime/netpoll.go 下。gopark（協程的阻塞）就是在這裡完成的。

gopark 這個函式就是 golang 內部阻塞協程的入口。

3.3 將新連線新增到 epoll 中。

我們再來說說假如客戶端連線已經到來了的情況。這時 fd.pfd.Accept 會返回新建的連線。然後會將該新連線也一併加入到 epoll 中進行高效的事件管理。

我們來看 netfd.init

runtime_pollOpen 這個runtime 函式我們在上面的 2.4 節介紹過了，就是把檔案控制代碼新增到 epoll 物件中。

四、Read 和 Write 內部過程

當連線接收完成後，剩下的就是在連線上的讀寫了。

4.1 Read 內部過程

我們先來看 Read。

來看詳細的程式碼。

Read 函式會進入到 FD 的 Read 中。在這個函式內部呼叫 Read 系統呼叫來讀取資料。如果資料還尚未到達則也是把自己阻塞起來。

其中 waitRead 是如何將當前協程阻塞掉的，這個和我們前面 3.2 節介紹的是一樣的，就不過多展開敘述了。

4.2 Write 內部過程

Write 的大體過程和 Read 是類似的。 先是呼叫 Write 系統呼叫傳送資料，如果核心傳送快取區不足的時候，就把自己先阻塞起來，然後等可寫時間發生的時候再繼續傳送。其原始碼入口位於 net/net.go。

pd.wait 之後的事情就又和 3.2 節介紹的過程一樣了。呼叫 runtime_pollWait 來講當前協程阻塞掉。

五、Golang 喚醒

前面我們討論的很多步驟裡都涉及到協程的阻塞。例如 Accept 時如果新連線還尚未到達。再比如像 Read 資料的時候對方還沒有傳送，當前協程都不會佔著 cpu 不放，而是會阻塞起來。

那麼當要等待的事件就緒的時候，被阻塞掉的協程又是如何被重新排程的呢？ 相信大家一定會好奇這個問題。

Go 語言的執行時會在排程或者系統監控中呼叫 sysmon，它會呼叫 netpoll，來不斷地呼叫 epoll_wait 來檢視 epoll 物件所管理的檔案描述符中哪一個有事件就緒需要被處理了。如果有，就喚醒對應的協程來進行執行。

其實除此之外還有幾個地方會喚醒協程，如

startTheWorldWithSema

findrunnable  在 schedule 中呼叫 有top 和 stop 之分。 其中 stop 中會導致阻塞。

pollWork

不過為了簡便起見，我們只選擇 sysmon 來作為一個切入口。sysmon 是一個週期性的監控協程，來看原始碼。

它會不斷觸發對 netpoll 的呼叫，在 netpoll 會呼叫 epollwait 看檢視是否有網路事件發生。

在 epoll 返回的時候，ev.data 中是就緒的網路 socket 的檔案描述符。根據網路就緒 fd 拿到 pollDesc。在 netpollready 中，將對應的協程推入可執行佇列等待排程執行。

本文總結

同步編碼方式的優點是符合人的直線思維。在這種模式下的程式碼很容易寫，寫出來也容易理解，但是缺點就是效能奇差。因為會導致頻繁的執行緒上下文切換。

所以現在 epoll 是 Linux 下網路程式工作的最主要的模式。現在各種語言下的流行的網路框架模型都是基於 epoll 來工作的。區別就是各自對 epoll 的使用方式上存在一些差別。主流各種基於 epoll 的非同步非阻塞的模型雖然提高了效能，但是基於回撥函式的程式設計方式卻非常不符合人的的直線思維模式。開發出來的程式碼的也不那麼容易被人理解。

Golang開闢了一種新的網路程式設計模型。這種模型在應用層看來仍然是同步的方式。但是在底層確實透過協程和 epoll 的配合避免了執行緒切換的效能高損耗，因此並不會阻塞使用者執行緒。代替的是切換開銷更小的協程。協程的切換開銷大約只有執行緒切換的三十分之一，參見 《協程究竟比執行緒牛在什麼地方？》

我個人一直覺得，Golang 封裝的網路程式設計模型非常之精妙，是世界級的程式碼。它非常值得你好好學習一下。學完了覺得好的話，轉發給你的朋友們一起來了解了解吧！

往期相關文章

程序/執行緒切換究竟需要多少開銷？

協程究竟比執行緒牛在什麼地方？

為什麼服務端程式都需要先 listen 一下？

[圖解 | 深入理解高效能網路開發路上的絆腳石 - 同步阻塞網路 IO ]( https://mp.weixin.qq.com/s/cIcw0S-Q8pBl1-WYN0UwnA )

圖解 | 深入揭秘 epoll 是如何實現 IO 多路複用的！

漫畫 | 看程序小 P 講述它的網路效能故事！

寫在最後，由於我的這些知識在公眾號裡文章比較分散，很多人似乎沒有理解到我對知識組織的體系結構。而且圖文也不像影片那樣理解起來更直接。所以我在知識星球上規劃了影片系列課程，包括 硬體原理、記憶體管理、程序管理、檔案系統、網路管理、Golang語言、容器原理、效能觀測、效能最佳化九大部分大約 120 節內容 ，每週更新。加入方式參見 我要開始搞知識星球啦 、 如何才能高效地學習技術,我投“融匯貫通”一票

Github： https://github.com/yanfeizhang/coder-kung-fu 關注公眾號：微信掃描下方二維碼
