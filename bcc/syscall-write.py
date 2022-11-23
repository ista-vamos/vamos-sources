#!/usr/bin/python3

from sys import stderr, path as importpath, argv
from os.path import abspath, dirname
from os import execve, environ
from multiprocessing import Process, Value
from time import sleep
from re import compile as regex_compile

from bcc import BPF

importpath.append(abspath(dirname(argv[0])+"../../../python"))
from shamon import *

submit_next_event=\
r"""
    event = buffer.ringbuf_reserve(sizeof(struct event));
    if (!event) {
        bpf_trace_printk("FAILED RESERVING SLOT IN BUFFER");
        return handle_dropping();
    } else {
        u64 *dropped = info.lookup(&zero);
        if (dropped && *dropped > 0) {
            event->count = *dropped;
            event->len = -2;
            event->off = 0;
            *dropped = 0;
            buffer.ringbuf_submit(event, 0);
            return 0;
        }
    }

    len = (args->count - off > BUF_SIZE) ? BUF_SIZE : args->count - off;
    ret = bpf_probe_read_user(event->buf, len, user_buf + off);
    if (ret != 0) {
        bpf_trace_printk("FAILED READING USER STRING");
        buffer.ringbuf_discard(event, 0);
        return 1;
    } else {
        event->count = args->count - off;
        event->fd = args->fd;
        event->len = len;
        event->off = off;
        buffer.ringbuf_submit(event, 0);
    }
    off += len;
    if (off >= args->count)
        return 0;

"""

src = r"""
BPF_RINGBUF_OUTPUT(buffer, 1 << 6);
BPF_ARRAY(info, u64, 1);

#define BUF_SIZE 255
struct event {
    char buf[BUF_SIZE];
    int count;
    int len;
    int off;
    int fd;
};

static int handle_dropping(void) {
    int zero = 0;
    u64 *dropped = info.lookup(&zero);
    if (dropped) {
        u64 newval = *dropped + 1;
        info.update(&zero, &newval);
    } else {
        u64 newval = 1;
        info.update(&zero, &newval);
        return 1;
    }
    return 0;
}

TRACEPOINT_PROBE(syscalls, sys_enter_write) {
    int ret;
    int zero = 0;
    unsigned int len, off = 0;

    if ((bpf_get_current_pid_tgid() >> 32) != @PID) {
        return 0;
    }

    if (args->fd != 1) {
        return 0; // not interested
    }

    if (args->count == 0)
        return 0;

    struct event *event;
    const char *user_buf = args->buf;

    @SUBMIT_EVENTS

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

    return 0;
}
""".replace("@SUBMIT_EVENTS", 18*submit_next_event)

def printstderr(x):
    print(x, file=stderr)

kind = 2     # FIXME
gap_kind = 3 # FIXME
evid = 1

_regexc = None
get_event = None
shmbuf = None
signature = None
waiting_for_buffer = 0
partial = b""

def push_dropped(shmbuf, count):
    global evid
    # start a write into the buffer
    addr = buffer_start_push(shmbuf)
    while not addr:
        addr = buffer_start_push(shmbuf)
        global waiting_for_buffer
        waiting_for_buffer += 1
    # push the base data (kind and event ID)
    addr = buffer_partial_push(shmbuf, addr, gap_kind.to_bytes(8, "little"), 8)
    addr = buffer_partial_push(shmbuf, addr, evid.to_bytes(8, "little"), 8)
    addr = buffer_partial_push(shmbuf, addr,
                               count.to_bytes(8, "little"), 8)
    buffer_finish_push(shmbuf)
    evid += 1

def parse_lines(buff, shmbuf, regexc):
    global evid
    for line in buff.splitlines():
        m = regexc.match(line)
        if not m:
            continue
        #printstderr(f"> \033[33;2m`{line}`\033[0m")

        n = 1

        # start a write into the buffer
        addr = buffer_start_push(shmbuf)
        while not addr:
            addr = buffer_start_push(shmbuf)
            global waiting_for_buffer
            waiting_for_buffer += 1
        # push the base data (kind and event ID)
        addr = buffer_partial_push(shmbuf, addr, kind.to_bytes(8, "little"), 8)
        addr = buffer_partial_push(shmbuf, addr, evid.to_bytes(8, "little"), 8)

        for o in signature:
            if o == 'i':
                addr = buffer_partial_push(shmbuf, addr,
                                           int(m[n]).to_bytes(4, "little"), 4)
            elif o == 'l':
                addr = buffer_partial_push(shmbuf, addr,
                                           int(m[n]).to_bytes(8, "little"), 8)
            elif o == 'S':
                addr = buffer_partial_push_str(shmbuf, addr, evid, m[n])
            elif o == 'M':
                addr = buffer_partial_push_str(shmbuf, addr, evid, m[0])
            else:
                raise NotImplementedError("Not implemented signature op")
            n += 1

        buffer_finish_push(shmbuf)
        evid += 1

def callback(ctx, data, size):
    global partial

    event = get_event(data)
    s = event.buf
   #printstderr(f"\033[34;1m[fd: {event.fd}, count: {event.count}, "\
   #            f"off: {event.off}, len: {event.len}]\033[0m")
    event_len = event.len
    if event_len == event.count:
        buff = None
        if partial:
            buff = partial + s
            partial = b""
        else:
            buff = s

        assert buff is not None
        parse_lines(buff.decode("utf-8", "ignore"), shmbuf, _regexc)
    elif event_len < event.count:
        if event_len == -1:
            #printstderr(partial + s)
            printstderr(f"... TEXT TOO LONG, DROPPED {event.count} CHARS")
            partial = b""
            raise NotImplementedError("Unhandled situation")
        elif event_len == -2:
            printstderr(f"DROPPED {event.count} EVENTS")
            push_dropped(shmbuf, event.count)
            partial = b""
        else:
            partial += s
    else:
        raise NotImplementedError("Unhandled situation")

def usage_and_exit():
    printstderr(f"Usage: {argv[0]} shmkey event-name regex signature -- command arg1 arg2 ...")
    exit(1)

def spawn_process(cmd, syncval):
    printstderr("Spawning " + " ".join(cmd))

    # notify parent that we are spawned
    syncval.value = 1
    # wait until the parent got our PID and started BPF program
    while syncval.value != 2:
        sleep(0.05)

    # everything is ready, go!
    execve(cmd[0], cmd, environ.copy())
            
def main():
    global _regexc
    global get_event
    global shmbuf
    global signature

    # for now we assume a fixed structure
    try:
        sep_idx = argv.index("--")
    except ValueError:
        usage_and_exit()
    if sep_idx != 5 or len(argv) < 7:
        usage_and_exit()

    shmkey = argv[1]
    event_name = argv[2]
    regex = argv[3]
    signature = argv[4]
    cmd = argv[6:]

    _regexc = regex_compile(regex)

    event_size = signature_get_size(signature) + 16  # + kind + id
    printstderr(f"Event size: {event_size}")

    shmbuf = create_shared_buffer(shmkey, event_size,
                                  f"{event_name}:{signature},gap:l")

    syncval = Value('i', 0)
    proc = Process(target=spawn_process, args=(cmd, syncval))
    proc.start()
    while syncval.value != 1:
        sleep(0.05)

    bpf = BPF(text=src.replace("@PID", str(proc.pid)))
    get_event = bpf['buffer'].event
    bpf['buffer'].open_ring_buffer(callback)

    # wait until a monitor is attached
    printstderr("Waiting for a monitor...")
    while not buffer_monitor_attached(shmbuf):
        sleep(0.05)

    # let the program proceed
    syncval.value = 2

    recv_event = bpf.ring_buffer_consume
    try:
        while True:
            recv_event()
            if not proc.is_alive():
                # make sure that we have consumed all events
                bpf.ring_buffer_consume()
                break
    except KeyboardInterrupt:
        exit(0)
    finally:
        printstderr(f"Waited for buffer {waiting_for_buffer} cycles")
        destroy_shared_buffer(shmbuf)
        proc.join()

if __name__ == "__main__":
    main()
