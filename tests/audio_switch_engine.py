# Real libmpv regression. Usage: python3 audio_switch_engine.py LIBMPV MULTITRACK_MP4
# Fixture: 35 seconds, two audio streams, 24 fps, keyframes 10 seconds apart.
import ctypes as c,time,json,sys
m=c.CDLL(sys.argv[1])
for name,args,ret in [('mpv_create',[],c.c_void_p),('mpv_initialize',[c.c_void_p],c.c_int),('mpv_set_option_string',[c.c_void_p,c.c_char_p,c.c_char_p],c.c_int),('mpv_command_string',[c.c_void_p,c.c_char_p],c.c_int),('mpv_get_property',[c.c_void_p,c.c_char_p,c.c_int,c.c_void_p],c.c_int)]:
 f=getattr(m,name);f.argtypes=args;f.restype=ret
h=m.mpv_create()
for k,v in {'vo':'null','ao':'null','config':'no','idle':'yes','keep-open':'yes','msg-level':'all=error'}.items():assert m.mpv_set_option_string(h,k.encode(),v.encode())>=0
assert m.mpv_initialize(h)>=0
def cmd(s):
 r=m.mpv_command_string(h,s.encode());assert r>=0,(s,r)
def pos():
 d=c.c_double(-1);m.mpv_get_property(h,b'time-pos',5,c.byref(d));return d.value
cmd('loadfile '+json.dumps(sys.argv[2]));time.sleep(.5);cmd('set pause yes; seek 11.25 absolute+exact');time.sleep(.3)

class E(c.Structure):_fields_=[('id',c.c_int),('error',c.c_int),('user',c.c_uint64),('data',c.c_void_p)]
m.mpv_wait_event.argtypes=[c.c_void_p,c.c_double];m.mpv_wait_event.restype=c.POINTER(E)
def drain():
 while m.mpv_wait_event(h,0).contents.id:pass
def restart():
 end=time.monotonic()+3
 while time.monotonic()<end:
  if m.mpv_wait_event(h,.01).contents.id==21:return
 raise Exception('no restart')
for target in [0.,.05,1.25,9.25,11.25,19.25,21.25,29.25,34.5]:
 cmd(f'seek {target} absolute+exact');restart();drain()
 for i in range(4):
  p=pos();drain()
  cmd('no-osd raw set lavfi-complex '+json.dumps('[aid1][aid2]amix=inputs=2:duration=longest:dropout_transition=0,volume=2[ao]' if i%2==0 else '')+('' if i%2==0 else '; set aid no; set aid 2'))
  restart();drain();cmd(f'seek {p} absolute+exact');restart()
  actual=pos();print(target,i,p,actual,flush=True);assert abs(actual-p)<.1

cmd('set pause yes; seek 1 absolute+exact');restart()
for speed in [1.,1.5]:
 cmd(f'set speed {speed}; set pause no');time.sleep(.1)
 for i in range(6):
  cmd('set pause yes');p=pos();drain()
  cmd('set lavfi-complex '+json.dumps('[aid1][aid2]amix=inputs=2[ao]' if i%2==0 else '')+('' if i%2==0 else '; set aid no; set aid 1'))
  restart();drain();cmd(f'seek {p} absolute+exact');restart()
  assert abs(pos()-p)<.1
  cmd('set pause no');time.sleep(.2);assert 0 < pos()-p < speed*.3
print('playing and speed restoration passed',flush=True)
