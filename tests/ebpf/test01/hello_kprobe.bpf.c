#include <linux/bpf.h>
#include <bpf/bpf_helpers.h>

// 定义一个打印信息的函数
SEC("kprobe/handle_mm_fault")
int bpf_probe_handle_mm_fault() {
    // 打印一条消息到内核调试日志
    bpf_printk("Hello, World from kprobe!\n");
    return 0;
}

char _license[] SEC("license") = "GPL";
