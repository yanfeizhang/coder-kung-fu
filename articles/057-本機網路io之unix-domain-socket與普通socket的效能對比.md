---
layout: default
title: "本機網路 (Network) IO 之 Unix Domain `Socket` 與普通 `socket` 的效能對比"
---

# 本機網路 (Network) IO 之 Unix Domain `Socket` 與普通 `socket` 的效能對比

> 原文標題：本机网络IO之Unix Domain Socket与普通socket的性能对比

> 原文連結：https://mp.weixin.qq.com/s/fHzKYlW0WMhP2jxh2H_59A

---

大家好，我是飛哥！

很多讀者在看完 [《127.0.0.1 之本機網路通訊過程知多少 ?》](https://mp.weixin.qq.com/s/6_OfoeD3ZpyQisY2F-4_bw) 這一篇後，讓我講講 Unix Domain   `Socket`。好了，今天就安排！

在本機網路 IO 中，我們講到過根據普通 `socket` 的本機網路通訊過程中，其實在核心工作流上並沒有節約太多的開銷。該走的系統呼叫 (System Call)、協定堆疊、鄰居系統、裝置驅動（雖然說對於本機網路 loopback 裝置來說只是一個軟體虛擬的東西）全都走了一遍。其工作過程如下圖

那麼我們今天來看另外一種本機網路 IO 通訊方式 -- Unix Domain   `Socket`。看看這種方式在效能開銷上和根據 127.0.0.1 的本機網路 IO 有沒有什麼差異呢。

本文中，我們將分析 Unix Domain `Socket` 的內部工作原理。你將理解為什麼這種方式的效能比 127.0.0.1 要好很多。最後我們還給出了實際的效能測試對比資料。

相信你已經迫不及待了，彆著急，讓我們一一展開細說！

## 一、使用方法

Unix Domain   `Socket`（後面統一簡稱 UDS） 使用起來和傳統的 `socket` 非常的相似。 區別點主要有兩個地方需要關注。

第一，在建立   `socket` 的時候，普通的 `socket` 第一個參數 family 為 `AF_INET`， 而 UDS 指定為 `AF_UNIX` 即可。

第二，Server 的標識不再是 ip 和埠，而是一個路徑，例如 `/dev/shm/fpm-cgi.sock`。

其實在平時我們使用 UDS 並不一定需要去寫一段程式碼，很多應用程式都支援在本機網路 IO 的時候設定。例如在 Nginx 中，如果要存取的本機 fastcgi 服務是以 UDS 方式提供服務的話，只需要在設定檔中設定這麼一行就完成了。

`fastcgi_pass` unix:`/dev/shm/fpm-cgi.sock`;

如果
對於一個 UDS 的 server 來說，它的程式碼範例大概結構如下，大家簡單瞭解一下。只是個範例不一定可執行。

```
int main()
{
    // 建立 unix domain socket
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);

    // 繫結監聽
    char *socket_path = "./server.sock";
    strcpy(serun.sun_path, socket_path);
    bind(fd, serun, ...);
    listen(fd, 128);

    while(1){
        //接收新連線
        conn = accept(fd, ...);

        //收發資料
        read(conn, ...);
        write(conn, ...);
    }
}
```

根據 UDS 的 client 也是和普通   `socket` 使用方式差不太多，建立一個 `socket`，然後 `connect` 即可。

```
int main(){
    sock = socket(AF_UNIX, SOCK_STREAM, 0);
    connect(sockfd, ...)
}
```

## 二、連線過程

總的來說，根據 UDS 的連線過程比 inet 的   `socket` 連線過程要簡單多了。用戶端先建立一個自己用的 `socket`，然後呼叫 `connect` 來和伺服器建立連線。

在 `connect` 的時候，會申請一個新 `socket` 給 server 端將來使用，和自己的 `socket` 建立好連線關係以後，就放到伺服器正在監聽的   `socket` 的接收佇列中。 這個時候，伺服器端透過 `accept` 就能取得到和用戶端配好對的新 `socket` 了。

總的 UDS 的連線建立流程如下圖。

核心原始碼中最重要的邏輯在 `connect` 函式中，我們來簡單展開看一下。 unix 協定族中定義了這類 `socket` 的所有方法，它位於 `net/unix/af_unix.c` 中。

```
//file: net/unix/af_unix.c
static const struct proto_ops unix_stream_ops = {
    .family =    PF_UNIX,
    .owner =    THIS_MODULE,
    .bind =        unix_bind,
    .connect =    unix_stream_connect,
    .socketpair =    unix_socketpair,
    .listen =    unix_listen,
    ...
};
```

我們找到   `connect` 函式的具體實作，`unix_stream_connect`。

```
//file: net/unix/af_unix.c
static int unix_stream_connect(struct socket *sock, struct sockaddr *uaddr,
                   int addr_len, int flags)
{
    struct sockaddr_un *sunaddr = (struct sockaddr_un *)uaddr;

    ...

    // 1. 為伺服器側申請一個新的 socket 物件
    newsk = unix_create1(sock_net(sk), NULL);

    // 2. 申請一個 skb，並關聯上 newsk
    skb = sock_wmalloc(newsk, 1, 0, GFP_KERNEL);
    ...

    // 3. 建立兩個 sock 物件之間的連線
    unix_peer(newsk)    = sk;
    newsk->sk_state        = TCP_ESTABLISHED;
    newsk->sk_type        = sk->sk_type;
    ...
    sk->sk_state    = TCP_ESTABLISHED;
    unix_peer(sk)    = newsk;

    // 4. 把連線中的一頭（新 socket）放到伺服器接收佇列中
    __skb_queue_tail(&other->sk_receive_queue, skb);
}
```

主要的連線操作都是在這個函式中完成的。和我們平常所見的 TCP 連線建立過程，這個連線過程簡直是太簡單了。沒有三次握手 (Three-Way Handshake)，也沒有全連線佇列 (Connection Queue)、半連線佇列，更沒有什麼超時重傳。

直接就是將兩個  `socket` 結構體中的指標互相指向對方就行了。就是 `unix_peer`(newsk) = sk 和 `unix_peer`(sk) = newsk 這兩句。

```
//file: net/unix/af_unix.c
#define unix_peer(sk) (unix_sk(sk)->peer)
```

當關聯關係建立好之後，透過 \_\_skb_queue_tail 將 `skb` 放到伺服器的接收佇列中。注意這裡的 `skb` 裡儲存著新   `socket` 的指標，因為服務程序 (Process) 透過 `accept` 取出這個 `skb` 的時候，就能取得到和客戶程序中 `socket` 建立好連線關係的另一個 `socket`。

怎麼樣，UDS 的連線建立過程是不是很簡單！？

## 三、傳送過程

看完了連線建立過程，我們再來看看根據 UDS 的資料的收發。這個收發過程一樣也是非常的簡單。傳送方是直接將資料寫到接收方的接收佇列裡的。

我們從   `send` 函式來看起。`send` 系統呼叫的原始碼位於檔案 `net/socket.c` 中。在這個系統呼叫裡，內部其實真正使用的是 sendto 系統呼叫。它只幹了兩件簡單的事情，

第一是在核心中把真正的 `socket` 找出來，在這個物件裡記錄著各種協定堆疊的函式位址。
第二是構造一個 struct msghdr 物件，把使用者傳入的資料，例如 buffer 位址、資料長度什麼的，統統都裝進去.
剩下的事情就交給下一層，協定堆疊裡的函式   `inet_sendmsg` 了，其中 `inet_sendmsg` 函式的位址是透過 `socket` 核心物件裡的 ops 成員找到的。大致流程如圖。

在進入到協定堆疊 `inet_sendmsg` 以後，核心接著會找到 `socket` 上的具體協定傳送函式。對於 Unix Domain `Socket` 來說，那就是 `unix_stream_sendmsg`。 我們來看一下這個函式

```
//file:
static int unix_stream_sendmsg(struct kiocb *kiocb, struct socket *sock,
                   struct msghdr *msg, size_t len)
{
    // 1.申請一塊快取區
    skb = sock_alloc_send_skb(sk, size, msg->msg_flags&MSG_DONTWAIT,
                  &err);

    // 2.複製使用者資料到核心快取區
    err = memcpy_fromiovec(skb_put(skb, size), msg->msg_iov, size);

    // 3. 查詢socket peer
    struct sock *other = NULL;
    other = unix_peer(sk);

    // 4.直接把 skb放到對端的接收佇列中
    skb_queue_tail(&other->sk_receive_queue, skb);

    // 5.傳送完畢回呼
    other->sk_data_ready(other, size);
}
```

和複雜的 TCP 傳送接收過程相比，這裡的傳送邏輯簡單簡單到令人髮指。申請一塊記憶體（`skb`），把資料複製進去。根據   `socket` 物件找到另一端，直接把 `skb` 給放到對端的接收佇列裡了

接收函式主題是 `unix_stream_recvmsg`，這個函式中只需要存取它自己的接收佇列就行了，原始碼就不展示了。所以在本機網路 IO 場景裡，根據 Unix DomainSocket 的服務效能上肯定要好一些的。

## 四、效能對比

為了驗證 Unix Domain  `Socket` 到底比根據 127.0.0.1 的效能好多少，我做了一個效能測試。
在網路效能對比測試，最重要的兩個指標是延遲和吞吐。我從 Github 上找了個好用的測試原始碼：[https://github.com/rigtorp/ipc-bench](https://github.com/rigtorp/ipc-bench)。 我的測試環境是一臺 4 核 CPU，8G 記憶體的 KVM 虛機。

在延遲指標上，對比結果如下圖。

可見在小包（100 位元組）的情況下，UDS 方法的「網路」 IO 平均延遲只有 2707 納秒，而根據 TCP（存取 127.0.0.1）的方式下延遲高達 5690 納秒。耗時整整是前者的兩倍。

在包體達到 100 KB 以後，UDS 方法延遲 24 微秒左右（1 微秒等於 1000 納秒），TCP 是 32 微秒，仍然高一截。這裡低於 2 倍的關係了，是因為當包足夠大的時候，網路協定堆疊上的開銷就顯得沒那麼明顯了。

再來看看吞吐效果對比。

在小包的情況下，頻寬指標可以達到 854 M，而根據 TCP 的 IO 方式下只有 386。

## 五、總結

本文分析了根據 Unix Domain  `Socket` 的連線建立、以及資料收發過程。其中資料收發的工作過程如下圖。

相對比本機網路 IO 通訊過程上，它的工作過程要清爽許多。其中 127.0.0.1 工作過程如下圖。

我們也對比了 UDP 和 TCP 兩種方式下的延遲和效能指標。在包體不大於 1KB 的時候，UDS 的效能大約是 TCP 的兩倍多。所以，在本機網路 IO 的場景下，如果對效能敏感，飛哥建議你使用 Unix Domain `Socket`。
