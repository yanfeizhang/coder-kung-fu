---
layout: default
title: "Redis 6 中的多執行緒實現方式比我預期的要差"
---

# Redis 6 中的多執行緒實現方式比我預期的要差

> 原文標題：Redis 6 中的多线程实现方式比我预期的要差

> 原文連結：http://kfngxl.cn/index.php/archives/278/

---

大家好，我是飛哥！

Redis 是一個高效能服務端的典範。它透過多路複用 epoll 來管理海量的使用者連線，只使用一個執行緒來透過事件迴圈來處理所有使用者請求，就可以達到每秒數萬 QPS 的處理能力。下圖是單執行緒版本 Redis 工作的核心原理圖，詳情參見。

單執行緒的 Redis 雖然效能很高，但是卻有兩個問題。一個問題是沒有辦法充分發揮現代 CPU 的多核處理能力，一個例項只能使用一個核的能力。二是如果某個使用者請求的處理過程卡住一段時間，會導致其它所有的請求都會出現超時的情況。所以，線上上的 redis 使用過程時是明確禁止使用 keys * 等長耗時的操作的。

那如何改進呢，思路和方向其實很明確。那就是和其它的主流程式一樣引入多執行緒，用更多的執行緒來分擔這些可能耗時的操作。事實上 Redis 也確實這麼幹了，在 6.0 以後的版本里，開始支援了多執行緒。我們今天就來領略一下 Redis 的多執行緒是如何實現的。

### 一、多執行緒 Redis 服務啟動

首先獲取多執行緒版本 Redis 的原始碼

```
# git clone https://github.com/redis/redis
# cd redis
# git checkout -b 6.2.0 6.2.0
```

預設情況下多執行緒是預設關閉的。如果想要啟動多執行緒，需要在配置檔案中做適當的修改。相關的配置項是 io-threads 和 io-threads-do-reads 兩個。

```
#vi /usr/local/soft/redis6/conf/redis.conf
io-threads 4 #啟用的 io 執行緒數量
io-threads-do-reads yes #讀請求也使用io執行緒
```

其中 io-threads 表示要啟動的 io 執行緒的數量。io-threads-do-reads 表示是否在讀階段也使用 io 執行緒，預設是隻在寫階段使用 io 執行緒的。

現在假設我們已經開啟了如上兩項多執行緒配置。帶著這個假設，讓我們進入到 Redis 的 main 入口函式。

```
//file: src/server.c
int main(int argc, char **argv) {
    ......

    // 1.1 主執行緒初始化
    initServer();

    // 1.2 啟動 io 執行緒
    InitServerLast();

    // 進入事件迴圈
    aeMain(server.el);
}
```

#### 1.1 主執行緒初始化

在 initServer 這個函式內，Redis 主執行緒做了這麼幾件重要的事情。

- 初始化讀任務佇列、寫任務佇列

- 建立一個 epoll 物件

- 對配置的監聽埠進行 listen

- 把 listen socket 讓 epoll 給管理起來

```
//file: src/server.c
void initServer() {

    // 1 初始化 server 物件
    server.clients_pending_write = listCreate();
    server.clients_pending_read = listCreate();
    ......

    // 2 初始化回撥 events，建立 epoll
    server.el = aeCreateEventLoop(server.maxclients+CONFIG_FDSET_INCR);

    // 3 繫結監聽服務埠
    listenToPort(server.port,server.ipfd,&server.ipfd_count);

    // 4 註冊 accept 事件處理器
    for (j = 0; j < server.ipfd_count; j++) {
        aeCreateFileEvent(server.el, server.ipfd[j], AE_READABLE,
            acceptTcpHandler,NULL);
    }
    ...
}
```

接下來我們分別來看。

##### 初始化 server 物件

在 initServer 的一開頭，先是對 server 的各種成員變數進行初始化。值得注意的是 clients_pending_write 和 clients_pending_read 這兩個成員，它們分別是寫任務佇列和讀任務佇列。將來主執行緒產生的任務都會放在放在這兩個任務佇列裡。

主執行緒會根據這兩個任務佇列來進行任務雜湊雜湊，以將任務分配到多個執行緒中進行處理。

##### aeCreateEventLoop 處理

我們來看 aeCreateEventLoop 詳細邏輯。它會初始化事件回撥 event，並且建立了一個 epoll 物件出來。

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

我們注意一下 eventLoop->events，將來在各種事件註冊的時候都會儲存到這個陣列裡。

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

##### 繫結監聽服務埠

我們再來看 Redis 中的 listen 過程，它在 listenToPort 函式中。呼叫鏈條很長，依次是 listenToPort => anetTcpServer => \_anetTcpServer => anetListen。在 anetListen 中，就是簡單的 bind 和 listen 的呼叫。

```
//file:src/anet.c
static int anetListen(......) {
    bind(s,sa,len);
    listen(s, backlog);
    ......
}
```

##### 註冊事件回撥函式

前面我們呼叫 aeCreateEventLoop 建立了 epoll，呼叫 listenToPort 進行了服務埠的 bind 和 listen。接著就呼叫的 aeCreateFileEvent 就是來註冊一個 accept 事件處理器。

我們來看 aeCreateFileEvent 具體程式碼。

```
//file: src/ae.c
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
//file: src/server.c
void initServer() {
    ......

    for (j = 0; j < server.ipfd_count; j++) {
        aeCreateFileEvent(server.el, server.ipfd[j], AE_READABLE,
            acceptTcpHandler,NULL);
    }
}
```

listen fd 對應的讀回撥函式 rfileProc 事實上就被設定成了 acceptTcpHandler，寫回撥沒有設定，私有資料 client_data 也為 null。

#### 1.2 io 執行緒啟動

在主執行緒啟動以後，會呼叫 InitServerLast => initThreadedIO 來建立多個 io 執行緒。

將來這些 IO 執行緒會配合主執行緒一起共同來處理所有的 read 和 write 任務。

我們來看 InitServerLast 建立 IO 執行緒的過程。

```
//file:src/server.c
void InitServerLast() {
    initThreadedIO();
    ......
}
```

```
//file:src/networking.c
void initThreadedIO(void) {
    //如果沒開啟多 io 執行緒配置就不建立了
    if (server.io_threads_num == 1) return;

    //開始 io 執行緒的建立
    for (int i = 0; i < server.io_threads_num; i++) {
        pthread_t tid;
        pthread_create(&tid,NULL,IOThreadMain,(void*)(long)i)
        io_threads[i] = tid;
    }
}
```

在 initThreadedIO 中呼叫 pthread_create 庫函式建立執行緒，並且註冊執行緒回撥函式 IOThreadMain。

```
//file:src/networking.c
void *IOThreadMain(void *myid) {
    long id = (unsigned long)myid;

    while(1) {
        //迴圈等待任務
        for (int j = 0; j < 1000000; j++) {
            if (getIOPendingCount(id) != 0) break;
        }

        //允許主執行緒來關閉自己
        ......

        //遍歷當前執行緒等待佇列裡的請求 client
        listIter li;
        listNode *ln;
        listRewind(io_threads_list[id],&li);
        while((ln = listNext(&li))) {
            client *c = listNodeValue(ln);
            if (io_threads_op == IO_THREADS_OP_WRITE) {
                writeToClient(c,0);
            } else if (io_threads_op == IO_THREADS_OP_READ) {
                readQueryFromClient(c->conn);
            } else {
                serverPanic("io_threads_op value is unknown");
            }
        }
        listEmpty(io_threads_list[id]);
    }
}
```

是將當前執行緒等待佇列 io_threads_list[id] 裡所有的請求 client，依次取出處理。 其中讀操作透過 readQueryFromClient 處理， 寫操作透過 writeToClient 處理。其中 io_threads_list[id] 中的任務是主執行緒分配過來的，後面我們將會看到。

### 二、主執行緒事件迴圈

接著我們進入到 Redis 最重要的 aeMain，這個函式就是一個死迴圈（Redis 不退出的話），不停地執行 aeProcessEvents 函式。

```
void aeMain(aeEventLoop *eventLoop) {
    eventLoop->stop = 0;
    while (!eventLoop->stop) {
        aeProcessEvents(eventLoop, AE_ALL_EVENTS|
                                   AE_CALL_BEFORE_SLEEP|
                                   AE_CALL_AFTER_SLEEP);
    }
}
```

其中 aeProcessEvents 就是所謂的事件分發器。它透過呼叫 epoll_wait 來發現所發生的各種事件，然後呼叫事先註冊好的處理函式進行處理。

接著看 aeProcessEvents 函式。

```
//file:src/ae.c
int aeProcessEvents(aeEventLoop *eventLoop, int flags)
{
    // 2.3 事件迴圈處理3：epoll_wait 前進行讀寫任務佇列處理
    if (eventLoop->beforesleep != NULL && flags & AE_CALL_BEFORE_SLEEP)
            eventLoop->beforesleep(eventLoop);

    //epoll_wait發現事件並進行處理
    numevents = aeApiPoll(eventLoop, tvp);

    // 從已就緒陣列中獲取事件
    aeFileEvent *fe = &eventLoop->events[eventLoop->fired[j].fd];

    //如果是讀事件，並且有讀回撥函式
    //2.1 如果是 listen socket 讀事件，則處理新連線請求
    //2.2 如果是客戶連線socket 讀事件，處理客戶連線上的讀請求
    fe->rfileProc()

    //如果是寫事件，並且有寫回撥函式
    fe->wfileProc()
    ......
}
```

其中 aeApiPoll 就是對 epoll_wait 的一個封裝而已。

```
//file: src/ae_epoll.c
static int aeApiPoll(aeEventLoop *eventLoop, struct timeval *tvp) {
    // 等待事件
    aeApiState *state = eventLoop->apidata;
    epoll_wait(state->epfd,state->events,eventLoop->setsize,
            tvp ? (tvp->tv_sec*1000 + tvp->tv_usec/1000) : -1);
    ...
}
```

aeProcessEvents 就是呼叫 epoll_wait 來發現事件。當發現有某個 fd 上事件發生以後，則調為其事先註冊的事件處理器函式 rfileProc 和 wfileProc。

#### 2.1 事件迴圈處理1：新連線到達

在 1.1 節中我們看到，主執行緒初始化的時候，將 listen socket 上的讀事件處理函式註冊成了 acceptTcpHandler。 也就是說如果有新連線到達的時候，acceptTcpHandler 將會被執行到。

在這個函式內，主要完成如下幾件事情。

- 呼叫 accept 接收連線

- 建立一個 redisClient物件

- 新增到 epoll

- 註冊讀事件處理函式

接下來讓我們進入 acceptTcpHandler 原始碼。

```
//file:src/networking.c
void acceptTcpHandler(aeEventLoop *el, int fd, void *privdata, int mask) {
    ......
    cfd = anetTcpAccept(server.neterr, fd, cip, sizeof(cip), &cport);
    acceptCommonHandler(connCreateAcceptedSocket(cfd),0,cip);
}
```

其中 netTcpAccept 呼叫 accept 系統呼叫獲取連線，就不展開了。我們看 acceptCommonHandler。

```
//file: src/networking.c
static void acceptCommonHandler(int fd, int flags) {
    // 建立客戶端
    redisClient *c;
    if ((c = createClient(fd)) == NULL) {
    }
}

client *createClient(connection *conn) {
    client *c = zmalloc(sizeof(client));

    // 為使用者連線註冊讀事件處理器
    if (conn) {
        ...
        connSetReadHandler(conn, readQueryFromClient);
        connSetPrivateData(conn, c);
    }

    selectDb(c,0);
    c->id = client_id;
    c->resp = 2;
    c->conn = conn;
    ......
}
```

在上面的程式碼中，我們重點關注`connSetReadHandler(conn, readQueryFromClient)`, 這一行是將這個新連線的讀事件處理函式設定成了 readQueryFromClient。

#### 2.2 事件迴圈處理2：使用者命令請求到達

在上面我們看到了， Redis 把使用者連線上的讀請求處理函式設定成了 readQueryFromClient，這意味著當使用者連線上有命令傳送過來的時候，會進入 readQueryFromClient 開始執行。

在多執行緒版本的 readQueryFromClient 中，處理邏輯非常簡單，僅僅只是將發生讀時間的 client 放到了任務佇列裡而已。

來詳細看 readQueryFromClient 程式碼。

```
//file:src/networking.c
void readQueryFromClient(connection *conn) {
    client *c = connGetPrivateData(conn);

    //如果啟動 threaded I/O 的話，直接入隊
    if (postponeClientRead(c)) return;

    //處理使用者連線讀請求
    ......
    c->querybuf = sdsMakeRoomFor(c->querybuf, readlen);
    nread = connRead(c->conn, c->querybuf+qblen, readlen);
    processInputBuffer(c);
}
```

在 postponeClientRead 中判斷，是不是開啟了多 io 執行緒，如果開啟了的話，那就將有請求資料到達的 client 直接放到讀任務佇列（server.clients_pending_read）中就算是完事。我們看下 postponeClientRead。

```
//file:src/networking.c
int postponeClientRead(client *c) {
    if (server.io_threads_active &&
        server.io_threads_do_reads &&
        !ProcessingEventsWhileBlocked &&
        !(c->flags & (CLIENT_MASTER|CLIENT_SLAVE|CLIENT_PENDING_READ)))
    {
        c->flags |= CLIENT_PENDING_READ;
        listAddNodeHead(server.clients_pending_read,c);
        return 1;
    } else {
        return 0;
    }
}
```

listAddNodeHead 就是把這個 client 物件新增到 server.clients_pending_read 而已。

#### 2.3 事件迴圈處理3：epoll_wait 前進行任務處理

在 aeProcessEvents 中假如 aeApiPoll(epoll_wait)中的事件都處理完了以後，則會進入下一次的迴圈再次進入 aeProcessEvents。

而這一次中 beforesleep 將會處理前面讀事件處理函式新增的讀任務佇列了。

```
//file:src/ae.c
int aeProcessEvents(aeEventLoop *eventLoop, int flags)
{
    // 參見 2.4 事件迴圈處理3：epoll_wait 前進行任務處理
    if (eventLoop->beforesleep != NULL && flags & AE_CALL_BEFORE_SLEEP)
            eventLoop->beforesleep(eventLoop);

    //epoll_wait發現事件並進行處理
    numevents = aeApiPoll(eventLoop, tvp);
    ......
}
```

在 beforeSleep 裡會依次處理兩個任務佇列。先處理讀任務佇列，解析其中的請求，並處理之。然後將處理結果寫到快取中，同時寫到寫任務佇列中。緊接著 beforeSleep 會進入寫任務佇列處理，會將處理結果寫到 socket 裡，進行真正的資料傳送。

我們來看 beforeSleep 的程式碼，這個函式中最重要的兩個呼叫是 handleClientsWithPendingReadsUsingThreads（處理讀任務佇列），handleClientsWithPendingWritesUsingThreads（處理寫任務佇列）

```
//file:src/server.c
void beforeSleep(struct aeEventLoop *eventLoop) {
    //處理讀任務佇列
    handleClientsWithPendingReadsUsingThreads();
    //處理寫任務佇列
    handleClientsWithPendingWritesUsingThreads();
    ......
}
```

值得注意的是，如果開啟了多 io 執行緒的話，handleClientsWithPendingReadsUsingThreads 和 handleClientsWithPendingWritesUsingThreads 中將會是主執行緒、io 執行緒一起配合來處理的。所以我們單獨分兩個小節來闡述。

#### 三、主執行緒 && io 執行緒處理讀請求

在 handleClientsWithPendingReadsUsingThreads 中，主執行緒會遍歷讀任務佇列 server.clients_pending_read，把其中的請求分配到每個 io 執行緒的處理佇列 io_threads_list[target_id] 中。然後通知各個 io 執行緒開始處理。

#### 3.1 主執行緒分配任務

我們來看 handleClientsWithPendingReadsUsingThreads 詳細程式碼。

```
//file:src/networking.c
//當開啟了 reading + parsing 多執行緒 I/O
//read handler 僅僅只是把 clients 推到讀佇列裡
//而這個函式開始處理該任務佇列
int handleClientsWithPendingReadsUsingThreads(void) {

    //訪問讀任務佇列 server.clients_pending_read
    listRewind(server.clients_pending_read,&li);

    //把每一個任務取出來
    //新增到指定執行緒的任務佇列裡 io_threads_list[target_id]
    while((ln = listNext(&li))) {
        client *c = listNodeValue(ln);
        int target_id = item_id % server.io_threads_num;
        listAddNodeTail(io_threads_list[target_id],c);
        item_id++;
    }

    //啟動Worker執行緒，處理讀請求
    io_threads_op = IO_THREADS_OP_READ;
    for (int j = 1; j < server.io_threads_num; j++) {
        int count = listLength(io_threads_list[j]);
        setIOPendingCount(j, count);
    }

    //主執行緒處理 0 號任務佇列
    listRewind(io_threads_list[0],&li);
    while((ln = listNext(&li))) {
        //需要先幹掉 CLIENT_PENDING_READ 標誌
        //否則 readQueryFromClient 並不處理，而是入隊
        client *c = listNodeValue(ln);
        readQueryFromClient(c->conn);
    }

    //主執行緒等待其它執行緒處理完畢
    while(1) {
        unsigned long pending = 0;
        for (int j = 1; j < server.io_threads_num; j++)
            pending += getIOPendingCount(j);
        if (pending == 0) break;
    }

    //再跑一遍任務佇列，目的是處理輸入
    while(listLength(server.clients_pending_read)) {
        ......
        processInputBuffer(c);
        if (!(c->flags & CLIENT_PENDING_WRITE) && clientHasPendingReplies(c))
            clientInstallWriteHandler(c);
    }
}
```

在主執行緒中將任務分別放到了 io_threads_list 的第 0 到第 N 個元素裡。並對 1 : N 號執行緒透過 setIOPendingCount 發訊息，告訴他們起來處理。這時候 io 執行緒將會在 IOThreadMain 中收到訊息並開始處理讀任務。

```
//file:src/networking.c
void *IOThreadMain(void *myid) {
    while(1) {
        //遍歷當前執行緒等待佇列裡的請求 client
        listRewind(io_threads_list[id],&li);
        while((ln = listNext(&li))) {
            client *c = listNodeValue(ln);
            if (io_threads_op == IO_THREADS_OP_WRITE) {
                writeToClient(c,0);
            } else if (io_threads_op == IO_THREADS_OP_READ) {
                readQueryFromClient(c->conn);
            } else {
                serverPanic("io_threads_op value is unknown");
            }
        }
    }
}
```

在 io 執行緒中，從自己的 io_threads_list[id] 中遍歷獲取待處理的 client。如果發現是讀請求處理，則進入 readQueryFromClient 開始處理特定的 client。

而主執行緒在分配完 1 ：N 任務佇列讓其它 io 執行緒處理後，自己則開始處理第 0 號任務池。同樣是會進入到 readQueryFromClient 中來執行。

```
//file:src/networking.c
int handleClientsWithPendingReadsUsingThreads(void) {
    ......
    //主執行緒處理 0 號任務佇列
    listRewind(io_threads_list[0],&li);
    while((ln = listNext(&li))) {
        //需要先幹掉 CLIENT_PENDING_READ 標誌
        //否則 readQueryFromClient 並不處理，而是入隊
        client *c = listNodeValue(ln);
        readQueryFromClient(c->conn);
    }
    ......
}
```

所以無論是主執行緒還是 io 執行緒，處理客戶端的讀事件都是會進入 readQueryFromClient。我們來看其原始碼。

#### 3.2 讀請求處理

```
//file:src/networking.c
void readQueryFromClient(connection *conn) {

    //讀取請求
    nread = connRead(c->conn, c->querybuf+qblen, readlen);

    //處理請求
    processInputBuffer(c);
}
```

在 connRead 中就是呼叫 read 將 socket 中的命令讀取出來，就不展開看了。接著在 processInputBuffer 中將輸入緩衝區中的資料解析成對應的命令。解析完命令後真正開始處理它。

```
//file:src/networking.c
void processInputBuffer(client *c) {
    while(c->qb_pos < sdslen(c->querybuf)) {
        //解析命令
        ......
        //真正開始處理 command
        processCommandAndResetClient(c);
    }
}
```

函式 processCommandAndResetClient 會呼叫 processCommand，查詢命令並開始執行。執行的核心方法是 call 函式，我們直接看它。

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
//file: src/t_string.c
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
//file: src/networking.c
void addReplyBulk(client *c, robj *obj) {
    addReplyBulkLen(c,obj);
    addReply(c,obj);
    addReply(c,shared.crlf);
}
```

#### 3.3 寫處理結果到傳送快取區

其主體是呼叫 addReply 來設定回覆資料。在 addReply 方法中做了兩件事情：

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
//file: src/networking.c
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

要注意的是，本節的讀請求處理過程是主執行緒和 io 執行緒在並行執行的。主執行緒在處理完後會等待其它的 io 執行緒處理。在所有的讀請求都處理完後，主執行緒 beforeSleep 中對 handleClientsWithPendingReadsUsingThreads 的呼叫就結束了。

#### 四、主執行緒 && io 執行緒配合處理寫請求

當所有的讀請求處理完後，handleClientsWithPendingReadsUsingThreads 會退出。主執行緒會緊接著進入 handleClientsWithPendingWritesUsingThreads 中來處理。

```
//file:src/server.c
void beforeSleep(struct aeEventLoop *eventLoop) {
    //處理讀任務佇列
    handleClientsWithPendingReadsUsingThreads();
    //處理寫任務佇列
    handleClientsWithPendingWritesUsingThreads();
    ......
}
```

#### 4.1 主執行緒分配任務

```
//file:src/networking.c
int handleClientsWithPendingWritesUsingThreads(void) {
    //沒有開啟多執行緒的話，仍然是主執行緒自己寫
    if (server.io_threads_num == 1 || stopThreadedIOIfNeeded()) {
        return handleClientsWithPendingWrites();
    }

    ......

    //獲取待寫任務
    int processed = listLength(server.clients_pending_write);

    //在N個任務列表中分配該任務
    listIter li;
    listNode *ln;
    listRewind(server.clients_pending_write,&li);
    int item_id = 0;
    while((ln = listNext(&li))) {
        client *c = listNodeValue(ln);
        c->flags &= ~CLIENT_PENDING_WRITE;

        /* Remove clients from the list of pending writes since
         * they are going to be closed ASAP. */
        if (c->flags & CLIENT_CLOSE_ASAP) {
            listDelNode(server.clients_pending_write, ln);
            continue;
        }

        //hash的方式進行分配
        int target_id = item_id % server.io_threads_num;
        listAddNodeTail(io_threads_list[target_id],c);
        item_id++;
    }

    //告訴對應的執行緒該開始幹活了
    io_threads_op = IO_THREADS_OP_WRITE;
    for (int j = 1; j < server.io_threads_num; j++) {
        int count = listLength(io_threads_list[j]);
        setIOPendingCount(j, count);
    }

    //主執行緒自己也會處理一些
    listRewind(io_threads_list[0],&li);
    while((ln = listNext(&li))) {
        client *c = listNodeValue(ln);
        writeToClient(c,0);
    }
    listEmpty(io_threads_list[0]);

    //迴圈等待其它執行緒結束處理
    while(1) {
        unsigned long pending = 0;
        for (int j = 1; j < server.io_threads_num; j++)
            pending += getIOPendingCount(j);
        if (pending == 0) break;
    }
    ......
}
```

在 io 執行緒中收到訊息後，開始遍歷自己的任務佇列 io_threads_list[id]，並將其中的 client 挨個取出來開始處理。

```
//file:src/networking.c
void *IOThreadMain(void *myid) {
    while(1) {
        //遍歷當前執行緒等待佇列裡的請求 client
        listRewind(io_threads_list[id],&li);
        while((ln = listNext(&li))) {
            client *c = listNodeValue(ln);
            if (io_threads_op == IO_THREADS_OP_WRITE) {
                writeToClient(c,0);
            } else if (io_threads_op == IO_THREADS_OP_READ) {
                readQueryFromClient(c->conn);
            }
        }
        listEmpty(io_threads_list[id]);
    }
}
```

#### 4.2 寫請求處理

由於這次任務佇列裡都是寫請求，所以 io 執行緒會進入 writeToClient。而主執行緒在分配完任務以後，自己開始處理起了 io_threads_list[0]，並也進入到 writeToClient。

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

當所有的寫請求也處理完後，beforeSleep 就退出了。主執行緒將會再次呼叫 epoll_wait 來發現請求，進入下一輪的使用者請求處理。

#### 五、總結

```
//file: src/server.c
int main(int argc, char **argv) {
    ......

    // 1.1 主執行緒初始化
    initServer();

    // 1.2 啟動 io 執行緒
    InitServerLast();

    // 進入事件迴圈
    aeMain(server.el);
}
```

在 initServer 這個函式內，Redis 做了這麼三件重要的事情。

- 建立一個 epoll 物件

- 對配置的監聽埠進行 listen

- 把 listen socket 讓 epoll 給管理起來

在 initThreadedIO 中呼叫 pthread_create 庫函式建立執行緒，並且註冊執行緒回撥函式 IOThreadMain。在 IOThreadMain 中等待其佇列 io_threads_list[id] 產生請求，當有請求到達的時候取出 client，依次處理。 其中讀操作透過 readQueryFromClient 處理， 寫操作透過 writeToClient 處理。

主執行緒在 aeMain 函式中，是一個無休止的迴圈，它是 Redis 中最重要的部分。它先是呼叫事件分發器發現事件。如果有新連線請求到達的時候，執行 accept 接收新連線，併為其註冊事件處理函式。

當使用者連線上有命令請求到達的時候，主執行緒在 read 處理函式中將其新增到讀傳送佇列中。然後接著在 beforeSleep 中開啟對讀任務佇列和寫任務佇列的處理。總體工作過程如下圖所示。

在這個處理過程中，對讀任務佇列和寫任務佇列的處理都是多執行緒並行進行的（前提是開篇我們開啟了多 IO 執行緒並且也併發處理讀）。

當讀任務佇列和寫任務佇列的都處理完的時候，主執行緒再一次呼叫 epoll_wait 去發現新的待處理事件，如此往復迴圈進行處理。

至此，多執行緒版本的 Redis 的工作原理就介紹完了。坦白講，我覺得這種多執行緒模型實現的並不足夠的好。

原因是主執行緒是在處理讀、寫任務佇列的時候還要等待其它的 io 執行緒處理完才能進入下一步。假設這時有 10 個使用者請求到達，其中 9 個處理耗時需要 1 ms，而另外一個命令需要 1 s。則這時主執行緒仍然會等待這個 io 執行緒處理 1s 結束後才能進入後面的處理。 整個 Redis 服務還是被一個耗時的命令給 block 住了。

我倒是希望我的理解哪裡有問題。因為這種方式真的是沒能很好地並發起來。

- 透過 epoll_wait 發現 listen socket 以及其它連線上的可讀、可寫事件

- 若發現 listen socket 上有新連線到達，則接收新連線，並追加到 epoll 中進行管理

- 若發現其它 socket 上有命令請求到達，則將讀任務新增到佇列中

- 進入 epoll_wait 前都呼叫 beforesleep 處理命令讀任務佇列，解析並處理命令，然後把命令結果寫到傳送快取，並將 client 加入寫任務佇列

- 進入 epoll_wait 前都呼叫 beforesleep 處理寫任務佇列，將寫任務佇列中的資料實際進行傳送

寫在最後，由於我的這些知識在公眾號裡文章比較分散，很多人似乎沒有理解到我對知識組織的體系結構。而且圖文也不像影片那樣理解起來更直接。所以我在知識星球上規劃了影片系列課程，包括硬體原理、記憶體管理、程序管理、檔案系統、網路管理、Golang語言、容器原理、效能觀測、效能最佳化九大部分大約 120 節內容，每週更新。加入方式參見[我要開始搞知識星球啦](https://mp.weixin.qq.com/s/_8ux274sY-As__Xwoqmewg)、[如何才能高效地學習技術,我投“融匯貫通”一票](https://mp.weixin.qq.com/s/z82z9jqnt08gBLYGxLHY2g)

Github：[https://github.com/yanfeizhang/coder-kung-fu](https://github.com/yanfeizhang/coder-kung-fu)
關注公眾號：微信掃描下方二維碼
