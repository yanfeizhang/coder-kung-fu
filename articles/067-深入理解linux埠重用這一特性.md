---
layout: default
title: "深入理解 Linux 埠重用這一特性"
---

# 深入理解 Linux 埠重用這一特性

> 原文標題：深入理解Linux端口重用这一特性

> 原文連結：https://mp.weixin.qq.com/s/SYCUMvzktgeGbyAfRdqhmg

---

大家好，我是飛哥！

開篇我先考大家一個小問題，如果你的伺服器上已經有個程序 (Process) 在   `listen` 6000 這個埠號了。那麼該伺服器上其他程序是否還能 `bind` 和 `listen` 該埠呢？

我相信一定會有一部分同學會答說是不能的。因為很多人都遇到過「Address already in use」這個錯誤。而這個錯誤產生的原因就是埠已經被佔用。

但其實在 Linux 3.9 以上的核心版本裡，是允許多個程序繫結到同一個埠號上。這就是我們今天要說的 REUSEPORT 新特性。

本文中我們將闡述 REUSEPORT 是為了解決什麼問題而產生的。如果有多個程序複用同一個埠，當使用者請求到達時核心是如何選一個程序進行響應的。學習完本文，你將深刻掌握這一提升伺服器端效能的利器！

## 一、 REUSEPORT 要解決的問題

我覺得理解一個技術點的很重要的前提是要弄明白這個問題產生的背景，弄明白了背景再理解起技術點來就會容易許多。

關於 REUSEPORT 特性產生的背景其實在 linux 的 commit 中提供的足夠詳細了（參見：[https://github.com/torvalds/linux/commit/da5e36308d9f7151845018369148201a5d28b46d](https://github.com/torvalds/linux/commit/da5e36308d9f7151845018369148201a5d28b46d)）。

我就在這個 commit 中的資訊的基礎上給大家展開了說一說。

大家有過伺服器端開發的經驗的同學都知道，一般一個服務是固定監聽某一個埠的。例如 Nginx 服務一般固定監聽 80 或 8080，Mysql 服務固定監聽 3306 等等。

在網民數量還不夠多，終端裝置也還沒有爆炸的年代裡，一直是在使用的是埠不可重複被監聽的模式。但是到了 2010 年之後，Web 網際網路 (Network) 已經發展到了一個高潮，移動端的裝置也開始迎來了大發展。這個時候埠不能重用的效能瓶頸就暴露出來了。

應對海量流量的主要措施就是應用多程序模型。在埠不可被重複 `bind` 和 `listen` 的年代裡，提供海量服務的多程序 Server 提供一般是採用如下兩種程序模型來工作。

第一種是專門做一個或多個程序服務 `accept` 新連線，接收請求，然後將請求轉給其他的 worker 程序來處理。

這種多程序模型有兩個問題，首先第一個是 dispatcher 程序並不處理任務，需要轉交給 worker 程序來處理和響應。這會導致一次額外的程序上下文切換 (Process Context Switch) 的開銷。 第二個問題是如果流量特別大的時候 dispatcher 程序很容易成為制約整個服務 qps 提升的瓶頸。

還有另一種多程序模型是多個程序複用一個   `listen` 狀態的 `socket`，多個程序同時從一個 `socket` 中 `accept` 請求來處理。Nginx 就採用的是這種模型。

這種程序模型解決了第一個模型的問題。但是又帶來了新的問題。當   `socket` 收到一條連線的時候，不能把所有的 worker 程序都招呼起來。需要用鎖來保證唯一性，這樣就會有鎖競爭的問題。

## 二、REUSEPORT 的誕生

為了更高效地讓多個使用者態 (User Mode) 的程序接收和響應用戶端的請求。Linux 在 2013 年的 3.9 版本中提供了 REUSEPORT 新特性。

> 詳細程式碼參見 [https://github.com/torvalds/linux/commit/da5e36308d9f7151845018369148201a5d28b46d](https://github.com/torvalds/linux/commit/da5e36308d9f7151845018369148201a5d28b46d) 和 [https://github.com/torvalds/linux/commit/055dc21a1d1d219608cd4baac7d0683fb2cbbe8a](https://github.com/torvalds/linux/commit/055dc21a1d1d219608cd4baac7d0683fb2cbbe8a)

該特性允許同一機器上的多個程序同時建立不同的 `socket` 來 `bind` 和 `listen` 在相同的埠上。然後在核心層面實作多個使用者程序的負載均衡。

我們來看下核心是如何支援 reuseport 這個特性的。

## 2.1   `SO_REUSEPORT` 設定

想給自己的服務開啟 REUSEPORT 很簡單，就是給自己 server 裡 `listen` 用的 `socket` 上加這麼一句。（這裡以 c 為 demo，其他語言可能會有差異，但基本上差不多）

setsockopt(fd, `SOL_SOCKET`, `SO_REUSEPORT`, ...);

這行程式碼在核心中對應的處理步驟就是把核心   `socket` 的 `sk_reuseport` 欄位設定為相應的值，開啟的話是 1。

```
//file: net/core/sock.c
int sock_setsockopt(struct socket *sock, int level, int optname,
            char __user *optval, unsigned int optlen)
{
    ...
    switch (optname) {
        ...
        case SO_REUSEPORT:
            sk->sk_reuseport = valbool;
        ...
    }
}
```

## 2.2   `bind` 時的處理

核心在 `inet_bind` 時會呼叫到 `inet_csk_get_port` 函式。我們來看看 `bind` 時對 reuseport 的處理過程。 來看原始碼：

```
//file: net/ipv4/inet_connection_sock.c
int inet_csk_get_port(struct sock *sk, unsigned short snum)
{
    ...
    //在繫結表（bhash）中查詢，
    head = &hashinfo->bhash[inet_bhashfn(net, snum,
            hashinfo->bhash_size)];
    inet_bind_bucket_for_each(tb, &head->chain)
        //找到了，在一個命名空間下而且埠號一致，表示該埠已經繫結
        if (net_eq(ib_net(tb), net) && tb->port == snum)
            goto tb_found;
    ...
}
```

核心透過拉鍊雜湊表的方式來管理所有的   `bind` 的 `socket`。其中 `inet_bhashfn` 是計算雜湊值的函式。

當計算找到雜湊槽位以後，透過 `inet_bind_bucket_for_each` 來遍歷所有的 `bind` 狀態的 `socket`，目的是為了判斷是否衝突。

`net_eq`(`ib_net`(tb), net) 這個條件表示網路命名空間 (Network Namespace) 匹配，`tb->port` == snum 表示埠號匹配。這兩個條件加起來，就是說在同一個命名空間 (Namespace) 下，該埠已經被繫結過了。我們接著看 `tb_found` 裡會做什麼。

```
//file: net/ipv4/inet_connection_sock.c
int inet_csk_get_port(struct sock *sk, unsigned short snum)
{
    ...
    if (((tb->fastreuse > 0 &&
          sk->sk_reuse && sk->sk_state != TCP_LISTEN) ||
         (tb->fastreuseport > 0 &&
          sk->sk_reuseport && uid_eq(tb->fastuid, uid))) &&
        smallest_size == -1) {
        goto success;
    } else {
        //繫結衝突
        ......
    }

我們看 tb->fastreuseport > 0 和 sk->sk_reuseport 這兩個條件。
```

這兩個條件的意思是已經   `bind` 的 `socket` 和正在 `bind` 的 `socket` 都開啟了   `SO_REUSEPORT` 特性。符合條件的話，將會跳轉到 success 進行繫結成功的處理。也就是說，這個埠可以重複繫結使用!

`uid_eq`(`tb->fastuid`, uid) 這個條件目的是安全性，必須要求相同的使用者程序下的 `socket` 才可以複用埠。避免跨使用者啟動相同埠來竊取另外使用者服務上的流量。

## 2.3 `accept` 響應新連線

當有多個程序都   `bind` 和 `listen` 了同一個埠的時候。有用戶端連線請求到來的時候就涉及到選擇哪個 `socket`（程序）進行處理的問題。我們再簡單看一下，響應連線時的處理過程。

核心仍然是透過 hash + 拉鍊的方式來儲存所有的 `listen` 狀態的 `socket`。

查詢 `listen` 狀態的 `socket` 的時候需要查詢該雜湊表。我們進入響應握手請求的時候進入的一個關鍵函式 \_\_inet_lookup_listener 來看。

```
//file: net/ipv4/inet_hashtables.c
struct sock *__inet_lookup_listener(struct net *net,
                    struct inet_hashinfo *hashinfo,
                    const __be32 saddr, __be16 sport,
                    const __be32 daddr, const unsigned short hnum,
                    const int dif)
{
    //所有 listen socket 都在這個 listening_hash 中
    struct inet_listen_hashbucket *ilb = &hashinfo->listening_hash[hash];

begin:
    result = NULL;
    hiscore = 0;
    sk_nulls_for_each_rcu(sk, node, &ilb->head) {
        score = compute_score(sk, net, hnum, daddr, dif);
        if (score > hiscore) {
            result = sk;
            hiscore = score;
            reuseport = sk->sk_reuseport;
            if (reuseport) {
                phash = inet_ehashfn(net, daddr, hnum,
                             saddr, sport);
                matches = 1;
            }
        } else if (score == hiscore && reuseport) {
            matches++;
            if (((u64)phash * matches) >> 32 == 0)
                result = sk;
            phash = next_pseudo_random32(phash);
        }
    }
    ...
    return result;
}
```

其中   `sk_nulls_for_each_rcu` 是在遍歷所有 hash 值相同的 `listen` 狀態的 `socket`。注意看 `compute_score` 這個函式，這裡是計算匹配分。當有多個 `socket` 都命中的時候，匹配分高的優先命中。我們來看一下這個函式里的一個細節。

```
//file: net/ipv4/inet_hashtables.c
static inline int compute_score(struct sock *sk, ...)
{
    int score = -1;
    struct inet_sock *inet = inet_sk(sk);

    if (net_eq(sock_net(sk), net) && inet->inet_num == hnum &&
            !ipv6_only_sock(sk)) {
        //如果服務繫結的是 0.0.0.0，那麼 rcv_saddr 為假
        __be32 rcv_saddr = inet->inet_rcv_saddr;
        score = sk->sk_family == PF_INET ? 2 : 1;
        if (rcv_saddr) {
            if (rcv_saddr != daddr)
                return -1;
            score += 4;
        }
        ...
    }
    return score;
}
```

那麼匹配分解決的是什麼問題呢？為了描述的更清楚，我們假設某臺伺服器有兩個 ip 位址，分別是 10.0.0.2 和 10.0.0.3。我們啟動了如下三個伺服器程序。

A 程序：`./test-server` 10.0.0.2 6000
B 程序：`./test-server` 0.0.0.0 6000
C 程序：`./test-server` 127.0.0.1 6000

那麼你的用戶端如果指定是連線 10.0.0.2:6000，那麼 A 程序會優先執行。因為當匹配到 A 程序的   `socket` 的時候，需要看一下握手封包中的目的 ip 和這個位址是否匹配，確實匹配那得分就是 4 分，最高分。

如果你指定連線的是 10.0.0.3，那麼 A 程序就無法被匹配到。這個時候 B 程序監聽時指定的是 0.0.0.0（`rcv_saddr` 為 false），則不需要進行目的位址的比對，得分為 2。由於沒有更高分，所以這次命中的是 B 程序。

C 程序只有你在本機存取，且指定 ip 使用 127.0.0.1 才能命中，得分也是為 4 分。外部伺服器或者是在本機使用其他 ip 都無法存取的到。

如果當多個   `socket` 的匹配分一致，透過呼叫 `next_pseudo_random32` 進行隨機的選擇。在核心態 (Kernel Mode) 做了負載均衡的事情，選定一個具體的 `socket`，避免了多個程序在同一個 `socket` 上的鎖競爭。

## 三、動手實踐

動手試試能體會更深刻。為此我動手寫了個簡單的開啟   `SO_REUSEPORT` 特性的 server 的程式碼。核心就是給 server 的 `socket`

詳細原始碼參見：[https://github.com/yanfeizhang/coder-kung-fu/blob/main/tests/network/test08/server.c](https://github.com/yanfeizhang/coder-kung-fu/blob/main/tests/network/test08/server.c)。 當然你也可以自己寫一個。

## 3.1 相同 port 多服務啟動

編譯後，分別在多個控制檯下執行一下試試，看是否能夠啟動起來。

$`./test-server` 0.0.0.0 6000
Start server on 0.0.0.0:6000 successed, pid is 23179
$`./test-server` 0.0.0.0 6000
Start server on 0.0.0.0:6000 successed, pid is 23177
$`./test-server` 0.0.0.0 6000
Start server on 0.0.0.0:6000 successed, pid is 23185
......

沒錯，全部起來了！ 這個 6000 的埠被多個 server 程序重複使用了。

## 3.2 核心負載均衡驗證

由於上述幾個監聽了相同埠的程序都使用的是 0.0.0.0，那麼在計算 score 的時候，他們的得分就都是 2 分。那麼就由核心以隨機的方式進行負載均衡了。

我們再啟動一個用戶端，隨意發起幾個連線請求，統計一下各個 server 程序收到的連線數。如下圖可見，該伺服器上收到的連線的確是平均雜湊在各個程序裡了。

Server 0.0.0.0 6000 (23179) `accept` success:15
Server 0.0.0.0 6000 (23177) `accept` success:25
Server 0.0.0.0 6000 (23185) `accept` success:20
Server 0.0.0.0 6000 (23181) `accept` success:19
Server 0.0.0.0 6000 (23183) `accept` success:21

## 3.3 匹配優先順序驗證

動用一臺兩個 ip 位址的伺服器，還假設你的 ip 分別是 10.0.0.2 和 10.0.0.3。啟動了如下三個伺服器程序。

A 程序：`./test-server` 10.0.0.2 6000
B 程序：`./test-server` 0.0.0.0 6000

另外一臺用戶端透過使用 telnet 命令即可測試。

$ telnet 10.0.0.2 6000 發現是命中 A 程序。
$ telnet 10.0.0.3 6000 發現是命中 B 程序。

## 3.4 跨使用者安全性驗證

先以 A 使用者啟動一個服務

$`./test-server` 0.0.0.0 6000
Start server on 0.0.0.0:6000 successed, pid is 30914

再切換到另外一個使用者下，例如 root。

## `./test-server` 0.0.0.0 6000
Server 30481 Error : `Bind` Failed!

這時候發現   `bind` 不會透過，服務啟動失敗！

## 四、總結

在 Linux 3.9 以前的版本中，一個埠只能被一個 `socket` 繫結。在多程序的場景下，無論是使用一個程序來在這個 `socket` 上 `accept`，還是說用多個 worker 來   `accept` 同一個 `socket`，在高併發的場景下效能都顯得有那麼一些低下。

在 2013 年釋出的 3.9 中新增了 portreuse 的特性。該特定允許多個程序分別用不同的 `socket` 繫結到同一個埠。當有流量到達的時候，在核心態以隨機的方式進行負載均衡。避免了鎖的開銷。

Linux 的這一特性是非常有用的，可惜還有大量的工程師不理解它的原理，也更是沒有把它用起來，實在可惜！

多說一句，如果你使用的是 1.9.1 以上版本的 nginx，只需要一行簡單的設定就可以體驗這個特性。

server {
`listen` 80 reuseport;
...
}

## 往期相關文章

- [為什麼伺服器端程式都需要先   `listen` 一下？](https://mp.weixin.qq.com/s/hv2tmtVpxhVxr6X-RNWBsQ)
- [TCP 連線中用戶端的埠號是如何確定的？](https://mp.weixin.qq.com/s/C-Eeoeh9GHxugF4J30fz1A)
- [能將三次握手 (Three-Way Handshake) 理解到這個深度，面試官拍案叫絕！](https://mp.weixin.qq.com/s/vlrzGc5bFrPIr9a7HIr2eA)
- [深入解析常見三次握手異常](https://mp.weixin.qq.com/s/7Cum6Y28H_gXLyrRFrthNw)
- [如何正確檢視線上半/全連線佇列 (Connection Queue) 溢位情況？](https://mp.weixin.qq.com/s/f-TFX2t8CWRCGoyCByGkOw)
