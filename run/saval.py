# Valeurs initiales de gta_sa.exe 1.0 US (donnees de l'exe) et references a une adresse dans le code :
#   python run\saval.py val <adresse hex> [f|i|b]...   python run\saval.py refs <adresse hex>
import sys, struct
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
        if va <= addr < va + rsz: return raw + addr - va
    return None
if sys.argv[1] == 'val':
    for a in sys.argv[2:]:
        a = int(a, 16); o = off(a)
        if o is None: print('%08X : hors fichier (bss, 0 au depart)' % a); continue
        print('%08X : f=%g i=%d b=%s' % (a, struct.unpack_from('<f', d, o)[0], struct.unpack_from('<i', d, o)[0], d[o:o+8].hex()))
else:
    a = int(sys.argv[2], 16)
    pat = struct.pack('<I', a)
    n, va, vsz, raw, rsz = secs[0]
    i = raw
    while True:
        i = d.find(pat, i, raw + rsz)
        if i < 0: break
        print('%08X' % (va + i - raw))
        i += 1
