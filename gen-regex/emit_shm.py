from utils import *

def adjust_regex(regex):
    """ Transform regex from POSIX syntax to RE2C syntax """
    return regex

class SourceGenerator:
    def __init__(self, template, output, shmkey, events):
        self.template = template
        if template.endswith(".in"):
            template = template[:-3]
        self.tmpoutput = f"{template}.tmp.c"
        self.output = output
        self.events = events
        self.shmkey = shmkey

        self.infile = open(self.template, "r")
        self.tmpfile = open(self.tmpoutput, "w")

    def _gen_push_simple_arg(self, write, n, c):
        fmt = c
        if c == "i":
            fmt = "d"
        elif c == "l":
            fmt = "ld"
        elif c == "d":
            fmt = "lf"

        write(\
        f"""
         tmp_c = *yypmatch[{n}];
         *yypmatch[{n}] = 0;
         sscanf(yypmatch[{n-1}], \"%@FMT\", &op.@ARG);
         *yypmatch[{n}] = tmp_c;
         addr = buffer_partial_push(shm, addr, &op.@ARG, sizeof(op.@ARG));
         """.replace("@FMT", fmt).replace("@ARG", c)
         )

    def gen_push_event(self, sig):
        write = self.tmpfile.write
        for n, c in enumerate(sig, start=1):
            idx_begin = 2*n
            idx_end = 2*n+1
            if c in ('i', 'l', 'f', 'd'):
                self._gen_push_simple_arg(write, idx_end, c)
            elif c == 'c':
                write("addr = buffer_partial_push(shm, addr, "
                      f"yypmatch[{idx_begin}], sizeof(op.c));\n")
            elif c == 'l':
                write(f"tmp_c = *yypmatch[{idx_end}];\n")
                write(f"*yypmatch[{idx_end}] = 0;\n")
                write(f"sscanf(yypmatch[{idx_begin}], \"%l\", &op.l);\n")
                write(f"*yypmatch[{idx_end}] = tmp_c;\n")
                write("addr = buffer_partial_push(shm, addr, &op.l, sizeof(op.k));\n")
            elif c == 'L':
                write("addr = buffer_partial_push_str(shm, addr, ev.base.id, line);\n")
            elif c == 'M':
                write("addr = buffer_partial_push_str_n(shm, addr, ev.base.id, "
                      "yypmatch[0], yypmatch[1] - yypmatch[0]);\n")
            else:
                raise NotImplementedError(f"Not implemented yet: {sig}")

    def gen_parse_and_push(self):
        events = self.events

        for i in range(0, len(events), 3):
            ev_idx = int(i/3)
            ev_name = events[i]
            ev_regex = adjust_regex(events[i + 1])
            ev_sig = events[i + 2]

            parse_start =\
            f"""
            YYCURSOR = line;
            YYLIMIT = line + len;

            /*!re2c
               .*{ev_regex}.* {{
                   while (!(out = buffer_start_push(shm))) {{
                       ++waiting_for_buffer;
                   }}
                   out->base.kind = events[{ev_idx}].kind; // FIXME: cache these
                   out->base.id = ++ev.base.id;
                   addr = out->args;

                   char tmp_c;
            """
            parse_end =\
            f"""
                   buffer_finish_push(shm);
                   continue;
                }}
                *      {{ printf("NO match\\n"); goto next{ev_idx + 1}; }}
              */
            """

            write = self.tmpfile.write
            write(f"/* parsing event {ev_idx}: {ev_name}:{ev_sig} -> {ev_regex} */\n")
            write(f"if (events[{ev_idx}].kind != 0) {{")
            write(parse_start)
            self.gen_push_event(ev_sig)
            write(parse_end)
            write("}")
            write(f"next{ev_idx + 1}: ;")

    def source_control_src(self):
        events = self.events
        evs = ", ".join(f"\"{events[i]}\", \"{events[i+2]}\""
                        for i in range(0, len(events), 3))
        return\
        f"""
        /* Initialize the info about this source */
        struct source_control *control
            = source_control_define({len(events)/3}, {evs});
        assert(control);
        """

    def create_shared_buffer_src(self):
        return\
        f"""
        /* Create the shared buffer */
        create_shared_buffer(
            \"{self.shmkey}\",
            source_control_max_event_size(control),
            control);
        free(control);
        """

    def emit_declarations(self):
        return None

    def gen(self):
        subs = [
            ("@EVENTS_NUM", str(int(len(self.events) / 3))),
            ("@SOURCE_CONTROL", self.source_control_src()),
            ("@CREATE_SHARED_BUFFER", self.create_shared_buffer_src()),
        ]

        decl = self.emit_declarations()
        if decl:
            subs.append(("@DECLARATIONS", decl))

        write = self.tmpfile.write
        for line in self.infile:
            # do substitution
            if "@" in line:
                if line.strip() == "@PARSE_AND_PUSH":
                    self.gen_parse_and_push()
                    continue
                for s in subs:
                    line = line.replace(s[0], s[1])
            write(line)

        self.infile.close()
        self.tmpfile.close()

        run_re2c(self.tmpoutput, self.output)

        return self.output


