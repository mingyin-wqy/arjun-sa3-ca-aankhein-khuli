import pathlib,ctypes,wave,time,shutil,json
root=pathlib.Path(__file__).resolve().parent
testdir=root/'native_v030_test';testdir.mkdir(exist_ok=True)
bridge_source=root/'ArjunSuperAudioNative.dll'
if not bridge_source.exists():bridge_source=root.parent/'reframework/data/ArjunSuperAudio/ArjunAudioBridge.dll'
shutil.copy2(bridge_source,testdir/'ArjunAudioBridge.dll')
for n in [1,2]:
    with wave.open(str(testdir/f'{n}.WAV'),'wb') as w:
        w.setnchannels(2);w.setsampwidth(2);w.setframerate(44100);w.writeframes(bytes(44100*4*3))
bridge=ctypes.CDLL(str(testdir/'ArjunAudioBridge.dll'))
bridge.arjun_init(None);assert bridge.arjun_test_status()==0
def wait_for(predicate,timeout=3):
    deadline=time.monotonic()+timeout
    while not predicate() and time.monotonic()<deadline:time.sleep(.005)
    assert predicate(),'Audio transition timed out'
wait_for(lambda:bridge.arjun_test_devices_ready()==1,timeout=8)
assert bridge.arjun_test_status()==0
# Real PCM amplification, clamping and fade coefficients.
assert bridge.arjun_test_percent()==120
assert bridge.arjun_test_scale(10000,120,1000)==12000
assert bridge.arjun_test_scale(10000,300,1000)==30000
assert bridge.arjun_test_scale(20000,300,1000)==32767
assert bridge.arjun_test_scale(-20000,300,1000)==-32768
assert bridge.arjun_test_scale(10000,120,500)==6000
assert bridge.arjun_test_scale(10000,120,0)==0
# Natural completion fades within the final second of the source timeline.
assert bridge.arjun_test_tail_sample(10000,120,0,132300,44100)==12000
assert bridge.arjun_test_tail_sample(10000,120,88200,132300,44100)==12000
assert bridge.arjun_test_tail_sample(10000,120,110250,132300,44100)==6000
assert bridge.arjun_test_tail_sample(10000,120,132299,132300,44100)==0
bridge.arjun_set_volume_300(None);assert bridge.arjun_test_percent()==300
bridge.arjun_set_volume_0(None);assert bridge.arjun_test_percent()==0
bridge.arjun_set_volume_120(None)
# Scheduled 150ms delay does not consume game/battle callbacks.
bridge.arjun_p1_track1(None)
assert bridge.arjun_test_voices()==1 and bridge.arjun_test_playing()==0
start=time.monotonic();time.sleep(.10);assert bridge.arjun_test_playing()==0
wait_for(lambda:bridge.arjun_test_playing()==1)
delay=time.monotonic()-start;assert .14<=delay<.35,delay
print('Observed submission delay with prepared device: %.3fs'%delay)
# Miss starts a one-second fade; repeating another stop must not restart it.
start=time.monotonic();bridge.arjun_stop_p1(None)
assert bridge.arjun_test_fading()==1 and bridge.arjun_test_voices()==1
time.sleep(.4);assert bridge.arjun_test_voices()==1
assert 30000<bridge.arjun_test_device_gain()<47000
bridge.arjun_stop_all(None)
wait_for(lambda:bridge.arjun_test_voices()==0)
elapsed=time.monotonic()-start;assert .90<=elapsed<=1.6,elapsed
# New same file immediately cuts old confirmed audio, across player slots.
bridge.arjun_p1_track1(None);wait_for(lambda:bridge.arjun_test_playing()==1)
bridge.arjun_confirm_p1(None)
bridge.arjun_p2_track1(None)
assert bridge.arjun_test_voices()==1 and bridge.arjun_test_playing()==0
assert bridge.arjun_test_fading()==0
wait_for(lambda:bridge.arjun_test_playing()==1)
# Different files may coexist, and restore-state stop fades both confirmed voices.
bridge.arjun_confirm_p2(None);bridge.arjun_p1_track2(None)
assert bridge.arjun_test_voices()==2
wait_for(lambda:bridge.arjun_test_playing()==2)
bridge.arjun_confirm_p1(None);bridge.arjun_stop_all(None)
assert bridge.arjun_test_fading()==2
time.sleep(.4);assert bridge.arjun_test_voices()==2
wait_for(lambda:bridge.arjun_test_voices()==0)
# A cancellation before the scheduled start stays cancelled.
bridge.arjun_p1_track2(None);bridge.arjun_stop_p1(None)
assert bridge.arjun_test_voices()==0
time.sleep(.35);assert bridge.arjun_test_voices()==0
# Streaming continues and finishes with no Lua/battle/update callbacks.
bridge.arjun_p1_track1(None);bridge.arjun_confirm_p1(None)
natural_start=time.monotonic()
wait_for(lambda:bridge.arjun_test_voices()==0,timeout=7)
natural_elapsed=time.monotonic()-natural_start
assert 3.10<natural_elapsed<3.6,natural_elapsed
assert bridge.arjun_test_underruns()==0,bridge.arjun_test_underruns()
print(f'PASS: continuous 3s PCM finished in {natural_elapsed:.3f}s with zero underruns.')
print(f'PASS: 150ms schedule; 0-300% PCM gain/default120; sample saturation; one-second fade {elapsed:.3f}s; hard same-file replacement; different-file overlap; restore fade; queued cancellation; independent streaming completion.')
