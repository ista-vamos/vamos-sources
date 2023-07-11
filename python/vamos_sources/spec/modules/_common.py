def gen_params(codegen, stmt, wr, wr_h):
    assert len(stmt.params) == 2, stmt
    for n, p in enumerate(stmt.params):
        if n > 0:
            wr(", ")
        codegen.gen(p, wr, wr_h)
