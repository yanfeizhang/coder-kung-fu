---
layout: default
title: "NVIDIA通用計算首代架構 Tesla 與 CUDA 1.0 剖析"
---

# NVIDIA通用計算首代架構 Tesla 與 CUDA 1.0 剖析

> 原文標題：NVIDIA通用计算首代架构 Tesla 与 CUDA 1.0 剖析

> 原文連結：https://www.cnblogs.com/wujianming-110117/p/19034994

---

- 英偉達為啥要從遊戲顯示卡轉型科學計算？

- Tesla 中的統一著色架構到底是什麼，為什麼重要？

- 通用GPU中 SM 流多處理器都包含了哪些硬體模組？

- 為什麼合理使用常量能大幅度提升 GPU 的計算能力？

- CUDA如何實現使用C/C++就能進行GPU程式設計的？

- 一般來說，同一架構可衍生多個核心，差異在於SM數量，CUDA 核心數量、視訊記憶體頻寬等引數。例如：

- GA102（RTX 3090/3080）：10752 CUDA 核心，384-bit 視訊記憶體位寬。

- GA104（RTX 3060 Ti）：4864 CUDA 核心，256-bit 視訊記憶體位寬。

- RTX 3070：8GB GDDR6，2912 MHz 頻率。

- RTX 3060 Ti：8GB GDDR6，1665 MHz 頻率。

- 三角函式：如sin()、cos()、tan()。

- 指數與對數：如exp()、log()、pow()。

- 倒數與平方根：如1/x、sqrt(x)。

- 總共有 8 個 SPs，每個 SP 每個週期可執行一次 FP32 MAD 操作（包含兩次運算）

- 總共有 2 個 SFUs，每個 FPU 每個週期也可執行 4 次 FP32 運算

- GPU 記憶體的管理

- 核函式定義

- GPU核的抽象

- cudaMalloc：申請 GPU 全域性記憶體

- cudaMemcpy：在 CPU 與 GPU 之間互相複製的方式傳輸資料

- cudaFree：釋放 GPU 全域性記憶體

- 執行緒（Thread）：最小執行單元，執行相同指令（SIMT）。

- 執行緒塊（Thread Block）：一組協作執行緒，共享記憶體並可同步。

- 網格（Grid）：多個執行緒塊的集合，可分佈在多個 GPU 流多處理器（SM）上。

- CUDA 2.0（2008）：引入動態並行、原子操作。

- CUDA 5.0（2012）：支援雙精度浮點（FP64），最佳化 Kepler 架構。

- CUDA 11（2020）：引入安培架構，支援張量核心和混合精度計算。
