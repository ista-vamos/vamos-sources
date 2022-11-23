from emit_shm import SourceGenerator

class DRioSourceGenerator(SourceGenerator):
    def __init__(self, template, output, shmkey, events):
        super().__init__(template, output, shmkey, events)

    def emit_declarations(self):
        return
        """
        static _Atomic(bool) _write_lock = false;
        
        static inline void write_lock() {
            _Atomic bool *l = &_write_lock;
            bool unlocked;
            do {
                unlocked = false;
            } while (atomic_compare_exchange_weak(l, &unlocked, true));
        }
        
        static inline void write_unlock() {
            /* FIXME: use explicit memory ordering, seq_cnt is not needed here */
            _write_lock = false;
        }
        """

    def gen_push_event(self, sig):
        # FIXME: the locked code is too big
        self.tmpfile.write("write_lock(); /*FIXME: lock only the push */\n")
        super().gen_push_event(sig)
        self.tmpfile.write("write_unlock();\n")


def gen_drio(evtype, shmkey, events):
    # TODO: we ignore 'evtype' for now
    if evtype == "drio-stdin":
        return DRioSourceGenerator("drio-stdin.c.in", "drio-stdin.c",
                                   shmkey, events).gen()
    raise NotImplementedError(f"Not implemented: {evtype}")

