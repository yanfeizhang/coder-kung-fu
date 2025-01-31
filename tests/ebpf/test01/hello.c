#include <stdio.h>
#include <stdlib.h>
#include <bpf/libbpf.h>
#include "hello_kprobe.skel.h"

static int libbpf_print_fn(enum libbpf_print_level level, const char *format, va_list args) {
    return vfprintf(stderr, format, args);
}

int main(int argc, char **argv) {
    struct hello_kprobe_bpf *skel;
    int err;

    // 设置 libbpf 的日志输出函数
    libbpf_set_print(libbpf_print_fn);

    // 打开并加载 eBPF 对象
    skel = hello_kprobe_bpf__open();
    if (!skel) {
        fprintf(stderr, "Failed to open BPF skeleton\n");
        return 1;
    }

    // 加载 eBPF 对象
    err = hello_kprobe_bpf__load(skel);
    if (err) {
        fprintf(stderr, "Failed to load BPF skeleton: %d\n", err);
        goto cleanup;
    }

    // 附加 kprobe
    err = hello_kprobe_bpf__attach(skel);
    if (err) {
        fprintf(stderr, "Failed to attach BPF skeleton: %d\n", err);
        goto cleanup;
    }

    printf("Kprobe attached successfully. Press Ctrl+C to exit.\n");

    // 保持程序运行，直到用户中断
    while (1) {
        sleep(1);
    }

cleanup:
    // 清理资源
    hello_kprobe_bpf__destroy(skel);
    return err < 0 ? -err : 0;
}