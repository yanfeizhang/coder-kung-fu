---
layout: default
title: "峰值 QPS 50 萬 +，效能優異的網路 (Network) 框架 Sogou Workflow！"
---

# 峰值 QPS 50 萬 +，效能優異的網路 (Network) 框架 Sogou Workflow！

> 原文標題：峰值 QPS 50 万 +，性能优异的网络框架开源力作 Sogou Workflow！

> 原文連結：https://mp.weixin.qq.com/s/clILKrOO7_XJs6uHp5hoUw

---

今天我來為大夥介紹一下我們搜狗的最新官方 - Sogou C++ Workflow ！

Sogou Workflow 是一款效能優異的網路框架。在搜狗內部，幾乎所有的 C++ 後端服務，包含但不限於搜尋，輸入法，廣告等都是根據這個框架，每天處理超過百億的請求，它是一個已經經過多年線上考驗的工業級框架。開源以後，在業界也已經有十多家公司進行了實際應用。

Github 位址：https://github.com/sogou/workflow

我們來看看這款新的網路框架到底都有哪些優勢。

先來看伺服器端

在伺服器領域，我們拿最常見的應用場景，HTTP Server 來舉例。如果用 workflow，我們可以像 Go 一樣幾行程式碼就輕鬆寫出一個 HTTP 伺服器。如下：

```
void process(WFHttpTask *  server_task)
{
server_task-> get_resp()->append_output_body("Hello World!");
}

int main(int argc, char *argv[])
{
...
WFHttpServer server(process);
if(server.start(8888) == 0){
server.stop();
}
return 0;
}
```

完整原始碼參見：https://github.com/sogou/workflow/blob/master/docs/tutorial-04-http_echo_server.md

可見，workflow 遮蔽了所有的底層概念，例如 IO 執行緒 (Thread)，工作執行緒，任務佇列和超時處理等等。為開發者提供了一個簡潔到極致的開發方式，使用起來非常的簡單。

那麼這個根據 HTTP 伺服器的效能怎麼樣呢？我把官網裡的對比實驗結果搬出來。

- 測試環境：CPU 40 core @ 2.20GHz，記憶體 (Memory) 192G，網路卡 (Network Card)  `25000Mb/s`
- 執行緒數
- workflow：將使用 16 個 poller 執行緒和 20 個 handler 執行緒
- nginx：40 個 Worker 程序 (Process)
- brpc：40 個執行緒
- 資料長度：分別設定為 64、512 和 4096
- 併發度：在[1, 2K]之間翻倍增長

我們看下實際的實驗結果：

?

特別是資料長度為 64 和 512 的兩條曲線，併發度足夠的時候，可以保持 500K 的 QPS。

再看用戶端最佳化

我們都知道阻塞網路 IO 是高效能伺服器開發的絆腳石。雖然很多網路框架例如 libevent 裡已經有非常高效的非阻塞 (Non-Blocking) 解決方案，但是由於 C++ 缺乏像 Golang 一樣統一的網路 IO 封裝，總會有出現一些「漏網之魚」。

例如說當你的 C++ Server 需要存取 mysql 的時候，在 workflow 出現之前，一般就是選擇傳統的 mysql 用戶端。傳統用戶端的特點就是一個使用者執行緒以同步阻塞的方式等待連線上的資料。而我們的伺服器是要處理很多使用者的請求的，這就不得已需要建立很多的執行緒才行。除了引入額外的建立成本不說，頻繁的上下文切換 (Context Switch) 將帶來大量無謂的 CPU 開銷。

類似的還有 http、redis 和 kafka 用戶端。只要用到它們，就又會將自己精心最佳化過的服務拖回到阻塞的低效能泥潭中。

鑑於業務的這個痛點，workflow 自己實作了純非同步的多種用戶端。這些作為用戶端角色的網路 `socket` 也和接收到的使用者連線一起，統一交給 poller 執行緒進行高效的非阻塞 IO 管理。進而大大提升了 Server 作為用戶端請求資料需求時的效能表現。

而且使用起來也是非常的簡單方便，拿 mysql 來舉例：

```cpp
//在回呼函式這裡完成對回傳結果的處理
void   mysql_callback(WFMySQLTask *task)
{
...
}
int main(int argc, char *argv[])
{
...
task = WFTaskFactory::create_mysql_task(url, RETRY_MAX, mysql_callback);
task->  get_req()->set_query("show databases");
task->start();
...
}
```

完整原始碼參見：https://github.com/sogou/workflow/blob/master/docs/tutorial-12-mysql_cli.md

簡單的幾行，就能夠提交一個非同步的 MySQL 任務。從此以後，再也不用擔心傳統用戶端拖垮你的效能了。在合理的設定下，單機每秒能處理幾萬次 MySQL 請求。

還有哪些特性？

作為一個搜狗公司的通用的基礎設施， workflow 除了具備網路框架的高效能封裝以外。還有其他很多特色。

1、友好的使用者體驗
workflow 給開發者使用者接觸到的是 task（任務）和 series（任務流）。這個高度的抽象使得開發可以聚集所有精力完成應用邏輯的開發，完全不需要考慮執行緒等底層概念。

2、通訊與計算一體
大部分的 C++ 框架著重只解決網路 IO 的效率問題，而計算與任務排程框架需要使用者自己實作。workflow 框架則將二者融為一體，一併解決了。Workflow 自動對任務進行排程，並且打通了網路、磁碟 (Disk) 等等其他資源。

3、完善的文件和 demo
不同於許多為了開源而開源的 kpi 專案，workflow 配備了完整的文件和 demo。非常的易於上手，而且遇到問題時還有技術支援。

專案位址：https://github.com/sogou/workflow
