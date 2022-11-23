CC=clang

all: program.bpf loader

vmlinux.h:
	bpftool btf dump file /sys/kernel/btf/vmlinux format c > vmlinux.h

program.bpf: program.bpf.c vmlinux.h
	$(CC) $(CFLAGS) -target bpf -g -c $< -o program.bpf

loader: loader.c
	$(CC) -g -lbpf loader.c -o loader



