# Gestionnaire d'une commande de script de gta_sa.exe 1.0 US : adresse du cas, nombre de parametres lus
# (CollectParameters 0x464080), et types vus dans main.scm (premiere occurrence plausible).
#   python run\saop.py 0936 0920 0157 ...
import sys, struct, re, capstone
EXE = r"D:\Games\COOPTEST\GTA San Andreas\SACoop-Joueur1\gta_sa.exe"
SCM = r"D:\Games\COOPTEST\GTA San Andreas\SACoop-Joueur1\data\script\main.scm"
d = open(EXE, 'rb').read()
pe = struct.unpack_from('<I', d, 0x3C)[0]
nsec = struct.unpack_from('<H', d, pe + 6)[0]
optsz = struct.unpack_from('<H', d, pe + 20)[0]
base = struct.unpack_from('<I', d, pe + 24 + 28)[0]
secs = []
for i in range(nsec):
    o = pe + 24 + optsz + i * 40
    vsz, va, rsz, raw = struct.unpack_from('<IIII', d, o + 8)
    secs.append((base + va, vsz, raw, rsz))
def off(a):
    for va, vsz, raw, rsz in secs:
        if va <= a < va + max(vsz, rsz): return raw + a - va
    raise ValueError(hex(a))
def u32(a): return struct.unpack_from('<I', d, off(a))[0]
md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
def dis(a, n=60):
    return list(md.disasm(d[off(a):off(a) + n * 8], a))[:n]

def case_addr(op):
    h = u32(0x8A6168 + (op // 100) * 4)
    ins = dis(h, 40)
    sub = None; btab = None; jtab = None
    for i in ins:
        m = re.match(r'(?:lea \w+, \[\w+ - (0x[0-9a-f]+)\]|add \w+, (0xfffff[0-9a-f]+)|add \w+, -(0x[0-9a-f]+))', i.mnemonic + ' ' + i.op_str)
        if m and sub is None:
            sub = int(m.group(1), 16) if m.group(1) else (0x100000000 - int(m.group(2), 16)) if m.group(2) else int(m.group(3), 16)
        m = re.search(r'byte ptr \[\w+ \+ (0x[0-9a-f]+)\]', i.op_str)
        if i.mnemonic == 'movzx' and m: btab = int(m.group(1), 16)
        m = re.search(r'jmp dword ptr \[\w+\*4 \+ (0x[0-9a-f]+)\]', i.mnemonic + ' ' + i.op_str)
        if m: jtab = int(m.group(1), 16); break
    if sub is None or jtab is None: return None
    idx = op - sub
    if btab: idx = d[off(btab + idx)]
    return u32(jtab + idx * 4)

def nparams(a):
    last = None
    for i in dis(a, 30):
        if i.mnemonic == 'push' and i.op_str.startswith('0x') or (i.mnemonic == 'push' and i.op_str.isdigit()):
            last = int(i.op_str, 0)
        if i.mnemonic == 'call' and i.op_str == '0x464080': return last
        if i.mnemonic in ('ret', 'jmp') and i.op_str != '' and not i.op_str.startswith('0x47'): pass
    return None

scm = open(SCM, 'rb').read()
def types(op, n):
    pat = struct.pack('<H', op)
    res = {}
    for m in re.finditer(re.escape(pat), scm):
        p = m.end(); ts = ''
        ok = True
        for k in range(n):
            if p >= len(scm): ok = False; break
            t = scm[p]; p += 1
            sz = {1: 4, 2: 2, 3: 2, 4: 1, 5: 2, 6: 4, 9: 8, 0xA: 2, 0xB: 2, 0xF: 16, 0x10: 2, 0x11: 2, 7: 6, 8: 6}.get(t)
            if t == 0xE: sz = 1 + scm[p]
            if sz is None: ok = False; break
            p += sz
            ts += {1: 'i', 4: 'i', 5: 'i', 6: 'f', 2: 'g', 3: 'l', 7: 'G', 8: 'L', 9: 's', 0xA: 's', 0xB: 's', 0xE: 's', 0xF: 's', 0x10: 's', 0x11: 's'}[t]
        if ok: res[ts] = res.get(ts, 0) + 1
    return sorted(res.items(), key=lambda x: -x[1])[:4]

for a in sys.argv[1:]:
    op = int(a, 16)
    c = case_addr(op)
    n = nparams(c) if c else None
    print('%04X : cas %s, %s parametres, types %s' % (op, hex(c) if c else '?', n, types(op, n) if n else '-'))
