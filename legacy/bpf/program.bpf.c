#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>

#include "vmlinux.h"

#define BUF_SIZE 255

struct event {
    char buf[BUF_SIZE];
    int count;
    int len;
    int off;
    int fd;
};

struct {
    __uint(type, BPF_MAP_TYPE_RINGBUF);
    __uint(max_entries, 4096 * 64);
} buffer SEC(".maps");

/*
struct {
    __uint(type, BPF_MAP_TYPE_ARRAY);
    __uint(max_entries, 4);
} info SEC(".maps");

static int handle_dropping(void) {
    int zero = 0;
    u64 *dropped = bpf_map_lookup_elem(&info, &zero);
    if (dropped) {
        u64 newval = *dropped + 1;
        bpf_map_update_elem(&info, &zero, &newval, 0);
    } else {
        u64 newval = 1;
        bpf_map_update_elem(&info, &zero, &newval, 0);
        return 1;
    }
    return 0;
}
*/

struct syscall_enter_write_args {
    unsigned long long unused;
    long __syscall_nr;
    unsigned long long fd;
    unsigned long long buf;
    unsigned long long count;
};

SEC("tracepoint/syscalls/sys_enter_write")
int tracepoint__syscalls__sys_enter_write(
    struct trace_event_raw_sys_enter *ctx) {
    int ret;
    int zero = 0;
    unsigned int len, off = 0;

    bpf_printk("BPF triggered\n");
    /*
    int pid = bpf_get_current_pid_tgid() >> 32;
    bpf_printk("BPF triggered from the PID %d.\n", pid);

    if ((bpf_get_current_pid_tgid() >> 32) != @PID) {
        return 0;
    }

    struct syscall_enter_write_args* args = (struct
    syscall_enter_write_args*)ctx; int fd = args->fd; if (fd != 1) { return 0;
    // not interested
    }

    int count = args->count;
    if (count == 0)
        return 0;

    struct event *event;
    const char *user_buf = (void*)args->buf;
    */

#if 0
    /* @SUBMIT_EVENTS */

    /* handle the left text if there is any
       (if we haven't terminated the program in SUBMIT_EVENTS) */
    event = buffer.ringbuf_reserve(sizeof(struct event));
    if (!event) {
        bpf_trace_printk("FAILED RESERVING SLOT (2) IN BUFFER\\n");
        return 1;
    }

    /* Tell monitor that the text was too long */
    event->buf[0] = 0;
    event->count = args->count - off;
    event->fd = args->fd;
    event->len = -1;
    event->off = off;
    buffer.ringbuf_submit(event, 0);
    bpf_trace_printk("TOO LONG\n", 10);
#endif

    return 0;
}

char _license[] SEC("license") = "GPL";
