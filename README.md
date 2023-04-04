
我们所有的开发同学每时每刻都在使用内核、都在使用硬件。所以能正确理解内核是怎么工作的、硬件的性能指标大概是多少，这些对于高阶的开发工程师来说非常重要。想成为技术大牛，扎实练习内功是必经之路！  

飞哥的开发内功修炼从 CPU、内存、磁盘、网络四大模块进行深度思考，总结出来，在这里分享给积极上进的你！欢迎添加飞哥微信交流与围观朋友圈: zhangyanfei748527  

本 repo 持续更新ing...  

---
**新文置顶:最近写的都是CPU、进程方向的**  
- [如何正确获取容器的CPU利用率？](https://mp.weixin.qq.com/s/nKedQRFxmIgPBxtlIJasZw)  <font color="#dd0000">(:cn:新)</font> 
- [Linux 中 CPU 利用率是如何算出来的？](https://mp.weixin.qq.com/s/40KWGKNBoa35s533YGWYIQ)
- [从进程栈内存底层原理到Segmentation fault报错](https://mp.weixin.qq.com/s/pTP7ELOm4-JsAl1o_nyS-Q) 
- [Linux 中的负载高低和 CPU 开销并不完全对应](https://mp.weixin.qq.com/s/1Pl4tT_Nq-fEZrtRpILiig)  
- [Docker容器里进程的 pid 是如何申请出来的？](https://mp.weixin.qq.com/s/LDu6s1eZw6_xEwfa6pMM-A) 
- [内核是如何给容器中的进程分配CPU资源的？](https://mp.weixin.qq.com/s/rUQLM8WfjMqa__Nvhjhmxw)


---

大家好，飞哥在博客文章的基础上出版了一本书，名字叫做《深入理解Linux网络》。该书市场接受度非常的高，在首发当天就斩获了京东科技类图书的第一名，几天就将首印卖断了货。

![avatar](imgs/book.png)  

点击：[京东购买地址](https://union-click.jd.com/jdc?e=618%7Cpc%7C&p=JF8BANcJK1olXwcFV19UDkoTC18IGVsTWAIFUG4ZVxNJXF9RXh5UHw0cSgYYXBcIWDoXSQVJQwYAVFhYDEwTHDZNRwYlCF1nABo_UA50SxVoUidML2RaNEQUeEcbM244GFoWVQIBV11UDnsnA2g4STXN67Da8e9B3OGY1uefK1olXQEGUVdYD04VBmsMGWsSXQ8yHQYCXglTVDtXK2slXjYFVFdJDjlWUXsOaWslXTYBZBwzCB4XVmYBGl9HWFNXUVYJDEMWAGcAT1kQXA8DXV5ZAEsnAW4JH1IlbQ)

另外大家也可以加飞哥微信（zhangyanfei748527）进读者交流群。


## 一、网络篇
**1.1 内核收发包原理**
- [🔥图解Linux网络包接收过程](https://mp.weixin.qq.com/s/GoYDsfy9m0wRoXi_NCfCmg)
- [🔥25 张图，一万字，拆解 Linux 网络包发送过程](https://mp.weixin.qq.com/s/wThfD9th9e_-YGHJJ3HXNQ) 
- [Linux网络包接收过程的监控与调优](https://mp.weixin.qq.com/s/6iQ-OhEbQJbEcgi9kakSjg)
- [🔥127.0.0.1 之本机网络通信过程知多少 ?！](https://mp.weixin.qq.com/s/6_OfoeD3ZpyQisY2F-4_bw)  
- [本机网络IO之Unix Domain Socket与普通socket的性能对比](https://mp.weixin.qq.com/s/fHzKYlW0WMhP2jxh2H_59A) &nbsp;&nbsp;[实验使用源码](https://github.com/rigtorp/ipc-bench)

**1.2 内核如何与用户进程协作**
- [图解 | 深入理解高性能网络开发路上的绊脚石 - 同步阻塞网络 IO](https://mp.weixin.qq.com/s/cIcw0S-Q8pBl1-WYN0UwnA) 
- [🔥图解 | 深入揭秘 epoll 是如何实现 IO 多路复用的！](https://mp.weixin.qq.com/s/OmRdUgO1guMX76EdZn11UQ)
- [漫画 | 看进程小 P 讲述它的网络性能故事！](https://mp.weixin.qq.com/s/r7EDYsvVhWA2fv52mwM_zg)
- [在 golang 中是如何对 epoll 进行封装的？](https://mp.weixin.qq.com/s/hjWhh_zHfxmH1yZFfvu_zA)
- [万字多图，搞懂 Nginx 高性能网络工作原理！](https://mp.weixin.qq.com/s/AX6Fval8RwkgzptdjlU5kg)
- [单线程 Redis 如何做到每秒数万 QPS 的超高处理能力！](https://mp.weixin.qq.com/s/2y60cxUjaaE2pWSdCBX1lA)
- [Redis 6 中的多线程实现方式比我预期的要差](https://mp.weixin.qq.com/s/MU8cxoKS3rU9mN_CY3WxWQ) 
- [剖析Netty内部网络实现原理](https://mp.weixin.qq.com/s/DS52g3bibU9kNH75UFxGqA)

**1.3 TCP之三次握手**
- [为什么服务端程序都需要先 listen 一下？](https://mp.weixin.qq.com/s/hv2tmtVpxhVxr6X-RNWBsQ)
- [深入理解Linux端口重用这一特性](https://mp.weixin.qq.com/s/SYCUMvzktgeGbyAfRdqhmg) &nbsp;&nbsp;[配套实验源码](https://github.com/yanfeizhang/coder-kung-fu/blob/main/tests/network/test08/server.c)
- [绑定特殊 IP 之 0.0.0.0 的内部工作原理](https://mp.weixin.qq.com/s/IOs39stus5C2K6RQFc5CQQ)
- [TCP连接中客户端的端口号是如何确定的？](https://mp.weixin.qq.com/s/C-Eeoeh9GHxugF4J30fz1A)
- [能将三次握手理解到这个深度，面试官拍案叫绝！](https://mp.weixin.qq.com/s/vlrzGc5bFrPIr9a7HIr2eA)  
- [深入解析常见三次握手异常](https://mp.weixin.qq.com/s/7Cum6Y28H_gXLyrRFrthNw) 
- [如何正确查看线上半/全连接队列溢出情况？](https://mp.weixin.qq.com/s/f-TFX2t8CWRCGoyCByGkOw)

**1.4 TCP连接时间开销、内存开销**
- [聊聊TCP连接耗时的那些事儿](https://mp.weixin.qq.com/s/wXyerOFoibRsaBmbl224gw)
- [刨根问底儿，看我如何处理 Too many open files 错误！](https://mp.weixin.qq.com/s/GBn94vdL4xUL80WYrGdUWQ)
- [漫画 | 花了七天时间测试，我彻底搞明白了 TCP 的这些内存开销!](https://mp.weixin.qq.com/s/BwddYkVLSYlkKFNeA-NUVg)  

**1.5 单机百万并发系列**
- [🔥漫画 | 一台Linux服务器最多能支撑多少个TCP连接](https://mp.weixin.qq.com/s/Lkyj42NtvqEj63DoCY5btQ)  
- [🔥漫画 | 理解了TCP连接的实现以后，客户端的并发也爆发了！](https://mp.weixin.qq.com/s/ta6upubg0o1w03YGUo8Trg)  
- [百看不如一练，动手测试单机百万连接的保姆级教程！](https://mp.weixin.qq.com/s/f_CMt2ni0vegB3-pf2BTTg) 

**1.6 网络工具**
- [用户态 tcpdump 如何实现抓到内核网络包的?](https://mp.weixin.qq.com/s/ZX8Jluh-RgJXcVh3OvycRQ)   

**1.7 虚拟化**
- [轻松理解 Docker 网络虚拟化基础之 veth 设备！](https://mp.weixin.qq.com/s/sSQFINJ8RO8Nc4XtcyQIjQ)  <font color="#dd0000">(:cn:新)</font> 
- [聊聊 Linux 上软件实现的“交换机” - Bridge！](https://mp.weixin.qq.com/s/JnKz1fUgZmGdvfxOm2ehZg)&nbsp;&nbsp;[配套实验源码](/tests/network/test05/) <font color="#dd0000">(:cn:新)</font> 
- [彻底弄懂 Linux 网络命名空间](https://mp.weixin.qq.com/s/lscMpc5BWAEzjgYw6H0wBw)&nbsp;&nbsp;[配套实验源码](/tests/network/test06/) <font color="#dd0000">(:cn:新)</font> 
- [手工模拟实现 Docker 容器网络！](https://mp.weixin.qq.com/s/Arcz3RWe_o0Ijw6uPWKdVw)&nbsp;&nbsp;[配套实验源码](/tests/network/test07/) <font color="#dd0000">(:cn:新)</font>
- [理解 iptables 原理](https://mp.weixin.qq.com/s/O084fYzUFk7jAzJ2DDeADg)
- [天天讲路由，那 Linux 路由到底咋实现的！？](https://mp.weixin.qq.com/s/UHYE6vwMffaAb-o5eNMrDg)

**1.8 电子书** 
- [🔥开发内功修炼网络篇电子书出炉！！！](https://mp.weixin.qq.com/s/kE8y9em9a0Xv80YaQqPbRg)

## 二、硬盘篇  
- [经典，Linux文件系统十问](https://mp.weixin.qq.com/s/pOKjwl3ONPMPSRF6RSmvaw)

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
- [read文件一个字节实际会发生多大的磁盘IO？](https://mp.weixin.qq.com/s/vekemOfUHBjZSy3uXb49Rw)
- [write文件一个字节后何时发起写磁盘IO？](https://zhuanlan.zhihu.com/p/142441899)
- [机械硬盘随机IO慢的超乎你的想象](https://mp.weixin.qq.com/s/qz57uPtFaoQ_5z59NSBEUQ)
- [搭载固态硬盘的服务器究竟比搭机械硬盘快多少？](https://mp.weixin.qq.com/s/4CKPLpEDDbIyqJGxYDAUeA)


## 三、内存篇
**3.1 硬件工作原理**
- [带你深入理解内存对齐最底层原理](https://mp.weixin.qq.com/s/F0NTfz-3x3UxQeF-GSavRg)
- [内存随机也比顺序访问慢，带你深入理解内存IO过程](https://mp.weixin.qq.com/s/ps8VfGpbL4-xKggM2ywjHw)
- [从DDR到DDR4，内存核心频率其实基本上就没太大的进步](https://mp.weixin.qq.com/s/LRxhKrUOgyCexYN1lOwTyA)

**3.2 内核内存管理**
- [说出来你可能不信，内核这家伙在内存的使用上给自己开了个小灶！](https://mp.weixin.qq.com/s/OR2XB4J76haGc1THeq7WQg) 
- [明明还有大量内存，为啥报错“无法分配内存”？](https://mp.weixin.qq.com/s/Jo8KEzfb1OXShrb3PT4U_A) <font color="#dd0000">(:cn:新)</font>  
- [从进程栈内存底层原理到Segmentation fault报错](https://mp.weixin.qq.com/s/pTP7ELOm4-JsAl1o_nyS-Q) <font color="#dd0000">(:cn:新)</font> 


**3.3 内存性能测试**
- [实际测试内存在顺序IO和随机IO时的访问延时差异](https://mp.weixin.qq.com/s/_-Ar944KlztzmFYdA3JXnQ)
- [揭穿内存厂家“谎言”，实测内存带宽真实表现](https://mp.weixin.qq.com/s/AJjBHCNPWN8YW8v0iXjjig)
- [NUMA架构下的内存访问延迟区别！](https://mp.weixin.qq.com/s/xUZl5wGRVvJI_Hfivg0hVQ)
- [挑战Redis单实例内存最大极限，“遭遇”NUMA陷阱！](https://mp.weixin.qq.com/s/dag1Zp1h4lQfqeUUGz4Ogw)

**3.4 内存性能优化实例**
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

**4.4 CPU性能优化**
- [一个likely的简单使用，背后却是对CPU工作原理的深刻理解](https://mp.weixin.qq.com/s/2YJVAEanfjSYgmZh8GRNrg) <font color="#dd0000">(:cn:新)</font>
- [Linux 中 CPU 利用率是如何算出来的？](https://mp.weixin.qq.com/s/40KWGKNBoa35s533YGWYIQ)  <font color="#dd0000">(:cn:新)</font> 
- [如何正确获取容器的CPU利用率？](https://mp.weixin.qq.com/s/nKedQRFxmIgPBxtlIJasZw)  <font color="#dd0000">(:cn:新)</font> 


**4.5 进程管理**
- [Linux进程是如何创建出来的？](https://mp.weixin.qq.com/s/ftrSkVvOr6s5t0h4oq4I2w)  <font color="#dd0000">(:cn:新)</font> 
- [你写的代码是如何跑起来的？](https://mp.weixin.qq.com/s/1bdktqYF7VyAMadRlcRrSg)  <font color="#dd0000">(:cn:新)</font> 
- [聊聊Linux中线程和进程的联系与区别！](https://mp.weixin.qq.com/s/--S94B3RswMdBKBh6uxt0w)  <font color="#dd0000">(:cn:新)</font> 
- [你的新进程是如何被内核调度执行到的？](https://mp.weixin.qq.com/s/y2axbQTzOGZweJn3LAhWvg)  <font color="#dd0000">(:cn:新)</font> 
- [Linux 中的负载高低和 CPU 开销并不完全对应](https://mp.weixin.qq.com/s/1Pl4tT_Nq-fEZrtRpILiig)  <font color="#dd0000">(:cn:新)</font> 

**4.6 容器进程管理**
- [一次限制进程的 CPU 用量的实操过程](https://mp.weixin.qq.com/s/WQXURUChn8AZW17_Bi5plg)
- [Docker容器里进程的 pid 是如何申请出来的？](https://mp.weixin.qq.com/s/LDu6s1eZw6_xEwfa6pMM-A) <font color="#dd0000">(:cn:新)</font> 
- [内核是如何给容器中的进程分配CPU资源的？](https://mp.weixin.qq.com/s/rUQLM8WfjMqa__Nvhjhmxw) <font color="#dd0000">(:cn:新)</font> 


## 五、应用篇
- [峰值 QPS 50 万 +，性能优异的网络框架开源力作 Sogou Workflow！](https://mp.weixin.qq.com/s/clILKrOO7_XJs6uHp5hoUw)

## 六、答读者问
- [今天聊聊飞哥是怎么阅读内核源码的](https://mp.weixin.qq.com/s/-WA6J9BZeZJIQTZtDHD5hA)
- [答读者问，飞哥能否给推荐几本硬核技术书？](https://mp.weixin.qq.com/s/OQyGEi0rvJRS0HJstaPWKQ)
- [程序员完全没有时间提升自己该怎么办？](https://mp.weixin.qq.com/s/5f7x60bNCNK2a5nz8FL5yg)
- [写给内功修炼读者的一封信!](https://mp.weixin.qq.com/s/Pzo46nZtSlsv_M6Tq7pv_A)
- [我是怎么样写出开发内功修炼的？秘密都在这里！](https://mp.weixin.qq.com/s/rUWqVtoP3_f0M6ltN_1gqA)
- [给想成长为高级别开发同学的七条建议](https://mp.weixin.qq.com/s/8lMGzBzXine-NAsqEaIE4g)
  
## 七、公众号二维码

**敬请扫码关注微信公众号「开发内功修炼」，及时获得最新硬核文章！**   
![avatar](imgs/official_accounts.jpg)   

--- 
为了方便大家阅读，本 Github 的网络篇核心内容整理成了 PDF 电子书，也即将出版。该书好评众多，欢迎大家下载！ [**百度网盘地址**](https://pan.baidu.com/s/11u6z26xnRNlIOSQ4ohzNuA) 密码:q79d

如若网盘地址出问题，也可以通过微信（zhangyanfei748527）找飞哥，我直接发你！ 