import socket, struct, time, math, cmath, sys, hashlib
if len(sys.argv)<2: raise SystemExit("usage: rtltcp_probe.py <ipad-wifi-address>")
HOST=sys.argv[1]
def connect():
    s=socket.create_connection((HOST,1234),timeout=5)
    h=b""
    while len(h)<12:
        d=s.recv(12-len(h))
        if not d: raise SystemExit("closed before greeting")
        h+=d
    return s,h
def cmd(s,a,v): s.sendall(struct.pack(">BI",a,v&0xFFFFFFFF))
def grab(s,secs):
    s.settimeout(3); buf=bytearray(); t0=time.time()
    while time.time()-t0<secs:
        d=s.recv(262144)
        if not d: break
        buf+=d
    return bytes(buf), time.time()-t0
def fft(x):
    n=len(x)
    if n==1: return x
    e=fft(x[0::2]); o=fft(x[1::2])
    t=[cmath.exp(-2j*math.pi*k/n)*o[k] for k in range(n//2)]
    return [e[k]+t[k] for k in range(n//2)]+[e[k]-t[k] for k in range(n//2)]
def analyse(b, rate, center):
    N=1024; K=32; acc=[0.0]*N
    step=len(b)//(K+1)//2*2
    for k in range(K):
        seg=b[k*step:k*step+2*N]
        X=fft([complex(seg[2*i]-127.5,seg[2*i+1]-127.5) for i in range(N)])
        for i in range(N): acc[i]+=abs(X[i])**2
    p=[10*math.log10(a/K+1e-9) for a in acc]; p=p[N//2:]+p[:N//2]
    med=sorted(p)[N//2]
    peaks=[]
    for i in sorted(range(N), key=lambda i:-p[i]):
        if i==N//2: continue
        f=center+(i-N//2)*rate/N
        if all(abs(f-q[0])>150e3 for q in peaks): peaks.append((f,p[i]-med))
        if len(peaks)==5: break
    probe=b[100000:100064]; rep=b.find(probe,100001)
    blocks=[hashlib.sha1(b[i:i+65536]).hexdigest() for i in range(0,len(b)-65536,65536)]
    I=[x-127.5 for x in b[0:400000:2]]; m=sum(I)/len(I); v=sum((x-m)**2 for x in I)/len(I)
    clip=(b.count(0)+b.count(255))/len(b)*100
    print("  stations (MHz, dB au-dessus du plancher):", ", ".join("%.2f +%.0f"%(f/1e6,d) for f,d in peaks))
    print("  repetition 64 o retrouvee: %s · blocs 64 Kio distincts %d/%d · I moy %.1f std %.1f · ecretage %.2f %%"%(
        "NON" if rep==-1 else "OUI a +%d"%(rep-100000), len(set(blocks)), len(blocks), m+127.5, math.sqrt(v), clip))
s,h=connect()
print("greeting", h[:4], "tuner", struct.unpack(">I",h[4:8])[0], "gains", struct.unpack(">I",h[8:12])[0])
for name,gm,g in (("manuel 29.7 dB",1,297),("automatique",0,None)):
    cmd(s,2,2560000); cmd(s,1,88800000); cmd(s,3,gm)
    if g is not None: cmd(s,4,g)
    grab(s,0.7)
    b,dt=grab(s,3.0)
    print("%s: %d o en %.2f s = %.3f Mo/s (attendu 5.120)"%(name,len(b),dt,len(b)/dt/1e6))
    analyse(b,2560000,88.8e6)
s.close()
