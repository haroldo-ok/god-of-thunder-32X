import sys, struct

RES = "/home/user/dl/gotdata/GOTRES.DAT"
data = open(RES,'rb').read()

def dec_hdr(buf):
    return bytes((b ^ ((0x80+i) & 0xff)) & 0xff for i,b in enumerate(buf))

ENT = 23
hdr = dec_hdr(data[:23*256])
ents=[]
for n in range(256):
    e = hdr[n*ENT:(n+1)*ENT]
    name = e[:9].split(b'\0')[0].decode('latin1')
    off, length, ucsize = struct.unpack('<III', e[9:21])
    f1, f2 = e[21], e[22]
    ents.append((n,name,off,length,ucsize,f1,f2))

print("n    name        offset   length   ucsize   f1  f2")
for e in ents[:20]:
    print("%-4d %-11s %-8d %-8d %-8d %-3d %-3d" % e)
print("...")
for e in ents[-8:]:
    print("%-4d %-11s %-8d %-8d %-8d %-3d %-3d" % e)

# sanity: contiguity + bounds
bad=0
for i in range(255):
    if ents[i][2]+ents[i][3] != ents[i+1][2]: bad+=1
print("noncontig:",bad,"filesize",len(data),"last end",ents[-1][2]+ents[-1][3])
print("nonzero f1 count:",sum(1 for e in ents if e[5]),"nonzero f2:",sum(1 for e in ents if e[6]))
print("distinct f1:",sorted(set(e[5] for e in ents)),"distinct f2:",sorted(set(e[6] for e in ents)))
