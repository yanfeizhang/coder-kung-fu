---
layout: default
title: "萬字多圖，理解 Nginx 高效能網路 (Network) 工作原理！"
---

# 萬字多圖，理解 Nginx 高效能網路 (Network) 工作原理！

> 原文標題：万字多图，搞懂 Nginx 高性能网络工作原理！

> 原文連結：https://mp.weixin.qq.com/s/AX6Fval8RwkgzptdjlU5kg

---

大家好，我是飛哥!

在單程序 (Process) 的網路程式設計模型中。所有的網路相關的動作都是在一個程序裡完成的，如監聽   `socket` 的建立，`bind`、 `listen`。再例如 `epoll` 的建立、要監聽事件的新增，以及   `epoll_wait` 等待時間發生。這些統統都是在一個程序裡完成。

一個用戶端和使用了 `epoll` 的伺服器端的互動過程如下圖所示。

以下是其大概的程式碼範例。

```
int main(){
    //監聽
    lfd = socket(AF_INET,SOCK_STREAM,0);
    bind(lfd, ...)
    listen(lfd, ...);

    //建立epoll物件，並把 listen socket的事件管理起來
    efd = epoll_create(...);
    epoll_ctl(efd, EPOLL_CTL_ADD, lfd, ...);

    //事件迴圈
    for (;;)
    {
        size_t nready = epoll_wait(efd, ep, ...);
        for (int i = 0; i < nready; ++i){

            if(ep[i].data.fd == lfd){
                //lfd上發生事件表示都連線到達，accept接收它
                fd = accept(listenfd, ...);
                epoll_ctl(efd, EPOLL_CTL_ADD, fd, ...);
            }else{
                //其他socket發生的事件都是讀寫請求、或者關閉連線
                ...
            }

        }
    }
}
```

在單程序模型中，不管有多少的連線，是幾萬還是幾十萬，伺服器都是透過   `epoll` 來監控這些連線 `socket` 上的可讀和可寫事件。當某個 `socket` 上有資料發生的時候，再以非阻塞 (Non-Blocking) 的方式對  `socket` 進行讀寫操作。

相對於同步阻塞的程式設計模型，這極大程度地減少了程序因為等待  `socket` 上的網路 IO 而被阻塞掉的情況，程序可以一直進行資料的計算和收發。因為砍掉了上下文切換 (Context Switch) 的系統開銷，所以大幅度地提升了應用程式的效能。

事實上，Redis 5.0 及以前的版本中，它的網路部分去掉對 handler 的封裝，去掉時間事件以後，程式碼基本和上述 demo 非常接近。而且因為 Redis 的業務特點只需要記憶體 (Memory) IO，且 CPU 計算少，所以可以達到數萬的 QPS。

但是單程序的問題也是顯而易見的，沒有辦法充分發揮多核的優勢。所以目前業界絕大部分的後端服務還都是需要根據多程序的方式來進行開發的。到了多程序的時候，更復雜的問題多程序之間的配合和協作問題就產生了。例如


- 哪個程序執行監聽  `listen`，以及 `accept` 接收新連線？
- 哪個程序負責發現使用者連線上的讀寫事件？
- 當有使用者請求到達的時候，如何均勻地將請求分散到不同的程序中？
- 需不需要單獨安排一部分程序執行計算工作
- ...

事實上，以上這些問題並沒有標準答案。各大應用或者網路框架都有自己不同的實作方式。為此業界還專門總結出了兩類網路設計模式 - Reactor 和 Proactor。不過今天我不想討論這種抽象模式，而是想帶大家看一個具體的 Case - Nginx 是如何在多程序下使用  `epoll` 的。

## 一、 Nginx Master 程序初始化

在 Nginx 中，將程序分成了兩類。一類是 Master 程序，一類是 Worker 程序。

在 Master 程序中，主要的任務是負責啟動整個程式、讀取設定檔、監聽和處理各種訊號，並對 Worker 程序進行統籌管理。

不過今天我們要檢視的重點問題是看網路。在 Master 程序中，和網路相關的操作非常簡單就是建立了  `socket` 並對其進行 `bind` 和監聽。

具體細節我們來看 Main 函式。

```
//file: src/core/nginx.c
int ngx_cdecl main(int argc, char *const *argv)
{
    ngx_cycle_t      *cycle, init_cycle;

    //1.1 ngx_init_cycle 中開啟監聽
    cycle = ngx_init_cycle(&init_cycle);

    //1.2 啟動主程序迴圈
    ngx_master_process_cycle(cycle);
}
```

在 Nginx 中，  `ngx_cycle_t` 是非常核心的一個結構體。這個結構體儲存了很多東西，也貫穿了好多的函式。其中對埠的 `bind` 和 `listen` 就是在它執行時完成的。

`ngx_master_process_cycle` 是 Master 程序的主事件迴圈。它先是根據設定啟動指定數量的 Worker 程序，然後就開始關注和處理重啟、退出等訊號。接下來我們分兩個小節來更詳細地看。

## 1.1 Nginx 的服務埠監聽

我們看下   `ngx_init_cycle` 中是如何執行 `bind` 和 `listen` 的。

```
//file: src/core/ngx_cycle.c
ngx_cycle_t *ngx_init_cycle(ngx_cycle_t *old_cycle)
{
    ......
    if (ngx_open_listening_sockets(cycle) != NGX_OK) {
        goto failed;
    }
}
```

真正的監聽還是在 `ngx_open_listening_sockets` 函式中，繼續看它的原始碼。

```
//file: src/core/ngx_connection.c
ngx_int_t ngx_open_listening_sockets(ngx_cycle_t *cycle)
{
    ......

    //要監聽的 socket 物件
    ls = cycle->listening.elts;
    for (i = 0; i < cycle->listening.nelts; i++) {

        //取得第i個socket
        s = ngx_socket(ls[i].sockaddr->sa_family, ls[i].type, 0);

        //繫結
        bind(s, ls[i].sockaddr, ls[i].socklen)

        //監聽
        listen(s, ls[i].backlog)
        ls[i].listen = 1;
        ls[i].fd = s;
    }
}
```

在這個函式中，遍歷要監聽的   `socket`。如果是啟用了 REUSEPORT 設定，那先把 `socket` 設定上 `SO_REUSEPORT` 選項。然後接下來就是大家都熟悉的 `bind` 和   `listen`。所以，`bind` 和 `listen` 是在 Master 程序中完成的。

## 1.2 Master 程序的主迴圈

在 `ngx_master_process_cycle` 中主要完成兩件事。

- 啟動 Worker 程序
- 將 Master 程序推入事件迴圈

在建立 Worker 程序的時候，是透過   `fork` 系統呼叫 (System Call) 讓 Worker 程序完全複製自己的資源，包括 `listen` 狀態的 `socket` 控制程式碼。

我們接下來看詳細的程式碼。

```
//file: src/os/unix/ngx_process_cycle.c
void ngx_master_process_cycle(ngx_cycle_t *cycle)
{
    ......
    ngx_start_worker_processes(cycle, ccf->worker_processes,
                               NGX_PROCESS_RESPAWN);
    //進入主迴圈,等待接收各種訊號
    for ( ;; ) {
        //ngx_quit
        //ngx_reconfigure
        //ngx_restart
        ...
    }
}
```

主程序在設定中讀取到了 Worker 程序的數量  `ccf->worker_processes`。透過 `ngx_start_worker_processes` 來啟動指定數量的 Worker。

```
//file:src/os/unix/ngx_process_cycle.c
static void ngx_start_worker_processes(...)
{
    for (i = 0; i < n; i++) {
        ngx_spawn_process(cycle, ngx_worker_process_cycle,
                          (void *) (intptr_t) i, "worker process", type);
        ...
    }
}
```

上述程式碼中值得注意的是，在呼叫   `ngx_spawn_process` 時的幾個參數

- cycle：nginx 的核心資料結構
- `ngx_worker_process_cycle`：worker 程序的入口函式
- i: 當前 worker 的序號

```
//file: src/os/unix/ngx_process.c
ngx_pid_t ngx_spawn_process(ngx_cycle_t *cycle, ngx_spawn_proc_pt proc,...)
{
    pid = fork();
    switch (pid) {
        case -1: // 出錯了
            ...
        case 0: // 子程序建立成功
            ngx_parent = ngx_pid;
            ngx_pid = ngx_getpid();
            proc(cycle, data);
            break;
        default:
            break;
    }
    ...
}
```

在   `ngx_spawn_process` 中呼叫 `fork` 來建立程序，建立成功後 Worker 程序就將進入 `ngx_worker_process_cycle` 來進行處理了。

總結：在網路上，master 程序其實只是 `listen` 了一下。 `listen` 過後的 `socket` 存到 `cycle->listening` 這裡了。剩下的網路操作都是在 Worker 中完成的。

## 二、Worker 程序處理

在上面小節中看到，Master 程序關於網路其實做的事情不多，只是   `bind` 和 `listen` 了一下。 `epoll` 相關的函式呼叫一個也沒見著，更別說 `accept` 接收連線，以及   `read`、`write` 函式處理了。那這些細節一定都是在 Worker 程序中完成的。

事實的確如此，  `epoll_create`、`epoll_ctl`、`epoll_wait` 都是在 Worker 程序中執行的。

在 Worker 程序中，建立了一個 `epoll` 核心物件，透過 `epoll_ctl` 將其想監聽的事件註冊上去，然後呼叫 `epoll_wait` 進入事件迴圈。

```
//file: src/os/unix/ngx_process_cycle.c
static void ngx_worker_process_cycle(ngx_cycle_t *cycle, void *data)
{
    //2.2 Worker程序初始化編譯進來的各個模組
    ngx_worker_process_init(cycle, worker);

    //進入事件迴圈
    for ( ;; ) {
        //2.3 進入 epollwait
        ngx_process_events_and_timers(cycle);
        ......
    }
}
```

接下來我們分別來細看。

## 2.1 Nginx 的網路相關 module

撇開 Worker 的工作流程不提，我們先來了解一個背景知識 - Nginx module。

Nginx 採用的是一種模組化的架構，它的模組包括核心模組、標準 HTTP 模組、可選 HTTP 模組、郵件服務模組和第三方模組等幾大類。每一個模組都以一個 module 的形式存在，都對應一個   `ngx_module_s` 結構體。透過這種方式來實作軟體可拔插，是一種非常優秀的軟體架構。

每個 module 根據自己的需求來實作各種 `init_xxx`, `exit_xxx` 方法來供 Nginx 在合適的時機呼叫。

```
//file: src/core/ngx_module.h
struct ngx_module_s {
    ......

    ngx_uint_t            version;

    void                 *ctx;
    ngx_command_t        *commands;
    ngx_uint_t            type;

    ngx_int_t           (*init_master)(ngx_log_t *log);
    ngx_int_t           (*init_module)(ngx_cycle_t *cycle);
    ngx_int_t           (*init_process)(ngx_cycle_t *cycle);
    ngx_int_t           (*init_thread)(ngx_cycle_t *cycle);
    void                (*exit_thread)(ngx_cycle_t *cycle);
    void                (*exit_process)(ngx_cycle_t *cycle);
    void                (*exit_master)(ngx_cycle_t *cycle);

    ......
};
```

其中和網路相關的 module 有   `ngx_events_module`、`ngx_event_core_module` 和具體的網路底層模組 `ngx_epoll_module`、 `ngx_kqueue_module` 等。

對於   `ngx_epoll_module` 來說，它在其上下文 `ngx_epoll_module_ctx` 中定義了各種 actions 方法（新增事件、刪除事件、新增連線等）。

```
//file:src/event/ngx_event.h
typedef struct {
    ngx_str_t              *name;

    void                 *(*create_conf)(ngx_cycle_t *cycle);
    char                 *(*init_conf)(ngx_cycle_t *cycle, void *conf);

    ngx_event_actions_t     actions;
} ngx_event_module_t;
```

```
//file:src/event/modules/ngx_epoll_module.c
static ngx_event_module_t  ngx_epoll_module_ctx = {
    &epoll_name,
    ngx_epoll_create_conf,               /* create configuration */
    ngx_epoll_init_conf,                 /* init configuration */

    {
        ngx_epoll_add_event,             /* add an event */
        ngx_epoll_del_event,             /* delete an event */
        ngx_epoll_add_event,             /* enable an event */
        ngx_epoll_del_event,             /* disable an event */
        ngx_epoll_add_connection,        /* add an connection */
        ngx_epoll_del_connection,        /* delete an connection */
#if (NGX_HAVE_EVENTFD)
        ngx_epoll_notify,                /* trigger a notify */
#else
        NULL,                            /* trigger a notify */
#endif
        ngx_epoll_process_events,        /* process the events */
        ngx_epoll_init,                  /* init the events */
        ngx_epoll_done,                  /* done the events */
    }
};
```

其中有一個 init 方法是   `ngx_epoll_init`，在這個 init 中會進行 `epoll` 物件的建立，以及 `ngx_event_actions` 方法的設定。

```
//file:src/event/modules/ngx_epoll_module.c
static ngx_int_t
ngx_epoll_init(ngx_cycle_t *cycle, ngx_msec_t timer)
{
    //建立一個 epoll 控制程式碼
    ep = epoll_create(cycle->connection_n / 2);
    ...

    ngx_event_actions = ngx_epoll_module_ctx.actions;
}
```

## 2.2 Worker 程序初始化各個模組

Worker 程序初始化的時候，在   `ngx_worker_process_init` 中讀取設定資訊進行一些設定，然後呼叫所有模組的 `init_process` 方法。

來看詳細程式碼。

```
//file: src/os/unix/ngx_process_cycle.c
static void
ngx_worker_process_init(ngx_cycle_t *cycle, ngx_int_t worker)
{
    ...

    //取得設定
    ccf = (ngx_core_conf_t *) ngx_get_conf(cycle->conf_ctx, ngx_core_module);

    //設定優先順序
    setpriority(PRIO_PROCESS, 0, ccf->priority)

    //設定檔案描述符限制
    setrlimit(RLIMIT_NOFILE, &rlmt)
    setrlimit(RLIMIT_CORE, &rlmt)

    //group 和 uid 設定
    initgroups(ccf->username, ccf->group)
    setuid(ccf->user)

    //CPU親和性
    cpu_affinity = ngx_get_cpu_affinity(worker)
    if (cpu_affinity) {
        ngx_setaffinity(cpu_affinity, cycle->log);
    }
    ......

    //呼叫各個模組的init_process進行模組初始化
    for (i = 0; cycle->modules[i]; i++) {
        if (cycle->modules[i]->init_process) {
            if (cycle->modules[i]->init_process(cycle) == NGX_ERROR) {
                /* fatal */
                exit(2);
            }
        }
    }

    ...
}
```

前面我們說過   `ngx_event_core_module`，它的 `init_process` 方法是 `ngx_event_process_init`。

```
//file: src/event/ngx_event.c
ngx_module_t  ngx_event_core_module = {
    ...
    ngx_event_process_init,                /* init process */
    ...
};
```

在   `ngx_event_core_module` 的 `ngx_event_process_init` 中，我們將看到 Worker 程序使用 `epoll_create` 來建立 `epoll` 物件，使用   `epoll_ctl` 來監聽 listensocket 上的連線請求。

來詳細看 `ngx_event_process_init` 的程式碼。

```
//file: src/event/ngx_event.c
static ngx_int_t ngx_event_process_init(ngx_cycle_t *cycle)
{
    //呼叫模組的init，建立 epoll 物件
    for (m = 0; cycle->modules[m]; m++) {
        if (cycle->modules[m]->type != NGX_EVENT_MODULE) {
            continue;
        }
        ...
        module->actions.init(cycle, ngx_timer_resolution)
        break;
    }
    ...

    //取得自己監聽的sokcet，將它們都新增到 epoll 中
    ngx_event_t         *rev
    ls = cycle->listening.elts;
    for (i = 0; i < cycle->listening.nelts; i++) {

        //取得一個 ngx_connection_t
        c = ngx_get_connection(ls[i].fd, cycle->log);

        //設定回呼函式為 ngx_event_accept
        rev->handler = ngx_event_accept

        if (ngx_add_event(rev, NGX_READ_EVENT, 0) == NGX_ERROR) {
            return NGX_ERROR;
        }
    }
    ...
}
```

透過   `ngx_add_event` 註冊的 `READ` 事件的處理函式。 `ngx_add_event` 就是一個抽象，對於 `epoll` 來說就是對 `epoll_ctl` 的封裝而已。

```
//file: src/event/ngx_event.h
#define ngx_add_event        ngx_event_actions.add
```

```
//file: src/event/modules/ngx_epoll_module.c
static ngx_int_t ngx_epoll_add_event(...)
{
    if (epoll_ctl(ep, op, c->fd, &ee) == -1) {
    ...
}
```

## 2.3 進入 epollwait

在   `ngx_worker_process_init` 中，`epoll_create` 和 `epoll_ctl` 都已經完成了。接下來就是進入事件迴圈，執行 `epoll_wait` 來處理。

```
//file: src/event/ngx_event.c
void
ngx_process_events_and_timers(ngx_cycle_t *cycle)
{
    ...
    // 防accept驚群鎖
    if (ngx_use_accept_mutex) {
        //嘗試取得鎖，取得失敗直接回傳
        if (ngx_trylock_accept_mutex(cycle) == NGX_ERROR) {
            return;
        }

        //取得鎖成功，則設定 NGX_POST_EVENTS 標記。
        if (ngx_accept_mutex_held) {
            flags |= NGX_POST_EVENTS;
        } else {
            ...
        }
    }

    //處理各種事件
    (void) ngx_process_events(cycle, timer, flags);
}
```

在   `ngx_process_events_and_timers` 開頭處，判斷是否使用 `accpet_mutext` 鎖。這是一個防止驚群的解決辦法。如果使用的話，先呼叫 `ngx_trylock_accept_mutex` 取得鎖，取得失敗則直接回傳，過段時間再來嘗試。取得成功是則設定 `NGX_POST_EVENTS` 的標誌位。

接下來呼叫   `ngx_process_events` 來處理各種網路和 timer 事件。對於 `epoll` 來說，這個函式就是對 `epoll_wait` 的封裝。

```
//file: src/event/ngx_event.h
#define ngx_process_events   ngx_event_actions.process_events
```

```
//file: src/event/modules/ngx_epoll_module.c
static ngx_int_t ngx_epoll_process_events(ngx_cycle_t *cycle, ...)
{
    events = epoll_wait(ep, event_list, (int) nevents, timer);
    for (i = 0; i < events; i++) {

        if (flags & NGX_POST_EVENTS) {
            ...
            ngx_post_event(rev, queue);
        }else{
            //呼叫回呼函式
            rev->handler(rev);
        }
        ...
    }
}

可見，在   ngx_epoll_process_events 是呼叫 epoll_wait 等待各種事件的發生。如果沒有 NGX_POST_EVENTS 標誌，則直接回呼 rev->handler 進行處理。使用了 accept_mutex 鎖的話，先把這個事件儲存起來，等後面合適的時機再去 accpet。
```

簡單對本節內容彙總一下。在 Master 程序中只是做了   `socket` 的 `bind` 和 `listen`。 而在 Worker 程序中所做的事情比較多，建立了 `epoll`，使用   `epoll_ctl` 將 `listen` 狀態的 `socket` 的事件監控起來。最後呼叫 `epoll_wait` 進入了事件迴圈，開始處理各種網路和 timer 事件。 本節流程總結如圖。

## 三、使用者連線來啦！

我們現在假設使用者的連線請求已經到了，這時候   `epoll_wait` 回傳後會執行其對應的 handler 函式 `ngx_add_event`。

在該回呼函式中被執行到的時候，表示   `listen` 狀態的 `socket` 上面有連線到了。所以這個函式主要做了三件事。

- 1. 呼叫   `accept` 取得使用者連線
- 2. 取得 connection 物件，其回呼函式為 `ngx_http_init_connection`
- 3. 將新連線   `socket` 透過 `epoll_ctl` 新增到 `epoll` 中進行管理

我們來看 `ngx_event_accept` 詳細程式碼。

```
//file: src/event/ngx_event_accept.c
void ngx_event_accept(ngx_event_t *ev)
{
    do {
        //接收建立好的連線
        s = accept(lc->fd, &sa.sockaddr, &socklen);

        if s {
            //3.1 取得 connection
            c = ngx_get_connection(s, ev->log);

            //3.2 新增新連線
            if (ngx_add_conn(c) == NGX_ERROR) {
                ngx_close_accepted_connection(c);
                return;
            }
        }
    } while (ev->available);
}
```

`listen` `socket` 上的讀事件發生的時候，就意味著有使用者連線就緒了。所以可以直接透過 `accept` 將其取出來。取出連線以後，再取得一個空閒的 connection 物件，透過 `ngx_add_conn` 將其新增到   `epoll` 中進行管理。

## 3.1 取得 connection

我們說一下 `ngx_get_connection`，這個函式本身倒是沒有什麼可說的。就是從 `ngx_cycle` 的 `free_connections` 中取得一個 connection 出來。

```
//file: src/core/ngx_connection.c
ngx_connection_t *ngx_get_connection(ngx_socket_t s, ngx_log_t *log)
{
    c = ngx_cycle->free_connections;
    c->read = rev;
    c->write = wev;
    c->fd = s;
    c->log = log;
    return c
}

值得說的是   free_connections 中的連線，對於 HTTP 服務來說，會經過 ngx_http_init_connection 的初始化處理。它會設定該連線讀寫事件的回呼函式 c-> read->handler 和 c-> write->handler。
```

```
//file: src/http/ngx_http_request.c
void ngx_http_init_connection(ngx_connection_t *c)
{
    ...
    rev = c->read;
    rev->handler = ngx_http_wait_request_handler;
    c->write->handler = ngx_http_empty_handler;
}
```

## 3.2 新增新連線

我們再來看   `ngx_add_conn`，對於 `epoll` module 來說，它就是 `ngx_epoll_add_connection` 這個函式。

```
//file: src/event/ngx_event.h
#define ngx_add_conn         ngx_event_actions.add_conn
```

```
//file: src/event/modules/ngx_epoll_module.c
static ngx_int_t
ngx_epoll_add_connection(ngx_connection_t *c)
{
    struct epoll_event  ee;
    ee.events = EPOLLIN|EPOLLOUT|EPOLLET|EPOLLRDHUP;
    ee.data.ptr = (void *) ((uintptr_t) c | c->read->instance);

    epoll_ctl(ep, EPOLL_CTL_ADD, c->fd, &ee)
    c->read->active = 1;
    c->write->active = 1;

    return NGX_OK;
}
```

可見這只是   `epoll_ctl` 的一個封裝而已。 這裡再補充說一下，如果這個用戶端連線 `socket` 上有資料到達的時候，就會進入到上面 3.1 節中註冊的 `ngx_http_wait_request_handler` 函式進行處理。後面就是 HTTP 的處理邏輯了。

## 四、總結

Nginx 的 Master 中做的網路相關動作不多，僅僅只是建立了 `socket`、然後 `bind` 並 `listen` 了一下。接著就是用自己   `fork` 出來多個 Worker 程序來。由於每個程序都一樣，所以每個 Worker 都有 Master 建立出來的 `listen` 狀態的 `socket` 控制程式碼。

Worker 程序處理的網路相關工作就比較多了。`epoll_create`、 `epoll_ctl`、 `epoll_wait` 都是在 Worker 程序中執行的，也包括使用者連線上的資料   `read`、處理和 `write`。

- 1. 先是使用   `epoll_create` 建立一個 `epoll` 物件出來
- 2. 設定回呼為   `ngx_event_accept`
- 3. 透過 `epoll_ctl` 將所有 `listen` 狀態的 `socket` 的事件都管理起來
- 4. 執行   `epoll_wait` 等待 listensocket 上的連線到來
- 5. 新連線到來是 `epoll_wait` 回傳，進入 `ngx_event_accept` 回呼
- 6. `ngx_event_accept` 回呼中將新連線也新增到 `epoll` 中進行管理（其回呼為 `ngx_http_init_connection`）
- 7. 繼續進入   `epoll_wait` 等待事件
- 8. 使用者資料請求到達時進入 http 回呼函式進行處理

講到這裡，你可以覺得我們已經討論完了。實際上有一個點我們還沒有考慮到。我們上面討論的流程是一個 Worker 在工作的情況。那麼在多 Worker 的情況下，Nginx 的全貌我們還沒展開說過。透過上文我們可以看到以下幾個細節：

- 1. 每個 Worker 都會有一個屬於自己的 `epoll` 物件
- 2. 每個 Worker 會關注所有的   `listen` 狀態上的新連線事件
- 3. 對於使用者連線，只有一個 Worker 會處理，其他 Worker 不會持有該使用者連線的 `socket`。

根據這三條結論，我們再畫一個 Nginx 的全貌圖。


由於最近常用極客時間會員看各門課，被他們的思考題傳染了，我也學學他們留一道。


答案

- 第一個辦法文中提到了 nginx 實作了 `accept_mutex` 來保證一條連線只有一個 Worker 去 `accept` 接收任務
- 還有一個方法就是 reuseport。在 Linux 3.9 以上的核心版本允許多個程序監聽同一個埠，這樣每個程序都有自己獨立的 `listen` `socket`，核心來實作負載均衡， `accept` 再也不會衝突。nginx 1.9.1 以後的版本可以開啟使用這個 reuseport 特性。

`accept` 驚群

- linux 2.6 之前主要靠 nginx 自己實作的鎖來保證只有一個程序去 `accept`
- linux 2.6 以後核心增加了   `WQ_FLAG_EXCLUSIVE` 保證多程序 `accept` 同一個 `listen` `socket` 時，只有一個程序被喚醒。
