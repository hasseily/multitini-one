// SPDX-License-Identifier: GPL-3.0-only
#include "vectrex32.h"
#include "../../third_party/gsbasic/GSBString.h"
#include "../../third_party/gsbasic/vio.h"
#include <stdexcept>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
struct Test {
    int frames = 0, stop_frame = 2, services = 0, stop_service = 100;
    int stop_render_service = 0;
    int opens = 0, closes = 0, offset = 0, writes = 0;
    bool read_error = false, stop_line = false;
    bool strict_psg = true;
    bool bios_available = true;
    std::string text;
    std::string file_data="hello\n";
    std::string console_input,console_output;
    size_t console_offset=0;
    bool console_cancel=false;
    bool supply_seed=true,supply_clock=true;
    unsigned seed=1,seed_calls=0;
    std::vector<int32_t> line;
    std::vector<int> sounds;
};
static void require(bool condition,const char *message) { if (!condition) { fprintf(stderr,"FAIL %s\n",message); exit(1); } }
static int service(void *p) { Test &t=*static_cast<Test *>(p); return ++t.services < t.stop_service; }
static int begin_frame(void *p) { Test &t=*static_cast<Test *>(p); if(t.stop_render_service){t.services=0;t.stop_service=t.stop_render_service;}return 1; }
static int line(void *p,int32_t x0,int32_t y0,int32_t x1,int32_t y1,uint8_t intensity,uint8_t pattern) {
    Test &t=*static_cast<Test *>(p); t.line.insert(t.line.end(),{x0,y0,x1,y1,intensity,pattern}); return !t.stop_line;
}
static int frame(void *p,unsigned hz,int controls[2][6]) {
    Test &t=*static_cast<Test *>(p); require(hz==60,"frame cadence"); controls[0][0]=-127; controls[0][2]=1; return ++t.frames < t.stop_frame;
}
static int sound(void *p,unsigned reg,uint8_t data) { Test &t=*static_cast<Test *>(p); if(t.strict_psg)require(reg==7 && data==63,"PSG mask"); require(reg<14,"PSG register bound"); t.sounds.push_back(int(reg)*256+data);++t.writes; return 1; }
static void text(void *p,const char *s) { static_cast<Test *>(p)->text += s; }
static void *open(void *p,const char *name) { Test &t=*static_cast<Test *>(p); require(!strcmp(name,"asset"),"relative file name"); ++t.opens; t.offset=0; return &t; }
static int read(void *,void *p) { Test &t=*static_cast<Test *>(p); if(t.read_error) return -2; return size_t(t.offset)<t.file_data.size() ? uint8_t(t.file_data[t.offset++]) : -1; }
static void close(void *p,void *) { ++static_cast<Test *>(p)->closes; }
// Synthetic test fixture, deliberately unrelated to Vectrex ROM data.
static int bios(void *p,uint16_t address,uint8_t *data,size_t count) {
    if(!static_cast<Test *>(p)->bios_available)return 0;
    for(size_t i=0;i<count;++i){unsigned a=address+unsigned(i);data[i]=0;
        if(a>=0xfc8d&&a<0xfd0d)data[i]=(a-0xfc8d)%2?0x23:0x01;
        if(a>=0xfee8&&a<0xfef8)data[i]=0xff;
        if(a>=0xfd0d&&a<0xfd11){const uint8_t header[]={0xfe,0xe8,0xfe,0xb6};data[i]=header[a-0xfd0d];}
        if(a==0xfd12)data[i]=0x80;
        if(a==0xf9f4+'A'-0x20)data[i]=0xa0;
    }return 1;
}
static int run(Test &t,const char *source,mt_v32_result &result) {
    mt_v32_callbacks cb{&t,service,begin_frame,line,frame,sound,text,open,read,close,bios};
    if(t.supply_clock)cb.monotonic_milliseconds=[](void *)->uint64_t{return 1525;};
    if(t.supply_seed)cb.random_seed=[](void *p)->uint32_t{auto &t=*static_cast<Test *>(p);++t.seed_calls;return t.seed;};
    cb.console_read_byte=[](void *p)->int{auto &t=*static_cast<Test *>(p);return t.console_offset<t.console_input.size()?uint8_t(t.console_input[t.console_offset++]):-2;};
    cb.console_write=[](void *p,const void *data,size_t size)->int{auto &t=*static_cast<Test *>(p);if(t.console_cancel)return -2;t.console_output.append(static_cast<const char *>(data),size);return 0;};
    return mt_v32_run(source,"test.bas",&cb,&result);
}
int main() {
    {
        String large('x',40000);bool failed=false;
        try{String oversized=large+large;}catch(const std::length_error &){failed=true;}
        require(failed,"string concatenation refuses uint16 overflow");
        require(String::ftoa(1e30f).length()>30,"large finite float formats within bound");
        require(String().c_str()!=nullptr&&String("x").Substr(1,1).empty(),"empty substring remains a valid C string");
        const char bytes[]={0,'A'};String binary(bytes,2);require(binary.length()==2,"short binary String preserves NUL length");
        FILE *output=tmpfile();require(output!=nullptr,"format test temporary file");char buf[]="trace 100%";
        VFPRINT(output,"%s",buf);rewind(output);char copied[40]={};require(fread(copied,1,sizeof copied-1,output)==strlen(buf)&&!strcmp(copied,buf),"format helper does not shadow caller buffer");fclose(output);
    }
    mt_v32_result result{};
    {
        Test t;
        require(!run(t,"call Sound({{7,255}})\n",result)&&!t.writes,"sound waits for frame boundary");
        t.bios_available=false;
        require(run(t,"call Play(PowerOn)\n",result)&&!strcmp(result.error,"Vectrex BIOS required for built-in music"),"missing BIOS reports error");
    }
    {
        Test t;
        const char *source="call SetFrameRate(60)\ncall ScaleSprite(10)\ncall MoveSprite(2,-3)\ncall IntensitySprite(60)\nx=LinesSprite({{MoveTo,0,0},{DrawTo,3,4}})\nc=WaitForFrame(JoystickDigital,Controller1,3)\nprint c[1,1]\nprint c[1,3]\ncall Sound({{7,255}})\nc=WaitForFrame(1,1,3)\n";
        require(!run(t,source,result),"graphics input sound program");
        require(result.stopped && result.frames==2,"frame cancellation");
        require(t.line.size()==12 && t.line[0]==24 && t.line[1]==-36 && t.line[2]==60 && t.line[3]==12 && t.line[4]==60 && t.line[5]==255,"scaled absolute vertices");
        require(t.text.find("-64")!=std::string::npos && t.writes==1,"input and PSG callback");
    }
    {
        Test t; t.stop_service=3;
        require(!run(t,"call endless()\nsub endless()\nwhile 1\nx=1\nendwhile\nendsub\n",result),"subroutine cancellation");
        require(result.stopped && result.statements==768,"bounded statement service");
    }
    {
        Test t;
        require(!run(t,"f=FOpen(\"asset\",\"r\")\ns=FGets(f)\nprint Len(s)\ncall FClose(f)\n",result),"file program");
        require(t.opens==1 && t.closes==1 && t.text.find('6')!=std::string::npos,"fgets newline and close");
    }
    {
        Test t; t.read_error=true;
        require(run(t,"f=FOpen(\"asset\",\"r\")\ns=FGets(f)\n",result)!=0,"file error propagates");
        require(result.error_line==2 && !strcmp(result.error,"Asset read failed") && t.closes==1,"error source and cleanup");
    }
    {
        Test t;
        require(run(t,"f=FOpen(\"../secret\",\"r\")\n",result)!=0 && t.opens==0,"parent path blocked");
        require(run(t,"include \"../secret\"\n",result)!=0,"include bypass blocked");
        require(run(t,"call CodeSprite({256})\n",result)!=0,"invalid low-level byte fails");
    }
    {
        Test t;
        require(!run(t,"sum=0\nfor a=1 to 3\nfor b=1 to 4\nsum=sum+1\nnext b\nnext a\nprint sum\nprint 2147483647+1\nprint -7\nprint 1.5\n",result),"language nested loops and constants");
        require(t.text.find("12")!=std::string::npos && t.text.find("-2147483648")!=std::string::npos && t.text.find("-7")!=std::string::npos && t.text.find("1.5")!=std::string::npos,"language numeric results");
        require(!run(t,"dim scalar,points[2]\nscalar=11\npoints[1]=12\nprint scalar+points[1]\n",result)&&t.text.find("23")!=std::string::npos,"scalar and indexed DIM use their actual VarSpec types");
    }
    {
        Test t;t.strict_psg=false;t.stop_frame=100;
        require(!run(t,"call SetFrameRate(60)\nm=Music({{ABC(NA3,NC4),2},{NG3,1}})\ncall Play(m)\nwhile MusicIsPlaying()\nc=WaitForFrame(0,0,0)\nendwhile\nprint MusicIsPlaying()\n",result),"music sequence");
        require(t.frames==6 && t.writes>30 && t.text.find('0')!=std::string::npos,"music duration and completion");
    }
    {
        Test t;t.strict_psg=false;t.stop_frame=100;
        require(!run(t,"call SetFrameRate(60)\ne=Explosion(8,0,0,16)\ncall Play(e)\nwhile MusicIsPlaying()\nc=WaitForFrame(0,0,0)\nendwhile\ncall Play(PowerOn)\ncall Play(nil)\nprint MusicIsPlaying()\n",result),"explosion and built-in music stop");
        require(t.frames==10 && t.writes>15 && t.text.find('0')!=std::string::npos,"explosion completion");
    }
    require(mt_v32_valid_relative_path("assets/monster") && !mt_v32_valid_relative_path("C:/secret") && !mt_v32_valid_relative_path("/secret") && !mt_v32_valid_relative_path("a\\..\\b"),"path validation");
    {
        Test t;t.stop_frame=100;
        require(!run(t,"call SetFrameRate(60)\ncall ScaleSprite(0,30)\ns=Lines3DSprite({{0,0,0,1},{1,1,0,1}})\np={1,0,0}\ncall SpriteTranslate(s,p)\np[1]=2\nc=WaitForFrame(0,0,0)\n",result),"3D live position and projection");
        require(t.line.size()==6&&t.line[0]==40&&t.line[2]==60,"world ratio and perspective");
    }
    {
        Test t;t.stop_frame=100;
        require(!run(t,"call SetFrameRate(60)\ncall ScaleSprite(0)\ns=LinesSprite({{0,-2,0},{170,2,0}})\nrect={{-1,-1},{1,1}}\ncall SpriteClip(s,rect)\nc=WaitForFrame(0,0,0)\nrect[2,1]=0\nc=WaitForFrame(0,0,0)\n",result),"live rectangle clipping");
        require(t.line.size()==12&&t.line[0]==-2&&t.line[2]==2&&t.line[5]==170&&t.line[6]==-2&&t.line[8]==0,"clipping endpoints and line pattern");
    }
    {
        Test t;t.stop_frame=100;
        require(!run(t,"call SetFrameRate(60)\ncall ScaleSprite(0,300)\ns=Lines3DSprite({{0,-1,0,-1},{1,1,0,1}})\nc=WaitForFrame(0,0,0)\n",result),"3D near plane crossing");
        require(t.line.size()==6&&t.line[0]==0&&t.line[2]==200,"3D crossing clips at z zero");
    }
    {
        Test t;
        require(!run(t,"print Distance({0,0,0},{{0,1,0},{0,1,2}})\nprint PtInRect({{4,4},{0,0}},{2,2})\na={{0,1,2,3}}\ncall Offset(a,4,5,6)\nprint a[1,4]\ncall CameraSetRotation(10,20,30)\ncall CameraRotate(1,2,3)\na=CameraGetRotation()\nprint a[3]\nx=ScaleSprite(1)\ny=ScaleSprite(2)\ncall PutSpriteBefore(x,y)\nprint DrawingListSprite(1)=y\n",result),"geometry camera and drawing order");
        require(t.text.find("9")!=std::string::npos&&t.text.find("33")!=std::string::npos,"geometry values");
    }
    {
        Test t;t.file_data="sub answer()\nprint 42\nendsub\n";
        require(!run(t,"include \"asset\"\ninclude \"asset\"\ncall answer()\n",result),"confined include with duplicate suppression");
        require(t.opens==1&&t.closes==1&&t.text.find("42")!=std::string::npos,"included function executed");
    }
    {
        Test t;t.file_data="print 87\n";
        require(!run(t,"chain \"asset\"\nprint 99\n",result),"confined chain program transition");
        require(t.text.find("87")!=std::string::npos&&t.text.find("99")==std::string::npos&&t.opens==t.closes,"chain unwinds before replacing program");
    }
    {
        Test t;t.file_data=std::string("\0x",2);
        require(!run(t,"f=FOpen(\"asset\",\"rb\")\ns=FRead(2,f)\nprint Len(s)\nprint GetTickCount()\ncall FClose(f)\n",result),"binary reads and monotonic clock");
        require(t.text.find('2')!=std::string::npos&&t.text.find("1464")!=std::string::npos,"short binary length and 960 Hz tick conversion");
        require(run(t,"f=FOpen(\"asset\",\"rb\")\ns=FRead(65536,f)\n",result),"oversized string read rejected before allocation");
    }
    {
        Test t;
        require(!run(t,"a={0,0,0}\ncall CameraTranslate(a)\nf=FOpen(\"asset\",\"r\")\na[1]=f\nf=nil\n",result)&&t.closes==1,"live camera array releases file before callbacks expire");
    }
    {
        Test t;t.stop_frame=100;
        require(!run(t,"call SetFrameRate(60)\ncall TextSizeSprite({40,5})\ncall MoveSprite(2,3)\ncall TextSprite(\"A\")\nc=WaitForFrame(0,0,0)\n",result),"caller BIOS raster font");
        require(!result.raster_font_fallback&&t.line.size()==12&&t.line[0]==258&&t.line[2]==338&&t.line[6]==418&&t.line[8]==498,"synthetic raster bits and position");
        t.bios_available=false;
        require(!run(t,"call SetFrameRate(60)\ncall TextSprite(\"A\")\nc=WaitForFrame(0,0,0)\n",result)&&result.raster_font_fallback,"missing font fallback explicitly reported");
    }
    {
        Test t;
        require(run(t,"s=TextSizeSprite(a)\n",result),"invalid text size rejected");
        require(run(t,"a={40,5}\ns=TextSizeSprite(a)\na[1]=\"bad\"\nc=WaitForFrame(0,0,0)\n",result),"live text size revalidated");
        require(run(t,"s=LinesSprite({{0,0,0},{1e30,1,1}})\nc=WaitForFrame(0,0,0)\n",result),"huge mode rejected before integer cast");
        require(run(t,"call ScaleSprite(0,1e30)\ns=Lines25DSprite({{0,0},{1,1}})\nc=WaitForFrame(0,0,0)\n",result),"huge projection rejected before integer cast");
    }
    {
        Test t;t.stop_frame=100;
        require(!run(t,"call SetFrameRate(60)\np={0,nil}\ncode={$86,$55,$B7,$C8,$00}\ns=CodeSprite(code)\ncall Peek($C800,1,p)\nprint p[1]\nc=WaitForFrame(0,0,0)\na=p[2]\nprint a[1]\nprint Peek(0)\ncode[2]=66\ncall Peek($C800,1,p)\nc=WaitForFrame(0,0,0)\na=p[2]\nprint a[1]\n",result),"live CodeSprite and deferred Peek");
        require(t.text.find("85")!=std::string::npos&&t.text.find("134")!=std::string::npos&&t.text.find("66")!=std::string::npos,"6809 writes real RAM and shared code bytes");
        Test loop;loop.stop_service=100000;
        require(run(loop,"call CodeSprite({$20,$FE})\nc=WaitForFrame(0,0,0)\n",result),"infinite machine code exhausts finite budget");
    }
    {
        Test t;t.console_input=std::string("\3\0A",3);
        require(!run(t,"call BreakOnControlC(0)\ns=FRead(3,Stdin)\nprint Len(s)\nprint FWrite(s,3,Stdout)\n",result),"binary console input output");
        require(t.console_output==t.console_input&&t.text.find('3')!=std::string::npos,"console preserves NUL and disabled Ctrl C");
        Test stop;stop.console_input="\3";
        require(!run(stop,"x=FGetc(Stdin)\n",result)&&result.stopped,"terminal Ctrl C cancels execution");
        Test output;output.console_cancel=true;
        require(!run(output,"call FPuts(\"stop\",Stdout)\n",result)&&result.stopped,"console output cancellation stays intentional");
    }
    {
        Test t;std::string long_line(70000,'x');long_line+='\n';
        require(run(t,long_line.c_str(),result)&&result.error_line==1,"oversized source line fails before String conversion");
        t.file_data=long_line;
        require(run(t,"include \"asset\"\n",result)&&t.opens==t.closes,"oversized included line fails with cleanup");
        require(run(t,"chain \"asset\"\n",result)&&t.opens==t.closes,"oversized chained line fails with cleanup");
        require(!run(t,"call SetFrameRate(60)\ncall TextSprite(Mid(\"a\",2,1))\nc=WaitForFrame(0,0,0)\nprint 1e30\n",result),"empty substring rendering and large float printing");
    }
    {
        Test t;
        require(!run(t,"s=LinesSprite({{0,0,0},{1,2,3}})\ncall DumpSprite(s)\ncall DumpSprites()\ncall SpritePrintVectors(s)\ne=Explosion(8,0,0,128)\n",result),"public diagnostics and maximum explosion duration");
        require(t.text.find("Lines: enabled")!=std::string::npos&&t.text.find("127")!=std::string::npos&&t.line.empty(),"diagnostics report attributes and vectors without emitting hardware lines");
    }
    {
        Test t;
        require(!run(t,"call SetFrameRate(60)\ncall ScaleSprite(0)\ncall MoveSprite(5,5)\ncall DotsSprite({{10,10},{20,20}})\ncall LinesSprite({{0,0},{1,1}})\nc=WaitForFrame(0,0,0)\n",result),"dots followed by lines");
        require(t.line.size()==18&&t.line[0]==30&&t.line[6]==50&&t.line[12]==0&&t.line[14]==2,"dots use sprite-local origin and reset pen to screen center");
    }
    {
        Test t;t.stop_service=1000;t.stop_render_service=3;
        require(!run(t,"dim p[1024,3]\nfor i=1 to 1024\np[i,1]=0\np[i,2]=i\np[i,3]=0\nnext i\ns=LinesSprite(p)\nc=WaitForFrame(0,0,0)\n",result)&&result.stopped&&!result.frames&&!result.lines&&t.services==3,"move-only paths remain cancellable without emitted lines");
        Test blank;blank.stop_service=1000;blank.stop_render_service=3;
        require(!run(blank,"s=\"\"\nfor i=1 to 1024\ns=s+\" \"\nnext i\ncall TextSprite(s)\nc=WaitForFrame(0,0,0)\n",result)&&result.stopped&&!result.frames&&!result.lines&&blank.services==3,"blank text remains cancellable");
    }
    {
        Test t;t.strict_psg=false;
        std::vector<unsigned> code;
        auto store=[&](unsigned value,unsigned address){code.insert(code.end(),{0x86,value,0xb7,address>>8,address&255});};
        auto psg=[&](unsigned reg,unsigned value){store(reg,0xd001);store(0x19,0xd000);store(1,0xd000);store(value,0xd001);store(0x11,0xd000);store(1,0xd000);};
        psg(13,9);psg(13,9);
        std::string source="call SetFrameRate(60)\ncall Sound({{8,15}})\ncode={ _\n";
        for(size_t i=0;i<code.size();++i){source+=std::to_string(code[i]);if(i+1<code.size())source+=",";if(i%15==14)source+=" _\n";}
        source+="}\ncall CodeSprite(code)\nc=WaitForFrame(0,0,0)\n";
        require(!run(t,source.c_str(),result),"CodeSprite repeated AY writes");
        require(t.sounds==std::vector<int>({8*256+15,13*256+9,13*256+9}),"Sound precedes both machine-code envelope restarts without coalescing");
    }
    {
        const char *prefix="call Randomize()\na=Rand()\ncall Randomize(";
        const char *suffix=")\nif a<>Rand() then\ncall CodeSprite({256})\nendif\ncall Randomize(77)\nb=Rand()\ncall Randomize(77)\nif b<>Rand() then\ncall CodeSprite({256})\nendif\n";
        Test injected;injected.seed=123;
        require(!run(injected,(std::string(prefix)+"123"+suffix).c_str(),result)&&injected.seed_calls==1,"no-argument Randomize uses callback; explicit seed stays repeatable");
        Test clock_seed;clock_seed.supply_seed=false;
        require(!run(clock_seed,(std::string(prefix)+"1525"+suffix).c_str(),result),"Randomize falls back to monotonic clock");
        Test fallback;fallback.supply_seed=fallback.supply_clock=false;
        require(!run(fallback,(std::string(prefix)+"1"+suffix).c_str(),result),"Randomize has documented seed1 fallback without host sources");
    }
    puts("Vectrex32 callback, language, cancellation, and filesystem tests passed");
    return 0;
}
