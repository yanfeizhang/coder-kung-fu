---
layout: default
title: "Docker 容器 (Container) 裡程序 (Process) 的 pid 是如何申請出來的？"
---

# Docker 容器 (Container) 裡程序 (Process) 的 pid 是如何申請出來的？

> 原文標題：Docker容器里进程的 pid 是如何申请出来的？

> 原文連結：https://mp.weixin.qq.com/s/LDu6s1eZw6_xEwfa6pMM-A

---

大家好，我是飛哥！

如果大家有過在容器中執行 ps 命令的經驗，都會知道在容器中的程序的 pid 一般是比較小的。例如下面我的這個例子。

```
# ps -ef
PID   USER     TIME  COMMAND
1 root      0:00 ./demo-ie
13 root      0:00 /bin/bash
21 root      0:00 ps -ef
```

不知道大家是否和我一樣好奇容器程序中的 pid 是如何申請出來的？和宿主機中申請 pid 有什麼不同？核心又是如何顯示容器中的程序號的？

前面我們在 [《Linux 程序是如何建立出來的？》](https://mp.weixin.qq.com/s/ftrSkVvOr6s5t0h4oq4I2w) 中介紹了程序的建立過程。事實上程序的 pid 命名空間 (Namespace)、pid 也都是在這個過程中申請的。我今天就來帶大家深入理解一下 docker 核心之一 pid 命名空間的工作原理。

## 一、Linux 的預設 pid 命名空間

前面的文章 [《Linux 程序是如何建立出來的？》](https://mp.weixin.qq.com/s/ftrSkVvOr6s5t0h4oq4I2w) 中我們提到了程序的命名空間成員 nsproxy。

```
//file:include/linux/sched.h
struct task_struct {
    ...
    /* namespaces */
    struct nsproxy *nsproxy;
}
```

Linux 在啟動的時候會有一套預設的命名空間，定義在 `kernel/nsproxy.c` 檔案下。

```
//file:kernel/nsproxy.c
struct nsproxy init_nsproxy = {
    .count    = ATOMIC_INIT(1),
    .uts_ns    = &init_uts_ns,
    .ipc_ns    = &init_ipc_ns,
    .mnt_ns    = NULL,
    .pid_ns    = &init_pid_ns,
    .net_ns    = &init_net,
};
```

其中預設的 pid 命名空間是   `init_pid_ns`，它定義在 `kernel/pid.c` 下。

```
//file:kernel/pid.c
struct pid_namespace init_pid_ns = {
    .kref = {
        .refcount       = ATOMIC_INIT(2),
    },
    .pidmap = {
        [ 0 ... PIDMAP_ENTRIES-1] = { ATOMIC_INIT(BITS_PER_PAGE), NULL }
    },
    .last_pid = 0,
    .level = 0,
    .child_reaper = &init_task,
    .user_ns = &init_user_ns,
    .proc_inum = PROC_PID_INIT_INO,
};
```

在 pid 命名空間裡我覺得最需要關注的是兩個欄位。一個是 level 表示當前 pid 命名空間的層級。另一個是 pidmap，這是一個 bitmap，一個 bit 如果為 1，就表示當前序號的 pid 已經分配出去了。

另外預設命名空間的 level 初始化是 0。這是一個表示樹的層次結構的節點。如果有多個命名空間建立出來，它們之間會組成一棵樹。level 表示樹在第幾層。根節點的 level 是 0.

`INIT_TASK` 0 號程序，也叫 idle 程序，它固定使用這個預設的 `init_nsproxy`。

```
//file:include/linux/init_task.h
#define INIT_TASK(tsk)    \
{
    .state        = 0,                        \
    .stack        = &init_thread_info,                \
    .usage        = ATOMIC_INIT(2),                \
    .flags        = PF_KTHREAD,                    \
    .prio        = MAX_PRIO-20,                    \
    .static_prio    = MAX_PRIO-20,                    \
    .normal_prio    = MAX_PRIO-20,                    \
    ...
    .nsproxy    = &init_nsproxy,                \
    ......
}
```

所有程序都是一個派生一個的方式產生出來的。如果不指定命名空間，所有程序使用的都是使用預設的命名空間。

## 二、Linux 新 pid 命名空間建立

在這裡，我們假設我們建立程序時指定了   `CLONE_NEWPID` 要建立一個獨立的 pid 命名空間出來。

在 [《Linux 程序是如何建立出來的？》](https://mp.weixin.qq.com/s/ftrSkVvOr6s5t0h4oq4I2w) 一文中我們已經瞭解了程序的建立過程。整個建立過程的核心是在於 `copy_process` 函式。

在這個函式中會申請和複製程序的位址空間、開啟檔案列表、檔案目錄等關鍵資訊，另外就是 pid 命名空間的建立也是在這裡完成的。

```
//file:kernel/fork.c
static struct task_struct *copy_process(...)
{
    ...
    //2.1 複製程序的命名空間 nsproxy
    retval = copy_namespaces(clone_flags, p);

    //2.2 申請 pid
    pid = alloc_pid(p->nsproxy->pid_ns);

    //2.3 記錄 pid
    p->pid = pid_nr(pid);
    p->tgid = p->pid;
    attach_pid(p, PIDTYPE_PID, pid);
    ...
}
```

## 2.1 建立程序時構造新命名空間

在上面的   `copy_process` 程式碼中我們看到對 `copy_namespaces` 函式的呼叫。命名空間就是在這個函式中操作的。

```
//file:kernel/nsproxy.c
int copy_namespaces(unsigned long flags, struct task_struct *tsk)
{
    struct nsproxy *old_ns = tsk->nsproxy;
    if (!(flags & (CLONE_NEWNS | CLONE_NEWUTS | CLONE_NEWIPC |
                CLONE_NEWPID | CLONE_NEWNET)))
        return 0;

    new_ns = create_new_namespaces(flags, tsk, user_ns, tsk->fs);
    tsk->nsproxy = new_ns;
    ...
}
```

如果在建立程序時候沒有傳入   `CLONE_NEWNS` 等幾個 flag，還是會複用之前的預設命名空間。這幾個 flag 的含義如下。

- `CLONE_NEWPID`: 是否建立新的程序編號命名空間，以便與宿主機的程序 PID 進行隔離
- `CLONE_NEWNS`: 是否建立新的掛載點（檔案系統 (File System)）命名空間，以便隔離檔案系統和掛載點
- `CLONE_NEWNET`: 是否建立新的網路 (Network) 命名空間，以便隔離網路卡 (Network Card)、IP、埠、路由表等網路資源
- `CLONE_NEWUTS`: 是否建立新的主機名與域名命名空間，以便在網路中獨立標識自己
- `CLONE_NEWIPC`: 是否建立新的 IPC 命名空間，以便隔離訊號量、訊息佇列和共享記憶體 (Memory)
- `CLONE_NEWUSER`: 用來隔離使用者和使用者組的。

因為我們本節開頭假設傳入了 `CLONE_NEWPID` 標記。所以會進入到 `create_new_namespaces` 中來申請新的命名空間。

```
//file:kernel/nsproxy.c
static struct nsproxy *create_new_namespaces(unsigned long flags,
    struct task_struct *tsk, struct user_namespace *user_ns,
    struct fs_struct *new_fs)
{
    //申請新的 nsproxy
    struct nsproxy *new_nsp;
    new_nsp = create_nsproxy();
    ......
    //複製或建立 PID 命名空間
    new_nsp->pid_ns = copy_pid_ns(flags, user_ns, tsk->nsproxy->pid_ns);
}
```

`create_new_namespaces` 中會呼叫 `copy_pid_ns` 來完成實際的建立，真正的建立過程是在 `create_pid_namespace` 中完成的。

```
//file:kernel/pid_namespace.c
static struct pid_namespace *create_pid_namespace(...)
{
    struct pid_namespace *ns;

    //新 pid namespace level + 1
    unsigned int level = parent_pid_ns->level + 1;

    //申請記憶體
    ns = kmem_cache_zalloc(pid_ns_cachep, GFP_KERNEL);
    ns->pidmap[0].page = kzalloc(PAGE_SIZE, GFP_KERNEL);
    ns->pid_cachep = create_pid_cachep(level + 1);

    //設定新命名空間 level
    ns->level = level;

    //新命名空間和舊命名空間組成一棵樹
    ns->parent = get_pid_ns(parent_pid_ns);

    //初始化 pidmap
    set_bit(0, ns->pidmap[0].page);
    atomic_set(&ns->pidmap[0].nr_free, BITS_PER_PAGE - 1);

    for (i = 1; i < PIDMAP_ENTRIES; i++)
        atomic_set(&ns->pidmap[i].nr_free, BITS_PER_PAGE);

    return ns;
}
```

在   `create_pid_namespace` 真正申請了新的 pid 命名空間，為它的 pidmap 申請了記憶體，也進行了初始化。

另外還有一點比較重要的是新命名空間和舊命名空間透過 parent、level 等欄位組成了一棵樹。其中 parent 指向了上一級命名空間，自己的 level 用來表示層次，設定成了上一級 level + 1。

其最終的效果就是新程序擁有了新的 pid namespace，並且這個新 pid namespace 和父 pidnamespace 串聯了起來，效果如下圖。

## 2.2 申請程序 id

建立完命名空間後，在   `copy_process` 中接下來接著就是呼叫 `alloc_pid` 來分配 pid（Docker 容器就是這麼做的）。

```
//file:kernel/fork.c
static struct task_struct *copy_process(...)
{
    ...
    //2.1 複製程序的命名空間 nsproxy
    retval = copy_namespaces(clone_flags, p);
    ...

    //2.2 申請 pid
    pid = alloc_pid(p->nsproxy->pid_ns);
    ...
}
```

注意傳入的參數是 `p->nsproxy->pid_ns`。前面程序建立了新的 pid namespace，這個時候該命名空間就是 level 為 1 的新   `pid_ns`。我們繼續來看 `alloc_pid` 具體 pid 的過程。

```
//file:kernel/pid.c
struct pid *alloc_pid(struct pid_namespace *ns)
{
    //申請 pid 核心物件
    pid = kmem_cache_alloc(ns->pid_cachep, GFP_KERNEL);

    //呼叫到alloc_pidmap來分配一個空閒的pid
    tmp = ns;
    pid->level = ns->level;
    for (i = ns->level; i >= 0; i--) {
        nr = alloc_pidmap(tmp);
        if (nr < 0)
            goto out_free;

        pid->numbers[i].nr = nr;
        pid->numbers[i].ns = tmp;
        tmp = tmp->parent;
    }
    ...
    return pid;
}
```

在上面的程式碼中要注意兩個細節。

- 我們平時說的 pid 在核心中並不是一個簡單的整數型別，而是一個小結構體來表示的（struct pid）。
- 申請 pid 並不是申請了一個，而是使用了一個 for 迴圈申請多個出來

之所以要申請多個，是因為對於容器裡的程序來說，並不是在自己當前的命名空間申請就完事了，還要到其父命名空間中也申請一個。我們把 for 迴圈的工作工程用下圖表示一下。

首先到當前層次的命名空間申請一個 pid 出來，然後順著命名空間的父節點，每一層也都要申請一個，並都記錄到 `pid->numbers` 陣列中。

這裡多說一下，如果 pid 申請失敗的話，會報 -ENOMEM 錯誤，在使用者層看起來就是「  `fork`:無法分配記憶體」，實際是由 pid 不足引起的。這個問題我在 [《明明還有大量記憶體，為什麼報錯「無法分配記憶體」？》](https://mp.weixin.qq.com/s/Jo8KEzfb1OXShrb3PT4U_A) 提到過。

## 2.3 設定整數格式 pid

當申請並構造完 pid 後，將其設定在 `task_struct` 上，記錄起來。

```
//file:kernel/fork.c
static struct task_struct *copy_process(...)
{
    ...
    //2.2 申請 pid
    pid = alloc_pid(p->nsproxy->pid_ns);

    //2.3 記錄 pid
    p->pid = pid_nr(pid);
    p->tgid = p->pid;
    attach_pid(p, PIDTYPE_PID, pid);
    ...
}
```

其中   `pid_nr` 是取得的根 pid 命名空間下的 pid 編號，參見 `pid_nr` 原始碼。

```
//file:include/linux/pid.h
static inline pid_t pid_nr(struct pid *pid)
{
    pid_t nr = 0;
    if (pid)
        nr = pid->numbers[0].nr;
    return nr;
}
```

然後再呼叫   `attach_pid` 是把申請到的 pid 結構掛到自己的 pids[`PIDTYPE_PID`] 連結串列裡了。

```
//file:kernel/pid.c
void attach_pid(struct task_struct *task, enum pid_type type,
        struct pid *pid)
{
    ...
    link = &task->pids[type];
    link->pid = pid;
    hlist_add_head_rcu(&link->node, &pid->tasks[type]);
}
```

```
//file:include/linux/sched.h
struct task_struct {
    ......
    struct pid_link pids[PIDTYPE_MAX];
}
```

## 三、容器程序 pid 檢視

pid 已經申請好了，那在容器中是如何檢視當前層次的程序號的呢？例如我們在容器中看到的 demo-ie 程序的 id 就是 1。

```
# ps -ef
PID   USER     TIME  COMMAND
1 root      0:00 ./demo-ie
...
```

核心提供了個函式用來檢視程序在當前某個命名空間的命名號。

```
//file:kernel/pid.c
pid_t pid_vnr(struct pid *pid)
{
    return pid_nr_ns(pid, task_active_pid_ns(current));
}
```

其中在容器中檢視程序 pid 使用的是   `pid_vnr`，`pid_vnr` 呼叫 `pid_nr_ns` 來檢視程序在特定命名空間裡的程序號。

函式 `pid_nr_ns` 接收連個參數

- 第一個參數是程序裡記錄的 pid 物件（儲存有在各個層次申請到的 pid 號）
- 第二個參數是指定的 pid 命名空間取得）。

當具備這兩個參數後，就可以根據 pid 命名空間裡記錄的層次 level 取得容器程序的當前 pid 了

```
//file:kernel/pid.c
pid_t pid_nr_ns(struct pid *pid, struct pid_namespace *ns)
{
    struct upid *upid;
    pid_t nr = 0;

    if (pid && ns->level <= pid->level) {
        upid = &pid->numbers[ns->level];
        if (upid->ns == ns)
            nr = upid->nr;
    }
    return nr;
}
```

在 `pid_nr_ns` 中透過判斷 level 就把容器 pid 整數值查出來了。

## 四、總結

最後，舉個例子，假如有一個程序在 level 0 級別的 pid 命名空間裡申請到的程序號是 1256，在 level 1 容器 pid 命名空間裡申請到的程序號是 5。那麼這個程序以及其 pid 在記憶體中的形式是下圖這個樣子的。

那麼容器在檢視程序的 pid 號的時候，傳入容器的 pid 命名空間，就可以將該程序在容器中的 pid 號 5 給列印出來了！！
