# Missions de main.scm 1.0 qui contiennent une commande (motif opcode + types plausibles) et a quelle distance du debut.
#   python run\scmfind.py 0936 [8]
import sys, struct, re
SCM = r"D:\Games\COOPTEST\GTA San Andreas\SACoop-Joueur1\data\script\main.scm"
d = open(SCM, 'rb').read()
# segments : chaque segment commence par un saut 0002 01 <adresse du suivant>
def seg_next(p): return struct.unpack_from('<i', d, p + 3)[0]
s1 = seg_next(0)            # objets
s2 = seg_next(s1)           # missions
s3 = seg_next(s2)
p = s2 + 8                  # (0002 01 xxxx + id de segment) : taille du main, plus grande mission, nombre, 4 octets
main_size, big, nmiss = struct.unpack_from('<IIH', d, p)
p += 16
offs = [struct.unpack_from('<I', d, p + 4 * i)[0] for i in range(nmiss)]
offs.append(len(d))
op = int(sys.argv[1], 16)
pat = struct.pack('<H', op)
for i in range(nmiss):
    a, b = offs[i], offs[i + 1]
    hits = [m.start() for m in re.finditer(re.escape(pat), d[a:b])]
    hits = [h for h in hits if d[a + h + 2] in (1, 2, 3, 4, 5, 6)]
    if hits: print('mission %3d : %d occurrences, premiere a +%d (taille %d)' % (i, len(hits), hits[0], b - a))
