
**公众号: 开发内功修炼**

在我十年的工作生涯中，我虽然从事的是应用层的开发，但仍然一直保持着对底层的好奇。把工作中遇到的一些问题，进行深度思考。总结出来，在这里分享给有缘的你！

持续更新ing...


## 一、网络篇
**1.1 内核收包原理**
- [图解Linux网络包接收过程](https://mp.weixin.qq.com/s/GoYDsfy9m0wRoXi_NCfCmg)
- [Linux网络包接收过程的监控与调优](https://mp.weixin.qq.com/s/6iQ-OhEbQJbEcgi9kakSjg)

**1.2 TCP连接时间开销**
- [聊聊TCP连接耗时的那些事儿](https://mp.weixin.qq.com/s/wXyerOFoibRsaBmbl224gw)

**1.3 TCP连接内存开销**
- [漫画 | 一台Linux服务器最多能支撑多少个TCP连接](https://mp.weixin.qq.com/s/Lkyj42NtvqEj63DoCY5btQ)
- [漫画 | 理解了TCP连接的实现以后，客户端的并发也爆发了！](https://mp.weixin.qq.com/s/ta6upubg0o1w03YGUo8Trg)
- [漫画 | 花了七天时间测试，我彻底搞明白了 TCP 的这些内存开销!](https://mp.weixin.qq.com/s/BwddYkVLSYlkKFNeA-NUVg)
- [刨根问底儿，看我如何处理 Too many open files 错误！](https://mp.weixin.qq.com/s/GBn94vdL4xUL80WYrGdUWQ) <font color="#dd0000">(:cn:新)</font>
- [说出来你可能不信，内核这家伙在内存的使用上给自己开了个小灶！](https://mp.weixin.qq.com/s/OR2XB4J76haGc1THeq7WQg) <font color="#dd0000">(:cn:新)</font>



## 二、硬盘篇
**2.1 硬件工作原理**
- [磁盘开篇：扒开机械硬盘坚硬的外衣！](https://mp.weixin.qq.com/s/OqhwSI4WsEyZlBhkFGPUlg)
- [磁盘分区也是隐含了技术技巧的](https://mp.weixin.qq.com/s/4HwUxy-4FClgIIei6JAzqw)
- [我们怎么解决机械硬盘既慢又容易坏的问题？](https://mp.weixin.qq.com/s/n9Hf3Utm4NFp3jNRTgZNwg)
- [拆解固态硬盘结构](https://mp.weixin.qq.com/s/6aPHMmz1kmiaBABhy8pF1Q)

**2.2 文件系统浅析**
- [新建一个空文件占用多少磁盘空间？](https://mp.weixin.qq.com/s/9YeUEnRnegplftpKlW4ZCA)
- [只有1个字节的文件实际占用多少磁盘空间](https://mp.weixin.qq.com/s/WE6BodR_q0GSKks_TgYL1w)
- [文件过多时ls命令为什么会卡住？](https://mp.weixin.qq.com/s/g-fFoYsBJkonV3ezdGDJKA)
- [理解格式化原理](https://mp.weixin.qq.com/s/DobymgQ-TRXrO32wjf2fWQ)

**2.3 文件读写性能**
- [read文件一个字节实际会发生多大的磁盘IO？](https://zhuanlan.zhihu.com/p/138371910)
- [write文件一个字节后何时发起写磁盘IO？](https://zhuanlan.zhihu.com/p/142441899)
- [机械硬盘随机IO慢的超乎你的想象](https://mp.weixin.qq.com/s/qz57uPtFaoQ_5z59NSBEUQ)
- [搭载固态硬盘的服务器究竟比搭机械硬盘快多少？](https://mp.weixin.qq.com/s/4CKPLpEDDbIyqJGxYDAUeA)


## 三、内存篇
**3.1 硬件工作原理**
- [带你深入理解内存对齐最底层原理](https://mp.weixin.qq.com/s/F0NTfz-3x3UxQeF-GSavRg)
- [内存随机也比顺序访问慢，带你深入理解内存IO过程](https://mp.weixin.qq.com/s/ps8VfGpbL4-xKggM2ywjHw)
- [从DDR到DDR4，内存核心频率其实基本上就没太大的进步](https://mp.weixin.qq.com/s/LRxhKrUOgyCexYN1lOwTyA)

**3.2 内存性能测试**
- [实际测试内存在顺序IO和随机IO时的访问延时差异](https://mp.weixin.qq.com/s/_-Ar944KlztzmFYdA3JXnQ)
- [揭穿内存厂家“谎言”，实测内存带宽真实表现](https://mp.weixin.qq.com/s/AJjBHCNPWN8YW8v0iXjjig)
- [NUMA架构下的内存访问延迟区别！](https://mp.weixin.qq.com/s/xUZl5wGRVvJI_Hfivg0hVQ)
- [挑战Redis单实例内存最大极限，“遭遇”NUMA陷阱！](https://mp.weixin.qq.com/s/dag1Zp1h4lQfqeUUGz4Ogw)

**3.3 内存性能优化实例**
- [一次内存性能提升的项目实践](https://mp.weixin.qq.com/s/foJJ2E7_jVgnOeJ9Du6qJg)
- [PHP7内存性能优化的思想精髓](https://mp.weixin.qq.com/s/3DrDb0CY8dUmFUKtuWzhqA)


## 四、CPU篇
**4.1 了解CPU硬件**
- [你以为你的多核CPU都是真核吗？多核“假象”](https://mp.weixin.qq.com/s/XX1yh8BTgT256pAnfosQkw)
- [听说你只知内存，而不知缓存？CPU表示很伤心！](https://mp.weixin.qq.com/s/PQTuFZO51an6OAe3WX4BVw)
- [TLB缓存是个神马鬼，如何查看TLB miss？](https://mp.weixin.qq.com/s/mssTS3NN7-w2df1vhYSuYw)

**4.2 内核CPU开销浅析**
- [进程/线程切换究竟需要多少开销？](https://mp.weixin.qq.com/s/uq5s5vwk5vtPOZ30sfNsOg)
- [软中断会吃掉你多少CPU？](https://mp.weixin.qq.com/s/mlenlX3-6H0shfOIvi8E8g)
- [一次系统调用开销到底有多大？](https://mp.weixin.qq.com/s/2nIDLeMR984_Sdgh01BHIQ)
- [一次简单的php请求redis会有哪些开销？](https://mp.weixin.qq.com/s/yl5EuQ1wEXDuIg4E98QfZA)

**4.3 用户态CPU开销分析**
- [协程究竟比线程牛在什么地方？](https://mp.weixin.qq.com/s/N4W0-0cP1wlxtLILx3oXpg)
- [函数调用太多了会有性能问题吗？](https://mp.weixin.qq.com/s/G30VtOIYjx2Wa54xlO7udw)
