#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <audiopolicy.h>
#include <ks.h>
#include <ksmedia.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>
#include <atomic>
#define API extern "C" __declspec(dllexport)
struct lua_State;
struct Track { WAVEFORMATEX fmt; int16_t* pcm; uint32_t frames; };
struct Voice { bool active,started,ended,fading; double cursor,end_due; ULONGLONG due,fade_start; };
static Track tracks[2];static Voice voices[2];static int pending[2]={-1,-1};
static HMODULE module;static wchar_t base[MAX_PATH];static CRITICAL_SECTION lock;
static HANDLE command_event,audio_event;static bool initialized=false,devices_ready=false;static std::atomic<bool> ready(false);
static int error_code=0,volume_percent=120,underruns=0,worker_delay=0;static ULONGLONG mix_passes=0;
// Non-default session, owned exclusively by this DLL. No endpoint/session/stream volume setters are used.
static const GUID session_id={0x713a48e2,0x2b3e,0x4812,{0x9b,0x35,0xc8,0xf5,0x47,0xea,0x6d,0x16}};
static const GUID float_subtype={3,0,0x10,{0x80,0,0,0xaa,0,0x38,0x9b,0x71}};
static void status_file(){wchar_t path[MAX_PATH],temp[MAX_PATH];swprintf(path,MAX_PATH,L"%lsnative_status.json",base);swprintf(temp,MAX_PATH,L"%lsnative_status.tmp",base);FILE*f=_wfopen(temp,L"wb");if(f){fprintf(f,"{\"ready\":%s,\"devices_ready\":%s,\"error\":%d,\"version\":\"0.3.3\",\"backend\":\"WASAPI isolated PCM\"}",ready?"true":"false",devices_ready?"true":"false",error_code);fclose(f);MoveFileExW(temp,path,MOVEFILE_REPLACE_EXISTING);}}
static int read_track(int n){wchar_t path[MAX_PATH];swprintf(path,MAX_PATH,L"%ls%d.WAV",base,n+1);FILE*f=_wfopen(path,L"rb");if(!f)return 1;unsigned char head[12];if(fread(head,1,12,f)!=12||memcmp(head,"RIFF",4)||memcmp(head+8,"WAVE",4)){fclose(f);return 2;}bool format=false;uint32_t bytes=0;for(;;){char tag[4];uint32_t size;if(fread(tag,1,4,f)!=4||fread(&size,4,1,f)!=1)break;long pos=ftell(f);if(!memcmp(tag,"fmt ",4)){if(size<16||size>4096){fclose(f);return 3;}if(fread(&tracks[n].fmt,1,16,f)!=16){fclose(f);return 4;}format=true;}else if(!memcmp(tag,"data",4)){if(bytes||!size||size>100*1024*1024){fclose(f);return 5;}tracks[n].pcm=(int16_t*)malloc(size);if(!tracks[n].pcm||fread(tracks[n].pcm,1,size,f)!=size){fclose(f);return 6;}bytes=size;}if(fseek(f,pos+size+(size&1),SEEK_SET))break;}fclose(f);WAVEFORMATEX&fmt=tracks[n].fmt;if(!format||!bytes||fmt.wFormatTag!=WAVE_FORMAT_PCM||fmt.wBitsPerSample!=16||fmt.nChannels<1||fmt.nChannels>2||!fmt.nSamplesPerSec||fmt.nBlockAlign!=fmt.nChannels*2||bytes%fmt.nBlockAlign)return 7;tracks[n].frames=bytes/fmt.nBlockAlign;return 0;}
static double tail_gain(double cursor,uint32_t frames,uint32_t rate){double left=(frames-cursor)/rate;return left>=1.?1.:left>0.?left:0.;}
static double interrupt_gain(const Voice&v,double render_ms){if(!v.fading)return 1.;double gain=1.-(render_ms-(double)v.fade_start)*.001;return gain>1.?1.:gain>0.?gain:0.;}
static int16_t scale_sample(int16_t sample,int percent,double gain){double value=sample*(percent*.01)*gain;return (int16_t)(value>32767.?32767.:value<-32768.?-32768.:value);}
static void stop_voice(int t){memset(&voices[t],0,sizeof(Voice));for(int p=0;p<2;p++)if(pending[p]==t)pending[p]=-1;}
static void fade_voice(int t){Voice&v=voices[t];if(!v.active)return;if(!v.started||GetTickCount64()<v.due){stop_voice(t);return;}if(!v.fading){v.fading=true;v.fade_start=GetTickCount64();}for(int p=0;p<2;p++)if(pending[p]==t)pending[p]=-1;}
static void play(int p,int t){if(!ready)return;EnterCriticalSection(&lock);stop_voice(t);Voice&v=voices[t];v.active=true;v.due=GetTickCount64()+150;pending[p]=t;LeaveCriticalSection(&lock);SetEvent(command_event);}
static void set_volume(int percent){if(!initialized)return;EnterCriticalSection(&lock);volume_percent=percent<0?0:percent>300?300:percent;LeaveCriticalSection(&lock);SetEvent(command_event);}
static bool any_voice(){return voices[0].active||voices[1].active;}
static void finish_expired(double now){for(int t=0;t<2;t++){Voice&v=voices[t];if(v.active&&v.ended&&now>=v.end_due)stop_voice(t);}}
static void mix_frame(double render_ms,uint32_t output_rate,double out[2]){
 out[0]=out[1]=0.;
 for(int t=0;t<2;t++){Voice&v=voices[t];Track&track=tracks[t];if(!v.active||v.ended||render_ms<(double)v.due)continue;
  v.started=true;double gain=interrupt_gain(v,render_ms);if(gain<=0.||v.cursor>=track.frames){v.ended=true;v.end_due=render_ms;continue;}
  double tail=tail_gain(v.cursor,track.frames,track.fmt.nSamplesPerSec);if(tail<gain)gain=tail;
  uint32_t a=(uint32_t)v.cursor,b=a+1<track.frames?a+1:a;double frac=v.cursor-a;
  for(int ch=0;ch<2;ch++){int src_ch=track.fmt.nChannels==1?0:ch;double sample=track.pcm[a*track.fmt.nChannels+src_ch]*(1.-frac)+track.pcm[b*track.fmt.nChannels+src_ch]*frac;
   out[ch]+=sample/32768.*(volume_percent*.01)*gain;}
  v.cursor+=(double)track.fmt.nSamplesPerSec/output_rate;
  if(v.cursor>=track.frames){v.ended=true;v.end_due=render_ms+1000./output_rate;}
 }
 for(int ch=0;ch<2;ch++){if(out[ch]>1.)out[ch]=1.;if(out[ch]<-1.)out[ch]=-1.;}
}
static void write_sample(BYTE*dst,double sample,bool floating,int bits){if(floating){float x=(float)sample;memcpy(dst,&x,4);}else if(bits==16){int16_t x=(int16_t)(sample*32767.);memcpy(dst,&x,2);}else if(bits==32){int32_t x=(int32_t)(sample*2147483647.);memcpy(dst,&x,4);}else{int32_t x=(int32_t)(sample*8388607.);dst[0]=(BYTE)x;dst[1]=(BYTE)(x>>8);dst[2]=(BYTE)(x>>16);}}
static DWORD WINAPI worker(LPVOID){
 IMMDeviceEnumerator*enumerator=NULL;IMMDevice*device=NULL;IAudioClient*client=NULL;IAudioRenderClient*render=NULL;WAVEFORMATEX*fmt=NULL;
 HRESULT hr=CoInitializeEx(NULL,COINIT_APARTMENTTHREADED);bool com_ok=SUCCEEDED(hr);bool floating=false;UINT32 capacity=0;bool running=false;ULONGLONG submitted=0;double clock_start=0.;
 if(FAILED(hr))goto failure;
 hr=CoCreateInstance(__uuidof(MMDeviceEnumerator),NULL,CLSCTX_ALL,__uuidof(IMMDeviceEnumerator),(void**)&enumerator);if(FAILED(hr))goto failure;
 hr=enumerator->GetDefaultAudioEndpoint(eRender,eMultimedia,&device);if(FAILED(hr))goto failure;
 hr=device->Activate(__uuidof(IAudioClient),CLSCTX_ALL,NULL,(void**)&client);if(FAILED(hr))goto failure;
 hr=client->GetMixFormat(&fmt);if(FAILED(hr))goto failure;
 floating=fmt->wFormatTag==WAVE_FORMAT_IEEE_FLOAT||(fmt->wFormatTag==WAVE_FORMAT_EXTENSIBLE&&IsEqualGUID(((WAVEFORMATEXTENSIBLE*)fmt)->SubFormat,float_subtype));
 if(!fmt->nSamplesPerSec||!fmt->nChannels||fmt->nChannels>8||(floating&&fmt->wBitsPerSample!=32)||(!floating&&fmt->wBitsPerSample!=16&&fmt->wBitsPerSample!=24&&fmt->wBitsPerSample!=32)){hr=E_INVALIDARG;goto failure;}
 hr=client->Initialize(AUDCLNT_SHAREMODE_SHARED,AUDCLNT_STREAMFLAGS_EVENTCALLBACK|AUDCLNT_STREAMFLAGS_NOPERSIST,300000,0,fmt,&session_id);if(FAILED(hr))goto failure;
 hr=client->SetEventHandle(audio_event);if(FAILED(hr))goto failure;
 hr=client->GetBufferSize(&capacity);if(FAILED(hr))goto failure;
 hr=client->GetService(__uuidof(IAudioRenderClient),(void**)&render);if(FAILED(hr))goto failure;
 {IAudioSessionControl*control=NULL;if(SUCCEEDED(client->GetService(__uuidof(IAudioSessionControl),(void**)&control))){control->SetDisplayName(L"Arjun SA3 / CA Audio",NULL);control->Release();}}
 SetThreadPriority(GetCurrentThread(),THREAD_PRIORITY_ABOVE_NORMAL);
 EnterCriticalSection(&lock);devices_ready=true;status_file();LeaveCriticalSection(&lock);
 for(;;){
  EnterCriticalSection(&lock);ResetEvent(command_event);finish_expired((double)GetTickCount64());bool active=any_voice();int delay=worker_delay;LeaveCriticalSection(&lock);
  if(!active){if(running){client->Stop();client->Reset();running=false;}WaitForSingleObject(command_event,INFINITE);continue;}
  if(!running){clock_start=(double)GetTickCount64();submitted=0;}
  UINT32 padding=0;hr=client->GetCurrentPadding(&padding);if(FAILED(hr))goto failure;
  if(running&&!padding&&submitted){EnterCriticalSection(&lock);underruns++;LeaveCriticalSection(&lock);}
  UINT32 available=capacity-padding;
  if(available){BYTE*data=NULL;hr=render->GetBuffer(available,&data);if(FAILED(hr))goto failure;
   // Correct the render timeline after an actual starvation without skipping source audio.
   double expected=clock_start+submitted*1000./fmt->nSamplesPerSec;double now=(double)GetTickCount64();
   if(expected<now-20.)clock_start=now-submitted*1000./fmt->nSamplesPerSec;
   EnterCriticalSection(&lock);
   for(UINT32 k=0;k<available;k++){double out[2];mix_frame(clock_start+(submitted+k)*1000./fmt->nSamplesPerSec,fmt->nSamplesPerSec,out);
    for(UINT32 ch=0;ch<fmt->nChannels;ch++){double sample=fmt->nChannels==1?(out[0]+out[1])*.5:ch<2?out[ch]:0.;write_sample(data+k*fmt->nBlockAlign+ch*(fmt->wBitsPerSample/8),sample,floating,fmt->wBitsPerSample);}}
   submitted+=available;mix_passes++;LeaveCriticalSection(&lock);
   hr=render->ReleaseBuffer(available,0);if(FAILED(hr))goto failure;
  }
  if(!running){hr=client->Start();if(FAILED(hr))goto failure;running=true;}
  HANDLE events[2]={command_event,audio_event};WaitForMultipleObjects(2,events,FALSE,100);
  if(delay)Sleep((DWORD)delay);
 }
failure:
 EnterCriticalSection(&lock);error_code=(int)hr;ready=false;devices_ready=true;status_file();for(int t=0;t<2;t++)stop_voice(t);LeaveCriticalSection(&lock);
 if(client&&running)client->Stop();
 if(render)render->Release();
 if(client)client->Release();
 if(device)device->Release();
 if(enumerator)enumerator->Release();
 if(fmt)CoTaskMemFree(fmt);
 if(com_ok)CoUninitialize();
 return 0;
}
API int arjun_init(lua_State*){HMODULE pinned;GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_PIN,(LPCWSTR)(uintptr_t)&arjun_init,&pinned);if(!initialized){InitializeCriticalSection(&lock);initialized=true;if(audio_base_override[0])wcscpy(base,audio_base_override);else{GetModuleFileNameW(module,base,MAX_PATH);wchar_t*s=wcsrchr(base,92);if(s)*(s+1)=0;}error_code=read_track(0);if(!error_code)error_code=read_track(1);command_event=CreateEventW(NULL,TRUE,FALSE,NULL);audio_event=CreateEventW(NULL,FALSE,FALSE,NULL);if(!command_event||!audio_event)error_code=10;ready=!error_code;status_file();if(ready){HANDLE thread=CreateThread(NULL,0,worker,NULL,0,NULL);if(thread)CloseHandle(thread);else{ready=false;error_code=11;status_file();}}}return 0;}
API int arjun_tick(lua_State*){return 0;}
API int arjun_p1_track1(lua_State*){play(0,0);return 0;}
API int arjun_p1_track2(lua_State*){play(0,1);return 0;}
API int arjun_p2_track1(lua_State*){play(1,0);return 0;}
API int arjun_p2_track2(lua_State*){play(1,1);return 0;}
API int arjun_stop_p1(lua_State*){if(initialized){EnterCriticalSection(&lock);if(pending[0]>=0)fade_voice(pending[0]);LeaveCriticalSection(&lock);SetEvent(command_event);}return 0;}
API int arjun_stop_p2(lua_State*){if(initialized){EnterCriticalSection(&lock);if(pending[1]>=0)fade_voice(pending[1]);LeaveCriticalSection(&lock);SetEvent(command_event);}return 0;}
API int arjun_confirm_p1(lua_State*){if(initialized){EnterCriticalSection(&lock);pending[0]=-1;LeaveCriticalSection(&lock);}return 0;}
API int arjun_confirm_p2(lua_State*){if(initialized){EnterCriticalSection(&lock);pending[1]=-1;LeaveCriticalSection(&lock);}return 0;}
API int arjun_stop_all(lua_State*){if(initialized){EnterCriticalSection(&lock);for(int t=0;t<2;t++)fade_voice(t);LeaveCriticalSection(&lock);SetEvent(command_event);}return 0;}
API int arjun_test_hard_stop_all(){EnterCriticalSection(&lock);for(int t=0;t<2;t++)stop_voice(t);LeaveCriticalSection(&lock);SetEvent(command_event);return 0;}
API int arjun_test_status(){EnterCriticalSection(&lock);int result=ready?0:error_code;LeaveCriticalSection(&lock);return result;}
API int arjun_test_voices(){EnterCriticalSection(&lock);int n=voices[0].active+voices[1].active;LeaveCriticalSection(&lock);return n;}
API int arjun_test_playing(){EnterCriticalSection(&lock);ULONGLONG now=GetTickCount64();int n=(voices[0].active&&voices[0].started&&now>=voices[0].due)+(voices[1].active&&voices[1].started&&now>=voices[1].due);LeaveCriticalSection(&lock);return n;}
API int arjun_test_fading(){EnterCriticalSection(&lock);int n=(voices[0].active&&voices[0].fading)+(voices[1].active&&voices[1].fading);LeaveCriticalSection(&lock);return n;}
API int arjun_test_devices_ready(){EnterCriticalSection(&lock);int n=devices_ready;LeaveCriticalSection(&lock);return n;}
API int arjun_test_underruns(){EnterCriticalSection(&lock);int n=underruns;LeaveCriticalSection(&lock);return n;}
API int arjun_test_worker_delay(int ms){EnterCriticalSection(&lock);worker_delay=ms;LeaveCriticalSection(&lock);return 0;}
API int arjun_test_device_gain(){EnterCriticalSection(&lock);int n=(int)(65535.*interrupt_gain(voices[0],(double)GetTickCount64()));LeaveCriticalSection(&lock);return n;}
API int arjun_test_percent(){EnterCriticalSection(&lock);int n=volume_percent;LeaveCriticalSection(&lock);return n;}
API int arjun_test_scale(int sample,int percent,int fade_ms){return scale_sample((int16_t)sample,percent,fade_ms*.001);}
API int arjun_test_tail_sample(int sample,int percent,int frame,int total,int rate){return scale_sample((int16_t)sample,percent,tail_gain(frame,total,rate));}
API uint64_t arjun_test_mix_passes(){EnterCriticalSection(&lock);uint64_t n=mix_passes;LeaveCriticalSection(&lock);return n;}
BOOL WINAPI DllMain(HINSTANCE h,DWORD reason,LPVOID){if(reason==DLL_PROCESS_ATTACH){module=h;DisableThreadLibraryCalls(h);}return TRUE;}
API int arjun_set_volume_0(lua_State*){set_volume(0);return 0;}
API int arjun_set_volume_1(lua_State*){set_volume(1);return 0;}
API int arjun_set_volume_2(lua_State*){set_volume(2);return 0;}
API int arjun_set_volume_3(lua_State*){set_volume(3);return 0;}
API int arjun_set_volume_4(lua_State*){set_volume(4);return 0;}
API int arjun_set_volume_5(lua_State*){set_volume(5);return 0;}
API int arjun_set_volume_6(lua_State*){set_volume(6);return 0;}
API int arjun_set_volume_7(lua_State*){set_volume(7);return 0;}
API int arjun_set_volume_8(lua_State*){set_volume(8);return 0;}
API int arjun_set_volume_9(lua_State*){set_volume(9);return 0;}
API int arjun_set_volume_10(lua_State*){set_volume(10);return 0;}
API int arjun_set_volume_11(lua_State*){set_volume(11);return 0;}
API int arjun_set_volume_12(lua_State*){set_volume(12);return 0;}
API int arjun_set_volume_13(lua_State*){set_volume(13);return 0;}
API int arjun_set_volume_14(lua_State*){set_volume(14);return 0;}
API int arjun_set_volume_15(lua_State*){set_volume(15);return 0;}
API int arjun_set_volume_16(lua_State*){set_volume(16);return 0;}
API int arjun_set_volume_17(lua_State*){set_volume(17);return 0;}
API int arjun_set_volume_18(lua_State*){set_volume(18);return 0;}
API int arjun_set_volume_19(lua_State*){set_volume(19);return 0;}
API int arjun_set_volume_20(lua_State*){set_volume(20);return 0;}
API int arjun_set_volume_21(lua_State*){set_volume(21);return 0;}
API int arjun_set_volume_22(lua_State*){set_volume(22);return 0;}
API int arjun_set_volume_23(lua_State*){set_volume(23);return 0;}
API int arjun_set_volume_24(lua_State*){set_volume(24);return 0;}
API int arjun_set_volume_25(lua_State*){set_volume(25);return 0;}
API int arjun_set_volume_26(lua_State*){set_volume(26);return 0;}
API int arjun_set_volume_27(lua_State*){set_volume(27);return 0;}
API int arjun_set_volume_28(lua_State*){set_volume(28);return 0;}
API int arjun_set_volume_29(lua_State*){set_volume(29);return 0;}
API int arjun_set_volume_30(lua_State*){set_volume(30);return 0;}
API int arjun_set_volume_31(lua_State*){set_volume(31);return 0;}
API int arjun_set_volume_32(lua_State*){set_volume(32);return 0;}
API int arjun_set_volume_33(lua_State*){set_volume(33);return 0;}
API int arjun_set_volume_34(lua_State*){set_volume(34);return 0;}
API int arjun_set_volume_35(lua_State*){set_volume(35);return 0;}
API int arjun_set_volume_36(lua_State*){set_volume(36);return 0;}
API int arjun_set_volume_37(lua_State*){set_volume(37);return 0;}
API int arjun_set_volume_38(lua_State*){set_volume(38);return 0;}
API int arjun_set_volume_39(lua_State*){set_volume(39);return 0;}
API int arjun_set_volume_40(lua_State*){set_volume(40);return 0;}
API int arjun_set_volume_41(lua_State*){set_volume(41);return 0;}
API int arjun_set_volume_42(lua_State*){set_volume(42);return 0;}
API int arjun_set_volume_43(lua_State*){set_volume(43);return 0;}
API int arjun_set_volume_44(lua_State*){set_volume(44);return 0;}
API int arjun_set_volume_45(lua_State*){set_volume(45);return 0;}
API int arjun_set_volume_46(lua_State*){set_volume(46);return 0;}
API int arjun_set_volume_47(lua_State*){set_volume(47);return 0;}
API int arjun_set_volume_48(lua_State*){set_volume(48);return 0;}
API int arjun_set_volume_49(lua_State*){set_volume(49);return 0;}
API int arjun_set_volume_50(lua_State*){set_volume(50);return 0;}
API int arjun_set_volume_51(lua_State*){set_volume(51);return 0;}
API int arjun_set_volume_52(lua_State*){set_volume(52);return 0;}
API int arjun_set_volume_53(lua_State*){set_volume(53);return 0;}
API int arjun_set_volume_54(lua_State*){set_volume(54);return 0;}
API int arjun_set_volume_55(lua_State*){set_volume(55);return 0;}
API int arjun_set_volume_56(lua_State*){set_volume(56);return 0;}
API int arjun_set_volume_57(lua_State*){set_volume(57);return 0;}
API int arjun_set_volume_58(lua_State*){set_volume(58);return 0;}
API int arjun_set_volume_59(lua_State*){set_volume(59);return 0;}
API int arjun_set_volume_60(lua_State*){set_volume(60);return 0;}
API int arjun_set_volume_61(lua_State*){set_volume(61);return 0;}
API int arjun_set_volume_62(lua_State*){set_volume(62);return 0;}
API int arjun_set_volume_63(lua_State*){set_volume(63);return 0;}
API int arjun_set_volume_64(lua_State*){set_volume(64);return 0;}
API int arjun_set_volume_65(lua_State*){set_volume(65);return 0;}
API int arjun_set_volume_66(lua_State*){set_volume(66);return 0;}
API int arjun_set_volume_67(lua_State*){set_volume(67);return 0;}
API int arjun_set_volume_68(lua_State*){set_volume(68);return 0;}
API int arjun_set_volume_69(lua_State*){set_volume(69);return 0;}
API int arjun_set_volume_70(lua_State*){set_volume(70);return 0;}
API int arjun_set_volume_71(lua_State*){set_volume(71);return 0;}
API int arjun_set_volume_72(lua_State*){set_volume(72);return 0;}
API int arjun_set_volume_73(lua_State*){set_volume(73);return 0;}
API int arjun_set_volume_74(lua_State*){set_volume(74);return 0;}
API int arjun_set_volume_75(lua_State*){set_volume(75);return 0;}
API int arjun_set_volume_76(lua_State*){set_volume(76);return 0;}
API int arjun_set_volume_77(lua_State*){set_volume(77);return 0;}
API int arjun_set_volume_78(lua_State*){set_volume(78);return 0;}
API int arjun_set_volume_79(lua_State*){set_volume(79);return 0;}
API int arjun_set_volume_80(lua_State*){set_volume(80);return 0;}
API int arjun_set_volume_81(lua_State*){set_volume(81);return 0;}
API int arjun_set_volume_82(lua_State*){set_volume(82);return 0;}
API int arjun_set_volume_83(lua_State*){set_volume(83);return 0;}
API int arjun_set_volume_84(lua_State*){set_volume(84);return 0;}
API int arjun_set_volume_85(lua_State*){set_volume(85);return 0;}
API int arjun_set_volume_86(lua_State*){set_volume(86);return 0;}
API int arjun_set_volume_87(lua_State*){set_volume(87);return 0;}
API int arjun_set_volume_88(lua_State*){set_volume(88);return 0;}
API int arjun_set_volume_89(lua_State*){set_volume(89);return 0;}
API int arjun_set_volume_90(lua_State*){set_volume(90);return 0;}
API int arjun_set_volume_91(lua_State*){set_volume(91);return 0;}
API int arjun_set_volume_92(lua_State*){set_volume(92);return 0;}
API int arjun_set_volume_93(lua_State*){set_volume(93);return 0;}
API int arjun_set_volume_94(lua_State*){set_volume(94);return 0;}
API int arjun_set_volume_95(lua_State*){set_volume(95);return 0;}
API int arjun_set_volume_96(lua_State*){set_volume(96);return 0;}
API int arjun_set_volume_97(lua_State*){set_volume(97);return 0;}
API int arjun_set_volume_98(lua_State*){set_volume(98);return 0;}
API int arjun_set_volume_99(lua_State*){set_volume(99);return 0;}
API int arjun_set_volume_100(lua_State*){set_volume(100);return 0;}
API int arjun_set_volume_101(lua_State*){set_volume(101);return 0;}
API int arjun_set_volume_102(lua_State*){set_volume(102);return 0;}
API int arjun_set_volume_103(lua_State*){set_volume(103);return 0;}
API int arjun_set_volume_104(lua_State*){set_volume(104);return 0;}
API int arjun_set_volume_105(lua_State*){set_volume(105);return 0;}
API int arjun_set_volume_106(lua_State*){set_volume(106);return 0;}
API int arjun_set_volume_107(lua_State*){set_volume(107);return 0;}
API int arjun_set_volume_108(lua_State*){set_volume(108);return 0;}
API int arjun_set_volume_109(lua_State*){set_volume(109);return 0;}
API int arjun_set_volume_110(lua_State*){set_volume(110);return 0;}
API int arjun_set_volume_111(lua_State*){set_volume(111);return 0;}
API int arjun_set_volume_112(lua_State*){set_volume(112);return 0;}
API int arjun_set_volume_113(lua_State*){set_volume(113);return 0;}
API int arjun_set_volume_114(lua_State*){set_volume(114);return 0;}
API int arjun_set_volume_115(lua_State*){set_volume(115);return 0;}
API int arjun_set_volume_116(lua_State*){set_volume(116);return 0;}
API int arjun_set_volume_117(lua_State*){set_volume(117);return 0;}
API int arjun_set_volume_118(lua_State*){set_volume(118);return 0;}
API int arjun_set_volume_119(lua_State*){set_volume(119);return 0;}
API int arjun_set_volume_120(lua_State*){set_volume(120);return 0;}
API int arjun_set_volume_121(lua_State*){set_volume(121);return 0;}
API int arjun_set_volume_122(lua_State*){set_volume(122);return 0;}
API int arjun_set_volume_123(lua_State*){set_volume(123);return 0;}
API int arjun_set_volume_124(lua_State*){set_volume(124);return 0;}
API int arjun_set_volume_125(lua_State*){set_volume(125);return 0;}
API int arjun_set_volume_126(lua_State*){set_volume(126);return 0;}
API int arjun_set_volume_127(lua_State*){set_volume(127);return 0;}
API int arjun_set_volume_128(lua_State*){set_volume(128);return 0;}
API int arjun_set_volume_129(lua_State*){set_volume(129);return 0;}
API int arjun_set_volume_130(lua_State*){set_volume(130);return 0;}
API int arjun_set_volume_131(lua_State*){set_volume(131);return 0;}
API int arjun_set_volume_132(lua_State*){set_volume(132);return 0;}
API int arjun_set_volume_133(lua_State*){set_volume(133);return 0;}
API int arjun_set_volume_134(lua_State*){set_volume(134);return 0;}
API int arjun_set_volume_135(lua_State*){set_volume(135);return 0;}
API int arjun_set_volume_136(lua_State*){set_volume(136);return 0;}
API int arjun_set_volume_137(lua_State*){set_volume(137);return 0;}
API int arjun_set_volume_138(lua_State*){set_volume(138);return 0;}
API int arjun_set_volume_139(lua_State*){set_volume(139);return 0;}
API int arjun_set_volume_140(lua_State*){set_volume(140);return 0;}
API int arjun_set_volume_141(lua_State*){set_volume(141);return 0;}
API int arjun_set_volume_142(lua_State*){set_volume(142);return 0;}
API int arjun_set_volume_143(lua_State*){set_volume(143);return 0;}
API int arjun_set_volume_144(lua_State*){set_volume(144);return 0;}
API int arjun_set_volume_145(lua_State*){set_volume(145);return 0;}
API int arjun_set_volume_146(lua_State*){set_volume(146);return 0;}
API int arjun_set_volume_147(lua_State*){set_volume(147);return 0;}
API int arjun_set_volume_148(lua_State*){set_volume(148);return 0;}
API int arjun_set_volume_149(lua_State*){set_volume(149);return 0;}
API int arjun_set_volume_150(lua_State*){set_volume(150);return 0;}
API int arjun_set_volume_151(lua_State*){set_volume(151);return 0;}
API int arjun_set_volume_152(lua_State*){set_volume(152);return 0;}
API int arjun_set_volume_153(lua_State*){set_volume(153);return 0;}
API int arjun_set_volume_154(lua_State*){set_volume(154);return 0;}
API int arjun_set_volume_155(lua_State*){set_volume(155);return 0;}
API int arjun_set_volume_156(lua_State*){set_volume(156);return 0;}
API int arjun_set_volume_157(lua_State*){set_volume(157);return 0;}
API int arjun_set_volume_158(lua_State*){set_volume(158);return 0;}
API int arjun_set_volume_159(lua_State*){set_volume(159);return 0;}
API int arjun_set_volume_160(lua_State*){set_volume(160);return 0;}
API int arjun_set_volume_161(lua_State*){set_volume(161);return 0;}
API int arjun_set_volume_162(lua_State*){set_volume(162);return 0;}
API int arjun_set_volume_163(lua_State*){set_volume(163);return 0;}
API int arjun_set_volume_164(lua_State*){set_volume(164);return 0;}
API int arjun_set_volume_165(lua_State*){set_volume(165);return 0;}
API int arjun_set_volume_166(lua_State*){set_volume(166);return 0;}
API int arjun_set_volume_167(lua_State*){set_volume(167);return 0;}
API int arjun_set_volume_168(lua_State*){set_volume(168);return 0;}
API int arjun_set_volume_169(lua_State*){set_volume(169);return 0;}
API int arjun_set_volume_170(lua_State*){set_volume(170);return 0;}
API int arjun_set_volume_171(lua_State*){set_volume(171);return 0;}
API int arjun_set_volume_172(lua_State*){set_volume(172);return 0;}
API int arjun_set_volume_173(lua_State*){set_volume(173);return 0;}
API int arjun_set_volume_174(lua_State*){set_volume(174);return 0;}
API int arjun_set_volume_175(lua_State*){set_volume(175);return 0;}
API int arjun_set_volume_176(lua_State*){set_volume(176);return 0;}
API int arjun_set_volume_177(lua_State*){set_volume(177);return 0;}
API int arjun_set_volume_178(lua_State*){set_volume(178);return 0;}
API int arjun_set_volume_179(lua_State*){set_volume(179);return 0;}
API int arjun_set_volume_180(lua_State*){set_volume(180);return 0;}
API int arjun_set_volume_181(lua_State*){set_volume(181);return 0;}
API int arjun_set_volume_182(lua_State*){set_volume(182);return 0;}
API int arjun_set_volume_183(lua_State*){set_volume(183);return 0;}
API int arjun_set_volume_184(lua_State*){set_volume(184);return 0;}
API int arjun_set_volume_185(lua_State*){set_volume(185);return 0;}
API int arjun_set_volume_186(lua_State*){set_volume(186);return 0;}
API int arjun_set_volume_187(lua_State*){set_volume(187);return 0;}
API int arjun_set_volume_188(lua_State*){set_volume(188);return 0;}
API int arjun_set_volume_189(lua_State*){set_volume(189);return 0;}
API int arjun_set_volume_190(lua_State*){set_volume(190);return 0;}
API int arjun_set_volume_191(lua_State*){set_volume(191);return 0;}
API int arjun_set_volume_192(lua_State*){set_volume(192);return 0;}
API int arjun_set_volume_193(lua_State*){set_volume(193);return 0;}
API int arjun_set_volume_194(lua_State*){set_volume(194);return 0;}
API int arjun_set_volume_195(lua_State*){set_volume(195);return 0;}
API int arjun_set_volume_196(lua_State*){set_volume(196);return 0;}
API int arjun_set_volume_197(lua_State*){set_volume(197);return 0;}
API int arjun_set_volume_198(lua_State*){set_volume(198);return 0;}
API int arjun_set_volume_199(lua_State*){set_volume(199);return 0;}
API int arjun_set_volume_200(lua_State*){set_volume(200);return 0;}
API int arjun_set_volume_201(lua_State*){set_volume(201);return 0;}
API int arjun_set_volume_202(lua_State*){set_volume(202);return 0;}
API int arjun_set_volume_203(lua_State*){set_volume(203);return 0;}
API int arjun_set_volume_204(lua_State*){set_volume(204);return 0;}
API int arjun_set_volume_205(lua_State*){set_volume(205);return 0;}
API int arjun_set_volume_206(lua_State*){set_volume(206);return 0;}
API int arjun_set_volume_207(lua_State*){set_volume(207);return 0;}
API int arjun_set_volume_208(lua_State*){set_volume(208);return 0;}
API int arjun_set_volume_209(lua_State*){set_volume(209);return 0;}
API int arjun_set_volume_210(lua_State*){set_volume(210);return 0;}
API int arjun_set_volume_211(lua_State*){set_volume(211);return 0;}
API int arjun_set_volume_212(lua_State*){set_volume(212);return 0;}
API int arjun_set_volume_213(lua_State*){set_volume(213);return 0;}
API int arjun_set_volume_214(lua_State*){set_volume(214);return 0;}
API int arjun_set_volume_215(lua_State*){set_volume(215);return 0;}
API int arjun_set_volume_216(lua_State*){set_volume(216);return 0;}
API int arjun_set_volume_217(lua_State*){set_volume(217);return 0;}
API int arjun_set_volume_218(lua_State*){set_volume(218);return 0;}
API int arjun_set_volume_219(lua_State*){set_volume(219);return 0;}
API int arjun_set_volume_220(lua_State*){set_volume(220);return 0;}
API int arjun_set_volume_221(lua_State*){set_volume(221);return 0;}
API int arjun_set_volume_222(lua_State*){set_volume(222);return 0;}
API int arjun_set_volume_223(lua_State*){set_volume(223);return 0;}
API int arjun_set_volume_224(lua_State*){set_volume(224);return 0;}
API int arjun_set_volume_225(lua_State*){set_volume(225);return 0;}
API int arjun_set_volume_226(lua_State*){set_volume(226);return 0;}
API int arjun_set_volume_227(lua_State*){set_volume(227);return 0;}
API int arjun_set_volume_228(lua_State*){set_volume(228);return 0;}
API int arjun_set_volume_229(lua_State*){set_volume(229);return 0;}
API int arjun_set_volume_230(lua_State*){set_volume(230);return 0;}
API int arjun_set_volume_231(lua_State*){set_volume(231);return 0;}
API int arjun_set_volume_232(lua_State*){set_volume(232);return 0;}
API int arjun_set_volume_233(lua_State*){set_volume(233);return 0;}
API int arjun_set_volume_234(lua_State*){set_volume(234);return 0;}
API int arjun_set_volume_235(lua_State*){set_volume(235);return 0;}
API int arjun_set_volume_236(lua_State*){set_volume(236);return 0;}
API int arjun_set_volume_237(lua_State*){set_volume(237);return 0;}
API int arjun_set_volume_238(lua_State*){set_volume(238);return 0;}
API int arjun_set_volume_239(lua_State*){set_volume(239);return 0;}
API int arjun_set_volume_240(lua_State*){set_volume(240);return 0;}
API int arjun_set_volume_241(lua_State*){set_volume(241);return 0;}
API int arjun_set_volume_242(lua_State*){set_volume(242);return 0;}
API int arjun_set_volume_243(lua_State*){set_volume(243);return 0;}
API int arjun_set_volume_244(lua_State*){set_volume(244);return 0;}
API int arjun_set_volume_245(lua_State*){set_volume(245);return 0;}
API int arjun_set_volume_246(lua_State*){set_volume(246);return 0;}
API int arjun_set_volume_247(lua_State*){set_volume(247);return 0;}
API int arjun_set_volume_248(lua_State*){set_volume(248);return 0;}
API int arjun_set_volume_249(lua_State*){set_volume(249);return 0;}
API int arjun_set_volume_250(lua_State*){set_volume(250);return 0;}
API int arjun_set_volume_251(lua_State*){set_volume(251);return 0;}
API int arjun_set_volume_252(lua_State*){set_volume(252);return 0;}
API int arjun_set_volume_253(lua_State*){set_volume(253);return 0;}
API int arjun_set_volume_254(lua_State*){set_volume(254);return 0;}
API int arjun_set_volume_255(lua_State*){set_volume(255);return 0;}
API int arjun_set_volume_256(lua_State*){set_volume(256);return 0;}
API int arjun_set_volume_257(lua_State*){set_volume(257);return 0;}
API int arjun_set_volume_258(lua_State*){set_volume(258);return 0;}
API int arjun_set_volume_259(lua_State*){set_volume(259);return 0;}
API int arjun_set_volume_260(lua_State*){set_volume(260);return 0;}
API int arjun_set_volume_261(lua_State*){set_volume(261);return 0;}
API int arjun_set_volume_262(lua_State*){set_volume(262);return 0;}
API int arjun_set_volume_263(lua_State*){set_volume(263);return 0;}
API int arjun_set_volume_264(lua_State*){set_volume(264);return 0;}
API int arjun_set_volume_265(lua_State*){set_volume(265);return 0;}
API int arjun_set_volume_266(lua_State*){set_volume(266);return 0;}
API int arjun_set_volume_267(lua_State*){set_volume(267);return 0;}
API int arjun_set_volume_268(lua_State*){set_volume(268);return 0;}
API int arjun_set_volume_269(lua_State*){set_volume(269);return 0;}
API int arjun_set_volume_270(lua_State*){set_volume(270);return 0;}
API int arjun_set_volume_271(lua_State*){set_volume(271);return 0;}
API int arjun_set_volume_272(lua_State*){set_volume(272);return 0;}
API int arjun_set_volume_273(lua_State*){set_volume(273);return 0;}
API int arjun_set_volume_274(lua_State*){set_volume(274);return 0;}
API int arjun_set_volume_275(lua_State*){set_volume(275);return 0;}
API int arjun_set_volume_276(lua_State*){set_volume(276);return 0;}
API int arjun_set_volume_277(lua_State*){set_volume(277);return 0;}
API int arjun_set_volume_278(lua_State*){set_volume(278);return 0;}
API int arjun_set_volume_279(lua_State*){set_volume(279);return 0;}
API int arjun_set_volume_280(lua_State*){set_volume(280);return 0;}
API int arjun_set_volume_281(lua_State*){set_volume(281);return 0;}
API int arjun_set_volume_282(lua_State*){set_volume(282);return 0;}
API int arjun_set_volume_283(lua_State*){set_volume(283);return 0;}
API int arjun_set_volume_284(lua_State*){set_volume(284);return 0;}
API int arjun_set_volume_285(lua_State*){set_volume(285);return 0;}
API int arjun_set_volume_286(lua_State*){set_volume(286);return 0;}
API int arjun_set_volume_287(lua_State*){set_volume(287);return 0;}
API int arjun_set_volume_288(lua_State*){set_volume(288);return 0;}
API int arjun_set_volume_289(lua_State*){set_volume(289);return 0;}
API int arjun_set_volume_290(lua_State*){set_volume(290);return 0;}
API int arjun_set_volume_291(lua_State*){set_volume(291);return 0;}
API int arjun_set_volume_292(lua_State*){set_volume(292);return 0;}
API int arjun_set_volume_293(lua_State*){set_volume(293);return 0;}
API int arjun_set_volume_294(lua_State*){set_volume(294);return 0;}
API int arjun_set_volume_295(lua_State*){set_volume(295);return 0;}
API int arjun_set_volume_296(lua_State*){set_volume(296);return 0;}
API int arjun_set_volume_297(lua_State*){set_volume(297);return 0;}
API int arjun_set_volume_298(lua_State*){set_volume(298);return 0;}
API int arjun_set_volume_299(lua_State*){set_volume(299);return 0;}
API int arjun_set_volume_300(lua_State*){set_volume(300);return 0;}

