# Desassembleur de gta_sa.exe 1.0 US (verifier une adresse avant de la detourner) : python run\sadis.py <adresse hex> [n]
import sys, struct, capstone
EXE = r"D:\Games\COOPTEST\GTA San Andreas\SACoop-Joueur1\gta_sa.exe"
d = open(EXE, 'rb').read()
pe = struct.unpack_from('<I', d, 0x3C)[0]
nsec = struct.unpack_from('<H', d, pe + 6)[0]
optsz = struct.unpack_from('<H', d, pe + 20)[0]
base = struct.unpack_from('<I', d, pe + 24 + 28)[0]
secs = []
for i in range(nsec):
    o = pe + 24 + optsz + i * 40
    name = d[o:o+8].rstrip(b'\0')
    vsz, va, rsz, raw = struct.unpack_from('<IIII', d, o + 8)
    secs.append((name, base + va, vsz, raw, rsz))
def off(addr):
    for n, va, vsz, raw, rsz in secs:
        if va <= addr < va + max(vsz, rsz): return raw + addr - va
    raise ValueError(hex(addr))
md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
addr = int(sys.argv[1], 16); n = int(sys.argv[2]) if len(sys.argv) > 2 else 40
o = off(addr)
for i in md.disasm(d[o:o + n * 8], addr):
    print('%08X  %-24s %s %s' % (i.address, i.bytes.hex(), i.mnemonic, i.op_str))
    n -= 1
    if n <= 0: break
