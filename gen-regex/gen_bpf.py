
def gen_bpf(evtype, shmkey, events):
    print(events)

    template = "syscall.enter.write.bpf.c.in"
    tmpoutput = "regex.c.tmp.c"
    output = "regex.c"
    events_num = int(len(events) / 3)

    evs = ", ".join(f"\"{events[i]}\", \"{events[i+2]}\""
                    for i in range(0, len(events), 3))
    source_control_src =\
    f"""
    /* Initialize the info about this source */
    struct source_control *control
        = source_control_define({events_num}, {evs});
    assert(control);
    """
    create_shared_buffer_src =\
    f"""
    /* Create the shared buffer */
    create_shared_buffer(
        \"{shmkey}\", source_control_max_event_size(control), control);
    free(control);
    """
    subs = [
        ("@EVENTS_NUM", str(events_num)),
        ("@SOURCE_CONTROL", source_control_src),
        ("@CREATE_SHARED_BUFFER", create_shared_buffer_src),
    ]

    outfile = open(tmpoutput, "w")
    infile = open(template, "r")
    for line in infile:
        # do substitution
        if "@" in line:
            if line.strip() == "@PARSE_AND_PUSH":
                gen_parse_and_push(outfile, events)
                continue
            for s in subs:
                line = line.replace(s[0], s[1])
        outfile.write(line)
    infile.close()
    outfile.close()

    run_re2c(tmpoutput, output)

    return output


