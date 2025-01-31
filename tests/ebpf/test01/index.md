## 环境准备

安装 clang 10 以上，我安装的是 clang-11

```sh
sudo apt install clang-11
```

安装 libbpf
```c
git clone https://github.com/libbpf/libbpf
cd libbpf/src 
make 
make install
INSTALL  bpf.h libbpf.h btf.h libbpf_common.h libbpf_legacy.h bpf_helpers.h bpf_helper_defs.h bpf_tracing.h bpf_endian.h bpf_core_read.h skel_internal.h libbpf_version.h usdt.bpf.h
INSTALL  ./libbpf.pc
INSTALL  ./libbpf.a ./libbpf.so ./libbpf.so.1 ./libbpf.so.1.6.0
```

## 编译运行程序

第一步，编译内核态程序 hello_kprobe.bpf.c

```sh

clang-11 -g -O2 -target bpf -D__TARGET_ARCH_x86_64 -I/usr/include/x86_64-linux-gnu -I. -c hello_kprobe.bpf.c -o hello_kprobe.bpf.o
```

第二步，基于可重定位未见生成 libbpf 脚手架代码

```c
bpftool gen skeleton hello_kprobe.bpf.o > hello_kprobe.skel.h
```

第三步，遍历链接用户态程序

```sh
clang-11 -g -O2 -Wall -I . -c hello.c -o hello.o
clang-11 -Wall -O2 -g hello.o -static -lbpf -lelf -lz -o hello
```

第四步，执行 eBPF 程序

```c
# sudo ./hello 
```

当程序运行起来后，就可以通过 trace_pipe 查看日志输出了

```sh
#sudo cat /sys/kernel/debug/tracing/trace_pipe
	pidof-2599356 [005] .... 1056938.433919: 0: Hello, World from kprobe!
	pidof-2599356 [005] .... 1056938.434294: 0: Hello, World from kprobe!
	pidof-2599356 [005] .... 1056938.434358: 0: Hello, World from kprobe!
	sudo-2599331 [014] .... 1056938.543342: 0: Hello, World from kprobe!
	sudo-2599331 [014] .... 1056938.543368: 0: Hello, World from kprobe!
	...
```



