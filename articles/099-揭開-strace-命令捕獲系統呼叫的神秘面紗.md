---
layout: default
title: "揭開 `strace` 命令捕獲系統呼叫 (System Call) 的神秘面紗"
---

# 揭開 `strace` 命令捕獲系統呼叫 (System Call) 的神秘面紗

> 原文標題：揭开 strace 命令捕获系统调用的神秘面纱

> 原文連結：https://mp.weixin.qq.com/s/Iz4YPPmyjGrXVnZ8eSap2Q

---

大家好，我是飛哥！

在效能觀測領域，  `strace` 命令是一個雖然很古老，但很常用的命令。使用它我們可以非常方便地觀察某個程序 (Process) 正在執行什麼系統呼叫。

這個命令的使用方式也很簡單，想觀察哪個程序，直接將其 pid 作為參數傳給 `strace` 命令即可。

```
# strace -p {pid}
read(3, "\177ELF\2\1\1\0\0\0\0\0\0\0\0\0\3\0>\0\1\0\0\0@k\0\0\0\0\0\0"..., 832) = 832
read(3, "\177ELF\2\1\1\3\0\0\0\0\0\0\0\0\3\0>\0\1\0\0\0\260A\2\0\0\0\0\0"..., 832) = 832
write(1, "anycast6   dev_snmp6\t if_inet6\ti"..., 137anycast6   dev_snmp6     if_inet6    ip6_mr_vif        ip_mr_vif           mcfilter   nf_conntrack           ptype  rt6_stats  sockstat      tcp6     unix
) = 137
......
```

然而我們都知道，正常來講作業系統 (Operating System) 中的各個程序之間是互相隔離的。那麼   `strace` 命令是如何做到能取得其他程序執行的系統呼叫資訊的呢，我們今天就來揭開這個謎底。

## 一、手工實作一個 `strace`

要想理解清楚   `strace` 命令原理，我想最有效的辦法是我們自己親手寫一個簡單程式來模擬 `strace` 的工作過程。

為了方便大家理解，我這裡只把這個程式的核心邏輯列出來。完整的程式原始碼請大家檢視 [`strace` 配套原始碼](https://github.com/yanfeizhang/coder-kung-fu/blob/main/tests/cpu/test11/main.c)

```
int main(int argc, char *argv[]) {

    // 1.attach 到 pid 指定的目標程序上
    ptrace(PTRACE_ATTACH, pid, NULL, NULL)

    while (1) {
        // 2.等待目標程序的 PTRACE_SYSCALL
        // 2.1 指定要捕獲目標程序的 PTRACE_SYSCALL
        ptrace(PTRACE_SYSCALL, pid, NULL, NULL)
        // 2.2 當目標程序有 SYSCALL 發生時醒來處理
        waitpid(pid, &status, 0)

        // 3.讀取並解析系統呼叫
        // 3.1 讀取目標程序正在執行的系統呼叫號
        syscall_number = ptrace(PTRACE_PEEKUSER, pid, 8 * ORIG_RAX, NULL);  、
        // 3.2 將系統呼叫號轉為系統呼叫名稱
        switch (syscall_number) {
            case 5: syscall_name = "read"; break;
            case 6: syscall_name = "write"; break;
            case 10: syscall_name = "open"; break;
            case 11: syscall_name = "close"; break;
            ......
            default: syscall_name = "unknown"; break;
        }
        // 3.3 列印系統呼叫名稱
        printf("Syscall: %s (number: %ld)\n", syscall_name, syscall_number);
    }
}
```

透過上面二十多行核心程式碼，我們就實作了一個模擬   `strace` 命令跟蹤系統呼叫功能的簡易程式。

在這個程式中，主要是透過三塊邏輯來實作：

第一，attach 到目標程序。

在 C 標準庫中有一個 ptrace 函式, 該函式是一個系統呼叫方法。透過指定它的第一個參數為 `PTRACE_ATTACH` pid，這樣就可以建立當前程式和目標程序的跟蹤關係了。要注意的是，這一步必須得有 root 許可權才可以。

第二，將自己註冊為目標程序的 syscall 偵錯程式。

這次還是使用 ptrace 函式。但第一個參數設為   `PTRACE_SYSCALL`，這樣就在告訴核心要將自己註冊為目標程序的 syscall 偵錯程式。每當目標程序發生系統呼叫的時候，都會通知當前程式。

第三，讀取目標程序系統呼叫名。

這裡涉及到一個基礎知識，Linux 核心 (Linux Kernel) 在幫使用者程序執行系統呼叫的時候，會將系統呼叫號儲存到 `ORIG_RAX` 暫存器中。

ptrace 函式第一個參數設為   `PTRACE_PEEKUSER`，這是在告訴核心幫忙讀取目標程序的使用者區域的資料。第三個參數指定要讀取目標程序的 `ORIG_RAX` 暫存器中儲存的系統呼叫號。在 / `usr/include/x86_64-linux-gnu`/ `asm/unistd_64.h` 中可以檢視到所有的系統呼叫號資訊。將其轉為系統呼叫名後輸出即可。

整個程式是一個迴圈，每當目標程式有系統呼叫發生的時候，都會通知到當前程式。當前程式再將其正在執行的系統呼叫資訊輸出出來。這樣就實作了對目標程序實時行為的動態跟蹤。

接下來我們分三個部分，從核心視角深入地探究一下底層工作原理。

## 二、attach 到目標程序

要想實作對目標程式的跟蹤，首先第一步準備工作便是呼叫 ptrace 把自己 attach 到目標程序上。

```
int main(int argc, char *argv[]) {
    // 1.attach 到 pid 指定的目標程序上
    ptrace(PTRACE_ATTACH, pid, NULL, NULL)
    ...
}
```

我們來看下這個所謂的 attach ，在 Linux 內部究竟是幹了點什麼。找來 ptrace 系統呼叫的原始碼。

```
//file:kernel/ptrace.c
SYSCALL_DEFINE4(ptrace, long, request, long, pid, unsigned long, addr, ...)
{
    // 1. 根據 pid 查詢目標程序核心物件
    struct task_struct *child;
    child = find_get_task_by_vpid(pid);
    ......

    // 2. 執行 ptrace_attach
    if (request == PTRACE_ATTACH || request == PTRACE_SEIZE) {
        ret = ptrace_attach(child, request, addr, data);
        ...
    }
    ......
}
```

在 ptrace 系統呼叫原始碼中，第一步比較簡單，根據參數中的 pid 查詢目標程序在核心中的   `task_struct` 核心物件。第二步操作中的 `ptrace_attach`，是 attach 到目標程序的核心函式。

```
//file:kernel/ptrace.c
static int ptrace_attach(struct task_struct *task, long request, ...)
{
    ...
    // 1.許可權檢查
    if (unlikely(task->flags & PF_KTHREAD))
        goto out;
    ...
    // 2.狀態設定
    ptrace_link(task, current);
    ...
}
```

在   `ptrace_attach` 中先要進行一些許可權檢查，例如核心執行緒 (Thread) 是不允許被 attach 的。接著呼叫 `ptrace_link` 來修改當前程序，以及要跟蹤的目標程序的核心物件相關欄位。 `ptrace_link` 的主要實作是 \_\_ptrace_link。

```
//file:kernel/ptrace.c
void __ptrace_link(struct task_struct *child, struct task_struct *new_parent, ...)
{
    list_add(&child->ptrace_entry, &new_parent->ptraced);
    child->parent = new_parent;
    ...
}
```

在   `ptrace_link` 函式中，先是透過 `list_add` 函式，將目標程序的 `ptrace_entry` 被插入到當前程序的 ptraced 連結串列的頭部。這樣當前程序就可以透過 ptraced 連結串列來跟蹤和管理所有在跟蹤的程序。

接著呼叫 `child->parent` =`new_parent` 把當前程序設定成了目標程序的 parent。目的是使當前程序能夠透過呼叫 waitpid 取得到目標程序的 SIGTRAP 訊號。

這樣就完成了到目標程序的 attach，當前程序透過 ptraced 來管理目標程序，目標程序也可以發出 SIGTRAP 訊號來和當前程序進行訊息傳遞。

## 三、捕獲目標程序 SYSCALL

## 3.1 設定等待目標程序 SYSCALL

在完成當前程序和目標程序的 attach 關聯後。接著下一步操作是告訴 Linux 核心，要等待和捕獲目標程序的系統呼叫。

```
int main(int argc, char *argv[]) {

    // 1.attach 到 pid 指定的目標程序上
    ...

    while (1) {
        // 2.等待目標程序的 PTRACE_SYSCALL
        // 2.1 指定要捕獲目標程序的 PTRACE_SYSCALL
        ptrace(PTRACE_SYSCALL, pid, NULL, NULL)
        // 2.2 當目標程序有 SYSCALL 發生時醒來處理
        waitpid(pid, &status, 0)
        ...
    }
}
```

在 ptrace 系統呼叫中，由於這次傳入的第一個參數是 `PTRACE_SYSCALL`。所以其執行的核心函式提煉後如下所示：

```
//file:kernel/ptrace.c
SYSCALL_DEFINE4(ptrace, long, request, long, pid, unsigned long, addr,
        unsigned long, data)
{
    struct task_struct *child;
    child = find_get_task_by_vpid(pid);
    ......
    ret = arch_ptrace(child, request, addr, data)
    ......
}
```

首先還是先根據 pid 取得目標程序的   `task_struct` 核心物件。接著執行 `arch_ptrace` 來為目標程序核心物件新增一個標記 `SYSCALL_TRACE`。具體設定是在 `ptrace_resume` 函式中執行的（arch_ptrace->ptrace_request->  `ptrace_resume`）。我們直接來看 `ptrace_resume` 原始碼。

```
//file:kernel/ptrace.c
static int ptrace_resume(struct task_struct *child, long request,
             unsigned long data)
{
    if (request == PTRACE_SYSCALL)
        set_task_syscall_work(child, SYSCALL_TRACE);
    ...
}
```

`set_task_syscall_work` 函式就是在給目標程序的設定了一個 `SYSCALL_TRACE` 標記位。

```
//file:include/linux/thread_info.h
#define set_task_syscall_work(t, fl) \
    set_bit(SYSCALL_WORK_BIT_##fl, &task_thread_info(t)->syscall_work)
```

這樣後面當該程序再執行系統呼叫的時候，透過判斷該標記就能發現有程序在跟蹤它了。

## 3.2 等待目標程序訊號發生

當前程序在設定完要對目標程序的 SYSCALL 進行觀察後，接著就呼叫 waitpid 進入了睡眠狀態。

```
int main(int argc, char *argv[]) {

    // 1.attach 到 pid 指定的目標程序上
    ...

    while (1) {
        // 2.等待目標程序的 PTRACE_SYSCALL
        // 2.1 指定要捕獲目標程序的 PTRACE_SYSCALL
        ptrace(PTRACE_SYSCALL, pid, NULL, NULL)
        // 2.2 當目標程序有 SYSCALL 發生時醒來處理
        waitpid(pid, &status, 0)
        ...
    }
}
```

waitpid 也是一個系統呼叫。

```
//file:kernel/exit.c
SYSCALL_DEFINE3(waitpid, pid_t, pid, int __user *, stat_addr, int, options)
{
    return kernel_wait4(pid, stat_addr, options, NULL);
}
```

在這個系統呼叫中，依次呼叫   `kernel_wait4`、`do_wait` 等核心函式，最後在 `add_wait_queue` 函式中，將當前程序加入到等待佇列中，更新程序狀態為 `TASK_INTERRUPTIBLE`（可中斷睡眠狀態），等待子程序訊號。

```
// file:kernel/exit.c
static long do_wait(struct wait_opts *wo)
{
    ...
    init_waitqueue_func_entry(&wo->child_wait, child_wait_callback);
    wo->child_wait.private = current;
    add_wait_queue(&current->signal->wait_chldexit, &wo->child_wait);
...
}
```

當子程序退出時，核心會向父程序傳送一個訊號，父程序的訊號處理程式會喚醒等待佇列中的程序，使它們重新進入可執行狀態，等待被排程器排程執行。

## 四、等待並讀取目標程序系統呼叫

## 4.1 目標程序系統呼叫發生

當目標程序系統呼叫發生的時候，會檢查是否有被設定   `SYSCALL_TRACE` 標記位。如果有，那就說明有程序正在跟蹤它。具體的檢測是在 `syscall_trace_enter` 核心函式中做的

```
//file:arch/m68k/kernel/entry.S
ENTRY(system_call)
    ...
    jbsr    syscall_trace_enter
```

```
//file:arch/m68k/kernel/ptrace.c
asmlinkage int syscall_trace_enter(void)
{
    int ret = 0;

    if (test_thread_flag(TIF_SYSCALL_TRACE))
        ret = ptrace_report_syscall_entry(task_pt_regs(current));
    return ret;
}
```

如果有   `SYSCALL_TRACE` 標誌，那就會設定一下退出碼，發出 SIGTRAP 訊號，喚醒正在追蹤它的程序，最後暫停當前程式的執行。具體的核心函式呼叫過程是經過 ptrace_report_syscall_entry-> ptrace_report_syscall-> ptrace_notify-> `ptrace_do_notify` 這麼一條長的呼叫鏈後，最終在 `ptrace_stop` 核心函式中執行的。我們直接來看這個最關鍵的 `ptrace_stop` 函式。

```
//file:kernel/signal.c
static int ptrace_stop(int exit_code, int why, unsigned long message,
               kernel_siginfo_t *info)
    __releases(&current->sighand->siglock)
    __acquires(&current->sighand->siglock)
{
    ......
    // 1.
    set_special_state(TASK_TRACED);


    // 2.設定當前程序的 exit_code
    current->ptrace_message = message;
    current->last_siginfo = info;
    current->exit_code = exit_code;


    // 3.
    if (current->ptrace)
        do_notify_parent_cldstop(current, true, why);
    if (gstop_done && (!current->ptrace || ptrace_reparented(current)))
        do_notify_parent_cldstop(current, false, why);

    cgroup_enter_frozen();
    schedule();
    ......

}
```

在這個函式中做了這麼幾件事情。

第一，呼叫  `set_special_state`(`TASK_TRACED`) 將當前程序的狀態設定為 `TASK_TRACED`，表示程序已被 ptrace 停止。該狀態意味著程序將不會在下次排程時被排程執行，因為它現在處於被跟蹤狀態。

第二，設定自己的退出碼，到 struct `task_struct` 的成員 `exit_code` 上。

第三，呼叫 `do_notify_parent_cldstop`()-->\_\_wake_up_parent() 喚醒跟蹤程序

第四，呼叫 schedule 掛起自己讓出 CPU。

## 3.2 跟蹤程序 waitpid 回傳

接下來跟蹤程序收到訊號，會被核心喚醒，並中 waitpid 函式中回傳。

```
//file:kernel/exit.c
SYSCALL_DEFINE3(waitpid, pid_t, pid, int __user *, stat_addr, int, options)
{
    return kernel_wait4(pid, stat_addr, options, NULL);
}

long kernel_wait4(pid_t upid, int __user *stat_addr, int options,
struct rusage *ru)
{
...
ret = do_wait(&wo);
put_pid(pid);

//將程序狀態儲存到使用者傳入的位址中
if (ret > 0 && stat_addr && put_user(wo.wo_stat, stat_addr))
ret = -EFAULT;
return ret;
}
```

這時，跟蹤程序就知道目標程序有系統呼叫發生了。下一步就可以讀取目標程序正在執行的系統呼叫資訊了。

## 3.3 讀取目標程序系統呼叫號

在 Linux 核心中，  `ORIG_RAX` 暫存器用於儲存程序在執行系統呼叫是的呼叫號。跟蹤程序只需要存取下目標程序的 `ORIG_RAX` 暫存器就可以知道目標程序正在執行哪個系統呼叫了。

具體執行方式是呼叫 ptrace 系統呼叫。第一個參數設定為   `PTRACE_PEEKUSER` 表示要讀取目標程序的一些資料。第三個參數指定為 8*`ORIG_RAX` 表示要讀取 `ORIG_RAX` 暫存器。8* `ORIG_RAX` 計算出 `ORIG_RAX` 在使用者空間的偏移位址。

```
int main(int argc, char *argv[]) {

    // 1.attach 到 pid 指定的目標程序上
    ...

    while (1) {
        // 2.等待目標程序的 PTRACE_SYSCALL
        ...

        // 3.讀取並解析系統呼叫
        // 3.1 讀取目標程序正在執行的系統呼叫號
        syscall_number = ptrace(PTRACE_PEEKUSER, pid, 8 * ORIG_RAX, NULL);  、
        // 3.2 將系統呼叫號轉為系統呼叫名稱
        switch (syscall_number) {
            case 5: syscall_name = "read"; break;
            case 6: syscall_name = "write"; break;
            case 10: syscall_name = "open"; break;
            case 11: syscall_name = "close"; break;
            ......
            default: syscall_name = "unknown"; break;
        }
        // 3.3 列印系統呼叫名稱
        printf("Syscall: %s (number: %ld)\n", syscall_name, syscall_number);
    }
}
```

核心執行 ptrace 系統呼叫的時候，會執行到 `arch_ptrace` 函式。

```
//file:arch/x86/kernel/ptrace.c
long arch_ptrace(struct task_struct *child, long request,
         unsigned long addr, unsigned long data)
{
    unsigned long __user *datap = (unsigned long __user *)data;
    ...
    switch (request) {
        ...
        case PTRACE_PEEKUSR: {
            tmp = 0;  /* Default return condition */
            if (addr < sizeof(struct user_regs_struct))
                tmp = getreg(child, addr);
            else if (addr >= offsetof(struct user, u_debugreg[0]) &&
                addr <= offsetof(struct user, u_debugreg[7])) {
                addr -= offsetof(struct user, u_debugreg[0]);
                tmp = ptrace_get_debugreg(child, addr / sizeof(data));
            }
            ret = put_user(tmp, datap);
            break;
        }
    }
}
```

在   `arch_ptrace` 判斷是 `PTRACE_PEEKUSER` 參數，會在計算一下目標程序資料位址 addr，然後將其讀取出來設定到跟蹤程序的使用者空間中。這樣，就讀取到系統呼叫號了。

接下來在 /  `usr/include/x86_64-linux-gnu/asm/unistd_64.h` 中可以檢視到所有的系統呼叫號資訊。將其轉為系統呼叫名後輸出即可。

## 五、總結

`strace` 命令跟蹤其他程序的系統呼叫的整個過程可以同下面一張圖來總結。

首先是  `strace` 程序執行下面三步操作

- 1.1 呼叫 ptrace(ATTACH, ...) 設定關聯跟蹤程序和目標程序
- 1.2 再呼叫 ptrace(SYSCALL, ...) 設定要要跟蹤目標程序的系統呼叫
- 1.3 接著就呼叫 waitpid 去等待子程序的訊號了，而先暫停執行了

再接下來當目標程序有系統呼叫發生時，

- 2.1 檢查當前程序是否被設定了  `SYSCALL_TRACE` 標記
- 2.2 如果有，那麼設定一下當前程序的狀態，也暫停執行了
- 2.3 透過訊號機制喚醒跟蹤程序

跟蹤程序收到訊號後會繼續執行

- 1.4 讀取目標程序  `ORIG_RAX` 暫存器，其中儲存著目標程序的系統呼叫號
- 1.5 將系統呼叫號轉換成系統呼叫名輸出

再接下來再呼叫  `wait_pid`，讓目標程序繼續執行。整體進入一個不斷取得，不斷列印的迴圈中。

從以上的執行過程可以看出。 `strace` 命令執行的過程中，會讓目標程序執行到系統呼叫時暫停執行，從而導致比較頻繁的上下文切換 (Context Switch)，會增加目標程序的執行時間。所以，如果是在生產環境中，使用 `strace` 命令的時候還是要小心一點。更萬萬不可當成一個長期的線上監控工具來使用。

不過，`strace` 命令還是非常簡單好用的，你說呢！？
