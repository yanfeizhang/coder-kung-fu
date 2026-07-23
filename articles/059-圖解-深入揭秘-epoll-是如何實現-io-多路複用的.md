---
layout: default
title: "圖解 | 深入揭秘 epoll 是如何實現 IO 多路複用的！"
---

# 圖解 | 深入揭秘 epoll 是如何實現 IO 多路複用的！

> 原文標題：图解 | 深入揭秘 epoll 是如何实现 IO 多路复用的！

> 原文連結：http://kfngxl.cn/index.php/archives/235/

---

程序在 Linux 上是一個開銷不小的傢伙，先不說建立，光是上下文切換一次就得幾個微秒。所以為了高效地對海量使用者提供服務，必須要讓一個程序能同時處理很多個 tcp 連線才行。現在假設一個程序保持了 10000 條連線，那麼如何發現哪條連線上有資料可讀了、哪條連線可寫了 ？

我們當然可以採用迴圈遍歷的方式來發現 IO 事件，但這種方式太低階了。我們希望有一種更高效的機制，在很多連線中的某條上有 IO 事件發生的時候直接快速把它找出來。其實這個事情 Linux 作業系統已經替我們都做好了，它就是我們所熟知的 IO 多路複用 機制。 這裡的複用指的就是對程序的複用。

在 Linux 上多路複用方案有 select、poll、epoll。 它們三個中 epoll 的效能表現是最優秀的，能支援的併發量也最大。所以我們今天把 epoll 作為要拆解的物件，深入揭秘核心是如何實現多路的 IO 管理的。

為了方便討論，我們舉一個使用了 epoll 的簡單示例（只是個例子，實踐中不這麼寫）：

其中和 epoll 相關的函式是如下三個：

epoll_create：建立一個 epoll 物件

epoll_ctl：向 epoll 物件中新增要管理的連線

epoll_wait：等待其管理的連線上的 IO 事件

藉助這個 demo，我們來展開對 epoll 原理的深度拆解。相信等你理解了這篇文章以後，你對 epoll 的駕馭能力將變得爐火純青！！

友情提示，萬字長文，慎入！！

一、accept 建立新 socket

我們直接從伺服器端的 accept 講起。當 accept 之後，程序會建立一個新的 socket 出來，專門用於和對應的客戶端通訊，然後把它放到當前程序的開啟檔案列表中。

其中一條連線的 socket 核心物件更為具體一點的結構圖如下。

接下來我們來看一下接收連線時 socket 核心物件的建立原始碼。accept 的系統呼叫程式碼位於原始檔 net/socket.c 下。

1.1 初始化 struct socket 物件

在上述的原始碼中，首先是呼叫 sock_alloc 申請一個 struct socket 物件出來。然後接著把 listen 狀態的 socket 物件上的協議操作函式集合 ops 賦值給新的 socket。（對於所有的 AF_INET 協議族下的 socket 來說，它們的 ops 方法都是一樣的，所以這裡可以直接複製過來）

其中 inet_stream_ops 的定義如下

1.2 為新 socket 物件申請 file

struct socket 物件中有一個重要的成員 -- file 核心物件指標。這個指標初始化的時候是空的。 在 accept 方法裡會呼叫 sock_alloc_file 來申請記憶體並初始化。 然後將新 file 物件設定到 sock->file 上。

來看 sock_alloc_file 的實現過程：

sock_alloc_file 又會接著呼叫到 alloc_file。注意在 alloc_file 方法中，把 socket_file_ops 函式集合一併賦到了新 file->f_op 裡了。

socket_file_ops 的具體定義如下：

這裡看到，在accept裡建立的新 socket 裡的 file->f_op->poll 函式指向的是 sock_poll。 接下來我們會呼叫到它，後面我們再說。

1.3 接收連線

在 socket 核心物件中除了 file 物件指標以外，有一個核心成員 sock。

這個 struct sock 資料結構非常大，是 socket 的核心核心物件。傳送佇列、接收佇列、等待佇列等核心資料結構都位於此。其定義位置檔案 include/net/sock.h，由於太長就不展示了。

在 accept 的原始碼中：

sock->ops->accept 對應的方法是 inet_accept。它執行的時候會從握手佇列裡直接獲取建立好的 sock。 sock 物件的完整建立過程涉及到三次握手，比較複雜，不展開了說了。咱們只看 struct sock 初始化過程中用到的一個函式：

在這裡把 sock 物件的 sk_data_ready 函式指標設定為 sock_def_readable 。這個這裡先記住就行了，後面會用到。

1.4 新增新檔案到當前程序的開啟檔案列表中

當 file、socket、sock 等關鍵核心物件建立完畢以後，剩下要做的一件事情就是把它掛到當前程序的開啟檔案列表中就行了。

二、epoll_create 實現

在使用者程序呼叫 epoll_create 時，核心會建立一個 struct eventpoll 的核心物件。並同樣把它關聯到當前程序的已開啟檔案列表中。

對於 struct eventpoll 物件，更詳細的結構如下（同樣只列出和今天主題相關的成員）。

epoll_create 的原始碼相對比較簡單。在 fs/eventpoll.c 下

struct eventpoll 的定義也在這個原始檔中。

eventpoll 這個結構體中的幾個成員的含義如下：

wq： 等待佇列連結串列。軟中斷資料就緒的時候會透過 wq 來找到阻塞在 epoll 物件上的使用者程序。

rbr： 一棵紅黑樹。為了支援對海量連線的高效查詢、插入和刪除，eventpoll 內部使用了一棵紅黑樹。透過這棵樹來管理使用者程序下新增進來的所有 socket 連線。

rdllist： 就緒的描述符的連結串列。當有的連線就緒的時候，核心會把就緒的連線放到 rdllist 連結串列裡。這樣應用程序只需要判斷連結串列就能找出就緒連線，而不用去遍歷整棵樹。

當然這個結構被申請完之後，需要做一點點的初始化工作，這都在 ep_alloc 中完成。

說到這兒，這些成員其實只是剛被定義或初始化了，還都沒有被使用。它們會在下面被用到。

三、epoll_ctl 新增 socket

理解這一步是理解整個 epoll 的關鍵 。

為了簡單，我們只考慮使用 EPOLL_CTL_ADD 新增 socket，先忽略刪除和更新。

假設我們現在和客戶端們的多個連線的 socket 都建立好了，也建立好了 epoll 核心物件。在使用 epoll_ctl 註冊每一個 socket 的時候，核心會做如下三件事情

1.分配一個紅黑樹節點物件 epitem，

2.新增等待事件到 socket 的等待佇列中，其回撥函式是 ep_poll_callback

3.將 epitem 插入到 epoll 物件的紅黑樹裡

透過 epoll_ctl 新增兩個 socket 以後，這些核心資料結構最終在程序中的關係圖大致如下：

我們來詳細看看 socket 是如何新增到 epoll 物件裡的，找到 epoll_ctl 的原始碼。

在 epoll_ctl 中首先根據傳入 fd 找到 eventpoll、socket相關的核心物件 。對於 EPOLL_CTL_ADD 操作來說，會然後執行到 ep_insert 函式。所有的註冊都是在這個函式中完成的。

3.1 分配並初始化 epitem

對於每一個 socket，呼叫 epoll_ctl 的時候，都會為之分配一個 epitem。 該結構的主要資料如下：

對 epitem 進行了一些初始化，首先在 epi->ep = ep 這行程式碼中將其 ep 指標指向 eventpoll 物件。另外用要新增的 socket 的 file、fd 來填充 epitem->ffd。

其中使用到的 ep_set_ffd 函式如下。

3.2 設定 socket 等待佇列

在建立 epitem 並初始化之後，ep_insert 中第二件事情就是設定 socket 物件上的等待任務佇列。並把函式 fs/eventpoll.c 檔案下的 ep_poll_callback 設定為資料就緒時候的回撥函式。

這一塊的原始碼稍微有點繞，沒有耐心的話直接跳到下面的加粗字型來看。首先來看 ep_item_poll。

看，這裡呼叫到了 socket 下的 file->f_op->poll。透過上面第一節的 socket 的結構圖，我們知道這個函式實際上是 sock_poll。

同樣回看第一節裡的 socket 的結構圖，sock->ops->poll 其實指向的是 tcp_poll。

在 sock_poll_wait 的第二個引數傳參前，先呼叫了 sk_sleep 函式。 在這個函式里它獲取了 sock 物件下的等待佇列列表頭 wait_queue_head_t，待會等待佇列項就插入這裡 。這裡稍微注意下，是 socket 的等待佇列，不是 epoll 物件的。來看 sk_sleep 原始碼：

接著真正進入 sock_poll_wait。

這裡的 qproc 是個函式指標，它在前面的 init_poll_funcptr 呼叫時被設定成了 ep_ptable_queue_proc 函式。

敲黑板！！！注意，廢了半天的勁，終於到了重點了！ 在 ep_ptable_queue_proc 函式中，新建了一個等待佇列項，並註冊其回撥函式為 ep_poll_callback 函式。然後再將這個等待項新增到 socket 的等待佇列中 。

在前文 [深入理解高效能網路開發路上的絆腳石 - 同步阻塞網路 IO]( https://mp.weixin.qq.com/s/cIcw0S-Q8pBl1-WYN0UwnA ) 裡阻塞式的系統呼叫 recvfrom 裡，由於需要在資料就緒的時候喚醒使用者程序，所以等待物件項的 private (這個變數名起的也是醉了) 會設定成當前使用者程序描述符 current。 而我們今天的 socket 是交給 epoll 來管理的，不需要在一個 socket 就緒的時候就喚醒程序，所以這裡的 q->private 沒有啥卵用就設定成了 NULL。

如上，等待佇列項中僅僅只設定了回撥函式 q->func 為 ep_poll_callback。在後面的第 5 節資料來啦中我們將看到，軟中斷將資料收到 socket 的接收佇列後，會透過註冊的這個 ep_poll_callback 函式來回撥，進而通知到 epoll 物件。

3.3 插入紅黑樹

分配完 epitem 物件後，緊接著並把它插入到紅黑樹中。一個插入了一些 socket 描述符的 epoll 裡的紅黑樹的示意圖如下：

這裡我們再聊聊為啥要用紅黑樹，很多人說是因為效率高。其實我覺得這個解釋不夠全面，要說查詢效率樹哪能比的上 HASHTABLE。我個人認為覺得更為合理的一個解釋是為了讓 epoll 在查詢效率、插入效率、記憶體開銷等等多個方面比較均衡，最後發現最適合這個需求的資料結構是紅黑樹。

四、epoll_wait 等待接收

epoll_wait 做的事情不復雜，當它被呼叫時它觀察 eventpoll->rdllist 連結串列裡有沒有資料即可。有資料就返回，沒有資料就建立一個等待佇列項，將其新增到 eventpoll 的等待佇列上，然後把自己阻塞掉就完事。

其原始碼如下：

4.1 判斷就緒佇列上有沒有事件就緒

首先呼叫 ep_events_available 來判斷就緒連結串列中是否有可處理的事件。

4.2 定義等待事件並關聯當前程序

假設確實沒有就緒的連線，那接著會進入 init_waitqueue_entry 中定義等待任務，並把 current （當前程序）新增到 waitqueue 上。

注意這裡的回撥函式名稱是 default_wake_function。 後續在第 5 節資料來啦時將會呼叫到該函式。

4.3 新增到等待佇列

在這裡，把上一小節定義的等待事件新增到了 epoll 物件的等待佇列中。

4.4 讓出CPU 主動進入睡眠狀態

透過 set_current_state 把當前程序設定為可打斷。 呼叫 schedule_hrtimeout_range 讓出 CPU，主動進入睡眠狀態

在 schedule 中選擇下一個程序排程

五、資料來啦

在前面 epoll_ctl 執行的時候，核心為每一個 socket 上都新增了一個等待佇列項。 在 epoll_wait 執行完的時候，又在 event poll 物件上新增了等待佇列元素。在討論資料開始接收之前，我們把這些佇列項的內容再稍微總結一下。

socket->sock->sk_data_ready 設定的就緒處理函式是 sock_def_readable

在 socket 的等待佇列項中，其回撥函式是 ep_poll_callback。另外其 private 沒有用了，指向的是空指標 null。

在 eventpoll 的等待佇列項中，回撥函式是 default_wake_function。其 private 指向的是等待該事件的使用者程序。

在這一小節裡，我們將看到軟中斷是怎麼樣在資料處理完之後依次進入各個回撥函式，最後通知到使用者程序的。

5.1 接收資料到任務佇列

關於軟中斷是怎麼處理網路幀，為了避免篇幅過於臃腫，這裡不再介紹。感興趣的可以看文章 《圖解Linux網路包接收過程》 。 我們今天直接從 tcp 協議棧的處理入口函式 tcp_v4_rcv 開始說起。

在 tcp_v4_rcv 中首先根據收到的網路包的 header 裡的 source 和 dest 資訊來在本機上查詢對應的 socket。找到以後，我們直接進入接收的主體函式 tcp_v4_do_rcv 來看。

我們假設處理的是 ESTABLISH 狀態下的包，這樣就又進入 tcp_rcv_established 函式中進行處理。

在 tcp_rcv_established 中透過呼叫  tcp_queue_rcv 函式中完成了將接收資料放到 socket 的接收佇列上。

如下原始碼所示

5.2 查詢就緒回撥函式

呼叫 tcp_queue_rcv 接收完成之後，接著再呼叫 sk_data_ready 來喚醒在 socket上等待的使用者程序。 這又是一個函式指標。 回想上面第一節我們在 accept 函式建立 socket 流程裡提到的 sock_init_data 函式，在這個函式里已經把 sk_data_ready 設定成 sock_def_readable 函式了。它是預設的資料就緒處理函式。

當 socket 上資料就緒時候，核心將以 sock_def_readable 這個函式為入口，找到 epoll_ctl 新增 socket 時在其上設定的回撥函式 ep_poll_callback。

我們來詳細看下細節：

這裡的函式名其實都有迷惑人的地方。

wq_has_sleeper，對於簡單的 recvfrom 系統呼叫來說，確實是判斷是否有程序阻塞。但是對於 epoll 下的 socket 只是判斷等待佇列不為空，不一定有程序阻塞的。

wake_up_interruptible_sync_poll，只是會進入到 socket 等待佇列項上設定的回撥函式，並不一定有喚醒程序的操作。

那接下來就是我們重點看 wake_up_interruptible_sync_poll 。

我們看一下核心是怎麼找到等待佇列項裡註冊的回撥函式的。

接著進入 \_\_wake_up_common

在 \_\_wake_up_common 中，選出等待佇列裡註冊某個元素 curr， 回撥其 curr->func。 回憶我們 ep_insert 呼叫的時候，把這個 func 設定成 ep_poll_callback 了。

5.3 執行 socket 就緒回撥函式

在上一小節找到了 socket 等待佇列項裡註冊的函式 ep_poll_callback，軟中斷接著就會呼叫它。

在 ep_poll_callback 根據等待任務佇列項上的額外的 base 指標可以找到 epitem， 進而也可以找到 eventpoll物件。

首先它做的第一件事就是 把自己的 epitem 新增到 epoll 的就緒佇列中 。

接著它又會檢視 eventpoll 物件上的等待佇列裡是否有等待項（epoll_wait 執行的時候會設定）。

如果沒執行軟中斷的事情就做完了。如果有等待項，那就查詢到等待項裡設定的回撥函式。

呼叫 wake_up_locked() => \_\_wake_up_locked() => \_\_wake_up_common。

在 \_\_wake_up_common裡， 呼叫 curr->func。 這裡的 func 是在 epoll_wait 是傳入的 default_wake_function 函式。

5.4 執行 epoll 就緒通知

在default_wake_function 中找到等待佇列項裡的程序描述符，然後喚醒之。

原始碼如下：

等待佇列項 curr->private 指標是在 epoll 物件上等待而被阻塞掉的程序。

將epoll_wait程序推入可執行佇列，等待核心重新排程程序。然後epoll_wait對應的這個程序重新執行後，就從 schedule 恢復

當程序醒來後，繼續從 epoll_wait 時暫停的程式碼繼續執行。把 rdlist 中就緒的事件返回給使用者程序

從使用者角度來看，epoll_wait 只是多等了一會兒而已，但執行流程還是順序的。

總結

我們來用一幅圖總結一下 epoll 的整個工作路程。

其中軟中斷回撥的時候回撥函式也整理一下： sock_def_readable： sock 物件初始化時設定的

總結下，epoll 相關的函式里核心執行環境分兩部分：

使用者程序核心態。進行呼叫 epoll_wait  等函式時會將程序陷入核心態來執行。這部分程式碼負責檢視接收佇列，以及負責把當前程序阻塞掉，讓出 CPU。

硬軟中斷上下文。在這些元件中，將包從網絡卡接收過來進行處理，然後放到 socket 的接收佇列。對於 epoll 來說，再找到 socket 關聯的 epitem，並把它新增到 epoll 物件的就緒連結串列中。 這個時候再捎帶檢查一下 epoll 上是否有被阻塞的程序，如果有喚醒之。

為了介紹到每個細節，本文涉及到的流程比較多，把阻塞都介紹進來了。

但其實 在實踐中，只要活兒足夠的多，epoll_wait 根本都不會讓程序阻塞 。使用者程序會一直幹活，一直幹活，直到 epoll_wait 裡實在沒活兒可乾的時候才主動讓出 CPU。這就是 epoll 高效的地方所在！

包括本文在內，飛哥總共用三篇文章分析了一件事情，一個網路包是如何從網絡卡達到你的使用者程序裡的。另外兩篇如下：

圖解 | Linux 網路包接收過程

圖解 | 深入理解高效能網路開發路上的絆腳石 - 同步阻塞網路 IO

恭喜你沒被核心原始碼勸退，一直能堅持到了現在。趕快給先自己鼓個掌，晚飯去加個雞腿！

當然網路程式設計剩下還有一些概念我們沒有講到，比如 Reactor 和 Proactor 等。不過相對核心來講，這些使用者層的技術相對就很簡單了。這些只是在討論當多程序一起配合工作時誰負責檢視 IO 事件、誰該負責計算、誰負責傳送和接收，僅僅是使用者程序的不同分工模式罷了。

最後，求轉發！！

PS：由於我的這些知識在公眾號裡文章比較分散，很多人似乎沒有理解到我對知識組織的體系結構。而且圖文也不像影片那樣理解起來更直接。所以我在知識星球上規劃了影片系列課程，包括 硬體原理、記憶體管理、程序管理、檔案系統、網路管理、Golang語言、容器原理、效能觀測、效能最佳化九大部分大約 120 節內容 ，每週更新。加入方式參見 我要開始搞知識星球啦 、 如何才能高效地學習技術,我投“融匯貫通”一票

Github： https://github.com/yanfeizhang/coder-kung-fu 關注公眾號：微信掃描下方二維碼
