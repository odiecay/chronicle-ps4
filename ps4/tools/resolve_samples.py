import bisect,collections,sys,re
samples_path,nm_path=sys.argv[1],sys.argv[2]
lines=open(samples_path).read().split('\n')
base=int(lines[0].split()[1],16); rips=[int(x,16) for x in lines[1:] if x]
IMG=0x140000000
addrs=[];names=[]
for l in open(nm_path,encoding='utf-8',errors='replace'):
    parts=l.rstrip('\n').split(' ',2)
    if len(parts)<3 or parts[1] not in 'Tt': continue
    try: a=int(parts[0],16)
    except: continue
    if a<IMG: continue
    addrs.append(a); names.append(parts[2])
cnt=collections.Counter(); area=collections.Counter(); out=0
for r in rips:
    va=r-base+IMG
    if va<IMG or va>IMG+0x20000000: out+=1; continue
    i=bisect.bisect_right(addrs,va)-1
    if i<0: out+=1; continue
    n=names[i]; cnt[n]+=1
total=len(rips); print('%d samples; %d outside the game image (system/driver: %.1f%%)'%(total,out,100*out/total))
for n,c in cnt.most_common(int(sys.argv[3]) if len(sys.argv)>3 else 35):
    print('%5.1f%%  %s'%(100*c/total,n[:110]))
