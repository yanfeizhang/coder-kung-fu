---
layout: default
title: "圖解 | 深入揭秘 `epoll` 是如何實作 IO 多路複用 (`I/O` Multiplexing) 的！"
---

# 圖解 | 深入揭秘 `epoll` 是如何實作 IO 多路複用 (`I/O` Multiplexing) 的！

> 原文標題：🔥图解 | 深入揭秘 epoll 是如何实现 IO 多路复用的！

> 原文連結：https://mp.weixin.qq.com/s/OmRdUgO1guMX76EdZn11UQ

---

程序 (Process) 在 Linux 上是一個開銷不小的傢伙，先不說建立，光是上下文切換 (Context Switch) 一次就得幾個微秒。所以為了高效地對海量使用者提供服務，必須要讓一個程序能同時處理很多個 tcp 連線才行。現在假設一個程序保持了 10000 條連線，那麼如何發現哪條連線上有資料可讀了、哪條連線可寫了 ？

我們當然可以採用迴圈遍歷的方式來發現 IO 事件，但這種方式太低階了。我們希望有一種更高效的機制，在很多連線中的某條上有 IO 事件發生的時候直接快速把它找出來。其實這個事情 Linux 作業系統 (Operating System) 已經替我們都做好了，它就是我們所熟知的 IO 多路複用機制。 這裡的複用指的就是對程序的複用。

在 Linux 上多路複用方案有   `select`、`poll`、 `epoll`。 它們三個中 `epoll` 的效能表現是最優秀的，能支援的併發量也最大。所以我們今天把   `epoll` 作為要拆解的物件，深入揭秘核心是如何實作多路的 IO 管理的。

為了方便討論，我們舉一個使用了 `epoll` 的簡單範例（只是個例子，實踐中不這麼寫）：

```
int main(){
    listen(lfd, ...);

    cfd1 = accept(...);
    cfd2 = accept(...);
    efd = epoll_create(...);

    epoll_ctl(efd, EPOLL_CTL_ADD, cfd1, ...);
    epoll_ctl(efd, EPOLL_CTL_ADD, cfd2, ...);
    epoll_wait(efd, ...)
}
```

其中和   `epoll` 相關的函式是如下三個：

- `epoll_create`：建立一個 `epoll` 物件
- `epoll_ctl`：向 `epoll` 物件中新增要管理的連線
- `epoll_wait`：等待其管理的連線上的 IO 事件

藉助這個 demo，我們來展開對   `epoll` 原理的深度拆解。相信等你理解了這篇文章以後，你對 `epoll` 的駕馭能力將變得爐火純青！！

友情提示，萬字長文，慎入！！

## 一、  `accept` 建立新 `socket`

我們直接從伺服器端的   `accept` 講起。當 `accept` 之後，程序會建立一個新的 `socket` 出來，專門用於和對應的用戶端通訊，然後把它放到當前程序的開啟檔案列表中。

其中一條連線的 `socket` 核心物件更為具體一點的結構圖如下。

接下來我們來看一下接收連線時   `socket` 核心物件的建立原始碼。`accept` 的系統呼叫 (System Call) 程式碼位於原始檔 `net/socket.c` 下。

```
//file: net/socket.c
SYSCALL_DEFINE4(accept4, int, fd, struct sockaddr __user *, upeer_sockaddr,
        int __user *, upeer_addrlen, int, flags)
{
    struct socket *sock, *newsock;

    //根據 fd 查詢到監聽的 socket
    sock = sockfd_lookup_light(fd, &err, &fput_needed);

    //1.1 申請並初始化新的 socket
    newsock = sock_alloc();
    newsock->type = sock->type;
    newsock->ops = sock->ops;

    //1.2 申請新的 file 物件，並設定到新 socket 上
    newfile = sock_alloc_file(newsock, flags, sock->sk->sk_prot_creator->name);
    ......

    //1.3 接收連線
    err = sock->ops->accept(sock, newsock, sock->file->f_flags);

    //1.4 新增新檔案到當前程序的開啟檔案列表
    fd_install(newfd, newfile);
```

## 1.1 初始化 struct   `socket` 物件

在上述的原始碼中，首先是呼叫 `sock_alloc` 申請一個 struct `socket` 物件出來。然後接著把 `listen` 狀態的   `socket` 物件上的協定操作函式集合 ops 賦值給新的 `socket`。（對於所有的 `AF_INET` 協定族下的 `socket` 來說，它們的 ops 方法都是一樣的，所以這裡可以直接複製過來）

其中 `inet_stream_ops` 的定義如下

```
//file: net/ipv4/af_inet.c
const struct proto_ops inet_stream_ops = {
    ...
    .accept        = inet_accept,
    .listen        = inet_listen,
    .sendmsg       = inet_sendmsg,
    .recvmsg       = inet_recvmsg,
    ...
}
```

## 1.2 為新   `socket` 物件申請 file

struct `socket` 物件中有一個重要的成員 -- file 核心物件指標。這個指標初始化的時候是空的。 在 `accept` 方法裡會呼叫 `sock_alloc_file` 來申請記憶體 (Memory) 並初始化。 然後將新 file 物件設定到 `sock->file` 上。

來看 `sock_alloc_file` 的實作過程：

```
struct file *sock_alloc_file(struct socket *sock, int flags,
const char *dname)
{
struct file *file;
file = alloc_file(&path, FMODE_READ | FMODE_WRITE,
&socket_file_ops);
......
sock->file = file;
}
```

`sock_alloc_file` 又會接著呼叫到 `alloc_file`。注意在 `alloc_file` 方法中，把 `socket_file_ops` 函式集合一併賦到了新 `file->f_op` 裡了。

```
//file: fs/file_table.c
struct file *alloc_file(struct path *path, fmode_t mode,
        const struct file_operations *fop)
{
    struct file *file;
    file->f_op = fop;
    ......
}
```

`socket_file_ops` 的具體定義如下：

```
//file: net/socket.c
static const struct file_operations socket_file_ops = {
    ...
    .aio_read   = sock_aio_read,
    .aio_write  = sock_aio_write,
    .poll     = sock_poll,
    .release  = sock_close,
    ...
};

這裡看到，在   accept 裡建立的新 socket 裡的 file-> f_op-> poll 函式指向的是 sock_poll。 接下來我們會呼叫到它，後面我們再說。
```

> 其實 file 物件內部也有一個  `socket` 指標，指向 `socket` 物件。

## 1.3 接收連線

在 `socket` 核心物件中除了 file 物件指標以外，有一個核心成員 sock。

```
//file: include/linux/net.h
struct socket {
    struct file     *file;
    struct sock     *sk;
}
```

這個 struct sock 資料結構非常大，是   `socket` 的核心核心物件。傳送佇列、接收佇列、等待佇列等核心資料結構都位於此。其定義位置檔案 `include/net/sock.h`，由於太長就不展示了。

在 `accept` 的原始碼中：

```
//file: net/socket.c
SYSCALL_DEFINE4(accept4, ...)
    ...
    //1.3 接收連線
    err = sock->ops->accept(sock, newsock, sock->file->f_flags);
}
```

`sock->ops->accept` 對應的方法是  `inet_accept`。它執行的時候會從握手佇列裡直接取得建立好的 sock。 sock 物件的完整建立過程涉及到三次握手 (Three-Way Handshake)，比較複雜，不展開了說了。我們只看 struct sock 初始化過程中用到的一個函式：

```
void sock_init_data(struct socket *sock, struct sock *sk)
{
sk->sk_wq   =   NULL;
sk->sk_data_ready   =   sock_def_readable;
}
```

在這裡把 sock 物件的   `sk_data_ready` 函式指標設定為 `sock_def_readable`。這個這裡先記住就行了，後面會用到。

## 1.4 新增新檔案到當前程序的開啟檔案列表中

當 file、`socket`、sock 等關鍵核心物件建立完畢以後，剩下要做的一件事情就是把它掛到當前程序的開啟檔案列表中就行了。

```
//file: fs/file.c
void fd_install(unsigned int fd, struct file *file)
{
    __fd_install(current->files, fd, file);
}

void __fd_install(struct files_struct *files, unsigned int fd,
struct file *file)
{
...
fdt = files_fdtable(files);
BUG_ON(fdt->fd[fd] != NULL);
rcu_assign_pointer(fdt->fd[fd], file);
}
```

## 二、  `epoll_create` 實作

在使用者程序呼叫 `epoll_create` 時，核心會建立一個 struct eventpoll 的核心物件。並同樣把它關聯到當前程序的已開啟檔案列表中。

對於 struct eventpoll 物件，更詳細的結構如下（同樣只列出和今天主題相關的成員）。

`epoll_create` 的原始碼相對比較簡單。在 `fs/eventpoll.c` 下

```
// file：fs/eventpoll.c
SYSCALL_DEFINE1(epoll_create1, int, flags)
{
struct eventpoll *ep = NULL;

//建立一個 eventpoll 物件
error = ep_alloc(&ep);
}
```

struct eventpoll 的定義也在這個原始檔中。

```
// file：fs/eventpoll.c
struct eventpoll {

//sys_epoll_wait 用到的等待佇列
wait_queue_head_t wq;

//接收就緒的描述符都會放到這裡
struct list_head rdllist;

//每個 epoll 物件中都有一顆紅黑樹
struct rb_root rbr;

......
}
```

eventpoll 這個結構體中的幾個成員的含義如下：

- wq：等待佇列連結串列。軟中斷 (Softirq) 資料就緒的時候會透過 wq 來找到阻塞在   `epoll` 物件上的使用者程序。
- rbr：一棵紅黑樹。為了支援對海量連線的高效查詢、插入和刪除，eventpoll 內部使用了一棵紅黑樹。透過這棵樹來管理使用者程序下新增進來的所有 `socket` 連線。
- rdllist：就緒的描述符的連結串列。當有的連線就緒的時候，核心會把就緒的連線放到 rdllist 連結串列裡。這樣應用程序只需要判斷連結串列就能找出就緒連線，而不用去遍歷整棵樹。

當然這個結構被申請完之後，需要做一點點的初始化工作，這都在 `ep_alloc` 中完成。

```
//file: fs/eventpoll.c
static int ep_alloc(struct eventpoll **pep)
{
    struct eventpoll *ep;

    //申請 epollevent 記憶體
    ep = kzalloc(sizeof(*ep), GFP_KERNEL);

    //初始化等待佇列頭
    init_waitqueue_head(&ep->wq);

    //初始化就緒列表
    INIT_LIST_HEAD(&ep->rdllist);

    //初始化紅黑樹指標
    ep->rbr = RB_ROOT;

    ......
}
```

說到這裡，這些成員其實只是剛被定義或初始化了，還都沒有被使用。它們會在下面被用到。

## 三、  `epoll_ctl` 新增 `socket`

理解這一步是理解整個   `epoll` 的關鍵。

為了簡單，我們只考慮使用 `EPOLL_CTL_ADD` 新增 `socket`，先忽略刪除和更新。

假設我們現在和用戶端們的多個連線的 `socket` 都建立好了，也建立好了 `epoll` 核心物件。在使用 `epoll_ctl` 註冊每一個   `socket` 的時候，核心會做如下三件事情

- 1. 分配一個紅黑樹節點物件 epitem，
- 2. 新增等待事件到 `socket` 的等待佇列中，其回呼函式是 `ep_poll_callback`
- 3. 將 epitem 插入到 `epoll` 物件的紅黑樹裡

透過   `epoll_ctl` 新增兩個 `socket` 以後，這些核心資料結構最終在程序中的關係圖大致如下：

我們來詳細看看   `socket` 是如何新增到 `epoll` 物件裡的，找到 `epoll_ctl` 的原始碼。

```
// file：fs/eventpoll.c
SYSCALL_DEFINE4(epoll_ctl, int, epfd, int, op, int, fd,
struct epoll_event __user *, event)
{
struct eventpoll *ep;
struct file *file, *tfile;

//根據 epfd 找到 eventpoll 核心物件
file = fget(epfd);
ep = file->private_data;

//根據 socket 控制程式碼號， 找到其 file 核心物件
tfile = fget(fd);

switch (op) {
case EPOLL_CTL_ADD:
if (!epi) {
epds.events |= POLLERR | POLLHUP;
error = ep_insert(ep, &epds, tfile, fd);
} else
error = -EEXIST;
clear_tfile_check_list();
break;
}
```

在   `epoll_ctl` 中首先根據傳入 fd 找到 eventpoll、`socket` 相關的核心物件 。對於 `EPOLL_CTL_ADD` 操作來說，會然後執行到 `ep_insert` 函式。所有的註冊都是在這個函式中完成的。

```
//file: fs/eventpoll.c
static int ep_insert(struct eventpoll *ep,
                struct epoll_event *event,
                struct file *tfile, int fd)
{
    //3.1 分配並初始化 epitem
    //分配一個epi物件
    struct epitem *epi;
    if (!(epi = kmem_cache_alloc(epi_cache, GFP_KERNEL)))
        return -ENOMEM;

    //對分配的epi進行初始化
    //epi->ffd中存了控制程式碼號和struct file物件位址
    INIT_LIST_HEAD(&epi->pwqlist);
    epi->ep = ep;
    ep_set_ffd(&epi->ffd, tfile, fd);

    //3.2 設定 socket 等待佇列
    //定義並初始化 ep_pqueue 物件
    struct ep_pqueue epq;
    epq.epi = epi;
    init_poll_funcptr(&epq.pt, ep_ptable_queue_proc);

    //呼叫 ep_ptable_queue_proc 註冊回呼函式
    //實際注入的函式為 ep_poll_callback
    revents = ep_item_poll(epi, &epq.pt);

    ......
    //3.3 將epi插入到 eventpoll 物件中的紅黑樹中
    ep_rbtree_insert(ep, epi);
    ......
}
```

## 3.1 分配並初始化 epitem

對於每一個   `socket`，呼叫 `epoll_ctl` 的時候，都會為之分配一個 epitem。 該結構的主要資料如下：

```
//file: fs/eventpoll.c
struct epitem {

    //紅黑樹節點
    struct rb_node rbn;

    //socket檔案描述符資訊
    struct epoll_filefd ffd;

    //所歸屬的 eventpoll 物件
    struct eventpoll *ep;

    //等待佇列
    struct list_head pwqlist;
}
```

對 epitem 進行了一些初始化，首先在 `epi->ep` = ep 這行程式碼中將其 ep 指標指向 eventpoll 物件。另外用要新增的   `socket` 的 file、fd 來填充 `epitem->ffd`。

其中使用到的 `ep_set_ffd` 函式如下。

```
static inline void ep_set_ffd(struct epoll_filefd *ffd,
struct file *file, int fd)
{
ffd->file = file;
ffd->fd = fd;
}
```

## 3.2 設定   `socket` 等待佇列

在建立 epitem 並初始化之後，`ep_insert` 中第二件事情就是設定 `socket` 物件上的等待任務佇列。並把函式 `fs/eventpoll.c` 檔案下的   `ep_poll_callback` 設定為資料就緒時候的回呼函式。

這一塊的原始碼稍微有點繞，沒有耐心的話直接跳到下面的加粗字型來看。首先來看 `ep_item_poll`。

```
static inline unsigned int ep_item_poll(struct epitem *epi, poll_table *pt)
{
pt->_key = epi->event.events;

return epi->ffd.file->f_op->poll(epi->ffd.file, pt) & epi->event.events;
}
```

看，這裡呼叫到了   `socket` 下的 file->f_op-> `poll`。透過上面第一節的 `socket` 的結構圖，我們知道這個函式實際上是 `sock_poll`。

/* No kernel lock held - perfect */
```
static unsigned int sock_poll(struct file *file, poll_table *wait)
{
...
return sock->ops->poll(file, sock, wait);
}
```

同樣回看第一節裡的   `socket` 的結構圖，`sock->ops->poll` 其實指向的是 `tcp_poll`。

```
//file: net/ipv4/tcp.c
unsigned int tcp_poll(struct file *file, struct socket *sock, poll_table *wait)
{
    struct sock *sk = sock->sk;

    sock_poll_wait(file, sk_sleep(sk), wait);
}
```

在   `sock_poll_wait` 的第二個參數傳參前，先呼叫了 `sk_sleep` 函式。在這個函式里它取得了 sock 物件下的等待佇列列表頭 `wait_queue_head_t`，待會等待佇列項就插入這裡。這裡稍微注意下，是 `socket` 的等待佇列，不是   `epoll` 物件的。來看 `sk_sleep` 原始碼：

```
//file: include/net/sock.h
static inline wait_queue_head_t *sk_sleep(struct sock *sk)
{
    BUILD_BUG_ON(offsetof(struct socket_wq, wait) != 0);
    return &rcu_dereference_raw(sk->sk_wq)->wait;
}
```

接著真正進入 `sock_poll_wait`。

```
static inline void sock_poll_wait(struct file *filp,
wait_queue_head_t *wait_address, poll_table *p)
{
poll_wait(filp, wait_address, p);
}

static inline void poll_wait(struct file * filp, wait_queue_head_t * wait_address, poll_table *p)
{
if (p && p->_qproc && wait_address)
p->_qproc(filp, wait_address, p);
}
```

這裡的 qproc 是個函式指標，它在前面的   `init_poll_funcptr` 呼叫時被設定成了 `ep_ptable_queue_proc` 函式。

```
static int ep_insert(...)
{
...
init_poll_funcptr(&epq.pt, ep_ptable_queue_proc);
...
}
```

```
//file: include/linux/poll.h
static inline void init_poll_funcptr(poll_table *pt,
    poll_queue_proc qproc)
{
    pt->_qproc = qproc;
    pt->_key   = ~0UL; /* all events enabled */
}
```

敲黑板！！！注意，廢了半天的勁，終於到了重點了！ 在   `ep_ptable_queue_proc` 函式中，新建了一個等待佇列項，並註冊其回呼函式為 `ep_poll_callback` 函式。然後再將這個等待項新增到 `socket` 的等待佇列中。

```
//file: fs/eventpoll.c
static void ep_ptable_queue_proc(struct file *file, wait_queue_head_t *whead,
                 poll_table *pt)
{
    struct eppoll_entry *pwq;
    f (epi->nwait >= 0 && (pwq = kmem_cache_alloc(pwq_cache, GFP_KERNEL))) {
                //初始化回呼方法
                init_waitqueue_func_entry(&pwq->wait, ep_poll_callback);

                //將ep_poll_callback放入socket的等待佇列whead（注意不是epoll的等待佇列）
                add_wait_queue(whead, &pwq->wait);

        }

在前文 [深入理解高效能網路 (Network) 開發路上的絆腳石 - 同步阻塞網路 IO (Synchronous Blocking Network I/O)](https://mp.weixin.qq.com/s/cIcw0S-Q8pBl1-WYN0UwnA) 裡阻塞式的系統呼叫 recvfrom 裡，由於需要在資料就緒的時候喚醒使用者程序，所以等待物件項的 private (這個變數名起的也是醉了) 會設定成當前使用者程序描述符 current。 而我們今天的   socket 是交給 epoll 來管理的，不需要在一個 socket 就緒的時候就喚醒程序，所以這裡的 q->private 沒有什麼卵用就設定成了 NULL。
```

```
//file:include/linux/wait.h
static inline void init_waitqueue_func_entry(
    wait_queue_t *q, wait_queue_func_t func)
{
    q->flags = 0;
    q->private = NULL;

    //ep_poll_callback 註冊到 wait_queue_t物件上
    //有資料到達的時候呼叫 q->func
    q->func = func;
}

如上，等待佇列項中僅僅只設定了回呼函式 q->func 為   ep_poll_callback。在後面的第 5 節資料來啦中我們將看到，軟中斷將資料收到 socket 的接收佇列後，會透過註冊的這個 ep_poll_callback 函式來回呼，進而通知到 epoll 物件。
```

## 3.3 插入紅黑樹

分配完 epitem 物件後，緊接著並把它插入到紅黑樹中。一個插入了一些   `socket` 描述符的 `epoll` 裡的紅黑樹的示意圖如下：

這裡我們再聊聊為什麼要用紅黑樹，很多人說是因為效率高。其實我覺得這個解釋不夠全面，要說查詢效率樹哪能比的上 HASHTABLE。我個人認為覺得更為合理的一個解釋是為了讓   `epoll` 在查詢效率、插入效率、記憶體開銷等等多個方面比較均衡，最後發現最適合這個需求的資料結構是紅黑樹。

## 四、`epoll_wait` 等待接收

`epoll_wait` 做的事情不復雜，當它被呼叫時它觀察 `eventpoll->rdllist` 連結串列裡有沒有資料即可。有資料就回傳，沒有資料就建立一個等待佇列項，將其新增到 eventpoll 的等待佇列上，然後把自己阻塞掉就完事。

> 注意：`epoll_ctl` 新增 `socket` 時也建立了等待佇列項。不同的是這裡的等待佇列項是掛在 `epoll` 物件上的，而前者是掛在 `socket` 物件上的。

其原始碼如下：

```
//file: fs/eventpoll.c
SYSCALL_DEFINE4(epoll_wait, int, epfd, struct epoll_event __user *, events,
        int, maxevents, int, timeout)
{
    ...
    error = ep_poll(ep, events, maxevents, timeout);
}

static int ep_poll(struct eventpoll *ep, struct epoll_event __user *events,
             int maxevents, long timeout)
{
    wait_queue_t wait;
    ......

fetch_events:
    //4.1 判斷就緒佇列上有沒有事件就緒
    if (!ep_events_available(ep)) {

        //4.2 定義等待事件並關聯當前程序
        init_waitqueue_entry(&wait, current);

        //4.3 把新 waitqueue 新增到 epoll->wq 連結串列裡
        __add_wait_queue_exclusive(&ep->wq, &wait);

        for (;;) {
            ...
            //4.4 讓出CPU 主動進入睡眠狀態
            if (!schedule_hrtimeout_range(to, slack, HRTIMER_MODE_ABS))
                timed_out = 1;
            ...
}
```

## 4.1 判斷就緒佇列上有沒有事件就緒

首先呼叫 `ep_events_available` 來判斷就緒連結串列中是否有可處理的事件。

```
//file: fs/eventpoll.c
static inline int ep_events_available(struct eventpoll *ep)
{
    return !list_empty(&ep->rdllist) || ep->ovflist != EP_UNACTIVE_PTR;
}
```

## 4.2 定義等待事件並關聯當前程序

假設確實沒有就緒的連線，那接著會進入   `init_waitqueue_entry` 中定義等待任務，並把 current （當前程序）新增到 waitqueue 上。

> 是的，當沒有 IO 事件的時候，`epoll` 也是會阻塞掉當前程序。這個是合理的，因為沒有事情可做了佔著 CPU 也沒什麼意義。網上的很多文章有個很不好的習慣，討論阻塞、非阻塞 (Non-Blocking) 等概念的時候都不說主語。這會導致你看的雲裡霧裡。拿 `epoll` 來說， `epoll` 本身是阻塞的，但一般會把 `socket` 設定成非阻塞。只有說了主語，這些概念才有意義。

```
//file: include/linux/wait.h
static inline void init_waitqueue_entry(wait_queue_t *q, struct task_struct *p)
{
    q->flags = 0;
    q->private = p;
    q->func = default_wake_function;
}
```

注意這裡的回呼函式名稱是 `default_wake_function`。 後續在第 5 節資料來啦時將會呼叫到該函式。

## 4.3 新增到等待佇列

```
static inline void __add_wait_queue_exclusive(wait_queue_head_t *q,
wait_queue_t *wait)
{
wait->flags |= WQ_FLAG_EXCLUSIVE;
__add_wait_queue(q, wait);
}
```

在這裡，把上一小節定義的等待事件新增到了   `epoll` 物件的等待佇列中。

## 4.4 讓出 CPU 主動進入睡眠狀態

透過 `set_current_state` 把當前程序設定為可打斷。 呼叫 `schedule_hrtimeout_range` 讓出 CPU，主動進入睡眠狀態

```
//file: kernel/hrtimer.c
int __sched schedule_hrtimeout_range(ktime_t *expires,
    unsigned long delta, const enum hrtimer_mode mode)
{
    return schedule_hrtimeout_range_clock(
            expires, delta, mode, CLOCK_MONOTONIC);
}

int __sched schedule_hrtimeout_range_clock(...)
{
    schedule();
    ...
}
```

在 schedule 中選擇下一個程序排程

```
//file: kernel/sched/core.c
static void __sched __schedule(void)
{
    next = pick_next_task(rq);
    ...
    context_switch(rq, prev, next);
}
```

## 五、資料來啦

在前面   `epoll_ctl` 執行的時候，核心為每一個 `socket` 上都新增了一個等待佇列項。 在 `epoll_wait` 執行完的時候，又在 event `poll` 物件上新增了等待佇列元素。在討論資料開始接收之前，我們把這些佇列項的內容再稍微總結一下。

- socket->sock-> `sk_data_ready` 設定的就緒處理函式是 `sock_def_readable`
- 在  `socket` 的等待佇列項中，其回呼函式是 `ep_poll_callback`。另外其 private 沒有用了，指向的是空指標 null。
- 在 eventpoll 的等待佇列項中，回呼函式是  `default_wake_function`。其 private 指向的是等待該事件的使用者程序。

在這一小節裡，我們將看到軟中斷是怎麼樣在資料處理完之後依次進入各個回呼函式，最後通知到使用者程序的。

## 5.1 接收資料到任務佇列

關於軟中斷是怎麼處理網路 (Network) 幀，為了避免篇幅過於臃腫，這裡不再介紹。感興趣的可以看文章 [《圖解 Linux 網路封包 (Network Packet) 接收過程》](https://mp.weixin.qq.com/s/GoYDsfy9m0wRoXi_NCfCmg)。 我們今天直接從 tcp 協定堆疊的處理入口函式 `tcp_v4_rcv` 開始說起。

```
// file: net/ipv4/tcp_ipv4.c
int tcp_v4_rcv(struct sk_buff *skb)
{
    ......
    th = tcp_hdr(skb); //取得tcp header
    iph = ip_hdr(skb); //取得ip header

    //根據資料封包 header 中的 ip、埠資訊查詢到對應的socket
    sk = __inet_lookup_skb(&tcp_hashinfo, skb, th->source, th->dest);
    ......

    //socket 未被使用者鎖定
    if (!sock_owned_by_user(sk)) {
        {
            if (!tcp_prequeue(sk, skb))
                ret = tcp_v4_do_rcv(sk, skb);
        }
    }
}
```

在   `tcp_v4_rcv` 中首先根據收到的網路封包的 header 裡的 source 和 dest 資訊來在本機上查詢對應的 `socket`。找到以後，我們直接進入接收的主體函式 `tcp_v4_do_rcv` 來看。

```
//file: net/ipv4/tcp_ipv4.c
int tcp_v4_do_rcv(struct sock *sk, struct sk_buff *skb)
{
    if (sk->sk_state == TCP_ESTABLISHED) {

        //執行連線狀態下的資料處理
        if (tcp_rcv_established(sk, skb, tcp_hdr(skb), skb->len)) {
            rsk = sk;
            goto reset;
        }
        return 0;
    }

    //其他非 ESTABLISH 狀態的資料封包處理
    ......
}
```

我們假設處理的是 ESTABLISH 狀態下的包，這樣就又進入 `tcp_rcv_established` 函式中進行處理。

```
//file: net/ipv4/tcp_input.c
int tcp_rcv_established(struct sock *sk, struct sk_buff *skb,
            const struct tcphdr *th, unsigned int len)
{
    ......

    //接收資料到佇列中
    eaten = tcp_queue_rcv(sk, skb, tcp_header_len,
                                    &fragstolen);

    //資料 ready，喚醒 socket 上阻塞掉的程序
    sk->sk_data_ready(sk, 0);
```

在   `tcp_rcv_established` 中透過呼叫 `tcp_queue_rcv` 函式中完成了將接收資料放到 `socket` 的接收佇列上。

如下原始碼所示

```
//file: net/ipv4/tcp_input.c
static int __must_check tcp_queue_rcv(struct sock *sk, struct sk_buff *skb, int hdrlen,
            bool *fragstolen)
{
    //把接收到的資料放到 socket 的接收佇列的尾部
    if (!eaten) {
        __skb_queue_tail(&sk->sk_receive_queue, skb);
        skb_set_owner_r(skb, sk);
    }
    return eaten;
}
```

## 5.2 查詢就緒回呼函式

呼叫   `tcp_queue_rcv` 接收完成之後，接著再呼叫 `sk_data_ready` 來喚醒在 `socket` 上等待的使用者程序。 這又是一個函式指標。 回想上面第一節我們在 `accept` 函式建立   `socket` 流程裡提到的 `sock_init_data` 函式，在這個函式里已經把 `sk_data_ready` 設定成 `sock_def_readable` 函式了。它是預設的資料就緒處理函式。

當   `socket` 上資料就緒時候，核心將以 `sock_def_readable` 這個函式為入口，找到 `epoll_ctl` 新增 `socket` 時在其上設定的回呼函式 `ep_poll_callback`。

我們來詳細看下細節：

```
//file: net/core/sock.c
static void sock_def_readable(struct sock *sk, int len)
{
    struct socket_wq *wq;

    rcu_read_lock();
    wq = rcu_dereference(sk->sk_wq);

    //這個名字起的不好，並不是有阻塞的程序，
    //而是判斷等待佇列不為空
    if (wq_has_sleeper(wq))
        //執行等待佇列項上的回呼函式
        wake_up_interruptible_sync_poll(&wq->wait, POLLIN | POLLPRI |
                        POLLRDNORM | POLLRDBAND);
    sk_wake_async(sk, SOCK_WAKE_WAITD, POLL_IN);
    rcu_read_unlock();
}
```

這裡的函式名其實都有迷惑人的地方。

- `wq_has_sleeper`，對於簡單的 recvfrom 系統呼叫來說，確實是判斷是否有程序阻塞。但是對於 `epoll` 下的 `socket` 只是判斷等待佇列不為空，不一定有程序阻塞的。
- `wake_up_interruptible_sync_poll`，只是會進入到 `socket` 等待佇列項上設定的回呼函式，並不一定有喚醒程序的操作。

那接下來就是我們重點看 `wake_up_interruptible_sync_poll`。

我們看一下核心是怎麼找到等待佇列項裡註冊的回呼函式的。

```
//file: include/linux/wait.h
#define wake_up_interruptible_sync_poll(x, m)       \
    __wake_up_sync_key((x), TASK_INTERRUPTIBLE, 1, (void *) (m))
```

```
//file: kernel/sched/core.c
void __wake_up_sync_key(wait_queue_head_t *q, unsigned int mode,
            int nr_exclusive, void *key)
{
    ...
    __wake_up_common(q, mode, nr_exclusive, wake_flags, key);
}
```

接著進入 \_\_wake_up_common

```
static void __wake_up_common(wait_queue_head_t *q, unsigned int mode,
int nr_exclusive, int wake_flags, void *key)
{
wait_queue_t *curr, *next;

list_for_each_entry_safe(curr, next, &q->task_list, task_list) {
unsigned flags = curr->flags;

if (curr->func(curr, mode, wake_flags, key) &&
(flags & WQ_FLAG_EXCLUSIVE) && !--nr_exclusive)
break;
}
}
```

在 \_\_wake_up_common 中，選出等待佇列裡註冊某個元素 curr， 回呼其 `curr->func`。 回憶我們   `ep_insert` 呼叫的時候，把這個 func 設定成 `ep_poll_callback` 了。

## 5.3 執行   `socket` 就緒回呼函式

在上一小節找到了 `socket` 等待佇列項裡註冊的函式 `ep_poll_callback`，軟中斷接著就會呼叫它。

```
//file: fs/eventpoll.c
static int ep_poll_callback(wait_queue_t *wait, unsigned mode, int sync, void *key)
{
    //取得 wait 對應的 epitem
    struct epitem *epi = ep_item_from_wait(wait);

    //取得 epitem 對應的 eventpoll 結構體
    struct eventpoll *ep = epi->ep;

    //1. 將當前epitem 新增到 eventpoll 的就緒佇列中
    list_add_tail(&epi->rdllink, &ep->rdllist);

    //2. 檢視 eventpoll 的等待佇列上是否有在等待
    if (waitqueue_active(&ep->wq))
        wake_up_locked(&ep->wq);
```

在   `ep_poll_callback` 根據等待任務佇列項上的額外的 base 指標可以找到 epitem， 進而也可以找到 eventpoll 物件。

首先它做的第一件事就是把自己的 epitem 新增到 `epoll` 的就緒佇列中。

接著它又會檢視 eventpoll 物件上的等待佇列裡是否有等待項（`epoll_wait` 執行的時候會設定）。

如果沒執行軟中斷的事情就做完了。如果有等待項，那就查詢到等待項裡設定的回呼函式。

呼叫 `wake_up_locked`() => \_\_wake_up_locked() => \_\_wake_up_common。

```
static void __wake_up_common(wait_queue_head_t *q, unsigned int mode,
int nr_exclusive, int wake_flags, void *key)
{
wait_queue_t *curr, *next;

list_for_each_entry_safe(curr, next, &q->task_list, task_list) {
unsigned flags = curr->flags;

if (curr->func(curr, mode, wake_flags, key) &&
(flags & WQ_FLAG_EXCLUSIVE) && !--nr_exclusive)
break;
}
}
```

在 \_\_wake_up_common 裡， 呼叫 `curr->func`。 這裡的 func 是在   `epoll_wait` 是傳入的 `default_wake_function` 函式。

## 5.4 執行   `epoll` 就緒通知

在 `default_wake_function` 中找到等待佇列項裡的程序描述符，然後喚醒之。

原始碼如下：

```
//file:kernel/sched/core.c
int default_wake_function(wait_queue_t *curr, unsigned mode, int wake_flags,
                void *key)
{
    return try_to_wake_up(curr->private, mode, wake_flags);
}

等待佇列項 curr->private 指標是在   epoll 物件上等待而被阻塞掉的程序。
```

將 `epoll_wait` 程序推入可執行佇列，等待核心重新排程程序。然後 `epoll_wait` 對應的這個程序重新執行後，就從 schedule 恢復

當程序醒來後，繼續從 `epoll_wait` 時暫停的程式碼繼續執行。把 rdlist 中就緒的事件回傳給使用者程序

```
//file: fs/eventpoll.c
static int ep_poll(struct eventpoll *ep, struct epoll_event __user *events,
             int maxevents, long timeout)
{

    ......
    __remove_wait_queue(&ep->wq, &wait);

    set_current_state(TASK_RUNNING);
    }
check_events:
    //回傳就緒事件給使用者程序
    ep_send_events(ep, events, maxevents))
}
```

從使用者角度來看，  `epoll_wait` 只是多等了一會兒而已，但執行流程還是順序的。

## 總結

我們來用一幅圖總結一下 `epoll` 的整個工作路程。

其中軟中斷回呼的時候回呼函式也整理一下：
`sock_def_readable`： sock 物件初始化時設定的

=> `ep_poll_callback` : `epoll_ctl` 時新增到 `socket` 上的
=> `default_wake_function`: `epoll_wait` 是設定到 `epoll` 上的

總結下，  `epoll` 相關的函式里核心執行環境分兩部分：

- 使用者程序核心態 (Kernel Mode)。進行呼叫 `epoll_wait` 等函式時會將程序陷入核心態來執行。這部分程式碼負責檢視接收佇列，以及負責把當前程序阻塞掉，讓出 CPU。
- 硬軟中斷上下文。在這些元件中，將封包從網路卡 (Network Card) 接收過來進行處理，然後放到   `socket` 的接收佇列。對於 `epoll` 來說，再找到 `socket` 關聯的 epitem，並把它新增到 `epoll` 物件的就緒連結串列中。 這個時候再捎帶檢查一下   `epoll` 上是否有被阻塞的程序，如果有喚醒之。

為了介紹到每個細節，本文涉及到的流程比較多，把阻塞都介紹進來了。

但其實在實踐中，只要工作足夠的多，`epoll_wait` 根本都不會讓程序阻塞。使用者程序會一直工作，一直工作，直到 `epoll_wait` 裡實在沒工作可乾的時候才主動讓出 CPU。這就是 `epoll` 高效的地方所在！

包括本文在內，飛哥總共用三篇文章分析了一件事情，一個網路封包是如何從網路卡達到你的使用者程序裡的。另外兩篇如下：

- [圖解 | Linux 網路封包接收過程](https://mp.weixin.qq.com/s/GoYDsfy9m0wRoXi_NCfCmg)
- [圖解 | 深入理解高效能網路開發路上的絆腳石 - 同步阻塞網路 IO (Synchronous Blocking Network I/O)](https://mp.weixin.qq.com/s/cIcw0S-Q8pBl1-WYN0UwnA)

恭喜你沒被核心原始碼勸退，一直能堅持到了現在。趕快給先自己鼓個掌，晚飯去加個雞腿！

當然網路程式設計剩下還有一些概念我們沒有講到，例如 Reactor 和 Proactor 等。不過相對核心來講，這些使用者層的技術相對就很簡單了。這些只是在討論當多程序一起配合工作時誰負責檢視 IO 事件、誰該負責計算、誰負責傳送和接收，僅僅是使用者程序的不同分工模式罷了。
