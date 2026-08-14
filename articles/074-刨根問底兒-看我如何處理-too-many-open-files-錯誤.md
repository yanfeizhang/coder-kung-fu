---
layout: default
title: "追根究柢，看我如何處理 Too many open files 錯誤！"
---

# 追根究柢，看我如何處理 Too many open files 錯誤！

> 原文標題：刨根问底儿，看我如何处理 Too many open files 错误！

> 原文連結：https://mp.weixin.qq.com/s/GBn94vdL4xUL80WYrGdUWQ

---

如果你的專案中支援高併發，或者是測試過比較多的併發連線。那麼相信你一定遇到過「Too many open files」這個錯誤。

這個錯誤的出現其實是正常的，因為每開啟一個檔案（包括   `socket`），都需要消耗一定的記憶體 (Memory) 資源。為了避免個別程序 (Process) 不受控制地開啟了過多的檔案而讓整個伺服器崩潰，Linux 對開啟的檔案描述符 (File Descriptor) 數量有限制。

但是解決這個錯誤「怪異」的地方在於，竟然需要修改三個參數：fs.nr_open、nofile（其實 nofile 還分 soft 和 hard） 和 fs.file-max。這幾個參數裡有的是程序級的、有的是系統級的、有的是使用者程序級的，說一遍都覺得好亂。而且另外這幾個參數還有依賴關係，著實比較複雜。

不知道你，反正飛哥我是根本記不住哪個是哪個。每次遇到這種問題，還是都得再繼續 Google 一遍。但由於複雜性，所以其實網上的很多貼文裡也都並沒有真正釐清。如果照搜尋出來的文章改，稍有不慎就會踩雷，把機器造成問題。

我在測試最大 TCP 連線數的時候就踩過兩次坑。

第一次是當時開了二十個子程序，每個子程序開啟了五萬個併發連線興高采烈準備測試百萬併發。 結果倒黴催地忘了改 file-max 了。實驗剛開始沒多大一會兒就開始報錯「Too many open files」。但問題是這個時候更悲催的是發現所有的命令包括 ps、kill 也同時無法使用了。因為它們也都需要開啟檔案才能工作。後來沒辦法，重啟系統解決的。

另外一次是重啟機器完了之後發現無法 ssh 登入了。後來找運維工程部的同學報障以後才算是修復。最終發現是因為 hard nofile 比 fs.  `nr_open` 高了，直接導致無法登入。（其實我把 fs.nr_open 加大過，但是用的是 echo 命令修改的。系統一重啟，還原了）。

## 一、找到原始碼

對於這三個傢伙，我真的是無法言語更多了。所以我下定了決心，一定要把它們徹底釐清。怎麼做？ 那沒有比把它的原始碼扒出來能看的更準確了。我們就拿建立   `socket` 來舉例，首先找到 `socket` 系統呼叫 (System Call) 的入口

```
//file: net/socket.c
SYSCALL_DEFINE3(socket, int, family, int, type, int, protocol)
{
    retval = sock_map_fd(sock, flags & (O_CLOEXEC | O_NONBLOCK));
    if (retval < 0)
        goto out_release;
}
```

我們看到   `socket` 呼叫 `sock_map_fd` 來建立相關核心物件。接著我們再進入 `sock_map_fd` 瞧瞧。

```
//file: net/socket.c
static int sock_map_fd(struct socket *sock, int flags)
{
    struct file *newfile;

    //在這裡會判斷開啟檔案數是否超過 soft nofile 和 fs.nr_open
    //取得 fd 控制程式碼號
    int fd = get_unused_fd_flags(flags);
    if (unlikely(fd < 0))
        return fd;

    //在這裡會判斷開啟檔案數是否超過 fs.file-max
    //建立 sock_alloc_file物件
    newfile = sock_alloc_file(sock, flags, NULL);
    if (likely(!IS_ERR(newfile))) {
        fd_install(fd, newfile);
        return fd;
    }

    put_unused_fd(fd);
    return PTR_ERR(newfile);
}
```

為什麼建立一個   `socket` 又要申請 fd，又要申請 `sock_alloc_file` 呢？我們看一個程序開啟檔案時的核心資料結構圖就明白了

結合上圖，就能輕鬆理解這兩個函式的作用

- `get_unused_fd_flags`：申請 fd，這只是一個在找一個可用的陣列下標而已
- `sock_alloc_file`：申請真正的 file 核心物件

## 二、找到程序級限制 nofile 和 fs.  `nr_open`

接下來，我們再回到最大檔案數量的判斷上。這裡我直接把結論丟擲來。`get_unused_fd_flags` 中判斷了 nofile、和 fs. `nr_open`。如果超過了這兩個參數，就會報錯。 請看！

```
//file: fs/file.c
int get_unused_fd_flags(unsigned flags)
{
    // RLIMIT_NOFILE 是 limits.conf 中設定的 nofile
    return __alloc_fd(
        current->files,
        0,
        rlimit(RLIMIT_NOFILE),
        flags
    );
}
```

在   `get_unused_fd_flags` 中，呼叫了 rlimit(`RLIMIT_NOFILE`)。這個是讀取的 limits.conf 中設定的 soft nofile，程式碼如下：

```
//file: include/linux/sched.h
static inline unsigned long task_rlimit(const struct task_struct *tsk,
        unsigned int limit)
{
    return ACCESS_ONCE(tsk->signal->rlim[limit].rlim_cur);
}
```

透過當前程序描述存取到 rlim[  `RLIMIT_NOFILE`]，這個物件的 `rlim_cur` 是 soft nofile（`rlim_max` 對應 hard nofile）。

緊接著讓我們進入 \_\_alloc_fd() 中來

```
//file: include/uapi/asm-generic/errno-base.h
#define    EMFILE        24    /* Too many open files */

int __alloc_fd(struct files_struct *files,
unsigned start, unsigned end, unsigned flags)
{
...
error = -EMFILE;

//看要分配的檔案號是否超過 end（limits.conf 中的 nofile）
if (fd >= end)
goto out;

error = expand_files(files, fd);
if (error < 0)
goto out;
...
}
```

在\_\_alloc_fd() 中會判斷要分配的控制程式碼號是不是超過了   limits.conf 中 nofile 的限制。fd 是當前程序相關的，是一個從 0 開始的整數。如果超限，就報錯 EMFILE （Too many open files）。

> 這裡注意個小細節，那就是程序裡的 fd 是一個從 0 開始的整數。只要確保分配出去的 fd 編號不超過 limits.conf 中 nofile，就能保證該程序開啟的檔案總數不會超過這個數。

接著我們看到呼叫又會進入 `expand_files`：

```
static int expand_files(struct files_struct *files, int nr)
{
//2. 判斷開啟檔案數是否超過 fs.nr_open
if (nr >= sysctl_nr_open)
return -EMFILE;
}
```

在   `expand_files` 我們看到，又到 nr (就是 fd 編號) 和 fs.nr_open 相比較了。超過這個限制，回傳錯誤 EMFILE （Too many open files）。

由上可見，無論是和 fs.  `nr_open`，還是和 soft nofile 比較，都用的是當前程序的檔案描述符序號在比較的，所以這兩個參數都是程序級別的。

有意思的是和這兩個參數的比較幾乎是前後腳進行的，所以它兩的作用也基本一樣。Linux 之所以分兩個參數來控制，那是因為 fs.nr_open 是系統全域性的，而 soft nofile 則可以分使用者來分別控制。

所以，現在我們可以得出第一個結論。

結論 1：soft nofile 和 fs.  `nr_open` 的作用一樣，它兩都是限制的單個程序的最大檔案數量。區別是 soft nofile 可以按使用者來設定，而 fs.nr_open 所有使用者只能配一個。

## 三、找到系統級限制 fs.  `nr_open`

我們在回過頭來看 `sock_map_fd` 中呼叫的另外一個函式 `sock_alloc_file`，在這個函式里我們發現它會和 fs.file-max 這個系統參數來比較。用什麼比的呢？

```
//file: fs/file_table.c
struct file *sock_alloc_file(struct socket *sock, int flags, const char *dname)
{
    file = alloc_file(&path, FMODE_READ | FMODE_WRITE,
      &socket_file_ops);
}

struct file *alloc_file(struct path *path, fmode_t mode,
        const struct file_operations *fop)
{
    file = get_empty_filp();
    ...
}

struct file *get_empty_filp(void)
{
```

```
//files_stat.max_files 就是 fs.file-max 參數
if (get_nr_files() >= files_stat.max_files
```

&& !capable(`CAP_SYS_ADMIN`) //注意這裡 root 帳號並不受限制
) {
}
}

可見是用   `get_nr_files`() 來和 fs.file-max 來比較的。根據該函式的註釋我們能看到它是當前系統開啟的檔案描述符總量。如下：

/*

* Return the total number of open files in the system
* /
```
static long get_nr_files(void)
{
...
```

另外注意下 !capable(`CAP_SYS_ADMIN`) 這行。看完這句，我才恍然大悟，原來  file-max 這個參數只限制非 root 使用者。開篇中我提到的檔案開啟過多時無法使用 ps，kill 等命令，是因為我用的非 root 帳號操作的。哎，下次再遇到這種檔案直接用 root 去 kill 就行了。之前竟然丟臉地採用了重啟機器大法。。

所以現在我們可以得出另一個結論了。
結論 2： fs.file-max: 整個系統上可開啟的最大檔案數，但不限制 root 使用者

## 總結一下

我們總結一下，其實在 Linux 上能開啟多少個檔案，限制有兩種：

- 第一種，程序級別的，限制的是單個程序上可開啟的檔案數。具體參數是 soft nofile 和 fs. `nr_open`。 它們兩個的區別是 soft nofile 可以不同使用者設定不同的值。而 fs.nr_open 在一臺 Linux 上只能配一次。
- 第二種，系統級別的，整個系統上可開啟的最大檔案數，具體參數是  fs.file-max。但是這個參數不限制 root 使用者。

另外這幾個參數之間還有耦合關係，因此還要注意以下三點：

- 1、如果你想加大 soft nofile, 那麼 hard nofile 也需要一起調整。因為如果 hard nofile 設定的低， 你的 soft nofile 設定的再高都沒用，實際生效的值會按二者裡最低的來。
- 2、如果你加大了 hard nofile，那麼  fs.nr_open 也都需要跟著一起調整。如果不小心把 hard nofile 設定的比 fs.nr_open 大了，後果比較嚴重。會導致該使用者無法登入。如果設定的是 * 的話，那麼所有的使用者都無法登入。
- 3、還要注意如果你加大了  fs.nr_open，但是用的是 echo "xx" > `../fs/nr_open` 的方式，剛改完你可能覺得沒問題。只要機器一重啟你的 fs. `nr_open` 設定就會失效，還是會無法登入。

假如你想讓你的程序可以開啟 100 萬個檔案描述符，我覺得比較穩妥點的修改方法是乾脆都直接用 conf 檔案的方式來改。這樣比較統一，也比較安全。

```
# vi /etc/sysctl.conf
fs.nr_open=1100000  //要比 hard nofile 大一點
fs.file-max=1100000 //多留點 buffer
```

```
# sysctl -p
```

```
# vi /etc/security/limits.conf
```

* soft  nofile  1000000
* hard  nofile  1000000

透過這種方式修改，你就可以繞過飛哥踩過的坑了。
