# Cherche dans le code de gta_sa.exe les instructions dont le texte contient un motif (regex).
#   python run\safind.py "\+ 0x514\]" [debut fin]
import sys, struct, re, capstone
EXE = r"D:\Games\COOPTEST\GTA San Andreas\SACoop-Joueur1\gta_sa.exe"
d = open(EXE, 'rb').read()
pe = struct.unpack_from('<I', d, 0x3C)[0]
optsz = struct.unpack_from('<H', d, pe + 20)[0]
base = struct.unpack_from('<I', d, pe + 24 + 28)[0]
o = pe + 24 + optsz
vsz, va, rsz, raw = struct.unpack_from('<IIII', d, o + 8)   # premiere section (.text)
start = int(sys.argv[2], 16) if len(sys.argv) > 2 else base + va
end = int(sys.argv[3], 16) if len(sys.argv) > 3 else base + va + rsz
md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
md.skipdata = True
pat = re.compile(sys.argv[1])
n = 0
for i in md.disasm(d[raw + start - base - va: raw + end - base - va], start):
    t = i.mnemonic + ' ' + i.op_str
    if pat.search(t):
        print('%08X  %s' % (i.address, t)); n += 1
        if n > 200: break
