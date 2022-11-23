from emit_shm import SourceGenerator

def gen_stdin(shmkey, events):
    return SourceGenerator("regex.c.in", "regex.c", shmkey, events).gen()

