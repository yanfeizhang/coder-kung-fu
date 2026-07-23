---
layout: default
title: "單執行緒 Redis 如何做到每秒數萬 QPS 的超高處理能力！"
---

# 單執行緒 Redis 如何做到每秒數萬 QPS 的超高處理能力！

> 原文標題：单线程 Redis 如何做到每秒数万 QPS 的超高处理能力！

> 原文連結：http://kfngxl.cn/index.php/archives/266/

---

大家好，我是飛哥！

今天開篇先給大家講個飛哥自己的小故事。我在學校和剛畢業頭一年主要從事的客戶端開發，那時候對伺服器端程式設計還不擅長。

有一次去面試伺服器端崗位，面試官問我有一個連線過來，你該怎麼程式設計處理它。我答道：“主執行緒收到請求後，建立一個子執行緒處理。” 面試官接著問，那如果有一千個連線同時來呢？ 我說“那就多建立一點執行緒，搞個執行緒池”。面試官繼續追問如果一萬個呢？我答道:“......”。

事實上，伺服器端只需要單執行緒可以達到非常高的處理能力，Redis 就是一個非常好的例子。僅僅靠單執行緒就可以支撐起每秒數萬 QPS 的高處理能力。今天我們就來帶大家看看 Redis 核心網路模組的內部實現，學習下 Redis 是如何做到如此的高效能的！

### 一、理解多路複用原理

在開始介紹 Redis 之前，我想有必要先來簡單介紹下 epoll。

在傳統的同步阻塞網路程式設計模型裡（沒有協程以前），效能上不來的根本原因在於程序執行緒都是笨重的傢伙。讓一個進(線)程只處理一個使用者請求確確實實是有點浪費了。

先拋開高記憶體開銷不說，在海量的網路請求到來的時候，光是頻繁的程序執行緒上下文就讓 CPU 疲於奔命了。

如果把程序比作牧羊人，一個進(線)程同時只能處理一個使用者請求，相當於一個人只能看一隻羊，放完這一隻才能放下一隻。如果同時來了 1000 只羊，那就得 1000 個人去放，這人力成本是非常高的。

效能提升思路很簡單，就是讓很多的使用者連線來複用同一個進(線)程，這就是多路複用。多路指的是許許多多個使用者的網路連線。複用指的是對進(線)程的複用。換到牧羊人的例子裡，就是一群羊只要一個牧羊人來處理就行了。

不過複用實現起來是需要特殊的 socket 事件管理機制的，最典型和高效的方案就是 epoll。放到牧羊人的例子來，epoll 就相當於一隻牧羊犬。

在 epoll 的系列函式里， epoll_create 用於建立一個 epoll 物件，epoll_ctl 用來給 epoll 物件新增或者刪除一個 socket。 epoll_wait 就是檢視它當前管理的這些 socket 上有沒有可讀可寫事件發生。

當網絡卡上收到資料包後，Linux 核心進行一系列的處理後把資料放到 socket 的接收佇列。然後會檢查是否有 epoll 在管理它，如果是則在 epoll 的就緒佇列中插入一個元素。epoll_wait 的操作就非常的簡單了，就是到 epoll 的就緒佇列上來查詢有沒有事件發生就行了。關於 epoll 這隻“牧羊犬”的工作原理參見[深入揭秘 epoll 是如何實現 IO 多路複用的](https://mp.weixin.qq.com/s/OmRdUgO1guMX76EdZn11UQ)(Javaer 習慣把基於 epoll 的網路開發模型叫做 NIO)

在基於 epoll 的程式設計中，和傳統的函式呼叫思路不同的是，我們並不能主動呼叫某個 API 來處理。因為無法知道我們想要處理的事件啥時候發生。所以只好提前把想要處理的事件的處理函式註冊到一個事件分發器上去。當事件發生的時候，由這個事件分發器呼叫回撥函式進行處理。這類基於實現註冊事件分發器的開發模式也叫 Reactor 模型。

### 二、Redis 服務啟動初始化

理解了 epoll 原理後，我們再來實際看 Redis 具體是如何使用 epoll 的。直接在 Github 上就可以非常方便地獲取 Redis 的原始碼。我們切到 5.0.0 版本來看單執行緒版本的實現（多執行緒我們改天再講）。

```
# git clone https://github.com/redis/redis
# cd redis
# git checkout -b 5.0.0 5.0.0
```

其中整個 Redis 服務的程式碼總入口在 src/server.c 檔案中，我把入口函式的核心部分摘了出來，如下。

```
//file:src/server.c
int main(int argc, char **argv) {
    ......
    // 啟動初始化
    initServer();
    // 執行事件處理迴圈，一直到伺服器關閉為止
    aeMain(server.el);
}
```

其實整個 Redis 的工作過程，就只需要理解清楚 main 函式中呼叫的 initServer 和 aeMain 這兩個函式就足夠了。

本節中我們重點介紹 initServer，在下一節介紹事件處理迴圈 aeMain。在 initServer 這個函式內，Redis 做了這麼三件重要的事情。

- 建立一個 epoll 物件

- 對配置的監聽埠進行 listen

- 把 listen socket 讓 epoll 給管理起來

```
//file:src/server.c
void initServer() {
    // 2.1.1 建立 epoll
    server.el = aeCreateEventLoop(server.maxclients+CONFIG_FDSET_INCR);

    // 2.1.2 繫結監聽服務埠
    listenToPort(server.port,server.ipfd,&server.ipfd_count);

    // 2.1.3 註冊 accept 事件處理器
    for (j = 0; j < server.ipfd_count; j++) {
        aeCreateFileEvent(server.el, server.ipfd[j], AE_READABLE,
            acceptTcpHandler,NULL);
    }
    ...
}
```

接下來我們分別來看。

#### 2.1 建立 epoll 物件

本小節的邏輯看起來貌似不短，但其實只是建立了一個 epoll 物件出來而已。

建立 epoll 物件的邏輯在 aeCreateEventLoop 中，在建立完後，Redis 將其儲存在 redisServer 的 aeEventLoop 成員中，以備後續使用。

```
//file:src/server.h
struct redisServer {
    ...
    aeEventLoop *el;
}
```

我們來看 aeCreateEventLoop 詳細邏輯。Redis 在作業系統提供的 epoll 物件基礎上又封裝了一個 eventLoop 出來，所以建立的時候是先申請和建立 eventLoop。

```
//file:src/ae.c
aeEventLoop *aeCreateEventLoop(int setsize) {
    aeEventLoop *eventLoop;
    eventLoop = zmalloc(sizeof(*eventLoop);

    //將來的各種回撥事件就都會存在這裡
    eventLoop->events = zmalloc(sizeof(aeFileEvent)*setsize);
    ......

    aeApiCreate(eventLoop);
    return eventLoop;
}
```

在 eventLoop 裡，我們稍微注意一下 eventLoop->events，將來在各種事件註冊的時候都會儲存到這個陣列裡。

```
//file:src/ae.h
typedef struct aeEventLoop {
    ......
    aeFileEvent *events; /* Registered events */
}
```

具體建立 epoll 的過程在 ae_epoll.c 檔案下的 aeApiCreate 中。在這裡，真正呼叫了 epoll_create

```
//file:src/ae_epoll.c
static int aeApiCreate(aeEventLoop *eventLoop) {
    aeApiState *state = zmalloc(sizeof(aeApiState));
    state->epfd = epoll_create(1024);
    eventLoop->apidata = state;
    return 0;
}
```

#### 2.2 繫結監聽服務埠

我們再來看 Redis 中的 listen 過程，它在 listenToPort 函式中。雖然呼叫鏈條很長，但其實主要就是執行了個簡單 listen 而已。

```
//file:src/redis.c
int listenToPort(int port, int *fds, int *count) {
    for (j = 0; j < server.bindaddr_count || j == 0; j++) {
        fds[*count] = anetTcpServer(server.neterr,port,NULL,
                server.tcp_backlog);
    }
}
```

Redis 是支援開啟多個埠的，所以在 listenToPort 中我們看到是啟用一個迴圈來呼叫 anetTcpServer。在 anetTcpServer 中，逐步會展開呼叫，直到執行到 bind 和 listen 系統呼叫。

```
//file:src/anet.c
int anetTcpServer(char *err, int port, char *bindaddr, int backlog)
{
    return _anetTcpServer(err, port, bindaddr, AF_INET, backlog);
}
static int _anetTcpServer(......)
{
    // 設定埠重用
    anetSetReuseAddr(err,s)
    // 監聽
    anetListen(err,s,p->ai_addr,p->ai_addrlen,backlog)
}
static int anetListen(......) {
    bind(s,sa,len);
    listen(s, backlog);
    ......
}
```

#### 2.3 註冊事件回撥函式

我們回頭再看一下 initServer，它呼叫 aeCreateEventLoop 建立了 epoll，呼叫 listenToPort 進行了服務埠的 bind 和 listen。接著就開始呼叫 aeCreateFileEvent 來註冊一個 accept 事件處理器。

```
//file:src/server.c
void initServer() {
    // 2.1.1 建立 epoll
    server.el = aeCreateEventLoop(server.maxclients+CONFIG_FDSET_INCR);

    // 2.1.2 監聽服務埠
    listenToPort(server.port,server.ipfd,&server.ipfd_count);

    // 2.1.3 註冊 accept 事件處理器
    for (j = 0; j < server.ipfd_count; j++) {
        aeCreateFileEvent(server.el, server.ipfd[j], AE_READABLE,
            acceptTcpHandler,NULL);
    }
    ...
}
```

我們來注意看呼叫 aeCreateFileEvent 時傳的重要引數是 acceptTcpHandler，它表示將來在 listen socket 上有新使用者連線到達的時候，該函式將被呼叫執行。我們來看 aeCreateFileEvent 具體程式碼。

```
//file:src/ae.c
int aeCreateFileEvent(aeEventLoop *eventLoop, int fd, int mask,
        aeFileProc *proc, void *clientData)
{
    // 取出一個檔案事件結構
    aeFileEvent *fe = &eventLoop->events[fd];

    // 監聽指定 fd 的指定事件
    aeApiAddEvent(eventLoop, fd, mask);

    // 設定檔案事件型別，以及事件的處理器
    fe->mask |= mask;
    if (mask & AE_READABLE) fe->rfileProc = proc;
    if (mask & AE_WRITABLE) fe->wfileProc = proc;

    // 私有資料
    fe->clientData = clientData;
}
```

函式 aeCreateFileEvent 一開始，從 eventLoop->events 獲取了一個 aeFileEvent 物件。
在 2.1 中我們介紹過 eventLoop->events 陣列，註冊的各種事件處理器會儲存在這個地方。

接下來呼叫 aeApiAddEvent。這個函式其實就是對 epoll_ctl 的一個封裝。主要就是實際執行 epoll_ctl EPOLL_CTL_ADD。

```
//file:src/ae_epoll.c
static int aeApiAddEvent(aeEventLoop *eventLoop, int fd, int mask) {
    // add or mod
    int op = eventLoop->events[fd].mask == AE_NONE ?
            EPOLL_CTL_ADD : EPOLL_CTL_MOD;
    ......

    // epoll_ctl 新增事件
    epoll_ctl(state->epfd,op,fd,&ee);
    return 0;
}
```

每一個 eventLoop->events 元素都指向一個 aeFileEvent 物件。在這個物件上，設定了三個關鍵東西

- rfileProc：讀事件回撥

- wfileProc：寫事件回撥

- clientData：一些額外的擴充套件資料

將來 當 epoll_wait 發現某個 fd 上有事件發生的時候，這樣 redis 首先根據 fd 到 eventLoop->events 中查詢 aeFileEvent 物件，然後再看 rfileProc、wfileProc 就可以找到讀、寫回撥處理函式。

回頭看 initServer 呼叫 aeCreateFileEvent 時傳參來看。

```
//file:src/server.c
void initServer() {
    ......

    // 2.1.3 註冊 accept 事件處理器
    for (j = 0; j < server.ipfd_count; j++) {
        aeCreateFileEvent(server.el, server.ipfd[j], AE_READABLE,
            acceptTcpHandler,NULL);
    }
}
```

listen fd 對應的讀回撥函式 rfileProc 事實上就被設定成了 acceptTcpHandler，寫回撥沒有設定，私有資料 client_data 也為 null。

### 三、Redis 事件處理迴圈

在上一節介紹完了 Redis 的啟動初始化過程，建立了 epoll，也進行了繫結監聽，也註冊了 accept 事件處理函式為 acceptTcpHandler。

```
//file:src/server.c
int main(int argc, char **argv) {
    ......

    // 啟動初始化
    initServer();

    // 執行事件處理迴圈，一直到伺服器關閉為止
    aeMain(server.el);
}
```

接下來，Redis 就會進入 aeMain 開始進行真正的使用者請求處理了。在 aeMain 函式中，是一個無休止的迴圈。在每一次的迴圈中，要做如下幾件事情。

- 透過 epoll_wait 發現 listen socket 以及其它連線上的可讀、可寫事件

- 若發現 listen socket 上有新連線到達，則接收新連線，並追加到 epoll 中進行管理

- 若發現其它 socket 上有命令請求到達，則讀取和處理命令，把命令結果寫到快取中，加入寫任務佇列

- 每一次進入 epoll_wait 前都呼叫 beforesleep 來將寫任務佇列中的資料實際進行傳送

- 如若有首次未傳送完畢的，當寫事件發生時繼續傳送

```
//file:src/ae.c
void aeMain(aeEventLoop *eventLoop) {

    eventLoop->stop = 0;
    while (!eventLoop->stop) {

        // 如果有需要在事件處理前執行的函式，那麼執行它
        // 3.4 beforesleep 處理寫任務佇列並實際傳送之
        if (eventLoop->beforesleep != NULL)
            eventLoop->beforesleep(eventLoop);

        // 開始等待事件並處理
        // 3.1 epoll_wait 發現事件
        // 3.2 處理新連線請求
        // 3.3 處理客戶連線上的可讀事件
        aeProcessEvents(eventLoop, AE_ALL_EVENTS);
    }
}
```

以上就是 aeMain 函式的核心邏輯所在，接下來我們分別對如上提到的幾件事情進行詳細的闡述。

#### 3.1 epoll_wait 發現事件

Redis 不管有多少個使用者連線，都是透過 epoll_wait 來統一發現和管理其上的可讀（包括 liisten socket 上的 accept事件）、可寫事件的。甚至連 timer，也都是交給 epoll_wait 來統一管理的。

每當 epoll_wait 發現特定的事件發生的時候，就會呼叫相應的事先註冊好的事件處理函式進行處理。我們來詳細看 aeProcessEvents 對 epoll_wait 的封裝。

```
//file:src/ae.c
int aeProcessEvents(aeEventLoop *eventLoop, int flags)
{
    // 獲取最近的時間事件
    tvp = xxx

    // 處理檔案事件，阻塞時間由 tvp 決定
    numevents = aeApiPoll(eventLoop, tvp);
    for (j = 0; j < numevents; j++) {
        // 從已就緒陣列中獲取事件
        aeFileEvent *fe = &eventLoop->events[eventLoop->fired[j].fd];

        //如果是讀事件，並且有讀回撥函式
        fe->rfileProc()

        //如果是寫事件，並且有寫回撥函式
        fe->wfileProc()
    }
}

//file:src/ae_epoll.c
static int aeApiPoll(aeEventLoop *eventLoop, struct timeval *tvp) {
    // 等待事件
    aeApiState *state = eventLoop->apidata;
    epoll_wait(state->epfd,state->events,eventLoop->setsize,
            tvp ? (tvp->tv_sec*1000 + tvp->tv_usec/1000) : -1);
    ...
}
```

aeProcessEvents 就是呼叫 epoll_wait 來發現事件。當發現有某個 fd 上事件發生以後，則調為其事先註冊的事件處理器函式 rfileProc 和 wfileProc。

#### 3.2 處理新連線請求

我們假設現在有新使用者連線到達了。前面在我們看到 listen socket 上的 rfileProc 註冊的是 acceptTcpHandler。也就是說，如果有連線到達的時候，會回撥到 acceptTcpHandler。

在 acceptTcpHandler 中，主要做了幾件事情

- 呼叫 accept 系統呼叫把使用者連線給接收回來

- 為這個新連線建立一個唯一 redisClient 物件

- 將這個新連線新增到 epoll，並註冊一個讀事件處理函式

接下來讓我們看上面這三件事情都分別是如何被處理的。

```
//file:src/networking.c
void acceptTcpHandler(aeEventLoop *el, int fd, ...) {
    cfd = anetTcpAccept(server.neterr, fd, cip, ...);
    acceptCommonHandler(cfd,0);
}
```

在 anetTcpAccept 中執行非常的簡單，就是呼叫 accept 把連線接收回來。

```
//file:src/anet.c
int anetTcpAccept(......) {
    anetGenericAccept(err,s,(struct sockaddr*)&sa,&salen)
}
static int anetGenericAccept(......) {
    fd = accept(s,sa,len)
}
```

接下來在 acceptCommonHandler 為這個新的客戶端連線 socket，建立一個 redisClient 物件。

```
//file:src/networking.c
static void acceptCommonHandler(int fd, int flags) {
    // 建立 redisClient 物件
    redisClient *c;
    c = createClient(fd);
    ......
}
```

在 createClient 中，建立 client 物件，並且為該使用者連線註冊了讀事件處理器。

```
//file:src/networking.c
redisClient *createClient(int fd) {

    // 為使用者連線建立 client 物件
    redisClient *c = zmalloc(sizeof(redisClient));

    if (fd != -1) {
        ...

        // 為使用者連線註冊讀事件處理器
        aeCreateFileEvent(server.el,fd,AE_READABLE,
            readQueryFromClient, c)
    }
    ...
}
```

關於 aeCreateFileEvent 的處理過程這裡就不贅述了，詳情參見 2.3 節。其效果就是將該使用者連線 socket fd 對應的讀處理函式設定為 readQueryFromClient, 並且設定私有資料為 redisClient c。

#### 3.3 處理客戶連線上的可讀事件

現在假設該使用者連線有命令到達了，就假設使用者傳送了`GET XXXXXX_KEY`命令。那麼在 Redis 的時間迴圈中呼叫 epoll_wait 發現該連線上有讀事件後，會呼叫在上一節中討論的為其註冊的讀處理函式 readQueryFromClient。

在讀處理函式 readQueryFromClient 中主要做了這麼幾件事情。

- 解析並查詢命令

- 呼叫命令處理

- 新增寫任務到佇列

- 將輸出寫到快取等待傳送

我們來詳細地看 readQueryFromClient 的程式碼。在 readQueryFromClient 中會呼叫 processInputBuffer，然後進入 processCommand 對命令進行處理。其呼叫鏈如下：

```
//file:src/networking.c
void readQueryFromClient(aeEventLoop *el, int fd, void *privdata, ...) {
    redisClient *c = (redisClient*) privdata;
    processInputBufferAndReplicate(c);
}

void processInputBufferAndReplicate(client *c) {
    ...
    processInputBuffer(c);
}

// 處理客戶端輸入的命令內容
void processInputBuffer(redisClient *c) {
    // 執行命令，
    processCommand(c);
}
```

我們再來詳細看 processCommand 。

```
//file:src/server.c
int processCommand(redisClient *c) {
    // 查詢命令，並進行命令合法性檢查，以及命令引數個數檢查
    c->cmd = c->lastcmd = lookupCommand(c->argv[0]->ptr);

    ......

    // 處理命令
    // 如果是 MULTI 事務，則入隊，否則呼叫 call 直接處理
    if (c->flags & CLIENT_MULTI && ...)
    {
        queueMultiCommand(c);
    } else {
        call(c,CMD_CALL_FULL);
        ...
    }
    return C_OK;
}
```

我們先忽略 queueMultiCommand，直接看核心命令處理方法 call。

```
//file:src/server.c
void call(client *c, int flags) {
    // 查詢處理命令，
    struct redisCommand *real_cmd = c->cmd;
    // 呼叫命令處理函式
    c->cmd->proc(c);
    ......
}
```

在 server.c 中定義了每一個命令對應的處理函式

```
//file:src/server.c
struct redisCommand redisCommandTable[] = {
    {"module",moduleCommand,-2,"as",0,NULL,0,0,0,0,0},
    {"get",getCommand,2,"rF",0,NULL,1,1,1,0,0},
    {"set",setCommand,-3,"wm",0,NULL,1,1,1,0,0},
    {"setnx",setnxCommand,3,"wmF",0,NULL,1,1,1,0,0},
    {"setex",setexCommand,4,"wm",0,NULL,1,1,1,0,0},
    ......

    {"mget",mgetCommand,-2,"rF",0,NULL,1,-1,1,0,0},
    {"rpush",rpushCommand,-3,"wmF",0,NULL,1,1,1,0,0},
    {"lpush",lpushCommand,-3,"wmF",0,NULL,1,1,1,0,0},
    {"rpushx",rpushxCommand,-3,"wmF",0,NULL,1,1,1,0,0},
    ......
}
```

對於 get 命令來說，其對應的命令處理函式就是 getCommand。也就是說當處理 GET 命令執行到`c->cmd->proc`的時候會進入到 getCommand 函式中來。

```
//file:src/t_string.c
void getCommand(client *c) {
    getGenericCommand(c);
}
int getGenericCommand(client *c) {
    robj *o;

    if ((o = lookupKeyReadOrReply(c,c->argv[1],shared.null[c->resp])) == NULL)
        return C_OK;
    ...
    addReplyBulk(c,o);
    return C_OK;
}
```

getGenericCommand 方法會呼叫 lookupKeyReadOrReply 來從記憶體中查詢對應的 key值。如果找不到，則直接返回 C_OK；如果找到了，呼叫 addReplyBulk 方法將值新增到輸出緩衝區中。

```
//file:src/networking.c
void addReplyBulk(client *c, robj *obj) {
    addReplyBulkLen(c,obj);
    addReply(c,obj);
    addReply(c,shared.crlf);
}
```

其主題是呼叫 addReply 來設定回覆資料。在 addReply 方法中做了兩件事情：

- prepareClientToWrite 判斷是否需要返回資料，並且將當前 client 新增到等待寫返回資料佇列中。

- 呼叫
\_addReplyToBuffer 和 \_addReplyObjectToList 方法將返回值寫入到輸出緩衝區中，等待寫入 socekt

```
//file:src/networking.c
void addReply(client *c, robj *obj) {
    if (prepareClientToWrite(c) != C_OK) return;

    if (sdsEncodedObject(obj)) {
        if (_addReplyToBuffer(c,obj->ptr,sdslen(obj->ptr)) != C_OK)
            _addReplyStringToList(c,obj->ptr,sdslen(obj->ptr));
    } else {
        ......
    }
}
```

先來看 prepareClientToWrite 的詳細實現，

```
//file:src/networking.c
int prepareClientToWrite(client *c) {
    ......
    if (!clientHasPendingReplies(c) && !(c->flags & CLIENT_PENDING_READ))
        clientInstallWriteHandler(c);
}

//file:src/networking.c
void clientInstallWriteHandler(client *c) {
    c->flags |= CLIENT_PENDING_WRITE;
    listAddNodeHead(server.clients_pending_write,c);
}
```

其中 server.clients_pending_write 就是我們說的任務佇列，佇列中的每一個元素都是有待寫返回資料的 client 物件。在 prepareClientToWrite 函式中，把 client 新增到任務佇列 server.clients_pending_write 裡就算完事。

接下再來 \_addReplyToBuffer，該方法是向固定快取中寫，如果寫不下的話就繼續呼叫 \_addReplyStringToList 往連結串列裡寫。簡單起見，我們只看 \_addReplyToBuffer 的程式碼。

```
//file:src/networking.c
int _addReplyToBuffer(client *c, const char *s, size_t len) {
    ......
    // 複製到 client 物件的 Response buffer 中
    memcpy(c->buf+c->bufpos,s,len);
    c->bufpos+=len;
    return C_OK;
}
```

#### 3.4 beforesleep 處理寫任務佇列

回想在 aeMain 函式中，每次在進入 aeProcessEvents 前都需要先進行 beforesleep 處理。這個函式名字起的怪怪的，但實際上大有用處。

```
//file:src/ae.c
void aeMain(aeEventLoop *eventLoop) {

    eventLoop->stop = 0;
    while (!eventLoop->stop) {
        // beforesleep 處理寫任務佇列並實際傳送之
        if (eventLoop->beforesleep != NULL)
            eventLoop->beforesleep(eventLoop);

        aeProcessEvents(eventLoop, AE_ALL_EVENTS);
    }
}
```

該函式處理了許多工作，其中一項便是遍歷傳送任務佇列，並將 client 傳送快取區中的處理結果透過 write 傳送到客戶端手中。

我們來看下 beforeSleep 的實際原始碼。

```
//file:src/server.c
void beforeSleep(struct aeEventLoop *eventLoop) {
    ......

    handleClientsWithPendingWrites();
}
```

```
//file:src/networking.c
int handleClientsWithPendingWrites(void) {
    listIter li;
    listNode *ln;
    int processed = listLength(server.clients_pending_write);

    //遍歷寫任務佇列 server.clients_pending_write
    listRewind(server.clients_pending_write,&li);
    while((ln = listNext(&li))) {
        client *c = listNodeValue(ln);
        c->flags &= ~CLIENT_PENDING_WRITE;
        listDelNode(server.clients_pending_write,ln);

        //實際將 client 中的結果資料傳送出去
        writeToClient(c->fd,c,0)

        //如果一次傳送不完則準備下一次傳送
        if (clientHasPendingReplies(c)) {
            //註冊一個寫事件處理器，等待 epoll_wait 發現可寫後再處理
            aeCreateFileEvent(server.el, c->fd, ae_flags,
                sendReplyToClient, c);
        }
        ......
    }
}
```

在 handleClientsWithPendingWrites 中，遍歷了傳送任務佇列 server.clients_pending_write，並呼叫 writeToClient 進行實際的傳送處理。

值得注意的是，傳送 write 並不總是能一次性傳送完的。假如要傳送的結果太大，而系統為每個 socket 設定的傳送快取區又是有限的。

在這種情況下，clientHasPendingReplies 判斷仍然有未傳送完的資料的話，就需要註冊一個寫事件處理函式到 epoll 上。等待 epoll 發現該 socket 可寫的時候再次呼叫 sendReplyToClient進行傳送。

```
//file:src/networking.c
int writeToClient(int fd, client *c, int handler_installed) {
    while(clientHasPendingReplies(c)) {
        // 先傳送固定緩衝區
        if (c->bufpos > 0) {
            nwritten = write(fd,c->buf+c->sentlen,c->bufpos-c->sentlen);
            if (nwritten <= 0) break;
            ......

        // 再傳送回復連結串列中資料
        } else {
            o = listNodeValue(listFirst(c->reply));
            nwritten = write(fd, o->buf + c->sentlen, objlen - c->sentlen);
            ......
        }
    }
}
```

writeToClient 中的主要邏輯就是呼叫 write 系統呼叫讓核心幫其把資料傳送出去即可。由於每個命令的處理結果大小是不固定的。所以 Redis 採用的做法用固定的 buf + 可變連結串列來儲存結果字串。這裡自然傳送的時候就需要分別對固定快取區和連結串列來進行傳送了。

### 四、高效能 Redis 網路原理總結

Redis 伺服器端只需要單執行緒可以達到非常高的處理能力，每秒可以達到數萬 QPS 的高處理能力。如此高效能的程式其實就是對 Linux 提供的多路複用機制 epoll 的一個較為完美的運用而已。

在 Redis 原始碼中，核心邏輯其實就是兩個，一個是 initServer 啟動服務，另外一個就是 aeMain 事件迴圈。把這兩個函式弄懂了，Redis 就吃透一大半了。

```
//file:src/server.c
int main(int argc, char **argv) {
    ......

    // 啟動初始化
    initServer();

    // 執行事件處理迴圈，一直到伺服器關閉為止
    aeMain(server.el);
}
```

在 initServer 這個函式內，Redis 做了這麼三件重要的事情。

- 建立一個 epoll 物件

- 對配置的監聽埠進行 listen

- 把 listen socket 讓 epoll 給管理起來

在 aeMain 函式中，是一個無休止的迴圈，它是 Redis 中最重要的部分。在每一次的迴圈中，要做的事情可以總結為如下圖。

- 透過 epoll_wait 發現 listen socket 以及其它連線上的可讀、可寫事件

- 若發現 listen socket 上有新連線到達，則接收新連線，並追加到 epoll 中進行管理

- 若發現其它 socket 上有命令請求到達，則讀取和處理命令，把命令結果寫到快取中，加入寫任務佇列

- 每一次進入 epoll_wait 前都呼叫 beforesleep 來將寫任務佇列中的資料實際進行傳送

其實事件分發器還處理了一個不明顯的邏輯，那就是如果 beforesleep 在將結果寫回給客戶端的時候，如果由於核心 socket 傳送快取區過小而導致不能一次傳送完畢的時候，也會註冊一個寫事件處理器。等到 epoll_wait 發現對應的 socket 可寫的時候，再執行 write 寫處理。

整個 Redis 的網路核心模組就在咱們這一篇文章中都敘述透了（剩下的 Redis 就是對各種資料結構的建立和處理了）。相信吃透這一篇對於你對網路程式設計的理解會有極大的幫助！

還等什麼，快把這篇文章也分享給你身邊和你一樣愛好深度技術的好友吧！

寫在最後，由於我的這些知識在公眾號裡文章比較分散，很多人似乎沒有理解到我對知識組織的體系結構。而且圖文也不像影片那樣理解起來更直接。所以我在知識星球上規劃了影片系列課程，包括硬體原理、記憶體管理、程序管理、檔案系統、網路管理、Golang語言、容器原理、效能觀測、效能最佳化九大部分大約 120 節內容，每週更新。加入方式參見[我要開始搞知識星球啦](https://mp.weixin.qq.com/s/_8ux274sY-As__Xwoqmewg)、[如何才能高效地學習技術,我投“融匯貫通”一票](https://mp.weixin.qq.com/s/z82z9jqnt08gBLYGxLHY2g)

Github：[https://github.com/yanfeizhang/coder-kung-fu](https://github.com/yanfeizhang/coder-kung-fu)
關注公眾號：微信掃描下方二維碼
