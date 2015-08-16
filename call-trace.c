#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <errno.h>
#include <sys/signal.h>
#include <sys/user.h>
#include <sys/types.h>
#include <sys/ptrace.h>
#include <sys/personality.h>

#ifndef __x86_64__
#error "Expects 64-bit"
#endif

int req_i(int i, char *msg) {
	if (i == -1) {
		perror(msg);
		exit(1);
	}
	return i;
}

int req_l(int l, char *msg) {
	if (l == -1) {
		perror(msg);
		exit(1);
	}
	return l;
}

void disable_aslr() {
	int persn = req_i(personality(0xFFFFFFFF), "Could not read personality");
	persn |= ADDR_NO_RANDOMIZE;
	req_i(personality(persn), "Could not change personality");
}

uint32_t get_32(int child, int addr, bool &ref) {
	long value = ptrace(PTRACE_PEEKDATA, child, cur_ptr, NULL);
	if (value == -1) {
		if (errno == EIO) {
			*ref = false;
			return 0;
		} else {
			perror("Could not read traceback");
			exit(1);
		}
	} else {
		*ref = true;
		return value;
	}
}

uint64_t get_64(int child, int addr, bool &ref) {
	*ref = true;
	uint32_t low = get_32(child, addr, ref);
	uint32_t high = *ref ? get_32(child, addr + 4, ref) : 0;
	return low | (high << 32);
}

int main(int argc, char *argv[]) {
	if (argc < 2) {
		fprintf(stderr, "Usage: %s <PATH> [ARGS...]\n", argv[0]);
		return 1;
	}
#ifndef NO_DISABLE_ASLR
	disable_aslr();
#endif
	int child = req_i(fork(), "Could not fork");
	if (child == 0) { // CHILD
		req_l(ptrace(PTRACE_TRACEME, 0, NULL, NULL), "Failed to PTRACE_TRACEME");
		execv(argv[1], argv + 1);
		perror("Failed to exec");
		exit(1);
	} else { // PARENT
		struct user_regs_struct regs;
		fprintf(stderr, "Child is %d.\n", child);
		uint64_t past = (uint64_t) -1, lastcur = 0, laststk = 0, lastbas = 0;
		int status = 0, count = -1;
		while (1) {
			req_i(waitpid(child, &status, 0), "Failed to wait");
			if (WIFEXITED(status)) {
				fprintf(stderr, "Child exited with %d.\n", WEXITSTATUS(status));
				break;
			} else if (WIFSIGNALED(status)) {
				fprintf(stderr, "Child terminated with signal %d.\n", WTERMSIG(status));
				exit(1);
			} else if (WIFCONTINUED(status)) {
				fprintf(stderr, "Child continued???\n");
				exit(1);
			} else if (WIFSTOPPED(status)) {
				if (WSTOPSIG(status) != SIGSTOP && WSTOPSIG(status) != SIGTRAP) {
					fprintf(stderr, "Child stopped by signal %d.\n", WSTOPSIG(status));
					exit(1);
				}
			} else {
				fprintf(stderr, "Unknown child status: %d\n", status);
				exit(1);
			}
			req_l(ptrace(PTRACE_GETREGS, child, NULL, &regs), "Could not get registers");
			uint64_t cur, stk, bas;
#ifdef __x86_64__
			cur = regs.rip;
			stk = regs.rsp;
			bas = regs.rbp;
#else
#ifdef __i386__
			cur = regs.eip;
			stk = regs.esp;
			bas = regs.ebp;
#else
#error "Cannot access registers!"
#endif
#endif
#if 0
			if ((cur - past) > 4 || (cur - past) < 0) {
#if 0
				if (lastcur == cur && stk == laststk) {
					printf("\r%x->%x [%x] (x%d)", past, cur, stk, ++count);
				} else {
#else
				if (stk != laststk) {
#endif
					printf("\n%x->%x [%x]", past, cur, stk);
					count = 1;
					lastcur = cur;
					laststk = stk;
				}
			}
#endif
			if (bas != lastbas) {
				printf("At %x\n", cur, bas);
				uint64_t cur_ptr = bas;
				while (cur_ptr != 0) {
					bool ref = true;
					uint64_t next_base = get_64(child, cur_ptr, &ref);
					uint64_t next_ret = get_64(child, cur WORKING HERE
					if (ref) {
						uint64_t value = valuelo + (valuehi << 32);
						printf("\t%x\n", cur_ptr, value);
						cur_ptr = value;
					} else {
						cur_ptr = 0;
					}
				}
			}
			lastbas = bas;
			laststk = stk;
			past = cur;
			req_l(ptrace(PTRACE_SINGLESTEP, child, NULL, NULL), "Could not step program");
		}
		return 0;
	}
}
