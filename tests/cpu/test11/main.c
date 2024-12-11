#include <stdio.h>
#include <stdlib.h>
#include <sys/ptrace.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <signal.h>
#include <errno.h>
#include <string.h>
#include <sys/reg.h>

void handle_syscall(pid_t pid) {
	long syscall_number;
	char *syscall_name = NULL;

	// 读取系统调用号
	syscall_number = ptrace(PTRACE_PEEKUSER, pid, 8 * ORIG_RAX, NULL);  // 8 * ORIG_RAX 是 x86_64 架构下系统调用号的寄存器位置

	// 根据系统调用号获取系统调用名称
	switch (syscall_number) {
		case 5: syscall_name = "read"; break;
		case 6: syscall_name = "write"; break;
		case 10: syscall_name = "open"; break;
		case 11: syscall_name = "close"; break;
		// 添加更多系统调用号和名称的映射
		default: syscall_name = "unknown"; break;
	}

	printf("Syscall: %s (number: %ld)\n", syscall_name, syscall_number);

	
}

int main(int argc, char *argv[]) {
	if (argc != 2) {
		fprintf(stderr, "Usage: %s <PID>\n", argv[0]);
		exit(1);
	}

	pid_t pid = atoi(argv[1]);

	int status;

	if (ptrace(PTRACE_ATTACH, pid, NULL, NULL) == -1) {
		perror("PTRACE_ATTACH");
		exit(1);
	}

	if (waitpid(pid, &status, 0) == -1) {
		perror("waitpid");
		exit(1);
	}

	if (!WIFSTOPPED(status)) {
		fprintf(stderr, "Process did not stop\n");
		exit(1);
	}

	printf("Attached to process %d\n", pid);

	while (1) {
		if (ptrace(PTRACE_SYSCALL, pid, NULL, NULL) == -1) {
			perror("PTRACE_SYSCALL");
			exit(1);
		}

		if (waitpid(pid, &status, 0) == -1) {
			perror("waitpid");
			exit(1);
		}

		if (WIFEXITED(status)) {
			printf("Process exited\n");
			break;
		}

		if (WIFSTOPPED(status)) {
			handle_syscall(pid);
		}
	}

	if (ptrace(PTRACE_DETACH, pid, NULL, NULL) == -1) {
		perror("PTRACE_DETACH");
		exit(1);
	}

	printf("Detached from process %d\n", pid);

	return 0;
}
