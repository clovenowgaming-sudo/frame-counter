import json, struct
def varint(n):
    out=bytearray()
    while True:
        b=n&0x7f; n>>=7
        if n: out.append(b|0x80)
        else: out.append(b); break
    return bytes(out)
def cstr(s): return s.encode()+b'\0'
def packed(delta, down):
    first=((delta&0x3f)<<2)|(2 if down else 0)
    rest=delta>>6
    if rest: return bytes([first|1])+varint(rest)
    return bytes([first])
def gdr2(inputs_p1, inputs_p2, tag="", ext=False, platformer=False, fr=240):
    o=bytearray(b'GDR')+varint(1)+cstr(tag)+cstr("me")+cstr("desc")+varint(1000)+varint(22081)+varint(fr)+varint(5)+varint(0)+bytes([0, 1 if platformer else 0])+cstr("xdBot")+varint(2)+varint(1234)+cstr("TestLevel")+varint(0)
    o+=varint(2)+varint(100)+varint(50)  # deaths
    o+=varint(len(inputs_p1)+len(inputs_p2))+varint(len(inputs_p1))
    for lst in (inputs_p1,inputs_p2):
        prev=0
        for f,d in lst:
            o+=packed(f-prev,d); prev=f
            if ext: o+=varint(4)+b'\0\0\0\0'
    return bytes(o)
p1=[(0,True),(5,False),(100,True),(103,False),(1000,True),(1001,False),(5000,True)]
p2=[(10,True),(300,False)]
open('a.gdr2','wb').write(gdr2(p1,p2))
open('b.gdr2','wb').write(gdr2(p1,p2,tag="xdBot",ext=True))
open('c.gdr2','wb').write(gdr2(p1,p2,platformer=True))
js={"framerate":240.0,"bot":{"name":"Eclipse"},"level":{"name":"X"},"inputs":[{"frame":f,"btn":1,"2p":False,"down":d} for f,d in p1]}
open('d.gdr','w').write(json.dumps(js))
# msgpack
def mp(v):
    if isinstance(v,bool): return b'\xc3' if v else b'\xc2'
    if isinstance(v,int):
        if 0<=v<128: return bytes([v])
        if v<65536: return b'\xcd'+struct.pack('>H',v)
        return b'\xce'+struct.pack('>I',v)
    if isinstance(v,float): return b'\xcb'+struct.pack('>d',v)
    if isinstance(v,str):
        b=v.encode(); return bytes([0xa0|len(b)])+b if len(b)<32 else b'\xd9'+bytes([len(b)])+b
    if isinstance(v,list): return (bytes([0x90|len(v)]) if len(v)<16 else b'\xdc'+struct.pack('>H',len(v)))+b''.join(mp(x) for x in v)
    if isinstance(v,dict): return (bytes([0x80|len(v)]) if len(v)<16 else b'\xde'+struct.pack('>H',len(v)))+b''.join(mp(k)+mp(x) for k,x in v.items())
open('e.gdr','wb').write(mp(js))
open('f.gdr','wb').write(b'garbage-not-a-macro')
