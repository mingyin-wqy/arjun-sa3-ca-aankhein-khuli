#include "ArjunBattleCore.hpp"
#include <cassert>
#include <iostream>
using namespace arjun_core;
struct H{State state;int plays=0,stops=0,releases=0;std::unordered_map<int,Model> models;Ops ops{[this]{plays++;},[this]{stops++;},[this]{releases++;}};void step(int id,double f=0,int seq=0,int guard=0,uintptr_t param=0,bool event=true){update(state,{id,f,{0,guard,0,0,param,false}},models,ops,event);}};
int main(){
 {H h;h.step(1237,0,25);assert(h.plays==1);h.step(1237,0,25,0,0,false);assert(h.plays==1);h.step(1239,0,27);assert(h.releases==1&&h.stops==0);h.step(0,0,169);h.step(1244,0,1);assert(h.plays==2);h.step(1246);assert(h.releases==2);h.step(1247);assert(h.plays==2);}
 {H h;h.step(1244);assert(h.plays==1);h.step(0);assert(h.stops==1);}
 {H h;Model m;m.kind=1;m.actions[1237]={20};m.params[11]=2;h.models[1237]=m;h.step(1237);assert(h.plays==1);Sample block{1237,10,{1000,15,0,0,11,true}};update(h.state,block,h.models,h.ops);assert(h.stops==1);}
 {H h;Model m;m.kind=1;m.actions[1237]={20};h.models[1237]=m;h.step(1237);h.step(1237,22,0,0,0,false);assert(h.stops==1);}
 {H h;h.step(1244,0,166);assert(h.plays==1);h.state={};h.step(1244,0,1,0,0,false);assert(h.plays==1);h.step(0);h.step(1244);assert(h.plays==2);}
 {H h;h.step(1237);assert(h.plays==1);Model m;m.kind=1;m.actions[1237]={20};h.models[1237]=m;h.step(1237,22,0,0,0,false);assert(h.plays==1&&h.stops==1);}
 std::cout<<"PASS: direct startup with no hit model/HP/serial ordering/UI; only live startup events play; low serial after 169; follow-up releases only stop monitoring; block/whiff/active-window fade requests; restored startup suppression; delayed stop model never gates playback.\n";
}
