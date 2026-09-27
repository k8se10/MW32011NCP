# usage: <venv>/bin/python dec32.py <x86.exe> <callgraph.cg from cg32.py> <hexVA>... -- angr decompile for the x86 build (no .pdata).
# Decompile x86 functions with angr; function bounds guessed from the direct-call-target set.
import logging, sys, bisect
import angr
for n in ('angr', 'cle', 'pyvex'): logging.getLogger(n).setLevel(logging.ERROR)
path, cg = sys.argv[1], sys.argv[2]
targets = sorted(set(int(l.split('\t')[2], 16) for l in open(cg)))
proj = angr.Project(path, auto_load_libs=False)
for a in (int(x, 16) for x in sys.argv[3:]):
    start = targets[bisect.bisect_right(targets, a) - 1]
    end = targets[bisect.bisect_right(targets, a)]
    cfg = proj.analyses.CFGFast(regions=[(start, max(end, start + 0x40))], function_starts=[start], normalize=True, resolve_indirect_jumps=True, data_references=True)
    f = cfg.kb.functions.get(start)
    print('=' * 20, hex(start), '(for %s)' % hex(a), '=' * 20)
    try:
        dd = proj.analyses.Decompiler(f, cfg=cfg.model); print(dd.codegen.text)
    except Exception as ex:
        print('decompile error', ex)
