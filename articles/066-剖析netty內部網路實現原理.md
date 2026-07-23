# 剖析Netty內部網路實現原理

> 原文標題：剖析Netty内部网络实现原理

> 原文連結：http://kfngxl.cn/index.php/archives/279/

---

大家好，我是飛哥！

我在前面的幾篇文章中分享了 Redis、Nginx 的網路模組內部實現原理。今天我就再帶大家來了解一下 Netty 的網路內部實現。

單執行緒 Redis 如何做到每秒數萬 QPS 的超高處理能力！

Redis 6 中的多執行緒實現方式比我預期的要差

萬字多圖，搞懂 Nginx 高效能網路工作原理！

Netty 是一個在 Java 生態裡應用非常廣泛的的網路程式設計工具包，它在 2004 年誕生到現在依然是火的一塌糊塗，光在 github 上就有 30000 多個專案在用它。所以要想更好地掌握網路程式設計，我想就繞不開 Netty。所以今天我們就來分析分析 Netty 內部網路模組的工作原理。

友情提示，本文算上程式碼將近有兩三萬字，比較長，如果時間緊迫中間部分可以跳著看。第一節和最後的第六節建議必讀。當然直接拖到尾部收藏點贊點轉發，也是 ok 的，哈哈！

一、Netty 用法

我們首先找一個 Netty 的例子，本篇文章整體都是圍繞這個例子來展開敘述的。我們下載 Netty 的原始碼，並在 examples 中找到 echo 這個 demo。同時，為了防止程式碼更新導致對本文敘述的影響，我們切到 4.1 分支上來。

在這個 demo 的 EchoServer 中，展示了使用 Netty 寫 Server 的經典用法。（飛哥在文章中會在不影響核心邏輯的表達上，對原始程式碼盡心適當的精簡，比如下面程式碼中的 try 就被我丟了）

如果你是一個 Java 新手，或者乾脆像飛哥一樣沒用 Netty 寫過服務，相信上述程式碼基本是看不懂的。究其根本原因是相比 C/C++ ，Java 的封裝程度比較高。Java 語言本身的 JVM 中 NIO 對網路的封裝就已經遮蔽了很多底層的概念了，再加上 Netty 又封裝了一層，所以 Java 開發者常用的一些術語和概念和其它語言出入很大。

比如上面程式碼中的 Channel、NioEventLoopGroup 等都是其它語言中所沒見過的。不過你也不用感到害怕，因為這其中的每一個概念都是 socket、程序等底層概念穿了一身不同的衣服而已。接下來我們分別細瞭解一下這些概念。

1.1 NioEventLoopGroup

如果你沒接觸過 Netty，可以簡單把 NioEventLoopGroup 理解為一個執行緒池就可以。每一個 NioEventLoopGroup 內部包含一個或者多個 NioEventLoop。

其中 NioEventLoop 是對執行緒、epoll 等概念進行了一個集中的封裝。

首先，EventLoop 本身就是一個執行緒。為什麼這麼說，我們透過看 NioEventLoop 的繼承關係就能看出來。NioEventLoop 繼承於 SingleThreadEventLoop，而 SingleThreadEventLoop 又繼承於 SingleThreadEventExecutor。SingleThreadEventExecutor 實現了在 Netty 中對本地執行緒的抽象。

在 SingleThreadEventExecutor 中不但封裝了執行緒物件 Thread，而且還配置了一個任務佇列 taskQueue，用於其它執行緒向它來放置待處理的任務。

1.2 selector

另外 NioEventLoopEventLoop 以 selector 的名義封裝了 epoll（在 Linux 作業系統下）。

在 NioEventLoop 物件內部，會有 selector 成員定義。這其實就是封裝的 epoll 而來的。我們來看具體的封裝過程。以及 selectedKeys，這是從 selector 上發現的待處理的事件列表。

NioEventLoopGroup 在構造的時候，會呼叫 SelectorProvider#provider 來生成 provider，在預設情況下會呼叫 sun.nio.ch.DefaultSelectorProvider.create 來建立。

在 Linux 下，預設建立的 provider 使用的就是 epoll。

1.3 Channel

Channel 是 JavaNIO 裡的一個概念。大家把它理解成 socket，以及在 socket 之上的一系列操作方法的封裝就可以了。

Java 在 Channel 中把 connect、bind、read、write 等方法都以成員方法的形式給封裝起來了。

另外在 Java 中，習慣把 listen socket 叫做父 channel，客戶端握手請求到達以後建立出來的新連線叫做子 channel，方便區分。

1.4 Pipeline

在每個 Channel 物件的內部，除了封裝了 socket 以外，還都一個特殊的資料結構 DefaultChannelPipeline pipeline。在這個 pipeline 裡是各種時機裡註冊的 handler。

Channel 上的讀寫操作都會走到這個 DefaultChannelPipeline 中，當 channel 上完成 register、active、read、readComplete 等操作時，會觸發 pipeline 中的相應方法。

這個 ChannelPipeline 其實就是一個雙向連結串列，以及連結串列上的各式各樣的操作方法。

1.5 EchoServer 解讀

現在我們具備了對 Java、對 Netty 的初步理解以後，我們再會後來看一下開篇提到的 EchoServer 原始碼。

在該程式碼一開頭， bossGroup = new NioEventLoopGroup(1) 這一行是建立了一個只有一個執行緒的執行緒池。 workerGroup = new NioEventLoopGroup 又建立了 worker 執行緒池，沒有指定數量，Netty 內部會根據當前機器的 CPU 核數來靈活決定。

ServerBootstrap 這是一個腳手架類，是為了讓我們寫起伺服器程式來更方便一些。

b.group(bossGroup, workerGroup) 這一行是將兩個執行緒池傳入，第一個作為 boss 只處理 accept 接收新的客戶端連線請求。 第二個引數作為 worker 執行緒池，來處理連線上的請求接收、處理以及結果傳送傳送。

我們注意下 childHandler是傳入了一個 ChannelInitializer，這是當有新的客戶端連線到達時會回撥的一個方法。在這個方法內部，我們給這個新的 chaneel 的 pipeline 上新增了一個處理器 serverHandler，以便收到資料的時候執行該處理器進行請求處理。

上面的幾個方法都是定義，在 b.bind 方法中真正開始啟動服務，建立父 channel（listen socket），建立 boss 執行緒。當有新連線到達的時候 boss 執行緒再建立子 channel，為其 pipeline 新增處理器，並啟動 worker 執行緒來進行處理。

二、Netty bootstrap 引數構建

簡言之 bootstrap.group() .channel() .childHandler() .childOption() 就是在構建 Netty Server 的各種引數。

2.1 group 設定

ServerBootstrap 和其父類 AbstractBootstrap 內部分別定義了兩個 EventLoopGroup group 成員。父類 AbstractBootstrap 的 group 是用來處理 accpet 事件的，ServerBootstrap 下的 childGroup 用來處理其它所有的讀寫等事件。

group() 方法就是把 EventLoopGroup 引數設定到自己的成員上完事。其中如果呼叫 group() 只傳入了一個執行緒池，那麼將來本服務下的所有事件都由這個執行緒池來處理。詳情檢視飛哥精簡後的原始碼。

2.2 channel 設定

再看 ServerBootstrap#channel 方法 是用來定義一個工廠方法，將來需要建立 channel 的時候都呼叫該工廠進行建立。

回頭看本文開頭 demo， .channel(NioServerSocketChannel.class) 指的是將來需要建立 channel 的時候，建立 NioServerSocketChannel 這個型別的。

2.3 option 設定

再看 option 方法，只是設定到了 options 成員中而已

2.4 handler 方法

本文 demo 設定了兩處 handler，一處是 handler，另一處是 childHandler。他們都是分別設定到自己的成員上就完事，看原始碼。

三、Netty bootstrap 啟動服務

ServerBootstrap 下的 bind 方法是服務啟動過程中非常重要的一個方法。建立父 channel（listen socket），建立 boss 執行緒，為 boss 執行緒繫結 Acceptor 處理器，呼叫系統呼叫 bind 進行繫結和監聽都是在這裡完成的。

先來直接看一下 bind 相關的入口原始碼。

在這個過程中，做了如下幾件重要的事情

建立父 channel（listen socket）

對父 channel（listen socket）進行初始化

register父 channel（listen socket）到主 group，並啟動主程序

真正的 bind

接下來我們分開來看。

3.1 建立父 channel（listen socket）

在 initAndRegister() 方法中建立 channel（socket），它呼叫了 channelFactory.newChannel()。

回想下 2.2 節的channel 方法，返回的是一個反射 ReflectiveChannelFactory。沒錯這裡的 newChannel 就是呼叫這個工廠方法來建立出來一個 NioServerSocketChannel 物件。

3.2 對父 channel（listen socket）進行初始化

在 initAndRegister 建立除了 channel 之後，需要呼叫 init 對其進行初始化。

在 init() 中對 channel 進行初始化，一是給 options 和 attrs 賦值，二是構建了父 channel 的 pipeline。

在 setChannelOptions 中對 channel 的各種 option 進行設定。回憶我們在使用 ServerBootstrap 時可以傳入 SO_BACKLOG，這就是其中的一個 option。在這裡會真正設定到 channel（socket）上。

在 init 中，稍微難理解一點是 p.addLast(new ChannelInitializer...)。這一段程式碼只是給父 channel 新增一個 handler 而已。其真正的執行要等到 register 後，我們待會再看。

3.3 register 父 channel

父 channel 在建立完，並且初始化之後，需要註冊到 boss 執行緒上才可用。

其中 config().group() 最終會呼叫到 AbstractBootstrap#group，在這個方法裡獲取的是我們傳入進來的 bossGroup。

其中 bossGroup 是一個 NioEventLoopGroup 例項，所以程式碼會進入到 NioEventLoopGroup#register 方法。

在 NioEventLoopGroup 裡包含一個或多個 EventLoop。上面的 next 方法就是從中選擇一個出來，然後將 channel 註冊到其上。

對於本文來講，我們使用的是 NioEventLoopGroup，其內部包含的自然也就是 NioEventLoop，我們繼續查詢其 register 方法。

可見，NioEventLoop 的 register 最後又呼叫到 channel 的 register 上了。在我們本文中，我們建立的 channel 是 NioServerSocketChannel，我們就依照這條線索來查。

在 channel 的父類 AbstractChannel 中的 register 中，先是把自己關聯到傳入的 eventLoop 上。 接著呼叫 inEventLoop 來判斷執行緒當前執行的執行緒是否是 EventExecutor的支撐執行緒，是則返回直接 register0。

一般來說，服務在啟動的時候都是主執行緒在執行。這個時候很可能 boss 執行緒還沒有啟動。所以如果發現當前不是 boss 執行緒的話，就呼叫 eventLoop.execute 來啟動 boss 執行緒。

NioEventLoop 的父類是 SingleThreadEventExecutor, 找到 execute 方法。

我們先來看 addTask(task)，它是將 task 新增到任務佇列中。等待執行緒起來以後再執行。

inEventLoop() 是判斷當前執行緒是不是自己繫結的執行緒，這時還在主執行緒中執行，所以 inEventLoop 為假，會進入 startThread 開始為 EventLoop 建立執行緒。

在 doStartThread 中呼叫 Java 執行緒管理工具 Executor 來啟動 boss 執行緒。

3.4 boss 執行緒啟動

當執行緒起來以後就進入了自己的執行緒迴圈中了，會遍歷自己的任務佇列，然後開始處理自己的任務。

前面我們在 3.3 節看到 eventLoop.execute 把一個 Runnable 任務新增到了任務佇列裡。當 EventLoop 執行緒啟動後，它會遍歷自己的任務佇列並開始處理。這時會進入到 AbstractChannel#register0 方法開始執行。

函式 doRegister 是在 AbstractNioChannel 類下。

上面最關鍵的一句是 selectionKey = javaChannel().register(eventLoop().unwrappedSelector(), 0, this); 。 這一句就相當於在 C 語言下呼叫 epoll_ctl 把 listen socket 新增到了 epoll 物件下。

其中 javaChannel 獲取父 channel，相當於 listen socket。unwrappedSelector 獲取 selector，相當於 epoll 物件。register 相當於使用 epoll_ctl 執行 add 操作。

當 channel 註冊完後，前面 init 時註冊的 ChannelInitializer 回撥就會被執行。再回頭看它的 回撥定義。

在 ChannelInitializer#initChannel 裡，又給 boss 執行緒的 pipeline 裡新增了一個任務。該任務是讓其在自己的 pipeline 上註冊一個 ServerBootstrapAcceptor handler。將來有新連線到達的時候，ServerBootstrapAcceptor 將會被執行。

3.5 真正的 bind

再看 doBind0 方法，呼叫 channel.bind 完成繫結。

四、新連線到達

我們再回到 boss 執行緒的主迴圈中。

假如執行緒任務佇列中的任務都處理乾淨了的情況下，boss 執行緒會呼叫 select 來發現其 selector 上的各種事件。相當於 C 語言中的 epoll_wait。

當發現有事件發生的時候，例如 OP_WRITE、OP_ACCEPT、OP_READ 等的時候，會進入相應的處理

對於服務端的 Unsafe.read()  這裡會執行 io.netty.channel.nio.AbstractNioMessageChannel.NioMessageUnsafe.read() 方法，它會呼叫 JDK 底層的 ServerSocketChannel.accept() 接收到客戶端的連線後，將其封裝成 Netty 的 NioSocketChannel，再透過 Pipeline 將 ChannelRead 事件傳播出去，這樣 ServerBootstrapAcceptor 就可以在 ChannelRead 回撥裡處理新的客戶端連線了。

我們直接看 ServerBootstrapAcceptor#ChannelRead。

在 channelRead 先是獲取到了新建立出來的子 channel，併為其 pipeline 新增 childHandler。回頭看 1.5 節，childHandler 是我們自定義的。

緊接著呼叫 childGroup.register(child) 將子 channel 註冊到 workerGroup 上。這個 register 過程和 3.3節、3.5節過程一樣。區別就是前面是父 channel 註冊到 bossGroup 上，這裡是子 channel 註冊到 workerGroup上。

在 register 完成後，子 channel 被掛到了 workerGroup 其中一個執行緒上，相應的執行緒如果沒有建立也會被建立出來並進入到自己的執行緒迴圈中。

當子 channel 註冊完畢的時候，childHandler 中 ChannelInitializer#initChannel 會被執行

在 initChannel 把子 channel 的處理類 serverHandler 新增上來了。Netty demo 中對這個處理類的定義非常的簡單，僅僅只是列印出來而已。

五、使用者請求到達

當 worker 執行緒起來以後，會進入執行緒迴圈(boss 執行緒和 worker 執行緒的 run 函式是一個)。在迴圈中會遍歷自己的任務佇列，如果沒有任務可處理，便 select 來觀察自己所負責的 channel 上是否有事件發生。

worker 執行緒會呼叫 select 發現自己所管理的所有子 channel 上的可讀可寫事件。在發現有可讀事件後，會呼叫 processSelectedKeys，最後觸發 pipeline 使得 EchoServerHandler 方法開始執行。

六、總結

事實上，Netty 對網路封裝的比較靈活。既支援單執行緒 Reactor，也支援多執行緒 Reactor、還支援主從多執行緒 Reactor。三種模型對應的用法如下：

為了表述的更全面，本文飛哥選擇的是最為經典的 主從多執行緒 Reactor 模式。本文中所描述的內容可以用下面一幅圖來表示。

在 Netty 中的 boss 執行緒中負責對父 channel（listen socket）上事件的監聽和處理，當有新連線到達的時候，選擇一個 worker 執行緒把這個子 channel（連線 socket ）叫給 worker 執行緒來處理。

其中 Worker 執行緒就是等待其管理的所有子 channel（連線 socket）上的事件的監聽和處理。當發現有事件發生的時候，回撥使用者設定的 handler 進行處理。在本文的例子中，這個使用者 handler 就是 EchoServerHandler#channelRead。

至此，Netty 網路模組的工作核心原理咱們就介紹完了。飛哥一直“鼓吹”內功的好處。只要你具備了堅實的內功，各種語言裡看似風牛馬不相及的東西，在底層實際上原理是想通的。我本人從來沒用 Java 開發過伺服器程式，更沒碰過 Netty。但是當你多epoll有了深入理解的時候，再看Netty也能很容易看懂，很快就能理解它的核心。這就是鍛鍊內功的好處！

飛哥期待你的點贊和轉發，感謝！！！

寫在最後，由於我的這些知識在公眾號裡文章比較分散，很多人似乎沒有理解到我對知識組織的體系結構。而且圖文也不像影片那樣理解起來更直接。所以我在知識星球上規劃了影片系列課程，包括 硬體原理、記憶體管理、程序管理、檔案系統、網路管理、Golang語言、容器原理、效能觀測、效能最佳化九大部分大約 120 節內容 ，每週更新。加入方式參見 我要開始搞知識星球啦 、 如何才能高效地學習技術,我投“融匯貫通”一票

Github： https://github.com/yanfeizhang/coder-kung-fu 關注公眾號：微信掃描下方二維碼
