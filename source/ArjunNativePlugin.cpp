#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "native_api/API.hpp"
using RF=reframework::API;
#include "native_api/imgui/imgui.h"
#include "native_api/json.hpp"
#include "ArjunBattleCore.hpp"
#include <filesystem>
#include <fstream>
#include <map>
#include <mutex>
#include <deque>
#include <atomic>
#include <string>
static wchar_t audio_base_override[MAX_PATH]={};
#include "ArjunAudioBridge_native.cpp"
namespace arjun_native {
using Json=nlohmann::json;using Type=RF::TypeDefinition;using Obj=RF::ManagedObject;
struct Ref{void* ptr=nullptr;Type* type=nullptr;bool value=false;explicit operator bool()const{return ptr&&type;}};
static std::recursive_mutex detector_mutex;
static std::map<std::pair<Type*,std::string>,RF::Field*> fields;
static RF::Field *global_player=nullptr,*global_command=nullptr;
static std::unordered_set<Type*> valid_types;
static std::atomic<bool> restore_requested(false);static bool restore_hook=false,action_hook=false;
static thread_local std::vector<void*> action_hook_stack;
static bool enabled=true;static int selected_track=1;static std::string note="Waiting for loaded Arjun battle",last_error;
static uint64_t callbacks=0,detector_passes=0,plays=0,confirms=0,stops=0,restores=0,model_builds=0;
static ULONGLONG next_status=0,last_motion=0;static uint64_t accepted_events=0,presents=0,ui_draws=0,log_dropped=0;
static std::atomic<uint64_t> hook_calls(0);static bool ui_visible=false,have_ui_visibility=false;
static std::deque<Json> event_log;
struct Binding{uintptr_t engine=0,fab=0,loaded=0,bcm=0;std::unordered_map<int,arjun_core::Model> models;arjun_core::State reducer;arjun_core::Sample sample;bool valid=false,suppress_event=false;ULONGLONG retry=0;};
static Binding bindings[2];
static void event(const char*name,Json details=Json::object()){FILETIME ft;GetSystemTimeAsFileTime(&ft);ULARGE_INTEGER stamp{};stamp.LowPart=ft.dwLowDateTime;stamp.HighPart=ft.dwHighDateTime;details["event"]=name;details["unix_ms"]=(stamp.QuadPart-116444736000000000ULL)/10000;details["tick_ms"]=GetTickCount64();if(event_log.size()>=128){event_log.pop_front();log_dropped++;}event_log.push_back(std::move(details));}
static Json context(int p){auto&b=bindings[p];return {{"player",p+1},{"engine",b.engine},{"action",b.sample.action},{"frame",b.sample.frame},{"opponent_hp",b.sample.opponent.hp},{"guard",b.sample.opponent.guard},{"damage",b.sample.opponent.damage},{"hit_param",b.sample.opponent.param},{"motion_frames",callbacks},{"ui_visible",ui_visible}};}
static bool memory(const void*src,void*dst,size_t size){SIZE_T got=0;return src&&ReadProcessMemory(GetCurrentProcess(),src,dst,size,&got)&&got==size;}
template<class T>static T read(const void*p,T fallback=T{}){T out{};return memory(p,&out,sizeof(out))?out:fallback;}
static Type* valid_type(Type*t){return valid_types.count(t)?t:nullptr;}
static Ref object(void*p){if(!p)return {};uintptr_t head=0;if(!memory(p,&head,8)||!head)return {};auto sdk=RF::get()->sdk();if(!sdk->managed_object->is_managed_object((REFrameworkManagedObjectHandle)p))return {};auto t=valid_type(((Obj*)p)->get_type_definition());return {p,t,false};}
static RF::Field* field(Type*t,const char*n){if(!t)return nullptr;auto key=std::make_pair(t,std::string(n));auto it=fields.find(key);if(it!=fields.end())return it->second;auto f=t->find_field(n);fields[key]=f;return f;}
static Ref from_field(RF::Field*f,void*p,bool value=false){if(!f)return {};void*data=f->get_data_raw(p,value);if(!data)return {};auto type=valid_type(f->get_type());if(!type)return {};
 if(type->is_pointer()){void*target=read<void*>(data);auto underlying=valid_type(type->get_underlying_type());return {target,underlying?underlying:type,true};}
 if(type->is_valuetype()||type->is_primitive()||type->is_enum())return {data,type,true};
 void*target=read<void*>(data);auto obj=object(target);return obj?obj:Ref{target,type,true};
}
static Ref member(Ref r,const char*n){return r?from_field(field(r.type,n),r.ptr,r.value):Ref{};}
static int64_t number(Ref r,int64_t fallback=0){if(!r)return fallback;auto size=r.type->get_valuetype_size();if(size==0||size>8)size=r.type->get_size();if(size<1||size>8)return fallback;uint64_t value=0;if(!memory(r.ptr,&value,size))return fallback;
 const char*name=r.type->get_name();if(name&&(!strcmp(name,"SByte")||!strcmp(name,"Int16")||!strcmp(name,"Int32")||!strcmp(name,"Int64"))){if(size==1)return (int8_t)value;if(size==2)return (int16_t)value;if(size==4)return (int32_t)value;return (int64_t)value;}return (int64_t)value;
}
static int64_t num(Ref r,const char*n,int64_t fallback=0){return number(member(r,n),fallback);}
static double frame(Ref r){if(!r)return 0.;const char*name=r.type->get_name();if(name&&!strcmp(name,"Single"))return read<float>(r.ptr);if(name&&!strcmp(name,"sfix"))return read<int32_t>(r.ptr)/65536.;return (double)number(r);}
// SF6/TDB71 REArray: contained type at 0x10, count at 0x1c, inline data at 0x20.
// Layout is from the matching REFramework SDK, with all reads bounded and checked.
static std::vector<Ref> array(Ref r,int limit=4096){std::vector<Ref> out;if(!r)return out;
 if(r.type->get_vm_obj_type()!=REFRAMEWORK_VM_OBJ_TYPE_ARRAY){auto items=member(r,"_items");if(!items)return out;auto count=num(r,"_size",-1);if(count<0||count>limit)return out;auto all=array(items,limit);if((size_t)count<all.size())all.resize((size_t)count);return all;}
 int count=read<int>((char*)r.ptr+0x1c,-1);if(count<0||count>limit)return out;auto element=valid_type(read<Type*>((char*)r.ptr+0x10));if(!element)return out;
 bool inline_value=element->get_vm_obj_type()==REFRAMEWORK_VM_OBJ_TYPE_VALTYPE;size_t stride=inline_value?element->get_valuetype_size():sizeof(void*);if(!stride||stride>8192)return out;
 out.reserve(count);for(int i=0;i<count;i++){void*slot=(char*)r.ptr+0x20+stride*i;out.push_back(inline_value?Ref{slot,element,true}:object(read<void*>(slot)));}return out;
}
static Ref at(Ref r,int index){if(!r||index<0)return {};if(r.type->get_vm_obj_type()!=REFRAMEWORK_VM_OBJ_TYPE_ARRAY){auto items=member(r,"_items");return at(items,index);}int count=read<int>((char*)r.ptr+0x1c,-1);if(count<0||count>16384||index>=count)return {};auto element=valid_type(read<Type*>((char*)r.ptr+0x10));if(!element)return {};bool in=element->get_vm_obj_type()==REFRAMEWORK_VM_OBJ_TYPE_VALTYPE;size_t stride=in?element->get_valuetype_size():8;if(!stride||stride>8192)return {};void*slot=(char*)r.ptr+0x20+index*stride;return in?Ref{slot,element,true}:object(read<void*>(slot));}
static std::vector<Ref> values(Ref r){if(!r)return {};auto entries=member(r,"_entries");if(!entries)return array(r);std::vector<Ref> out;for(auto e:array(entries)){auto v=member(e,"value");if(v&&num(e,"hashCode",0)>=0)out.push_back(v);}return out;}
static std::unordered_map<int,Ref> dictionary(Ref r){std::unordered_map<int,Ref> out;auto entries=member(r,"_entries");if(entries){for(auto e:array(entries)){auto v=member(e,"value");if(v&&num(e,"hashCode",0)>=0)out[(int)number(member(e,"key"))]=v;}}else{auto items=array(r);for(size_t i=0;i<items.size();i++)if(items[i])out[(int)i]=items[i];}return out;}
static uintptr_t dmg_address(Ref cp){auto f=field(cp.type,"pDmgHitDT");return f?read<uintptr_t>(f->get_data_raw(cp.ptr,cp.value)):0;}
static arjun_core::Opponent opponent(Ref cp){arjun_core::Opponent out;if(!cp)return out;out.valid=true;out.hp=(int)num(cp,"vital_new");out.guard=(int)num(cp,"guard_time");out.damage=(int)num(cp,"damage_time");out.hitstop=(int)num(cp,"hit_stop");out.param=dmg_address(cp);return out;}
static int kind(uint64_t flags){if(flags&(1ULL<<49))return 2;if((flags&32768)&&(flags&1024))return 2;if((flags&32768)&&(flags&512))return 1;return 0;}
static bool build_models(Ref fab,Ref bcm,Ref loaded,std::unordered_map<int,arjun_core::Model>&models){
 std::unordered_map<int,int>starts;for(auto trigger:array(member(bcm,"pTrigger"))){int k=kind((uint64_t)num(trigger,"category_flags"));if(k)starts[(int)num(trigger,"action_id")]=k;}
 if(starts.empty())starts={{1237,1},{1238,1},{1244,2},{1245,2}};
 std::unordered_map<int,Ref>actions;for(auto style:values(member(fab,"StyleDict")))for(auto action:values(member(style,"ActionList")))if(action)actions[(int)num(action,"ActionID")]=action;
 auto hits=dictionary(loaded);if(actions.empty())return false;
 std::unordered_map<int,arjun_core::Model> result;
 for(auto start:starts){if(!actions.count(start.first))continue;arjun_core::Model model;model.kind=start.second;std::deque<int>queue{start.first};std::unordered_set<int>visited;
  while(!queue.empty()){int id=queue.front();queue.pop_front();if(visited.count(id))continue;visited.insert(id);if(visited.size()>128)return false;auto it=actions.find(id);if(it==actions.end())continue;arjun_core::Action parsed;std::vector<int>hit_ids;
   for(auto list:array(member(it->second,"Keys")))for(auto key:array(list)){if(!key)continue;const char*name=key.type->get_name();
    if(!strcmp(name,"BranchKey")){int to=(int)num(key,"Action");if(to>=1000&&!visited.count(to))queue.push_back(to);}
    else if(!strcmp(name,"AttackCollisionKey")||!strcmp(name,"OtherCollisionKey")||!strcmp(name,"GimmickCollisionKey")){
     int hid=(int)num(key,"AttackDataListIndex",-1);if(hid>=0)hit_ids.push_back(hid);auto end=member(key,"_EndFrame");if(!end)end=member(key,"EndFrame");if(end)parsed.attack_end=std::max(parsed.attack_end,frame(end));
    }else if(!strcmp(name,"LockKey")&&num(key,"Type")==2){int hid=(int)num(key,"Param02",-1);if(hid>=0)hit_ids.push_back(hid);}
   }
   model.actions[id]=parsed;
   for(int hid:hit_ids){auto hit=hits.find(hid);if(hit==hits.end())continue;auto params=array(member(hit->second,"param"));for(size_t i=0;i<params.size()&&i<20;i++){if(!params[i])continue;uintptr_t addr=(uintptr_t)params[i].ptr;int category=i>=16?2:1;auto existing=model.params.find(addr);if(existing!=model.params.end()&&existing->second!=category)existing->second=3;else model.params[addr]=category;}}
  }
  if(model.actions.count(start.first))result[start.first]=std::move(model);
 }
 if(result.empty())return false;
 models=std::move(result);model_builds++;return true;
}
static arjun_core::Ops ops(int p){return {[p](){auto data=context(p);data["track"]=selected_track;data["audio_ready"]=ready.load();data["delay_ms"]=150;play(p,selected_track-1);plays++;event(ready?"audio_scheduled":"audio_unavailable",data);note="SA3 / CA started";},[p](){if(p==0)arjun_stop_p1(nullptr);else arjun_stop_p2(nullptr);stops++;auto data=context(p);auto&s=bindings[p].reducer;auto it=s.model?s.model->params.find(bindings[p].sample.opponent.param):std::unordered_map<uintptr_t,int>::const_iterator{};bool block=bindings[p].sample.opponent.guard>0||(s.model&&it!=s.model->params.end()&&it->second==2);data["reason"]=block?"block":"whiff_or_left_attack_window";event("audio_fade",data);note="Miss/block: own audio fading";},[p](){if(p==0)arjun_confirm_p1(nullptr);else arjun_confirm_p2(nullptr);confirms++;event("stop_monitor_released",context(p));note="Super follow-up: audio continues";}};}
static void detach_all(){for(int p=0;p<2;p++){arjun_core::detach(bindings[p].reducer,ops(p));bindings[p].valid=false;bindings[p].engine=0;bindings[p].suppress_event=false;bindings[p].models.clear();}}
static std::filesystem::path path(const wchar_t*n){return std::filesystem::path(audio_base_override)/n;}
static bool write_json(const wchar_t*n,const Json&j){auto target=path(n),temp=target;temp+=L".tmp";std::ofstream f(temp,std::ios::binary);if(!f)return false;f<<j.dump(2);f.close();return MoveFileExW(temp.c_str(),target.c_str(),MOVEFILE_REPLACE_EXISTING)!=FALSE;}
// Disk writes happen on a throttled Present callback, never in an action hook.
static void flush_events(){if(event_log.empty())return;auto logfile=path(L"native_events.jsonl");std::error_code error;if(std::filesystem::file_size(logfile,error)>4*1024*1024&&!error)MoveFileExW(logfile.c_str(),path(L"native_events.previous.jsonl").c_str(),MOVEFILE_REPLACE_EXISTING);std::ofstream output(logfile,std::ios::app|std::ios::binary);if(!output)return;while(!event_log.empty()){output<<event_log.front().dump()<<'\n';event_log.pop_front();}}
static void save_settings(){int percent=arjun_test_percent();write_json(L"settings.json",{{"enabled",enabled},{"track",selected_track},{"volume",percent},{"volume_schema",1}});}
static void status(){Json players=Json::array();for(int p=0;p<2;p++){auto&b=bindings[p];players.push_back({{"slot",p},{"bound",b.valid},{"action",b.sample.action},{"frame",b.sample.frame},{"starts",b.reducer.starts},{"pending_hit",b.reducer.pending},{"models",b.models.size()}});}
 write_json(L"native_plugin_status.json",{{"version","0.3.3"},{"lua_dependency",false},{"callback_frames",callbacks},{"present_frames",presents},{"ui_draws",ui_draws},{"framework_ui_visible",ui_visible},{"motion_gap_ms",last_motion?GetTickCount64()-last_motion:0},{"hook_calls",hook_calls.load()},{"accepted_action_events",accepted_events},{"action_hook",action_hook},{"restore_hook",restore_hook},{"detector_passes",detector_passes},{"plays",plays},{"checks_finished",confirms},{"stops",stops},{"restores",restores},{"model_builds",model_builds},{"audio_ready",ready.load()},{"audio_error",arjun_test_status()},{"audio_voices",arjun_test_voices()},{"audio_playing",arjun_test_playing()},{"audio_fading",arjun_test_fading()},{"audio_underruns",arjun_test_underruns()},{"log_dropped",log_dropped},{"note",note},{"error",last_error},{"players",players}});
}
static bool initialize_metadata(){auto db=RF::get()->tdb();if(!db)return false;for(uint32_t i=0;i<db->get_num_types();i++)valid_types.insert(db->get_type(i));auto battle=db->find_type("gBattle");if(!battle)return false;global_player=battle->find_field("Player");global_command=battle->find_field("Command");return global_player&&global_command;}
// Only a real live Arjun engine is accepted. Rollback clones share the listener,
// but do not match its live _Engine / parent _Engine. No rollback getter is invoked.
static int owner(Ref engine,Ref&listener){listener=member(engine,"m_listener");if(!listener||num(listener,"_CharaID",-1)!=34)return -1;int p=(int)num(listener,"_PlayerID",-1);if(p<0||p>1)return -1;const char*name=listener.type->get_name();if(strcmp(name,"CharaActionPart")&&strcmp(name,"ActController"))return -1;auto live=member(listener,"_Engine"),parent_live=member(member(listener,"_Parent"),"_Engine");return live.ptr==engine.ptr||parent_live.ptr==engine.ptr?p:-1;}
static bool refresh(int p,Ref engine,Ref listener,bool resource_event=false){
 auto&b=bindings[p];auto param=member(engine,"mParam"),act=member(listener,"ActParam");auto cp=member(act,"OwnerAdrs");if(!cp||num(cp,"vital_max")<=0)return false;auto opposite=member(cp,"vs_player");auto fab=at(member(engine,"mResource"),1);if(!fab||!member(fab,"StyleDict"))fab=at(member(engine,"mResource"),(int)num(param,"base_slot_index",1));
 bool engine_changed=b.engine!=(uintptr_t)engine.ptr;bool changed=engine_changed||b.fab!=(uintptr_t)fab.ptr;
 if(changed){if(engine_changed){arjun_core::detach(b.reducer,ops(p));b.reducer={};b.suppress_event=restore_requested.load();}b.reducer.model=nullptr;b.models.clear();b.engine=(uintptr_t)engine.ptr;b.fab=(uintptr_t)fab.ptr;b.loaded=0;b.bcm=0;b.retry=0;event("engine_bound",{{"player",p+1},{"engine",b.engine},{"fab",b.fab}});}
 if(resource_event&&!b.models.empty()){auto player=from_field(global_player,nullptr),command=from_field(global_command,nullptr);auto loaded=at(member(player,"mpLoadHitDataAddress"),p),bcm=at(member(command,"mpBCMResource"),p);if(loaded&&(b.loaded!=(uintptr_t)loaded.ptr||(bcm&&b.bcm!=(uintptr_t)bcm.ptr))){b.reducer.model=nullptr;b.models.clear();b.retry=0;event("hit_resource_changed",{{"player",p+1}});}}
 if(!resource_event&&fab&&b.models.empty()&&GetTickCount64()>=b.retry){b.retry=GetTickCount64()+1000;auto player=from_field(global_player,nullptr),command=from_field(global_command,nullptr);auto loaded=at(member(player,"mpLoadHitDataAddress"),p),bcm=at(member(command,"mpBCMResource"),p);b.loaded=(uintptr_t)loaded.ptr;b.bcm=(uintptr_t)bcm.ptr;
  if(build_models(fab,bcm,loaded,b.models)){note="Ready: native Arjun action detection";last_error.clear();Json ids=Json::array();for(auto&m:b.models)ids.push_back(m.first);event("models_ready",{{"player",p+1},{"start_actions",ids},{"loaded_hit_table",b.loaded}});}else{last_error="Stop data not ready; startup playback remains active";event("model_not_ready",{{"player",p+1},{"fab",b.fab},{"loaded",b.loaded},{"bcm",b.bcm}});}
 }
 b.valid=true;b.sample.action=(int)num(param,"id",-1);b.sample.frame=frame(member(param,"frame"));b.sample.opponent=opponent(opposite);return b.sample.action>=0;
}
static int before_action(int argc,void**argv,REFrameworkTypeDefinitionHandle*,unsigned long long){hook_calls++;action_hook_stack.push_back(argc>1?argv[1]:nullptr);return REFRAMEWORK_HOOK_CALL_ORIGINAL;}
static void after_action(void**,REFrameworkTypeDefinitionHandle,unsigned long long){if(action_hook_stack.empty())return;void*ptr=action_hook_stack.back();action_hook_stack.pop_back();if(!ptr)return;std::lock_guard<std::recursive_mutex>guard(detector_mutex);if(!enabled)return;try{auto engine=object(ptr);Ref listener;int p=owner(engine,listener);if(p<0){static ULONGLONG next_rejection=0;if(num(listener,"_CharaID",-1)==34&&GetTickCount64()>=next_rejection){next_rejection=GetTickCount64()+1000;event("arjun_engine_filtered",{{"engine",(uintptr_t)ptr},{"slot",num(listener,"_PlayerID",-1)},{"listener_type",listener.type->get_name()},{"live_engine",(uintptr_t)member(listener,"_Engine").ptr},{"parent_engine",(uintptr_t)member(member(listener,"_Parent"),"_Engine").ptr},{"reason","not_live_owner_or_invalid_slot"}});}return;}if(!refresh(p,engine,listener,true)){static ULONGLONG next_not_ready[2]={};if(GetTickCount64()>=next_not_ready[p]){next_not_ready[p]=GetTickCount64()+1000;auto cp=member(member(listener,"ActParam"),"OwnerAdrs");event("engine_not_ready",{{"player",p+1},{"engine",(uintptr_t)ptr},{"cp_available",(bool)cp},{"vital_max",num(cp,"vital_max",-1)}});}return;}accepted_events++;auto&b=bindings[p];auto details=context(p);details["model_ready"]=b.models.count(b.sample.action)>0;details["restore_suppressed"]=restore_requested.load()||b.suppress_event;event("action_event",details);
 // Consume the actual startup snapshot now. Waiting for UpdateMotion can miss
 // a startup that has already branched into its cinematic continuation.
 if(!arjun_core::startup_kind(b.sample.action,b.models))b.suppress_event=false;
 if(!restore_requested.load())arjun_core::update(b.reducer,b.sample,b.models,ops(p),!b.suppress_event);
 }catch(const std::exception&e){last_error=e.what();event("action_error",{{"error",last_error}});}}
static void detect(){
 if(restore_requested.exchange(false)){arjun_stop_all(nullptr);for(auto&b:bindings){b.reducer={};b.suppress_event=true;}restores++;event("training_restore",{{"fade_ms",1000}});note="Saved state restored: own audio fading";return;}
 if(!enabled){detach_all();return;}
 for(int p=0;p<2;p++){auto&b=bindings[p];if(!b.valid)continue;auto engine=object((void*)b.engine);Ref listener;int slot=owner(engine,listener);if(slot!=p||!refresh(p,engine,listener)){event("engine_detached",{{"player",p+1},{"engine",b.engine},{"reason",slot!=p?"owner_changed":"resource_not_ready"}});arjun_core::detach(b.reducer,ops(p));b.valid=false;b.engine=0;b.models.clear();continue;}detector_passes++;arjun_core::update(b.reducer,b.sample,b.models,ops(p));if(!arjun_core::startup_kind(b.sample.action,b.models))b.suppress_event=false;}
}
static void on_frame(){std::lock_guard<std::recursive_mutex>guard(detector_mutex);callbacks++;last_motion=GetTickCount64();try{detect();}catch(const std::exception&e){last_error=e.what();event("frame_error",{{"error",last_error}});detach_all();}}
static void on_present(){std::lock_guard<std::recursive_mutex>guard(detector_mutex);presents++;auto now=GetTickCount64();if(now<next_status)return;auto fn=RF::get()->param()->functions->is_drawing_ui;bool visible=fn?fn():false;if(!have_ui_visibility||visible!=ui_visible){ui_visible=visible;have_ui_visibility=true;event("ui_visibility",{{"visible",ui_visible},{"motion_frames",callbacks}});}status();flush_events();next_status=now+2000;}
static int on_restore(int,void**,REFrameworkTypeDefinitionHandle*,unsigned long long){restore_requested=true;return REFRAMEWORK_HOOK_CALL_ORIGINAL;}
static void draw(REFImGuiFrameCbData*data){if(!data||!data->context)return;std::lock_guard<std::recursive_mutex>guard(detector_mutex);ui_draws++;ImGui::SetAllocatorFunctions((ImGuiMemAllocFunc)data->malloc_fn,(ImGuiMemFreeFunc)data->free_fn,data->user_data);ImGui::SetCurrentContext((ImGuiContext*)data->context);
 if(!ImGui::TreeNode("Arjun SA3 / CA Audio v0.3.3 (Native)"))return;
 ImGui::TextUnformatted("Native plugin - no Lua dependency");ImGui::Text("Native callback frames: %llu",(unsigned long long)callbacks);
 int track=selected_track-1;if(ImGui::Combo("Audio",&track,"1.WAV (18.8 s)\0" "2.WAV (11.1 s)\0")){selected_track=track+1;save_settings();}
 int percent=arjun_test_percent();if(ImGui::SliderInt("Volume (%)",&percent,0,300)){set_volume(percent);save_settings();}
 if(ImGui::Checkbox("Enable",&enabled))save_settings();
 ImGui::TextUnformatted("Start delay 0.15 s | Miss/block/end fade 1.00 s");ImGui::TextUnformatted(note.c_str());
 ImGui::Text("Audio ready: %s | Action hook: %s | Restore: %s",ready?"yes":"no",action_hook?"ready":"unavailable",restore_hook?"ready":"unavailable");
 for(int p=0;p<2;p++){auto&b=bindings[p];ImGui::Text("P%d | %s | Action %d | Frame %.1f | Starts %llu",p+1,b.valid?"bound":"waiting",b.sample.action,b.sample.frame,(unsigned long long)b.reducer.starts);}
 if(!last_error.empty())ImGui::TextWrapped("Error: %s",last_error.c_str());
 if(ImGui::Button("Preview selected audio"))play(0,selected_track-1);
 ImGui::TreePop();
}
}
API void reframework_plugin_required_version(REFrameworkPluginVersion*version){version->major=REFRAMEWORK_PLUGIN_VERSION_MAJOR;version->minor=REFRAMEWORK_PLUGIN_VERSION_MINOR;version->patch=0;version->game_name=nullptr;}
API bool reframework_plugin_initialize(const REFrameworkPluginInitializeParam*param){
 if(!param||!param->sdk||!param->functions)return false;
 try{RF::initialize(param);wchar_t plugin_dir[MAX_PATH];GetModuleFileNameW(module,plugin_dir,MAX_PATH);wchar_t*slash=wcsrchr(plugin_dir,92);if(!slash)return false;*slash=0;slash=wcsrchr(plugin_dir,92);if(!slash)return false;*(slash+1)=0;swprintf(audio_base_override,MAX_PATH,L"%lsdata\\ArjunSuperAudio\\",plugin_dir);
  arjun_init(nullptr);std::ifstream input(arjun_native::path(L"settings.json"));if(input){auto cfg=nlohmann::json::parse(input,nullptr,false);if(cfg.is_object()){arjun_native::selected_track=std::clamp(cfg.value("track",1),1,2);arjun_native::enabled=cfg.value("enabled",true);set_volume(std::clamp(cfg.value("volume",120),0,300));}}
  if(!arjun_native::initialize_metadata())return false;
  auto engine_type=RF::get()->tdb()->find_type("nAction.Engine");auto action_method=engine_type?engine_type->find_method("SetActionData"):nullptr;if(action_method)arjun_native::action_hook=action_method->add_hook(arjun_native::before_action,arjun_native::after_action,false)!=0;
  auto td=RF::get()->tdb()->find_type("app.training.TrainingManager");auto method=td?td->find_method("FromStorage"):nullptr;if(method)arjun_native::restore_hook=method->add_hook(arjun_native::on_restore,nullptr,false)!=0;
  bool frame=param->functions->on_post_application_entry("UpdateMotion",arjun_native::on_frame);bool ui=param->functions->on_imgui_draw_ui(arjun_native::draw);bool present=param->functions->on_present(arjun_native::on_present);
  arjun_native::note=frame?"Native plugin loaded; waiting for Arjun":"Native frame callback registration failed";arjun_native::event("plugin_initialized",{{"version","0.3.3"},{"update_motion",frame},{"present",present},{"action_hook",arjun_native::action_hook},{"restore_hook",arjun_native::restore_hook}});arjun_native::status();param->functions->log_info("[ArjunNativeAudio] v0.3.3 initialized; UpdateMotion=%d; UI=%d; Present=%d; direct startup",frame,ui,present);return frame&&ui&&present&&arjun_native::action_hook;
 }catch(const std::exception&e){param->functions->log_error("[ArjunNativeAudio] initialization failed: %s",e.what());return false;}
}
