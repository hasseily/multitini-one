// SPDX-License-Identifier: GPL-3.0-only
#include "vectrex32.h"
#include "vectrex32_debug.h"
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <queue>
#include <string>
#include <vector>
namespace fs = std::filesystem;
struct Host {
    std::vector<uint8_t> bios;
    fs::path root;
    unsigned frames = 0, maximum = 2000, opens = 0;
    uint64_t lines = 0, services = 0, digest = 1469598103934665603ull;
    int32_t min_x = 0,max_x = 0,min_y = 0,max_y = 0;
    unsigned sound = 0;
    bool pressed = false;
    bool explore = false;
    bool entered = false, exited = false, attacked = false;
    bool combat = false;
    unsigned entries = 0;
    int last_depth = 0, last_food = -1, last_hp = -1;
    int previous_monster_hp[10] = {};
    fs::path capture_root;
    unsigned last_source = 0;
    std::ofstream svg;
    std::vector<std::string> frame_lines;
    std::string console;
};
static int service(void *p) { return ++static_cast<Host *>(p)->services < 1000000; }
static int begin(void *p) { static_cast<Host *>(p)->frame_lines.clear(); return 1; }
static int observe(const char *name,int index=-1) { double value=0; mt_v32_debug_number(name,index,&value); return int(value); }
static void capture(const Host &h,const char *name) {
    if(h.capture_root.empty()) return;
    std::ofstream out(h.capture_root/name);
    out << "<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"-18000 -24000 36000 48000\"><rect x=\"-18000\" y=\"-24000\" width=\"36000\" height=\"48000\" fill=\"black\"/><g fill=\"none\" stroke=\"white\" stroke-width=\"35\">\n";
    for(const auto &line:h.frame_lines) out<<line;
    out<<"</g></svg>\n";
}
static int next_direction(int x,int y,bool dungeon,bool retreat) {
    const int width=dungeon?9:21;
    const int start=(y-1)*width+x-1;
    std::vector<int> direction(width*width,-1);
    std::queue<int> pending; pending.push(start);direction[start]=0;
    const int dx[]={0,1,0,-1},dy[]={-1,0,1,0};
    while(!pending.empty()) {
        int node=pending.front();pending.pop();
        int nx=node%width+1,ny=node/width+1;
        int content=dungeon?observe("level",node*5+4):observe("outsideMap",(nx-1)*21+ny-1);
        bool target=dungeon?(retreat?content==11:content>=1&&content<=10):content==4;
        if(target) return direction[node];
        for(int d=0;d<4;++d) {
            int xx=nx+dx[d],yy=ny+dy[d];
            if(xx<1||xx>width||yy<1||yy>width) continue;
            if(dungeon ? observe("level",node*5+d)==1 : observe("outsideMap",(xx-1)*21+yy-1)==1) continue;
            int next=(yy-1)*width+xx-1;
            if(direction[next]>=0) continue;
            direction[next]=node==start?d+1:direction[node];pending.push(next);
        }
    }
    return -1;
}
static void explore(Host &h,int controls[2][6]) {
    std::fill(&controls[0][0],&controls[0][0]+12,0);
    unsigned source=mt_v32_debug_line();
    int x=observe("playerDetails",0),y=observe("playerDetails",1),depth=observe("playerDetails",6);
    int food=observe("playerDetails",3),hp=observe("playerDetails",4);
    if(depth!=h.last_depth) {
        fprintf(stderr,"depth_transition=%d->%d frame=%u\n",h.last_depth,depth,h.frames);
        if(depth>0){h.entered=true;++h.entries;capture(h,"dungeon-entry.svg");}
        else if(h.entered){h.exited=true;capture(h,"dungeon-exit.svg");}
        h.last_depth=depth;
        for(int i=0;i<10;++i)h.previous_monster_hp[i]=observe("monsterHP",i);
    }
    if(food!=h.last_food||hp!=h.last_hp) {
        fprintf(stderr,"resources frame=%u food=%d hp=%d\n",h.frames,food,hp);
        h.last_food=food;h.last_hp=hp;
    }
    if(depth>0)for(int i=0;i<10;++i){
        int mhp=observe("monsterHP",i);
        if(mhp<h.previous_monster_hp[i]){h.combat=true;fprintf(stderr,"monster_damage=%d:%d->%d frame=%u\n",i+1,h.previous_monster_hp[i],mhp,h.frames);capture(h,"combat.svg");}
        h.previous_monster_hp[i]=mhp;
    }
    if(source==739||source==852) {
        int selection=observe("selection");
        int target=food<600?2:observe("playerDetails",11)==0&&observe("playerDetails",5)>=8?3:1;
        if(selection>=8 || selection==target)controls[0][2]=1;
        else controls[0][1]=selection<target?1:-1;
    } else if(source==192) {
        if(!depth) {
            int direction=next_direction(x,y,false,false);
            if(direction==0)controls[0][2]=1;
            else if(direction==1)controls[0][1]=-1;
            else if(direction==2)controls[0][0]=1;
            else if(direction==3)controls[0][1]=1;
            else if(direction==4)controls[0][0]=-1;
        } else {
            bool retreat=h.entries==1||h.combat;
            int direction=next_direction(x,y,true,retreat);
            if(direction==0&&retreat)controls[0][2]=1;
            else if(direction>0){
                int facing=observe("playerDetails",2);
                if(facing!=direction)controls[0][0]=1;
                else {
                    const int dx[]={0,1,0,-1},dy[]={-1,0,1,0};
                    int next=(y-1+dy[direction-1])*9+x-1+dx[direction-1];
                    int content=observe("level",next*5+4);
                    if(content>=1&&content<=10){controls[0][3]=1;h.attacked=true;}
                    else controls[0][1]=1;
                }
            }
        }
    } else {
        controls[0][2]=h.frames>90 && h.frames%90<15;
    }
}
static int line(void *p,int32_t x0,int32_t y0,int32_t x1,int32_t y1,uint8_t intensity,uint8_t pattern) {
    Host &h = *static_cast<Host *>(p); ++h.lines;
    h.min_x = std::min({h.min_x,x0,x1}); h.max_x = std::max({h.max_x,x0,x1});
    h.min_y = std::min({h.min_y,y0,y1}); h.max_y = std::max({h.max_y,y0,y1});
    uint32_t data[] = {uint32_t(x0),uint32_t(y0),uint32_t(x1),uint32_t(y1),intensity,pattern};
    for (uint32_t word : data) for(int j=0;j<4;++j) { h.digest ^= uint8_t(word>>(j*8)); h.digest *= 1099511628211ull; }
    if (h.svg.is_open()) {
        char buffer[256];
        snprintf(buffer,sizeof buffer,"<path d=\"M%d %dL%d %d\" opacity=\"%.3f\"/>\n",x0,-y0,x1,-y1,double(intensity)/127);
        h.frame_lines.emplace_back(buffer);
    }
    return 1;
}
static int frame(void *p,unsigned hz,int controls[2][6]) {
    Host &h = *static_cast<Host *>(p); ++h.frames;
    if (h.explore && mt_v32_debug_line() != h.last_source) {
        h.last_source = mt_v32_debug_line();
        double x=0,y=0,depth=0,food=0,hp=0;
        mt_v32_debug_number("playerDetails",0,&x);mt_v32_debug_number("playerDetails",1,&y);
        mt_v32_debug_number("playerDetails",6,&depth);mt_v32_debug_number("playerDetails",3,&food);mt_v32_debug_number("playerDetails",4,&hp);
        fprintf(stderr,"state frame=%u line=%u x=%.0f y=%.0f depth=%.0f food=%.0f hp=%.0f\n",h.frames,h.last_source,x,y,depth,food,hp);
    }
    if (h.frames % 300 == 0) fprintf(stderr,"frame=%u hz=%u lines=%llu opens=%u\n",h.frames,hz,(unsigned long long)h.lines,h.opens);
    // Press/release button 1 through setup; later walk around the generated world.
    controls[0][2] = h.frames > 90 && (h.frames % 90) < 15;
    if (h.frames > 700) {
        unsigned phase = h.frames / 120 % 4;
        controls[0][0] = phase == 0 ? 1 : phase == 2 ? -1 : 0;
        controls[0][1] = phase == 1 ? 1 : phase == 3 ? -1 : 0;
    }
    if(h.explore) explore(h,controls);
    // Replay decisions use direction signs; the callback supplies physical
    // joystick values, so drive to full travel before runtime quantization.
    controls[0][0]*=127;controls[0][1]*=127;
    if (h.frames == h.maximum && h.svg.is_open()) {
        h.svg << "<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"-18000 -22000 36000 44000\"><rect x=\"-18000\" y=\"-22000\" width=\"36000\" height=\"44000\" fill=\"black\"/><g fill=\"none\" stroke=\"white\" stroke-width=\"35\">\n";
        for (const auto &l : h.frame_lines) h.svg << l;
        h.svg << "</g></svg>\n";
    }
    return h.frames < h.maximum && !(h.explore && h.combat && h.exited && h.last_depth==0);
}
static int psg(void *p,unsigned,uint8_t) { ++static_cast<Host *>(p)->sound; return 1; }
static void text(void *p,const char *text) { Host &h=*static_cast<Host *>(p); if(h.console.size()<1024*1024) h.console+=text; }
static void *open(void *p,const char *name) {
    Host &h = *static_cast<Host *>(p);
    if (!mt_v32_valid_relative_path(name)) return nullptr;
    std::error_code error;
    fs::path path = fs::canonical(h.root / name,error);
    if (error) return nullptr;
    fs::path relative = path.lexically_relative(h.root);
    if (relative.empty() || *relative.begin() == "..") return nullptr;
    FILE *file = fopen(path.string().c_str(),"rb");
    if(file) { ++h.opens; fprintf(stderr,"asset=%s\n",name); }
    return file;
}
static int read(void *,void *f) { int c = fgetc(static_cast<FILE *>(f)); return c == EOF && ferror(static_cast<FILE *>(f)) ? -2 : c; }
static void close(void *,void *f) { fclose(static_cast<FILE *>(f)); }
static int read_bios(void *p,uint16_t address,uint8_t *data,size_t size) {
    const auto &bios=static_cast<Host *>(p)->bios;
    if(bios.size()!=8192||address<0xe000||size>65536u-address)return 0;
    memcpy(data,bios.data()+address-0xe000,size);return 1;
}
int main(int argc,char **argv) {
    const char *bios_path=nullptr;std::vector<char *> arguments{argv[0]};
    for(int i=1;i<argc;++i){if(!strcmp(argv[i],"--bios")){if(bios_path||i+1==argc){fprintf(stderr,"--bios requires one external 8192-byte BIOS\n");return 2;}bios_path=argv[++i];}else arguments.push_back(argv[i]);}
    argc=int(arguments.size());argv=arguments.data();
    if (argc < 2 || argc > 5) { fprintf(stderr,"usage: vectrex32_host external.bas [frames] [last-frame.svg] [explore] [--bios external.bin]\n"); return 2; }
    Host host;
    if(bios_path){std::ifstream input(bios_path,std::ios::binary);if(input)host.bios.assign(std::istreambuf_iterator<char>(input),{});
        if(host.bios.size()!=8192||input.bad()){fprintf(stderr,"External BIOS must contain exactly 8192 bytes\n");return 2;}}
    std::error_code error;
    fs::path source = fs::canonical(argv[1],error);
    if(error) { fprintf(stderr,"Cannot resolve source\n"); return 2; }
    host.root = source.parent_path();
    if(argc > 2) host.maximum = unsigned(strtoul(argv[2],nullptr,10));
    if(!host.maximum) return 2;
    if(argc > 3) {host.svg.open(argv[3],std::ios::binary);host.capture_root=fs::absolute(argv[3]).parent_path();}
    if(argc > 4) host.explore = !strcmp(argv[4],"explore");
    std::ifstream input(source,std::ios::binary);
    if(!input) return 2;
    std::string content{std::istreambuf_iterator<char>(input),std::istreambuf_iterator<char>()};
    mt_v32_callbacks callbacks{&host,service,begin,line,frame,psg,text,open,read,close};
    if(bios_path)callbacks.read_bios=read_bios;
    callbacks.monotonic_milliseconds=[](void *)->uint64_t{return uint64_t(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());};
    callbacks.random_seed=[](void *)->uint32_t{return 1;};
    mt_v32_result result{};
    int status = mt_v32_run(content.c_str(),source.filename().string().c_str(),&callbacks,&result);
    if(!host.capture_root.empty()) {std::ofstream log(host.capture_root/"console.log");log<<host.console;}
    if(host.explore) fprintf(stderr,"exploration entered=%d exited=%d attacks=%d damage=%d entries=%u\n",host.entered,host.exited,host.attacked,host.combat,host.entries);
    printf("raster_font=%s\n",result.raster_font_fallback?"vector_fallback":"bios_or_unused");
    printf("status=%d frames=%llu statements=%llu lines=%llu assets=%u psg=%u digest=%016llx bounds=%d,%d,%d,%d stopped=%d error_line=%u error=%s\n",
        status,(unsigned long long)result.frames,(unsigned long long)result.statements,(unsigned long long)result.lines,
        host.opens,host.sound,(unsigned long long)host.digest,host.min_x,host.min_y,host.max_x,host.max_y,result.stopped,result.error_line,result.error);
    return status;
}
