#include <bpf/libbpf.h>
#include <stdio.h>
#include <unistd.h>

struct event {
    __u32 pid;
    char filename[16];
};

static int buf_process_sample(void *ctx, void *data, size_t len) {
    struct event *evt = (struct event *)data;
    printf("%d %s\n", evt->pid, evt->filename);

    return 0;
}

int main(int argc, char *argv[]) {
    int prog_fd, map;
    const char *bpf_file = "program.bpf";
    struct bpf_object *obj;
    struct bpf_program *prog;
    struct ring_buffer *ring_buffer;

    obj = bpf_object__open(bpf_file);
    if (libbpf_get_error(obj)) {
        perror("failed loading BPF program");
        return 1;
    }

    prog = bpf_object__find_program_by_name(
        obj, "tracepoint__syscalls__sys_enter_write");
    if (!prog) {
        fprintf(stderr, "Failed finding the program\n");
        return 1;
    }

    bpf_program__set_type(prog, BPF_PROG_TYPE_TRACEPOINT);
    int err = bpf_object__load(obj);
    if (err < 0) {
        perror("failed loading BPF object");
        return 1;
    }
    prog_fd = bpf_program__fd(prog);

    map = bpf_object__find_map_fd_by_name(obj, "buffer");
    if (map < 0) {
        fprintf(stderr, "buffer not found\n");
        return 1;
    }

    ring_buffer = ring_buffer__new(map, buf_process_sample, NULL, NULL);

    if (!ring_buffer) {
        fprintf(stderr, "failed to create ring buffer\n");
        return 1;
    }

    bpf_program__attach(prog);

    /*
    bpf_program__attach_tracepoint(prog, "syscalls", "sys_enter_execve");
    */

    while (1) {
        ring_buffer__consume(ring_buffer);
        sleep(1);
    }

    return 0;
}
