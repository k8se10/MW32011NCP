# usage: <venv>/bin/python dec.py <exe> <hexVA>... -- angr per-function decompile bounded by .pdata (pip install angr in a venv).
# Decompile specific functions of an x64 PE with angr, using .pdata bounds (no whole-binary CFG).
# usage: venv/bin/python dec.py <exe> <hexVA> [<hexVA>...]
import logging, sys
import angr, pefile
logging.getLogger('angr').setLevel(logging.ERROR)
logging.getLogger('cle').setLevel(logging.ERROR)
logging.getLogger('pyvex').setLevel(logging.ERROR)
path = sys.argv[1]; addrs = [int(a, 16) for a in sys.argv[2:]]
pe = pefile.PE(path, fast_load=True); pe.parse_data_directories(directories=[3])
base = pe.OPTIONAL_HEADER.ImageBase
ranges = sorted((base + e.struct.BeginAddress, base + e.struct.EndAddress) for e in pe.DIRECTORY_ENTRY_EXCEPTION)
proj = angr.Project(path, auto_load_libs=False, load_options={'main_opts': {'base_addr': base}})
for a in addrs:
    # merge chained .pdata chunks that belong to the same function (contiguous, following)
    lo = a; hi = next((e for s, e in ranges if s == a), a + 0x400)
    extra = [(s, e) for s, e in ranges if hi <= s < hi + 0x2000]
    regions = [(lo, hi)] + extra[:32]
    cfg = proj.analyses.CFGFast(regions=regions, function_starts=[a], normalize=True, force_complete_scan=False, resolve_indirect_jumps=True, data_references=True)
    f = cfg.kb.functions.get(a)
    print('=' * 20, hex(a), '=' * 20)
    if f is None:
        print('no function'); continue
    try:
        d = proj.analyses.Decompiler(f, cfg=cfg.model)
        print(d.codegen.text if d.codegen else 'decompile failed')
    except Exception as ex:
        print('decompile error:', ex)
