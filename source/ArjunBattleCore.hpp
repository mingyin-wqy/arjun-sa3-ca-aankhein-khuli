#pragma once
#include <stdint.h>
#include <unordered_map>
#include <unordered_set>
#include <functional>
namespace arjun_core {
struct Opponent {int hp=0,guard=0,damage=0,hitstop=0;uintptr_t param=0;bool valid=false;};
struct Action {double attack_end=-1;};
struct Model {std::unordered_map<int,Action> actions;std::unordered_map<uintptr_t,int> params;int kind=0;};
struct Sample {int action=0;double frame=0;Opponent opponent;};
struct State {bool pending=false;const Model* model=nullptr;int kind=0;uint64_t starts=0;};
// release only ends miss/block monitoring; it does not start or stop audio.
struct Ops {std::function<void()> play,stop,release;};
inline int startup_kind(int id,const std::unordered_map<int,Model>&models){auto m=models.find(id);if(m!=models.end()&&m->second.kind)return m->second.kind;switch(id){case 1237:case 1238:return 1;case 1244:case 1245:return 2;default:return 0;}}
inline bool continuation(int kind,int id){return kind==1?id==1239:kind==2&&(id==1246||id==1247);}
inline void detach(State&s,const Ops&ops){if(s.pending)ops.release();s.pending=false;s.model=nullptr;s.kind=0;}
inline void update(State&s,const Sample&x,const std::unordered_map<int,Model>&models,const Ops&ops,bool live_action_event=false){
 int next_kind=startup_kind(x.action,models);
 bool start=next_kind&&live_action_event;
 if(start){ops.play();s.starts++;s.pending=true;s.kind=next_kind;auto m=models.find(x.action);s.model=m==models.end()?nullptr:&m->second;}
 else if(s.pending){
  if(!s.model){auto m=models.find(x.action);if(m!=models.end())s.model=&m->second;}
  if(continuation(s.kind,x.action)){ops.release();s.pending=false;}
  else{
   bool block=false;if(s.model&&x.opponent.valid&&x.opponent.guard>0){auto hit=s.model->params.find(x.opponent.param);block=hit!=s.model->params.end()&&hit->second==2;}
   bool miss=!next_kind;
   if(s.model&&next_kind){auto a=s.model->actions.find(x.action);if(a!=s.model->actions.end()&&a->second.attack_end>=0&&x.frame>a->second.attack_end+1)miss=true;}
   if(block||miss){ops.stop();s.pending=false;}
  }
 }
}
}
