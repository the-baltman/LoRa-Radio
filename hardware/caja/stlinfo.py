import sys,re
def load(p):
    tris=[];cur=[]
    for line in open(p):
        line=line.strip()
        if line.startswith('vertex'):
            cur.append(tuple(map(float,line.split()[1:4])))
            if len(cur)==3: tris.append(cur);cur=[]
    return tris
def vol(tris):
    v=0
    for a,b,c in tris:
        v+=(a[0]*(b[1]*c[2]-b[2]*c[1])-a[1]*(b[0]*c[2]-b[2]*c[0])+a[2]*(b[0]*c[1]-b[1]*c[0]))/6
    return v
def comps(tris):
    par={}
    def f(x):
        while par.setdefault(x,x)!=x:
            par[x]=par[par[x]];x=par[x]
        return x
    for t in tris:
        r=[f(tuple(round(c,3) for c in v)) for v in t]
        par[f(r[0])]=f(r[1]);par[f(r[1])]=f(r[2])
    g={}
    for t in tris:
        g.setdefault(f(tuple(round(c,3) for c in t[0])),[]).append(t)
    return list(g.values())
if __name__=='__main__':
    for p in sys.argv[1:]:
        t=load(p)
        if not t: print(p,'VACIO');continue
        cs=comps(t)
        print(p,'V=%.1f mm3'%vol(t),'tris',len(t),'bloques',len(cs))
        if len(sys.argv)==2 or '-v' in sys.argv:
            pass
        for c in cs:
            xs=[v[i] for tr in c for v in tr for i in (0,)];ys=[v[1] for tr in c for v in tr];zs=[v[2] for tr in c for v in tr]
            print('   bbox x %.1f..%.1f y %.1f..%.1f z %.1f..%.1f  V=%.2f'%(min(xs),max(xs),min(ys),max(ys),min(zs),max(zs),vol(c)))
