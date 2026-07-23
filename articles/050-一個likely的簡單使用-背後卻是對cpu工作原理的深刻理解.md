---
layout: default
title: "一個likely的簡單使用，背後卻是對CPU工作原理的深刻理解"
---

# 一個likely的簡單使用，背後卻是對CPU工作原理的深刻理解

> 原文標題：一个likely的简单使用，背后却是对CPU工作原理的深刻理解

> 原文連結：http://kfngxl.cn/index.php/archives/618/

---

大家好，我是飛哥！

今天我給大家分享一個核心中常用的提升效能的小技巧。

在核心中很多地方都充斥著 likely、unlikely 這一對兒函式的使用。隨便揪兩處，比如在 TCP 連線建立的過程中的這兩個函式。

```
//file: net/ipv4/tcp_ipv4.c
int tcp_v4_conn_request(struct sock *sk, struct sk_buff *skb)
{
    if (likely(!do_fastopen))
        ...
}
//file: net/ipv4/tcp_input.c
int tcp_rcv_established(struct sock *sk, ...)
{
    if (unlikely(sk->sk_rx_dst == NULL))
        ......
}
```

在剛開始看原始碼的時候，我也沒弄懂這一對兒函式的玄機。後來才搞懂它原來對效能提升是有幫助的。今天我就來和大家聊聊 likely、unlikely 是如何幫助效能提升的。

#### 1.likely 和 unlikely

咱們先來挖挖這對兒函式的底層實現。

```
//file: include/linux/compiler.h
#define likely(x)   __builtin_expect(!!(x),1)
#define unlikely(x) __builtin_expect(!!(x),0)
```

可以看到其實它們就是對 \_\_builtin_expect 的一個封裝而已。\_\_builtin_expect 這個指令是 gcc 引入的。該函式作用是允許程式設計師將最有可能執行的分支告訴編譯器，來輔助系統進行分支預測。(參見[https://gcc.gnu.org/onlinedocs/gcc/Other-Builtins.html](https://gcc.gnu.org/onlinedocs/gcc/Other-Builtins.html))

它的用法為：\_\_builtin_expect(EXP, N)。意思是：EXP == N的機率很大。那麼上面 likely 和 unlikely 這兩句的具體含義就是：

- \_\_builtin_expect(!!(x),1) x 為真的可能性更大

- \_\_builtin_expect(!!(x),0) x 為假的可能性更大

當正確地使用了\_\_builtin_expect後，編譯器在編譯過程中會根據程式設計師的指令，將可能性更大的程式碼緊跟著前面的程式碼，從而減少指令跳轉帶來的效能上的下降。

#### 2.實際動手驗證

那我們實際動手寫兩個例子，來窺探一下編譯器是如何進行最佳化的。為此我寫了一段簡單的測試程式碼，如下：

```
#include <stdio.h>
#define likely(x) __builtin_expect(!!(x), 1)
#define unlikely(x)  __builtin_expect(!!(x), 0)

int main(int argc, char *argv[])
{
    int n;
    n = atoi(argv[1]);

    if (likely(n == 10)){
        n = n + 2;
    } else {
        n = n - 2;
    }
    printf("%d\n", n);
    return 0;
}
```

這段程式碼非常簡單，對使用者的輸入做一個 if else 判斷。在 if 中使用了 likely，也就是假設這個條件為真的機率更大。那麼我們來看看它編譯後的彙編碼來看看。

圖中上面紅框內是對 if 的彙編結果，可見它使用的是 jne 指令。它的作用是看它的上一句比較結果，如果不相等則跳轉到 4004bc 處，相等的話則繼續執行後面的指令。

在 jne 指令後面緊挨著的是 n = n + 2 對應的彙編程式碼，
也就是說它把`n = n + 2`這段程式碼邏輯放在了緊挨著自己身邊。而把 n = n - 2 的執行邏輯放在了離當前指令較遠的 4004bc 處。

我們再把 likey 換成 unlikey 看一下，發現結果是正好相反，這次把 n = n - 2 的執行邏輯放在前面了，如圖。

注意，編譯時需要加 -O2 選項，使用 objdump -S 來檢視彙編指令。為了方便大家使用，我把它寫到 makefile 裡了，和測試程式碼一起放到了咱們的 Github 上了。大家想動手的可以玩玩，地址：[https://github.com/yanfeizhang/coder-kung-fu/tree/main/tests/cpu/test01](https://github.com/yanfeizhang/coder-kung-fu/tree/main/tests/cpu/test01)

#### 3.效能提升原理

那這麼做為什麼就能夠提升程式執行效能呢？原因有兩個。

首先第一個原因就是 CPU 快取記憶體。現代的 CPU 一般都有三級的快取，用來解決記憶體訪問過慢的問題。訪問資料時優先從 L1 讀取，讀取不到再讀 L2、還讀不到就讀 L3，最後用記憶體兜底。

圖中的資料來源於飛哥早期的一次測試，參見[實際內訪問延時](https://mp.weixin.qq.com/s/_-Ar944KlztzmFYdA3JXnQ)

這裡要注意的是，每一次快取資料的單位都是以一個 CacheLine64 位元組為單位進行儲存的。假如說要查詢的資料在 L1 中不存在，那麼 CPU 的做法是一次性從 L2 中把要訪問的資料及其後面的64 個位元組全部快取進來。

假如下一次再執行的時候要訪問的指令在上一次已經在 L1 中存在了，那麼就直接訪問 L1，就不必再從 L2 來讀取了。回到上面的例子，假如說我們執行完了 cmp 對比指令以後，判斷確實是要進加法分支，那緊接著就會執行 jne 後面的 mov xor 等指令大機率就可以從 L1 中取到了。

假如說 cmp 對比執行後，發現是要跳到 4004bc 處的指令執行。那大機率該位置處的指令還沒有被載入到快取中（實踐中一個分支下可能會包含很多程式碼，而不是像我這個例子中簡單的兩三行），就避免不了從 L2 L3 甚至是速度更慢的 L3 去讀取指令。

除了快取記憶體這個原因以外，還有一個更底層的原理。那就是 CPU 的流水線技術。CPU 在執行程式指令的時候，並不是先執行一個，執行完了再執行下一個這樣的。而是把每個指令都分成了多個階段，並讓不同指令的各步操作重疊，從而實現幾條指令並行處理。

還拿上面編譯出來的彙編碼來舉例，程式中 cmp、jne、mov 幾個指令是挨著的，那麼 CPU 在執行的時候實際是大概這樣的。

當 jne 指令正在執行的時候，後面的兩個 mov 指令都已經分別進入到譯碼和取址階段了都。假如說分支預測失敗，那麼這工作就白乾了。

#### 4.小結

總結一下，今天分享的 likely 和 unlikely​ 其實是屬於是輔助 CPU 分支預測的效能最佳化方法。這就是 likely 和 unlikely 背後的這點小秘密。它是為了讓 CPU 的快取記憶體命中率更高，同時也為了讓 CPU 的流水線更好地工作。​

Linux 作為一個基礎程式，在效能上真的是考慮到了極致。核心的作者們內功都是非常的深厚，都深諳計算機的底層工作原理。為了極致的效能追求精心打磨每一個細節，非常值得我們學習和借鑑。

不過這裡要說的一點是，likely 和 unlikely 的機率判斷務必準確。如果寫反了，反而會起到反作用。
今天分享到此結束，求轉發！

寫在最後，由於我的這些知識在公眾號裡文章比較分散，很多人似乎沒有理解到我對知識組織的體系結構。而且圖文也不像影片那樣理解起來更直接。所以我在知識星球上規劃了影片系列課程，包括硬體原理、記憶體管理、程序管理、檔案系統、網路管理、Golang語言、容器原理、效能觀測、效能最佳化九大部分大約 120 節內容，每週更新。加入方式參見[我要開始搞知識星球啦](https://mp.weixin.qq.com/s/_8ux274sY-As__Xwoqmewg)、[如何才能高效地學習技術,我投“融匯貫通”一票](https://mp.weixin.qq.com/s/z82z9jqnt08gBLYGxLHY2g)

Github：[https://github.com/yanfeizhang/coder-kung-fu](https://github.com/yanfeizhang/coder-kung-fu)
關注公眾號：微信掃描下方二維碼

我打造了一套更為完整的影片課程。總共包括了硬體原理、記憶體管理、程序管理、網路管理、檔案系統、容器原理、效能觀測、效能最佳化、呼叫原理、等幾個大部分的內容。

其中有 5 個大部分已經更新完畢，下圖中彩色部分為已更新內容，總時長 1900 多分鐘。

剩下各個大部分還在持續更新中。

除了影片內容，星球裡我也給大家開放各種電子 PDF、PPT 的下載。

目前我給大家仍然還開放 200 元的大額優惠券。加入後一年是 299 元。後期再續費目前是 6 折，也是 299。直接使用下方優惠券加入即可看上面的這些影片內容。
