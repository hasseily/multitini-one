// SPDX-License-Identifier: GPL-3.0-only
#include "vectrex32.h"
#include "lowlevel.h"
#include "Executive.h"
#include "BuiltInFunctions.h"
#include "BuiltInConstants.h"
#include "vectrex32/VectorFont.h"
#include "vectrex32/VectrexSound.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <new>
#include <memory>
#include <string>
#include <vector>

namespace {
enum Kind { Origin, Move, Scale, Intensity, Lines, Dots, Text, TextSize, Lines3D, Lines25D, VectorText, VectorTextList, RasterText, Code };
struct Sprite : GSDataObject {
    Kind kind;
    Value data;
    Value user_data, translation, clipping;
    bool enabled = true;
    double angle = 0, mag_x = 1, mag_y = 1, translate_x = 0, translate_y = 0;
    double angles[3] = {}, mag_z = 1, ratio = 0;
    explicit Sprite(Kind k, const Value &v = Value()) : kind(k), data(v) {}
};
struct Runtime {
    const mt_v32_callbacks &cb;
    mt_v32_result &result;
    Runtime(const mt_v32_callbacks &callbacks,mt_v32_result &output):cb(callbacks),result(output){}
    std::vector<Value> drawing;
    Value music;
    uint8_t psg[14] = {};
    uint16_t pending_psg = 0;
    uint16_t frequencies[64] = {};
    bool frequencies_loaded = false;
    uint8_t raster_font[7*80] = {};
    bool raster_checked = false,raster_available = false;
    double camera_angles[3] = {}, focal_length = 50, world_units = 324.0/0.097;
    Value camera_translation;
    mt_v32_callbacks low_callbacks{};
    mt_v32_lowlevel *lowlevel=nullptr;
    uint64_t code_budget=0;
    struct PeekRequest {uint16_t address;unsigned count;Value array;};
    std::vector<PeekRequest> peeks;
    unsigned hz = 30;
    bool cancelled = false;
    bool break_on_control_c=true;
    bool diagnostic_vectors=false;
    unsigned source_line = 0;
    unsigned compiled_sprites = 0;
    unsigned render_work = 0;
    RunTimeEnvironment *frame_environment = nullptr;
    bool accepted(int result) { if (!result) cancelled = true; return result != 0; }
};
Runtime *active = nullptr;
bool render_progress() {
    return (++active->render_work & 255u) || !active->cb.service ||
        active->accepted(active->cb.service(active->cb.context));
}
bool flush_psg() {
    for(unsigned reg=0;reg<14;++reg)if(active->pending_psg&(1u<<reg)) {
        if(!active->cb.psg||!active->accepted(active->cb.psg(active->cb.context,reg,active->psg[reg])))return false;
    }
    active->pending_psg=0;return true;
}
template<class T,class... Args> Value handle_value(Args&&... args){
    std::unique_ptr<T> owner(new T(std::forward<Args>(args)...));Value result(owner.get());owner.release();return result;
}
bool emit(double,double,double,double,uint8_t,uint8_t=255);
bool lowlevel() {
    if(active->lowlevel)return true;
    active->low_callbacks=active->cb;
    active->low_callbacks.line=[](void *,int32_t x0,int32_t y0,int32_t x1,int32_t y1,uint8_t intensity,uint8_t pattern)->int{return emit(x0,y0,x1,y1,intensity,pattern);};
    active->low_callbacks.psg=[](void *,unsigned reg,uint8_t value)->int{
        if(!flush_psg())return 0;
        if(reg<14){if(reg==7)value&=63;active->psg[reg]=value;}
        return active->cb.psg&&active->accepted(active->cb.psg(active->cb.context,reg,value));
    };
    active->lowlevel=mt_v32_lowlevel_create(&active->low_callbacks);return active->lowlevel!=nullptr;
}
ErrorCode lowlevel_error(mt_v32_lowlevel_status status) {
    if(status==MT_V32_LL_OK)return nullptr;
    if(status==MT_V32_LL_CANCELLED){active->cancelled=true;return StopExecution;}
    return mt_v32_lowlevel_error(active->lowlevel);
}
struct BuiltinMusic : GSDataObject { uint16_t address; explicit BuiltinMusic(uint16_t a):address(a){} };
bool read_bios(uint16_t address,uint8_t *data,size_t count) {
    return active->cb.read_bios && count<=65536u-address && active->cb.read_bios(active->cb.context,address,data,count);
}
bool load_frequencies() {
    if(active->frequencies_loaded)return true;
    uint8_t bytes[128];if(!read_bios(0xfc8d,bytes,sizeof bytes))return false;
    for(unsigned i=0;i<64;++i)active->frequencies[i]=uint16_t(bytes[i*2]<<8|bytes[i*2+1]);
    active->frequencies_loaded=true;return true;
}
struct File : GSDataObject {
    void *handle;
    bool modern, eof = false;
    unsigned terminal=0;
    int64_t position = 0;
    explicit File(void *h,bool m=false) : handle(h),modern(m) {}
    explicit File(unsigned t):handle(reinterpret_cast<void *>(uintptr_t(t))),modern(false),terminal(t){}
    int close() {
        if(!handle)return -1;
        if(terminal){handle=nullptr;return 0;}
        int status=0;
        if(modern)status=active->cb.file_close(active->cb.context,handle);
        else active->cb.close(active->cb.context,handle);
        handle=nullptr;return status;
    }
    int read() {
        int value;
        if(terminal){
            if(terminal!=1||!active->cb.console_read_byte)return -2;
            for(;;){value=active->cb.console_read_byte(active->cb.context);
                if(value==-1){if(!active->cb.service||!active->accepted(active->cb.service(active->cb.context)))return -2;continue;}
                if(value==-2){value=-1;break;}if(value<0||value>255)return -2;
                if(value==3&&active->break_on_control_c){active->cancelled=true;return -2;}break;
            }
        }else if(modern){uint8_t byte;size_t received=0;
            if(!active->cb.file_read||active->cb.file_read(active->cb.context,handle,&byte,1,&received)||received>1)return -2;
            value=received?byte:-1;
        }else value=active->cb.read_byte(active->cb.context,handle);
        if(value==-1)eof=true;else if(value>=0)++position;
        return value;
    }
    ~File() override {
        if(handle&&close()&&active&&!active->result.error[0])snprintf(active->result.error,sizeof active->result.error,"Asset close failed");
    }
};
Value file_value(void *handle,bool modern) {
    if(!handle)return Value();
    File *file;
    try {file=new File(handle,modern);}catch(...){if(modern)active->cb.file_close(active->cb.context,handle);else active->cb.close(active->cb.context,handle);throw;}
    std::unique_ptr<File> owner(file);Value result(file);owner.release();return result;
}
float number(const Value &v) { return v.IsInteger() ? float(v.Integer()) : v.Float(); }
bool array(const Value &v, int rank, int width) {
    return v.IsArray() && v.DimensionsSize() == rank && v.Dimensions()[rank - 1] == width;
}
ErrorCode sprite(RunTimeEnvironment &r, Kind kind, const Value &value = Value()) {
    Value handle=handle_value<Sprite>(kind,value);
    active->drawing.push_back(handle);
    SetReturnValue(r, handle);
    return nullptr;
}
ErrorCode clear(Executive &, RunTimeEnvironment &, int) { active->drawing.clear(); return nullptr; }
ErrorCode origin(Executive &, RunTimeEnvironment &r, int) { return sprite(r, Origin); }
ErrorCode move(Executive &, RunTimeEnvironment &r, int) {
    const Value &x = r.calculationStack.back(1), &y = r.calculationStack.back();
    if (!x.IsNumeric() || !y.IsNumeric()) return IllegalArgument;
    uint16_t dim = 2; Value v(&dim, 1); v[0] = x; v[1] = y;
    return sprite(r, Move, v);
}
ErrorCode scale(Executive &, RunTimeEnvironment &r, int count) {
    const Value &v=r.calculationStack.back(count-1);
    if(!v.IsNumeric()||!std::isfinite(number(v))||number(v)<0||number(v)>255)return IllegalArgument;
    double ratio=0;if(count==2){const Value &value=r.calculationStack.back();if(!value.IsNumeric()||!std::isfinite(number(value)))return IllegalArgument;ratio=number(value);}
    ErrorCode error=sprite(r,Scale,v);static_cast<Sprite *>(active->drawing.back().Handle())->ratio=ratio;return error;
}
ErrorCode intensity(Executive &, RunTimeEnvironment &r, int) {
    const Value &v = r.calculationStack.back();
    if (!v.IsNumeric() || !std::isfinite(number(v)) || number(v) < 0 || number(v) > 127) return IllegalArgument;
    return sprite(r, Intensity, v);
}
ErrorCode lines(Executive &, RunTimeEnvironment &r, int) {
    const Value &v = r.calculationStack.back();
    if (!array(v, 2, 3) && !array(v,2,2)) return IllegalArgument;
    return sprite(r, Lines, v);
}
ErrorCode lines_3d(Executive &,RunTimeEnvironment &r,int) {
    const Value &v=r.calculationStack.back();if(!array(v,2,3)&&!array(v,2,4))return IllegalArgument;return sprite(r,Lines3D,v);
}
ErrorCode lines_25d(Executive &,RunTimeEnvironment &r,int) {
    const Value &v=r.calculationStack.back();if(!array(v,2,2)&&!array(v,2,3))return IllegalArgument;return sprite(r,Lines25D,v);
}
bool machine_bytes(const Value &v){
    if(!v.IsArray()||v.DimensionsSize()!=1||v.NumberOfElements()>2048)return false;
    for(size_t i=0;i<v.NumberOfElements();++i)if(!v[int(i)].IsInteger()||v[int(i)].Integer()<0||v[int(i)].Integer()>255)return false;
    return true;
}
ErrorCode code_sprite(Executive &,RunTimeEnvironment &r,int){const Value &v=r.calculationStack.back();if(!machine_bytes(v))return IllegalArgument;if(!lowlevel())return "Cannot allocate 6809 execution state";return sprite(r,Code,v);}
ErrorCode peek(Executive &,RunTimeEnvironment &r,int count){
    if(count!=1&&count!=3)return WrongNumberOfArguments;
    const Value &address=r.calculationStack.back(count-1);if(!address.IsInteger())return IllegalArgument;
    if(count==1){if(address.Integer()<0||address.Integer()>2047)return "Synchronous Peek supports shared RAM addresses 0..2047";
        if(!lowlevel())return "Cannot allocate 6809 execution state";uint8_t value;
        ErrorCode error=lowlevel_error(mt_v32_lowlevel_peek_dpram(active->lowlevel,unsigned(address.Integer()),&value,1));if(error)return error;SetReturnValue(r,Value(int(value)));return nullptr;
    }
    const Value &length=r.calculationStack.back(1);Value &array_value=r.calculationStack.back();
    if(!length.IsInteger()||length.Integer()<1||length.Integer()>16||!array(array_value,1,2)||address.Integer()<32768||address.Integer()>65536-length.Integer())return IllegalArgument;
    if(!lowlevel())return "Cannot allocate 6809 execution state";
    array_value[0]=0;uint16_t dim=uint16_t(length.Integer());array_value[1]=Value(&dim,1);
    active->peeks.push_back({uint16_t(address.Integer()),unsigned(length.Integer()),array_value});return nullptr;
}
ErrorCode dots(Executive &, RunTimeEnvironment &r, int) {
    const Value &v = r.calculationStack.back();
    if (!array(v, 2, 2)) return IllegalArgument;
    return sprite(r, Dots, v);
}
ErrorCode text_list(Executive &, RunTimeEnvironment &r, int) {
    const Value &v = r.calculationStack.back();
    if (!array(v, 2, 3)) return IllegalArgument;
    return sprite(r, Text, v);
}
ErrorCode text_size(Executive &, RunTimeEnvironment &r, int) {
    const Value &v = r.calculationStack.back();
    if (!array(v, 1, 2) || !v[0].IsNumeric() || !v[1].IsNumeric()) return IllegalArgument;
    return sprite(r, TextSize, v);
}
ErrorCode set_hz(Executive &, RunTimeEnvironment &r, int) {
    const Value &v = r.calculationStack.back();
    if (!v.IsNumeric() || !std::isfinite(number(v)) || number(v) < 1) return IllegalArgument;
    active->hz = unsigned(std::min(150.0f,number(v))); return nullptr;
}
ErrorCode get_hz(Executive &, RunTimeEnvironment &r, int) { SetReturnValue(r, Value(int(active->hz))); return nullptr; }
ErrorCode get_tick(Executive &,RunTimeEnvironment &r,int) {
    if(!active->cb.monotonic_milliseconds)return "Monotonic clock callback required";
    uint64_t ms=active->cb.monotonic_milliseconds(active->cb.context);
    SetReturnValue(r,Value(int(int32_t(uint32_t((ms/25)*24+(ms%25)*24/25)))));return nullptr;
}
ErrorCode compiled_count(Executive &,RunTimeEnvironment &r,int){SetReturnValue(r,Value(int(active->compiled_sprites)));return nullptr;}
Sprite *sprite_arg(RunTimeEnvironment &r,int depth=0) {
    const Value &v = r.calculationStack.back(depth);
    return v.IsHandle() ? dynamic_cast<Sprite *>(v.Handle()) : nullptr;
}
ErrorCode remove_sprite(Executive &,RunTimeEnvironment &r,int) {
    Sprite *s=sprite_arg(r); if(!s)return IllegalArgument;
    auto &drawing=active->drawing;
    drawing.erase(std::remove_if(drawing.begin(),drawing.end(),[s](const Value &v){return v.Handle()==s;}),drawing.end());
    return nullptr;
}
ErrorCode drawing_sprite(Executive &,RunTimeEnvironment &r,int) {
    const Value &v=r.calculationStack.back();
    if(!v.IsInteger() || v.Integer()<1 || size_t(v.Integer())>active->drawing.size())return IllegalArgument;
    SetReturnValue(r,active->drawing[v.Integer()-1]);return nullptr;
}
ErrorCode enable_sprite(Executive &,RunTimeEnvironment &r,int) {
    Sprite *s=sprite_arg(r,1); const Value &v=r.calculationStack.back();
    if(!s||!v.IsInteger())return IllegalArgument;s->enabled=v.Integer()!=0;return nullptr;
}
ErrorCode set_data(Executive &,RunTimeEnvironment &r,int) {
    Sprite *s=sprite_arg(r,1);const Value &v=r.calculationStack.back();if(!s)return IllegalArgument;
    if(s->kind==Code){if(!machine_bytes(v))return IllegalArgument;s->data=v;return nullptr;}
    int width=s->kind==Lines3D?3:2;
    if(s->kind!=Lines&&s->kind!=Dots&&s->kind!=Lines3D&&s->kind!=Lines25D)return IllegalArgument;
    if(!array(v,2,width)&&(s->kind==Dots||!array(v,2,width+1)))return IllegalArgument;
    s->data=v;return nullptr;
}
ErrorCode set_user_data(Executive &,RunTimeEnvironment &r,int) {
    Sprite *s=sprite_arg(r,1);if(!s)return IllegalArgument;s->user_data=r.calculationStack.back();return nullptr;
}
ErrorCode get_user_data(Executive &,RunTimeEnvironment &r,int) {
    Sprite *s=sprite_arg(r);if(!s)return IllegalArgument;SetReturnValue(r,s->user_data);return nullptr;
}
ErrorCode set_intensity(Executive &,RunTimeEnvironment &r,int) {
    Sprite *s=sprite_arg(r,1);const Value &v=r.calculationStack.back();
    if(!s||s->kind!=Intensity||!v.IsNumeric()||!std::isfinite(number(v))||number(v)<0||number(v)>127)return IllegalArgument;s->data=v;return nullptr;
}
ErrorCode set_scale(Executive &,RunTimeEnvironment &r,int count) {
    Sprite *s=sprite_arg(r,count-1);const Value &v=r.calculationStack.back(count-2);
    if(!s||s->kind!=Scale||!v.IsNumeric()||!std::isfinite(number(v))||number(v)<0||number(v)>255)return IllegalArgument;
    double ratio=0;if(count==3){const Value &value=r.calculationStack.back();if(!value.IsNumeric()||!std::isfinite(number(value)))return IllegalArgument;ratio=number(value);}
    s->data=v;s->ratio=ratio;return nullptr;
}
ErrorCode set_move(Executive &,RunTimeEnvironment &r,int) {
    Sprite *s=sprite_arg(r,2);const Value &x=r.calculationStack.back(1),&y=r.calculationStack.back();
    if(!s||s->kind!=Move||!x.IsNumeric()||!y.IsNumeric())return IllegalArgument;s->data[0]=x;s->data[1]=y;return nullptr;
}
bool three_d(const Sprite *s){return s&&(s->kind==Lines3D||s->kind==Lines25D);}
bool transformable(const Sprite *s){return s&&(s->kind==Lines||s->kind==Dots||three_d(s)||s->kind==VectorTextList);}
bool numeric_array(const Value &v,int rank,int width){
    if(!array(v,rank,width))return false;
    for(size_t i=0;i<v.NumberOfElements();++i)if(!v[int(i)].IsNumeric()||!std::isfinite(number(v[int(i)])))return false;
    return true;
}
ErrorCode translate(Executive &,RunTimeEnvironment &r,int) {
    Sprite *s=sprite_arg(r,1);const Value &v=r.calculationStack.back();
    if(!transformable(s)||!numeric_array(v,1,three_d(s)?3:2))return IllegalArgument;
    s->translation=v;return nullptr;
}
ErrorCode change_angle(RunTimeEnvironment &r,int count,bool relative) {
    Sprite *s=sprite_arg(r,count-1);if(!transformable(s)||count!=(three_d(s)?4:2))return IllegalArgument;
    for(int i=0;i<count-1;++i){const Value &v=r.calculationStack.back(count-i-2);if(!v.IsNumeric()||!std::isfinite(number(v)))return IllegalArgument;}
    for(int i=0;i<count-1;++i){double &angle=three_d(s)?s->angles[i]:s->angle;angle=(relative?angle:0)+number(r.calculationStack.back(count-i-2));angle=fmod(angle,360);if(angle<0)angle+=360;}
    return nullptr;
}
ErrorCode set_angle(Executive &,RunTimeEnvironment &r,int count){return change_angle(r,count,false);}
ErrorCode rotate(Executive &,RunTimeEnvironment &r,int count){return change_angle(r,count,true);}
ErrorCode get_angle(Executive &,RunTimeEnvironment &r,int) {
    Sprite *s=sprite_arg(r);if(!transformable(s))return IllegalArgument;
    if(three_d(s)){uint16_t dim=3;Value v(&dim,1);for(int i=0;i<3;++i)v[i]=float(s->angles[i]);SetReturnValue(r,v);}
    else SetReturnValue(r,Value(s->angle));return nullptr;
}
ErrorCode set_magnification(Executive &,RunTimeEnvironment &r,int count) {
    Sprite *s=sprite_arg(r,count-1);if(!transformable(s)||(count==4&&!three_d(s)))return IllegalArgument;
    double mags[3];for(int i=0;i<3;++i){const Value &v=r.calculationStack.back(std::max(0,count-i-2));if(!v.IsNumeric()||!std::isfinite(number(v)))return IllegalArgument;mags[i]=number(v);}
    s->mag_x=mags[0];s->mag_y=mags[1];s->mag_z=mags[2];return nullptr;
}
ErrorCode get_magnification(Executive &,RunTimeEnvironment &r,int) {
    Sprite *s=sprite_arg(r);if(!transformable(s))return IllegalArgument;
    if(s->mag_x==s->mag_y&&(!three_d(s)||s->mag_y==s->mag_z))SetReturnValue(r,Value(s->mag_x));
    else{uint16_t dim=three_d(s)?3:2;Value v(&dim,1);v[0]=float(s->mag_x);v[1]=float(s->mag_y);if(dim==3)v[2]=float(s->mag_z);SetReturnValue(r,v);}return nullptr;
}
ErrorCode clip_sprite(Executive &,RunTimeEnvironment &r,int){
    Sprite *s=sprite_arg(r,1);const Value &v=r.calculationStack.back();
    if(!transformable(s)||(!v.IsUninitialized()&&(!numeric_array(v,2,2)||v.Dimensions()[0]!=2)))return IllegalArgument;
    s->clipping=v;return nullptr;
}
ErrorCode text_to_lines(Executive &,RunTimeEnvironment &r,int) {
    String text=r.calculationStack.back().ToString();SetReturnValue(r,TextToLines(text.c_str()));return nullptr;
}
ErrorCode vector_text(Executive &,RunTimeEnvironment &r,int) {
    String text=r.calculationStack.back().ToString();return sprite(r,VectorText,TextToLines(text.c_str()));
}
ErrorCode vector_text_list(Executive &,RunTimeEnvironment &r,int){const Value &v=r.calculationStack.back();if(!array(v,2,3))return IllegalArgument;return sprite(r,VectorTextList,v);}
ErrorCode raster_text(Executive &,RunTimeEnvironment &r,int){return sprite(r,RasterText,Value(r.calculationStack.back().ToString()));}
ErrorCode metrics(Executive &,RunTimeEnvironment &r,int) {
    uint16_t dim=5;Value v(&dim,1);const auto &font=vectorFontTable;
    v[0]=font.charWidth*6;v[1]=font.charHeight*6;v[2]=font.advance*6;v[3]=font.lineHeight*6;v[4]=font.ascent*6;
    SetReturnValue(r,v);return nullptr;
}
ErrorCode polygon(Executive &,RunTimeEnvironment &r,int count) {
    const Value &sides=r.calculationStack.back(count-1),&radius=r.calculationStack.back(count-2);
    if(!sides.IsInteger()||sides.Integer()<3||sides.Integer()>65534||!radius.IsNumeric())return IllegalArgument;
    double start=0;if(count==3){const Value &v=r.calculationStack.back();if(!v.IsNumeric())return IllegalArgument;start=number(v)*3.141592653589793/180;}
    uint16_t dims[2]={uint16_t(sides.Integer()+1),3};Value v(dims,2);
    for(int i=0;i<=sides.Integer();++i){double angle=start+i*6.283185307179586/sides.Integer();v[i*3]=i?1:0;v[i*3+1]=float(cos(angle)*number(radius));v[i*3+2]=float(sin(angle)*number(radius));}
    SetReturnValue(r,v);return nullptr;
}
struct Point3 {double x,y,z;};
// Row-vector Tait-Bryan YZX transform from the audited MIT VectrexCommand.cpp.
Point3 rotate_3d(Point3 p,const double angles[3],double sign=1) {
    constexpr double radians=3.141592653589793/180;
    double pitch=angles[0]*radians*sign,roll=angles[1]*radians*sign,yaw=angles[2]*radians*sign;
    double s1=sin(yaw),c1=cos(yaw),s2=sin(roll),c2=cos(roll),s3=sin(pitch),c3=cos(pitch);
    return {p.x*c1*c2+p.y*s2-p.z*c2*s1,
        p.x*(s1*s3-c1*c3*s2)+p.y*c2*c3+p.z*(c1*s3+c3*s1*s2),
        p.x*(c3*s1+c1*s2*s3)-p.y*c2*s3+p.z*(c1*c3-s1*s2*s3)};
}
ErrorCode camera_change_angle(RunTimeEnvironment &r,bool relative) {
    for(int i=0;i<3;++i){const Value &v=r.calculationStack.back(2-i);if(!v.IsNumeric()||!std::isfinite(number(v)))return IllegalArgument;}
    for(int i=0;i<3;++i)active->camera_angles[i]=(relative?active->camera_angles[i]:0)+number(r.calculationStack.back(2-i));return nullptr;
}
ErrorCode camera_set_angle(Executive &,RunTimeEnvironment &r,int){return camera_change_angle(r,false);}
ErrorCode camera_rotate(Executive &,RunTimeEnvironment &r,int){return camera_change_angle(r,true);}
ErrorCode camera_get_angle(Executive &,RunTimeEnvironment &r,int){uint16_t dim=3;Value v(&dim,1);for(int i=0;i<3;++i)v[i]=float(active->camera_angles[i]);SetReturnValue(r,v);return nullptr;}
ErrorCode camera_focal(Executive &,RunTimeEnvironment &r,int){const Value &v=r.calculationStack.back();if(!v.IsNumeric()||!std::isfinite(number(v)))return IllegalArgument;active->focal_length=number(v);return nullptr;}
ErrorCode camera_get_focal(Executive &,RunTimeEnvironment &r,int){SetReturnValue(r,Value(active->focal_length));return nullptr;}
ErrorCode camera_translate(Executive &,RunTimeEnvironment &r,int){const Value &v=r.calculationStack.back();if(!numeric_array(v,1,3))return IllegalArgument;active->camera_translation=v;return nullptr;}
ErrorCode vector_rotate(Executive &,RunTimeEnvironment &r,int count) {
    if(count!=2&&count!=4)return WrongNumberOfArguments;
    int dimension=count==2?2:3;const Value &v=r.calculationStack.back(count-1);
    if(!numeric_array(v,1,dimension))return IllegalArgument;
    double angles[3]={};for(int i=0;i<count-1;++i){const Value &a=r.calculationStack.back(count-i-2);if(!a.IsNumeric()||!std::isfinite(number(a)))return IllegalArgument;angles[i]=number(a);}
    uint16_t dim=uint16_t(dimension);Value result(&dim,1);
    if(dimension==3){Point3 p=rotate_3d({number(v[0]),number(v[1]),number(v[2])},angles);result[0]=float(p.x);result[1]=float(p.y);result[2]=float(p.z);}
    else{double angle=angles[0]*3.141592653589793/180;result[0]=float(number(v[0])*cos(angle)-number(v[1])*sin(angle));result[1]=float(number(v[0])*sin(angle)+number(v[1])*cos(angle));}
    SetReturnValue(r,result);return nullptr;
}
ErrorCode offset(Executive &,RunTimeEnvironment &r,int count) {
    Value &v=r.calculationStack.back(count-1);int dimension=count-1;
    if(dimension==2?(!array(v,2,2)&&!array(v,2,3)):!array(v,2,4))return IllegalArgument;
    double offsets[3]={};for(int i=0;i<dimension;++i){const Value &a=r.calculationStack.back(count-i-2);if(!a.IsNumeric()||!std::isfinite(number(a)))return IllegalArgument;offsets[i]=number(a);}
    int stride=v.Dimensions()[1];
    for(int row=0;row<v.Dimensions()[0];++row){int index=row*stride+stride-dimension;
        for(int i=0;i<dimension;++i){if(v[index+i].IsUninitialized())return nullptr;if(!v[index+i].IsNumeric())return IllegalArgument;}
        for(int i=0;i<dimension;++i)v[index+i]=float(number(v[index+i])+offsets[i]);
    }return nullptr;
}
ErrorCode distance(Executive &,RunTimeEnvironment &r,int) {
    const Value *point=&r.calculationStack.back(),*other=&r.calculationStack.back(1);
    if(!point->IsArray()||!other->IsArray())return IllegalArgument;
    if(point->DimensionsSize()!=1)std::swap(point,other);
    int dim=point->Dimensions()[0];
    if((dim!=2&&dim!=3)||!numeric_array(*point,1,dim)||
       !(numeric_array(*other,1,dim)||(numeric_array(*other,2,dim)&&other->Dimensions()[0]==2)))return IllegalArgument;
    double dot=0,length=0;
    if(other->DimensionsSize()==2)for(int i=0;i<dim;++i){double delta=number((*other)[dim+i])-number((*other)[i]);dot+=(number((*point)[i])-number((*other)[i]))*delta;length+=delta*delta;}
    double ratio=length?std::max(0.0,std::min(1.0,dot/length)):0,sum=0;
    for(int i=0;i<dim;++i){double nearest=number((*other)[i]);if(other->DimensionsSize()==2)nearest+=ratio*(number((*other)[dim+i])-nearest);double delta=number((*point)[i])-nearest;sum+=delta*delta;}
    SetReturnValue(r,Value(sqrt(sum)));return nullptr;
}
ErrorCode in_rect(Executive &,RunTimeEnvironment &r,int count) {
    double data[6];if(count==2){const Value &point=r.calculationStack.back(),&rect=r.calculationStack.back(1);if(!numeric_array(point,1,2)||!numeric_array(rect,2,2)||rect.Dimensions()[0]!=2)return IllegalArgument;for(int i=0;i<4;++i)data[i]=number(rect[i]);for(int i=0;i<2;++i)data[4+i]=number(point[i]);}
    else if(count==6){for(int i=0;i<6;++i){const Value &v=r.calculationStack.back(5-i);if(!v.IsNumeric())return IllegalArgument;data[i]=number(v);}}
    else return WrongNumberOfArguments;
    SetReturnValue(r,Value(int(data[4]>=std::min(data[0],data[2])&&data[4]<=std::max(data[0],data[2])&&data[5]>=std::min(data[1],data[3])&&data[5]<=std::max(data[1],data[3]))));return nullptr;
}
ErrorCode reorder_sprite(RunTimeEnvironment &r,bool after) {
    Sprite *a=sprite_arg(r),*b=sprite_arg(r,1);if(!a||!b)return IllegalArgument;if(a==b)return nullptr;
    Value moved=r.calculationStack.back();auto &list=active->drawing;
    list.erase(std::remove_if(list.begin(),list.end(),[a](const Value &v){return v.Handle()==a;}),list.end());
    auto target=std::find_if(list.begin(),list.end(),[b](const Value &v){return v.Handle()==b;});
    if(target==list.end())target=after?list.end():list.begin();else if(after)++target;
    list.insert(target,moved);return nullptr;
}
ErrorCode put_after(Executive &,RunTimeEnvironment &r,int){return reorder_sprite(r,true);}
ErrorCode put_before(Executive &,RunTimeEnvironment &r,int){return reorder_sprite(r,false);}
ErrorCode abc(Executive &,RunTimeEnvironment &r,int count) {
    uint32_t packed=uint32_t(count-1)<<24;
    for(int i=0;i<count;++i){const Value &v=r.calculationStack.back(count-i-1);if(!v.IsInteger()||v.Integer()<0||v.Integer()>127)return IllegalArgument;packed|=uint32_t(v.Integer())<<(i*8);}
    SetReturnValue(r,Value(int(packed)));return nullptr;
}
ErrorCode music(Executive &,RunTimeEnvironment &r,int) {
    const Value &notes=r.calculationStack.back();
    if(!array(notes,2,2)||!notes.Dimensions()[0])return IllegalArgument;
    for(size_t i=0;i<notes.NumberOfElements();i+=2){
        if(!notes[int(i)].IsInteger()||!notes[int(i)+1].IsInteger())return IllegalArgument;
        uint32_t packed=uint32_t(notes[int(i)].Integer());
        if((packed>>24)>2||notes[int(i)+1].Integer()<0||notes[int(i)+1].Integer()>63)return IllegalArgument;
        for(unsigned shift=0;shift<=8*(packed>>24);shift+=8)if(((packed>>shift)&255)>127)return IllegalArgument;
    }
    unsigned char envelope[16];signed char twang[16];
    if(!load_frequencies()||!read_bios(0xfee8,envelope,16)||!read_bios(0xfeb6,reinterpret_cast<uint8_t *>(twang),16))return "Vectrex BIOS required for music";
    SetReturnValue(r,handle_value<VectrexMusicPlayer>(notes,envelope,twang));return nullptr;
}
ErrorCode explosion(Executive &,RunTimeEnvironment &r,int) {
    int bytes[4];for(int i=0;i<4;++i){const Value &v=r.calculationStack.back(3-i);if(!v.IsInteger())return IllegalArgument;bytes[i]=v.Integer();}
    if(bytes[0]<0||bytes[0]>63||bytes[1]<-128||bytes[1]>255||bytes[2]<-128||bytes[2]>255||bytes[3]<1||bytes[3]>128)return IllegalArgument;
    SetReturnValue(r,handle_value<VectrexMusicPlayer>(bytes[0],bytes[1],bytes[2],bytes[3]));return nullptr;
}
ErrorCode play(Executive &,RunTimeEnvironment &r,int) {
    Value value=r.calculationStack.back();
    if(!active->cb.psg)return NotImplemented;
    VectrexMusicPlayer *player=nullptr;
    if(!value.IsUninitialized()){
        if(!value.IsHandle())return IllegalArgument;
        if(auto *builtin=dynamic_cast<BuiltinMusic *>(value.Handle())) {
            uint8_t header[4],envelope[16];signed char twang[16];std::vector<uint8_t> data;
            if(!load_frequencies()||!read_bios(builtin->address,header,4)||
               !read_bios(uint16_t(header[0]<<8|header[1]),envelope,16)||
               !read_bios(uint16_t(header[2]<<8|header[3]),reinterpret_cast<uint8_t *>(twang),16))return "Vectrex BIOS required for built-in music";
            unsigned address=builtin->address+4;bool finished=false;
            // Decode note groups to find the duration terminator, never scan beyond ROM.
            while(address<65536&&!finished) {
                uint8_t byte;unsigned channels=0;
                do {if(address>=65536||!read_bios(uint16_t(address++),&byte,1)||++channels>3)return "Invalid BIOS music";data.push_back(byte);}while(byte&128);
                if(address>=65536||!read_bios(uint16_t(address++),&byte,1))return "Invalid BIOS music";
                data.push_back(byte);finished=(byte&128)!=0;
            }
            if(!finished)return "Invalid BIOS music";
            VMusic tune(envelope,twang,int(data.size()),data.data());
            value=handle_value<VectrexMusicPlayer>(&tune);
        }
        player=dynamic_cast<VectrexMusicPlayer *>(value.Handle());
        if(!player)return IllegalArgument;
    }
    if(active->music.IsHandle())static_cast<VectrexMusicPlayer *>(active->music.Handle())->stop();
    active->music=value;
    v_noSound();
    if(player)player->setPlaying(true);
    return active->cancelled?StopExecution:nullptr;
}
ErrorCode music_playing(Executive &,RunTimeEnvironment &r,int) {
    bool playing=active->music.IsHandle()&&static_cast<VectrexMusicPlayer *>(active->music.Handle())->isPlaying();
    SetReturnValue(r,Value(int(playing)));return nullptr;
}
bool emit(double x0, double y0, double x1, double y1, uint8_t intensity,uint8_t pattern) {
    const double limit = 2147483647.0;
    if (!std::isfinite(x0) || !std::isfinite(y0) || !std::isfinite(x1) || !std::isfinite(y1) ||
        std::abs(x0) > limit || std::abs(y0) > limit || std::abs(x1) > limit || std::abs(y1) > limit) return false;
    if(active->diagnostic_vectors){if(active->cb.text){char text[192];snprintf(text,sizeof text,"(%ld,%ld) -> (%ld,%ld), intensity %u, pattern %u\n",long(x0),long(y0),long(x1),long(y1),unsigned(intensity),unsigned(pattern));active->cb.text(active->cb.context,text);}return true;}
    ++active->result.lines;
    return !active->cb.line || active->accepted(active->cb.line(active->cb.context,
        int32_t(x0), int32_t(y0), int32_t(x1), int32_t(y1), intensity, pattern));
}
bool clip_line(double &x0,double &y0,double &x1,double &y1,const double rect[4]) {
    double dx=x1-x0,dy=y1-y0,t0=0,t1=1;
    const double p[4]={-dx,dx,-dy,dy},q[4]={x0-rect[0],rect[2]-x0,y0-rect[1],rect[3]-y0};
    for(int i=0;i<4;++i){if(p[i]==0){if(q[i]<0)return false;}else {double ratio=q[i]/p[i];if(p[i]<0)t0=std::max(t0,ratio);else t1=std::min(t1,ratio);if(t0>t1)return false;}}
    x1=x0+t1*dx;y1=y0+t1*dy;x0+=t0*dx;y0+=t0*dy;return true;
}
bool clipping_rect(const Sprite &s,double rect[4]) {
    if(!s.clipping.IsUninitialized()) {
        if(!numeric_array(s.clipping,2,2)||s.clipping.Dimensions()[0]!=2)return false;
        rect[0]=std::min(number(s.clipping[0]),number(s.clipping[2]));rect[2]=std::max(number(s.clipping[0]),number(s.clipping[2]));
        rect[1]=std::min(number(s.clipping[1]),number(s.clipping[3]));rect[3]=std::max(number(s.clipping[1]),number(s.clipping[3]));
    }else{rect[0]=-268;rect[1]=-324;rect[2]=268;rect[3]=324;}return true;
}
bool translation(const Value &v,int dimension,double out[3]) {
    if(v.IsUninitialized()){out[0]=out[1]=out[2]=0;return true;}
    if(!numeric_array(v,1,dimension))return false;
    for(int i=0;i<dimension;++i)out[i]=number(v[i]);return true;
}
Point3 transform_3d(Point3 p,const Sprite &s,const double offset[3],const double camera[3]) {
    p=rotate_3d({p.x*s.mag_x,p.y*s.mag_y,p.z*s.mag_z},s.angles);
    return rotate_3d({p.x+offset[0]-camera[0],p.y+offset[1]-camera[1],p.z+offset[2]-camera[2]},active->camera_angles,-1);
}
Point3 project(Point3 p) {
    double factor=(active->focal_length/50)*0.5*active->world_units/(0.5+p.z);
    return {p.x*factor,p.y*factor,0};
}
ErrorCode draw_path(const Sprite &s,const Value &v,double scale,double &x,double &y,uint8_t intensity) {
    int dimension=s.kind==Lines3D?3:2,stride=v.Dimensions()[1];
    double ox=x,oy=y,offset[3]={},camera[3]={},rect[4];
    if(!translation(s.translation,three_d(&s)?3:2,offset)||!clipping_rect(s,rect)||!translation(active->camera_translation,3,camera))return IllegalArgument;
    Point3 previous{0,0,0};double previous_x=0,previous_y=0;
    double angle=s.angle*3.141592653589793/180,cosine=cos(angle),sine=sin(angle);
    double billboard_x=0,billboard_y=0,billboard_scale=1;
    if(s.kind==Lines25D){Point3 a=transform_3d({0,0,0},s,offset,camera),b=transform_3d({0,1,0},s,offset,camera);if(a.z<0&&b.z<0)return nullptr;
        if(a.z<0||b.z<0){double t=a.z/(a.z-b.z);Point3 crossing{a.x+(b.x-a.x)*t,a.y+(b.y-a.y)*t,0};if(a.z<0)a=crossing;else b=crossing;}
        a=project(a);b=project(b);
        for(double value:{a.x,a.y,b.y-a.y})if(!std::isfinite(value)||value<INT32_MIN||value>INT32_MAX)return IllegalArgument;
        billboard_x=int(a.x);billboard_y=int(a.y);billboard_scale=int(b.y-a.y);
    }
    for(size_t i=0;i<v.NumberOfElements();i+=stride){
        if(!render_progress())return StopExecution;
        if(v[int(i)].IsUninitialized())break;
        for(int j=0;j<stride;++j)if(!v[int(i)+j].IsNumeric()||!std::isfinite(number(v[int(i)+j])))return IllegalArgument;
        if(stride!=dimension&&(double(number(v[int(i)]))<INT32_MIN||double(number(v[int(i)]))>INT32_MAX))return IllegalArgument;
        int mode=s.kind==Dots?1:(stride==dimension?(i?1:0):int(number(v[int(i)])));
        int coord=int(i)+stride-dimension;
        double nx,ny,ax=previous_x,ay=previous_y;
        bool draw=mode!=0;
        if(s.kind==Lines3D){
            Point3 point=transform_3d({number(v[coord]),number(v[coord+1]),number(v[coord+2])},s,offset,camera);
            Point3 a=previous,b=point;previous=point;
            if(a.z<0&&b.z<0)draw=false;
            else if(a.z<0||b.z<0){double t=a.z/(a.z-b.z);Point3 crossing{a.x+(b.x-a.x)*t,a.y+(b.y-a.y)*t,0};if(a.z<0)a=crossing;else b=crossing;}
            if(!draw&&point.z<0){previous_x=previous_y=0;continue;}
            a=project(a);b=project(b);ax=a.x;ay=a.y;nx=b.x;ny=b.y;
        }else if(s.kind==Lines25D){nx=billboard_x+number(v[coord])*billboard_scale;ny=billboard_y+number(v[coord+1])*billboard_scale;}
        else {double px=number(v[coord])*s.mag_x,py=number(v[coord+1])*s.mag_y;nx=px*cosine-py*sine+offset[0];ny=px*sine+py*cosine+offset[1];}
        previous_x=nx;previous_y=ny;
        double bx=nx,by=ny;if(s.kind==Dots){ax=nx;ay=ny;}
        if(draw&&(three_d(&s)||!s.clipping.IsUninitialized()))draw=clip_line(ax,ay,bx,by,rect);
        if(draw&&!emit(ox+ax*scale,oy+ay*scale,ox+bx*scale,oy+by*scale,intensity,mode==1?255:uint8_t(mode)))return active->cancelled?StopExecution:IllegalArgument;
        x=ox+nx*scale;y=oy+ny*scale;
    }return nullptr;
}
ErrorCode render() {
    double x = 0, y = 0, scale = 129, width = 40, height = 5;
    uint8_t intensity = 127;
    active->compiled_sprites=0;
    active->code_budget=1500000/active->hz;
    if(active->lowlevel)mt_v32_lowlevel_begin_frame(active->lowlevel);
    if (active->cb.begin_frame && !active->accepted(active->cb.begin_frame(active->cb.context))) return StopExecution;
    for (const Value &handle : active->drawing) {
        if(!render_progress())return StopExecution;
        const Sprite &s = *static_cast<Sprite *>(handle.Handle());
        if(!s.enabled)continue;
        const Value &v = s.data;
        switch (s.kind) {
        case Code: {
            if(!machine_bytes(v)||!std::isfinite(x)||!std::isfinite(y)||x<INT32_MIN||x>INT32_MAX||y<INT32_MIN||y>INT32_MAX)return IllegalArgument;
            std::vector<uint8_t> bytes(v.NumberOfElements());for(size_t i=0;i<bytes.size();++i)bytes[i]=uint8_t(v[int(i)].Integer());
            mt_v32_lowlevel_pen pen{int32_t(x),int32_t(y),uint8_t(scale-2),intensity,255};mt_v32_lowlevel_set_pen(active->lowlevel,&pen);
            uint64_t before=mt_v32_lowlevel_cycles(active->lowlevel);
            ErrorCode error=lowlevel_error(mt_v32_lowlevel_execute(active->lowlevel,bytes.data(),bytes.size(),active->code_budget));
            uint64_t spent=mt_v32_lowlevel_cycles(active->lowlevel)-before;active->code_budget=spent<active->code_budget?active->code_budget-spent:0;
            if(error)return error;mt_v32_lowlevel_get_pen(active->lowlevel,&pen);x=pen.x;y=pen.y;scale=pen.scale+2;intensity=pen.intensity;break;
        }
        case Origin: x = y = 0; break;
        case Scale: scale = int(number(v)) + 2;if(s.ratio)active->world_units=s.ratio;break;
        case Intensity: intensity = uint8_t(number(v)); break;
        case Move: x += number(v[0]) * scale; y += number(v[1]) * scale; break;
        case TextSize: if(!numeric_array(v,1,2))return IllegalArgument;width = number(v[0]); height = number(v[1]); break;
        case Lines: case Dots: case Lines3D: case Lines25D: case VectorText: {
            ErrorCode error=draw_path(s,v,s.kind==VectorText?17:scale,x,y,intensity);if(error)return error;
            if(s.kind==Dots)x=y=0;break;
        }
        case VectorTextList: {
            for(size_t i=0;i<v.NumberOfElements();i+=3){if(v[int(i)].IsUninitialized())break;
                if(!render_progress())return StopExecution;
                if(!v[int(i)].IsNumeric()||!v[int(i)+1].IsNumeric())return IllegalArgument;
                x=number(v[int(i)])*129;y=number(v[int(i)+1])*129;
                Value lines=TextToLines(v[int(i)+2].ToString().c_str());ErrorCode error=draw_path(s,lines,17,x,y,intensity);if(error)return error;
            }break;
        }
        case Text: case RasterText: {
            if(!active->raster_checked){active->raster_checked=true;active->raster_available=read_bios(0xf9f4,active->raster_font,sizeof active->raster_font);}
            if(!active->raster_available)active->result.raster_font_fallback=1;
            for (size_t i = 0; i < (s.kind==Text?v.NumberOfElements():3); i += 3) {
                if(!render_progress())return StopExecution;
                double tx=x,ty=y;String text;
                if(s.kind==Text){
                if (v[int(i)].IsUninitialized()) break;
                if (!v[int(i)].IsNumeric() || !v[int(i)+1].IsNumeric()) return IllegalArgument;
                tx=number(v[int(i)])*128;ty=number(v[int(i)+1])*128;text=v[int(i)+2].ToString();
                }else text=v.ToString();
                for (const char *c = text.c_str(); *c; ++c) {
                    if(!render_progress())return StopExecution;
                    unsigned char ch = (unsigned char)*c;
                    if(active->raster_available){
                        if(ch<0x20||ch>0x6f)ch='?';
                        // BIOS rows are 80 bytes apart. Shift bits occupy two CPU
                        // cycles; the character loop advances in 18 cycles.
                        for(unsigned row=0;row<7;++row){uint8_t bits=active->raster_font[row*80+ch-0x20];
                            for(unsigned bit=0;bit<8;){if(!(bits&(0x80u>>bit))){++bit;continue;}unsigned start=bit;while(bit<8&&(bits&(0x80u>>bit)))++bit;
                                double ry=ty-double(row)*16*height;
                                if(!emit(tx+start*2*width,ry,tx+bit*2*width,ry,intensity))return active->cancelled?StopExecution:IllegalArgument;
                            }
                        }
                        tx+=18*width;continue;
                    }
                    if (ch > 'c' && ch >= 'a' && ch <= 'z') ch -= 'a' - 'A';
                    if (ch < vectorFontTable.firstChar || ch > vectorFontTable.lastChar) ch = '?';
                    const CharacterDefinition &glyph = vectorFontTable.characterDefinitions[ch-vectorFontTable.firstChar];
                    double px = tx, py = ty;
                    for (int j = 0; j < glyph.linesCount; ++j) {
                        const CharacterLine &line = glyph.lines[j];
                        double nx = tx + line.x * width * 102 / 40;
                        double ny = ty + line.y * height * 102 / 5;
                        if (line.mode && !emit(px, py, nx, ny, intensity)) return active->cancelled ? StopExecution : IllegalArgument;
                        px = nx; py = ny;
                    }
                    tx += vectorFontTable.advance * width * 102 / 40;
                }
            }
            x = y = 0;
            break;
        }
        }
        ++active->compiled_sprites;
    }
    return nullptr;
}
ErrorCode frame(Executive &, RunTimeEnvironment &r, int count) {
    int args[4] = {0,0,0,0};
    for (int i = 0; i < count; ++i) {
        const Value &v = r.calculationStack.back(count-i-1);
        if (!v.IsInteger()) return IllegalArgument;
        args[i] = v.Integer();
    }
    if (args[0] < 0 || args[0] > 2 || args[1] < 0 || args[1] > 3 || args[2] < 0 || args[2] > 3) return IllegalArgument;
    if(args[3]<0||args[3]>128||(args[3]&&(args[3]&(args[3]-1))))return IllegalArgument;
    ErrorCode error = render(); if (error) return error;
    for(auto &request:active->peeks){uint8_t bytes[16];error=lowlevel_error(mt_v32_lowlevel_read_memory(active->lowlevel,request.address,bytes,request.count));if(error)return error;
        uint16_t dim=uint16_t(request.count);Value values(&dim,1);for(unsigned i=0;i<request.count;++i)values[int(i)]=int(bytes[i]);request.array[1]=values;request.array[0]=1;
    }
    active->peeks.clear();
    if(active->music.IsHandle())static_cast<VectrexMusicPlayer *>(active->music.Handle())->play();
    if(!flush_psg())return StopExecution;
    if(active->cancelled)return StopExecution;
    int controls[2][6] = {};
    ++active->result.frames;
    active->frame_environment = &r;
    bool keep_running = !active->cb.frame || active->accepted(active->cb.frame(active->cb.context, active->hz, controls));
    active->frame_environment = nullptr;
    if (!keep_running) return StopExecution;
    if(active->lowlevel)mt_v32_lowlevel_set_controls(active->lowlevel,controls);
    uint16_t dims[2] = {2,6}; Value values(dims, 2);
    for (int controller = 0; controller < 2; ++controller) for (int field = 0; field < 6; ++field) {
        int input = controls[controller][field];
        if (field < 2) {
            if (!args[0] || !(args[1] & (1<<controller)) || !(args[2] & (1<<field))) input = 0;
            else if (args[0] == 1) input = input>=64?64:input<=-64?-64:0;
            else {input=std::max(-128,std::min(127,input));int step=args[3]?std::min(256,args[3]*2):1;input=((input+128)/step)*step-128;}
        } else input = input != 0;
        values[controller*6+field] = input;
    }
    SetReturnValue(r, values); return nullptr;
}
ErrorCode open_file(Executive &, RunTimeEnvironment &r, int) {
    const Value &name = r.calculationStack.back(1), &mode = r.calculationStack.back();
    if (!name.IsString() || !mode.IsString() ||
        !mt_v32_valid_relative_path(name.String().c_str())) return IllegalArgument;
    static const char *modes[]={"r","rb","r+","r+b","rb+","w","wb","w+","w+b","wb+","a","ab","a+","a+b","ab+"};
    bool valid=false;for(auto candidate:modes)if(!strcmp(mode.String().c_str(),candidate))valid=true;
    if(!valid)return IllegalArgument;
    if(active->cb.file_open) {
        if(!active->cb.file_close)return NotImplemented;
        void *handle=active->cb.file_open(active->cb.context,name.String().c_str(),mode.String().c_str());
        SetReturnValue(r,file_value(handle,true));return nullptr;
    }
    if(strcmp(mode.String().c_str(),"r")&&strcmp(mode.String().c_str(),"rb"))return NotImplemented;
    if (!active->cb.open_read || !active->cb.read_byte || !active->cb.close) return CantOpenFile;
    void *handle = active->cb.open_read(active->cb.context, name.String().c_str());
    SetReturnValue(r,file_value(handle,false)); return nullptr;
}
File *file_arg(RunTimeEnvironment &r,int depth=0) {
    Value &v = r.calculationStack.back(depth);
    return v.IsHandle() ? dynamic_cast<File *>(v.Handle()) : nullptr;
}
ErrorCode get_line(Executive &, RunTimeEnvironment &r, int) {
    File *file = file_arg(r); if (!file || !file->handle) return IllegalArgument;
    std::string line;
    int c;
    // Match upstream fgets: retain newline and return at most 255 bytes.
    c = -1;
    while (line.size() < 255 && (c = file->read()) >= 0) {
        line.push_back(char(c));
        if (c == '\n') break;
    }
    if (c < -1) return active->cancelled?StopExecution:"Asset read failed";
    SetReturnValue(r, Value(line.c_str())); return nullptr;
}
ErrorCode close_file(Executive &, RunTimeEnvironment &r, int) {
    File *file = file_arg(r); if (!file || !file->handle) return IllegalArgument;
    SetReturnValue(r,Value(file->close())); return nullptr;
}
ErrorCode get_char(Executive &,RunTimeEnvironment &r,int) {
    File *file=file_arg(r);if(!file||!file->handle)return IllegalArgument;
    int value=file->read();if(value<-1)return active->cancelled?StopExecution:"Asset read failed";
    SetReturnValue(r,Value(value));return nullptr;
}
ErrorCode eof_file(Executive &,RunTimeEnvironment &r,int) {
    File *file=file_arg(r);if(!file||!file->handle)return IllegalArgument;
    int eof=file->modern&&active->cb.file_eof?active->cb.file_eof(active->cb.context,file->handle):int(file->eof);
    if(eof<0)return "Asset read failed";SetReturnValue(r,Value(eof!=0));return nullptr;
}
ErrorCode read_file(Executive &,RunTimeEnvironment &r,int) {
    File *file=file_arg(r);const Value &size=r.calculationStack.back(1);
    if(!file||!file->handle||!size.IsInteger()||size.Integer()<0||size.Integer()>UINT16_MAX)return IllegalArgument;
    String data('\0',size.Integer());size_t received=0;
    if(file->modern) {
        if(!active->cb.file_read)return NotImplemented;
        if(active->cb.file_read(active->cb.context,file->handle,data.buffer(),size.Integer(),&received)||received>size_t(size.Integer()))return "Asset read failed";
    } else while(received<size_t(size.Integer())){int c=file->read();if(c<-1)return active->cancelled?StopExecution:"Asset read failed";if(c<0)break;data.buffer()[received++]=char(c);}
    String result('\0',int(received));if(received)memcpy(result.buffer(),data.c_str(),received);
    SetReturnValue(r,Value(result));return nullptr;
}
ErrorCode seek_file(Executive &,RunTimeEnvironment &r,int) {
    File *file=file_arg(r,2);const Value &offset=r.calculationStack.back(1),&origin=r.calculationStack.back();
    if(!file||!file->handle||!offset.IsInteger()||!origin.IsInteger()||origin.Integer()<0||origin.Integer()>2)return IllegalArgument;
    if(!file->modern||!active->cb.file_seek)return NotImplemented;
    int status=active->cb.file_seek(active->cb.context,file->handle,offset.Integer(),origin.Integer());
    if(!status)file->eof=false;SetReturnValue(r,Value(status));return nullptr;
}
ErrorCode tell_file(Executive &,RunTimeEnvironment &r,int) {
    File *file=file_arg(r);if(!file||!file->handle)return IllegalArgument;
    if(file->modern&&!active->cb.file_tell)return NotImplemented;
    int64_t offset=file->modern?active->cb.file_tell(active->cb.context,file->handle):file->position;
    if(offset>INT32_MAX)return "File offset exceeds BASIC integer range";
    SetReturnValue(r,Value(int(offset)));return nullptr;
}
ErrorCode write_file(Executive &,RunTimeEnvironment &r,int count) {
    File *file=file_arg(r);const Value &data=r.calculationStack.back(count-1);
    if(!file||!file->handle||!data.IsString())return IllegalArgument;
    size_t length=data.String().length();
    if(count==3){const Value &size=r.calculationStack.back(1);if(!size.IsInteger()||size.Integer()<0||size_t(size.Integer())>length)return IllegalArgument;length=size.Integer();}
    if(file->terminal){
        if(file->terminal!=2||!active->cb.console_write)return "Console output callback required";
        int status=active->cb.console_write(active->cb.context,data.String().c_str(),length);
        if(status==-2){active->cancelled=true;return StopExecution;}
        if(status)return "Console write failed";
        SetReturnValue(r,Value(count==3?int(length):0));return nullptr;
    }
    if(!file->modern||!active->cb.file_write)return NotImplemented;
    size_t written=0;int status=active->cb.file_write(active->cb.context,file->handle,data.String().c_str(),length,&written);
    if(written>length)return "Invalid file callback count";
    if(count==3&&status&&written==length)return "Asset write or flush failed";
    SetReturnValue(r,Value(count==3?int(written):(status||written!=length?-1:0)));return nullptr;
}
ErrorCode remove_file(Executive &,RunTimeEnvironment &r,int) {
    const Value &name=r.calculationStack.back();if(!name.IsString()||!mt_v32_valid_relative_path(name.String().c_str()))return IllegalArgument;
    if(!active->cb.file_remove)return NotImplemented;
    SetReturnValue(r,Value(active->cb.file_remove(active->cb.context,name.String().c_str())));return nullptr;
}
ErrorCode rename_file(Executive &,RunTimeEnvironment &r,int) {
    const Value &old_name=r.calculationStack.back(1),&new_name=r.calculationStack.back();
    if(!old_name.IsString()||!new_name.IsString()||!mt_v32_valid_relative_path(old_name.String().c_str())||!mt_v32_valid_relative_path(new_name.String().c_str()))return IllegalArgument;
    if(!active->cb.file_rename)return NotImplemented;
    SetReturnValue(r,Value(~active->cb.file_rename(active->cb.context,old_name.String().c_str(),new_name.String().c_str())));return nullptr;
}
ErrorCode sound(Executive &, RunTimeEnvironment &r, int) {
    const Value &v = r.calculationStack.back();
    if (!v.IsArray() || !(array(v,1,14) || array(v,2,2))) return IllegalArgument;
    if (!active->cb.psg) return NotImplemented;
    int count = v.Dimensions()[0];
    for (int i = 0; i < count; ++i) {
        const Value &value = v[v.DimensionsSize() == 1 ? i : i*2+1];
        if (!value.IsInteger()) return IllegalArgument;
        int reg = i;
        if (v.DimensionsSize() == 2) { if (!v[i*2].IsInteger()) return IllegalArgument; reg = v[i*2].Integer(); }
        if (reg < 0 || reg > 13) return IllegalArgument;
        int byte = value.Integer(); if (reg == 7) byte &= 63;
        v_writePSG_double_buffered(unsigned(reg),unsigned(byte));
    }
    return nullptr;
}
void describe_sprite(const Sprite &s){
    if(!active->cb.text)return;
    static const char *names[]={"Origin","Move","Scale","Intensity","Lines","Dots","TextList","TextSize","Lines3D","Lines25D","Text2","Text2List","Text","Code"};
    char text[256];snprintf(text,sizeof text,"%s: %s, %lu data elements; magnification=(%.6g,%.6g,%.6g), rotation=(%.6g,%.6g,%.6g)\n",names[s.kind],s.enabled?"enabled":"disabled",static_cast<unsigned long>(s.data.IsArray()?s.data.NumberOfElements():0),s.mag_x,s.mag_y,s.mag_z,three_d(&s)?s.angles[0]:s.angle,s.angles[1],s.angles[2]);
    active->cb.text(active->cb.context,text);
}
ErrorCode dump_sprite(Executive &,RunTimeEnvironment &r,int){Sprite *s=sprite_arg(r);if(!s)return IllegalArgument;describe_sprite(*s);return nullptr;}
ErrorCode dump_sprites(Executive &,RunTimeEnvironment &,int){for(const Value &v:active->drawing)describe_sprite(*static_cast<Sprite *>(v.Handle()));return nullptr;}
ErrorCode print_vectors(Executive &,RunTimeEnvironment &r,int){
    Sprite *s=sprite_arg(r);if(!transformable(s))return IllegalArgument;
    struct DiagnosticGuard{DiagnosticGuard(){active->diagnostic_vectors=true;}~DiagnosticGuard(){active->diagnostic_vectors=false;}} guard;
    double x=0,y=0;ErrorCode error=nullptr;
    if(s->kind==VectorTextList){for(size_t i=0;i<s->data.NumberOfElements();i+=3){if(s->data[int(i)].IsUninitialized())break;
        if(!s->data[int(i)].IsNumeric()||!s->data[int(i)+1].IsNumeric())return IllegalArgument;
        x=number(s->data[int(i)])*129;y=number(s->data[int(i)+1])*129;Value lines=TextToLines(s->data[int(i)+2].ToString().c_str());error=draw_path(*s,lines,17,x,y,127);if(error)break;
    }}else error=draw_path(*s,s->data,1,x,y,127);
    return error;
}
ErrorCode break_control_c(Executive &,RunTimeEnvironment &r,int){const Value &v=r.calculationStack.back();if(!v.IsInteger())return IllegalArgument;active->break_on_control_c=v.Integer()!=0;return nullptr;}
ErrorCode version(Executive &,RunTimeEnvironment &r,int){SetReturnValue(r,Value(127));return nullptr;}
const BuiltInFunctionInfo functions[] = {
    {"dumpsprite",dump_sprite,1,0},{"dumpsprites",dump_sprites,0,0},{"spriteprintvectors",print_vectors,1,0},
    {"breakoncontrolc",break_control_c,1,0},{"version",version,0,0},
    {"cameragetfocallength",camera_get_focal,0,0},{"cameragetrotation",camera_get_angle,0,0},
    {"camerarotate",camera_rotate,3,0},{"camerasetfocallength",camera_focal,1,0},
    {"camerasetrotation",camera_set_angle,3,0},{"cameratranslate",camera_translate,1,0},
    {"distance",distance,2,0},{"lines25dsprite",lines_25d,1},{"lines3dsprite",lines_3d,1},
    {"offset",offset,3,4},{"ptinrect",in_rect,2,6},{"putspriteafter",put_after,2,0},{"putspritebefore",put_before,2,0},
    {"spriteclip",clip_sprite,2,0},{"text2listsprite",vector_text_list,1,0},{"text2sprite",vector_text,1,0},
    {"textsprite",raster_text,1,0},{"vectorrotate",vector_rotate,2,4},
    {"gettickcount",get_tick,0,0},{"getcompiledspritecount",compiled_count,0,0},
    {"codesprite",code_sprite,1,0},{"peek",peek,1,3},
    {"abc",abc,1,3}, {"clearscreen",clear,0,0}, {"dotssprite",dots,1,0}, {"drawinglistsprite",drawing_sprite,1,0},
    {"explosion",explosion,4,0}, {"fclose",close_file,1,0}, {"feof",eof_file,1,0},{"fgetc",get_char,1,0},
    {"fgets",get_line,1,0}, {"fopen",open_file,2,0}, {"fputs",write_file,2,0},{"fread",read_file,2,0},
    {"fseek",seek_file,3,0},{"ftell",tell_file,1,0},{"fwrite",write_file,3,0},{"getfontmetrics",metrics,0,0}, {"getframerate",get_hz,0,0},
    {"intensitysprite",intensity,1,0}, {"linessprite",lines,1,0}, {"movesprite",move,2,0},
    {"music",music,1,0}, {"musicisplaying",music_playing,0,0}, {"play",play,1,0},
    {"regularpolygon",polygon,2,3}, {"remove",remove_file,1,0},{"removesprite",remove_sprite,1,0}, {"rename",rename_file,2,0},{"returntooriginsprite",origin,0,0},
    {"scalesprite",scale,1,2}, {"setframerate",set_hz,1,0}, {"sound",sound,1,0},
    {"spriteenable",enable_sprite,2,0}, {"spritegetmagnification",get_magnification,1,0}, {"spritegetrotation",get_angle,1,0},
    {"spritegetuserdata",get_user_data,1,0}, {"spriteintensity",set_intensity,2,0}, {"spritemove",set_move,3,0},
    {"spriterotate",rotate,2,4}, {"spritescale",set_scale,2,3}, {"spritesetdata",set_data,2,0},
    {"spritesetmagnification",set_magnification,2,4}, {"spritesetrotation",set_angle,2,4},
    {"spritesetuserdata",set_user_data,2,0}, {"spritetranslate",translate,2,0},
    {"textlistsprite",text_list,1,0}, {"textsizesprite",text_size,1,0}, {"texttolines",text_to_lines,1,0},
    {"waitforframe",frame,3,4}
};
const BuiltInConstantInfo constants[] = {
    {"controller1",Value(1)}, {"controller2",Value(2)}, {"controllernone",Value(0)},
    {"drawto",Value(1)}, {"joystickanalog",Value(2)}, {"joystickdigital",Value(1)},
    {"joysticknone",Value(0)}, {"joystickx",Value(1)}, {"joysticky",Value(2)}, {"moveto",Value(0)}
};
struct Notifications : GSBASICNotifications {
    std::vector<std::string> constant_names;
    std::vector<BuiltInConstantInfo> registered_constants;
    std::vector<BuiltInFunctionInfo> registered_functions;
    void RegisterConstants() override {
        registered_constants.assign(std::begin(constants),std::end(constants));
        constant_names.reserve(64);
        static const char *pitch[]={"C","CS","D","DS","E","F","FS","G","GS","A","AS","B"};
        for(int note=0;note<64;++note){
            int midi=43+note;
            constant_names.push_back(std::string("N")+pitch[midi%12]+std::to_string(midi/12-1));
            registered_constants.push_back({constant_names.back().c_str(),Value(note)});
        }
        registered_constants.push_back({"NOctave",Value(12)});
        struct Tune {const char *name;uint16_t address;};
        const Tune tunes[]={ {"Berzerk",0xfd1d},{"MelodyMaster",0xfef8},
            {"Music1",0xfd81},{"Music2",0xfe76},{"Music3",0xfec6},
            {"Music4",0xff26},{"Music5",0xff44},{"Music6",0xff62},
            {"Music7",0xff7a},{"Music8",0xff8f},{"PowerOn",0xfd0d},
            {"Scramble",0xfdd3},{"SolarQuest",0xfe38} };
        for(const Tune &tune:tunes)registered_constants.push_back({tune.name,handle_value<BuiltinMusic>(tune.address)});
        registered_constants.push_back({"Stdin",handle_value<File>(1u)});
        registered_constants.push_back({"Stdout",handle_value<File>(2u)});
        registered_constants.push_back({"SEEK_CUR",Value(1)});
        registered_constants.push_back({"SEEK_END",Value(2)});
        registered_constants.push_back({"SEEK_SET",Value(0)});
        std::sort(registered_constants.begin(),registered_constants.end(),[](const BuiltInConstantInfo &a,const BuiltInConstantInfo &b){return strcasecmp(a.name,b.name)<0;});
        RegisterBuiltInConstants(registered_constants.data(),int(registered_constants.size()));
        registered_functions.assign(std::begin(functions),std::end(functions));
        std::sort(registered_functions.begin(),registered_functions.end(),[](const BuiltInFunctionInfo &a,const BuiltInFunctionInfo &b){return strcasecmp(a.name,b.name)<0;});
        RegisterBuiltInFunctions(registered_functions.data(),int(registered_functions.size()));
    }
    void RuntimeReset() override {
        active->drawing.clear();active->music.Clear();active->hz=30;
        active->camera_translation.Clear();for(double &angle:active->camera_angles)angle=0;
        active->focal_length=50;active->world_units=324.0/0.097;
        active->peeks.clear();if(active->lowlevel)mt_v32_lowlevel_reset(active->lowlevel);
    }
};
}
ErrorCode mt_v32_read_source(const char *name,std::string &source,bool extension) {
    if(!active||!mt_v32_valid_relative_path(name))return IllegalArgument;
    bool modern=active->cb.file_open!=nullptr;
    if(modern?(!active->cb.file_read||!active->cb.file_close):(!active->cb.open_read||!active->cb.read_byte||!active->cb.close))return CantOpenFile;
    auto open=[&](const char *path){return modern?active->cb.file_open(active->cb.context,path,"rb"):active->cb.open_read(active->cb.context,path);};
    void *handle=open(name);
    if(!handle&&extension){std::string with_extension=std::string(name)+".bas";handle=open(with_extension.c_str());}
    if(!handle)return CantOpenFile;
    File file(handle,modern);source.clear();
    for(;;){int c=file.read();if(c<-1)return "Source read failed";if(c<0)break;if(!c)return "NUL byte in BASIC source";
        if(source.size()==4*1024*1024)return "BASIC source exceeds 4 MiB";
        source.push_back(char(c));
        if(!(source.size()&4095)&&active->cb.service&&!active->accepted(active->cb.service(active->cb.context)))return StopExecution;
    }
    return file.close()?"Source close failed":nullptr;
}
extern "C" {
int usbKeyboardAvailable = 0, disableSprites = 0, vectrexMode = 1;
int T_WIDTH=0,T_HEIGHT=0,MAX_VLINES=0,Y_START=0,MAX_VROWS=0;
void printToLine(char *text, int) { if (active && active->cb.text) active->cb.text(active->cb.context, text); }
void printToLineC(char c,int wr) { char text[2] = {c,0}; printToLine(text,wr); }
void showVectrexOut() {}
void clearVectrexOut() {}
int printBackspace() { return 0; }
void v_WaitRecal() {}
void v_message(const char *s) { printToLine(const_cast<char *>(s),1); }
void v_writePSG_double_buffered(unsigned reg,unsigned value) {
    if(!active||reg>=14)return;
    if(reg==7)value&=63;
    active->psg[reg]=uint8_t(value);
    active->pending_psg|=uint16_t(1u<<reg);
}
unsigned mt_v32_note_frequency(unsigned note) {return active&&note<64?active->frequencies[note]:0;}
unsigned v_readPSG_double_buffered(unsigned reg) {return active&&reg<14?active->psg[reg]:0;}
void v_noSound() {for(unsigned reg=8;reg<=10;++reg)v_writePSG_double_buffered(reg,0);v_writePSG_double_buffered(7,63);}
void ClearVectrex() { if (active) active->drawing.clear(); }
unsigned char USPiKeyboardLastKeyDown() { return 0; }
unsigned RandomSeed() {
    if(active&&active->cb.random_seed)return active->cb.random_seed(active->cb.context);
    if(active&&active->cb.monotonic_milliseconds){uint64_t time=active->cb.monotonic_milliseconds(active->cb.context);return uint32_t(time)^uint32_t(time>>32);}
    return 1;
}
void OutOfMemoryHandler() { throw std::bad_alloc(); }
int mt_v32_poll_statement(unsigned line) {
    if (!active) return 0;
    active->source_line = line;
    ++active->result.statements;
    if (!(active->result.statements & 255) && active->cb.service)
        active->accepted(active->cb.service(active->cb.context));
    return !active->cancelled;
}
unsigned mt_v32_debug_line() { return active && active->frame_environment ? active->source_line : 0; }
int mt_v32_debug_number(const char *name,int index,double *result) {
    if (!active || !active->frame_environment || !name || !result) return 0;
    RunTimeEnvironment &r = *active->frame_environment;
    VariableHandle handle;
    if (r.compileEnvironment.FindOrAllocateVariable(String(name),true,false,&handle)) return 0;
    const Value *value = &r.GetValue(handle);
    if (index >= 0) {
        if (!value->IsArray() || size_t(index) >= value->NumberOfElements()) return 0;
        value = &(*value)[index];
    }
    if (!value->IsNumeric()) return 0;
    *result = value->IsInteger() ? double(value->Integer()) : double(value->Float());
    return 1;
}
int mt_v32_valid_relative_path(const char *name) {
    if (!name || !*name || *name == '/' || *name == '\\') return 0;
    const char *part = name;
    for (const char *c = name;; ++c) {
        if (*c == ':' || *c=='*' || *c=='?' || ((unsigned char)*c < 32 && *c)) return 0;
        if (!*c || *c == '/' || *c == '\\') {
            if (c == part || c[-1]=='.' || c[-1]==' ') return 0;
            part = c+1;
            if (!*c) return 1;
        }
    }
}
int mt_v32_run(const char *source,const char *name,const mt_v32_callbacks *cb,mt_v32_result *result) {
    if (!source || !name || !cb || !result || active) return 1;
    *result = {};
    Runtime runtime{*cb,*result}; active = &runtime;
    try {
        Notifications notifications;
        Executive executive(&notifications,false,'\n');
        Error error = executive.LoadSource(String(name),source);
        while(!error) {
            error=executive.Run();
            if(runtime.cancelled||(error&&error.Message()!=StopExecution))break;
            String next=executive.TakeChain();if(next.empty())break;
            std::string content;ErrorCode read_error=mt_v32_read_source(next.c_str(),content,true);
            if(read_error){error=Error(read_error,0,0);break;}
            error=executive.LoadSource(next,content.c_str());
        }
        if (error && error.Message() != StopExecution) {
            result->error_line = error.LineNumber();
            snprintf(result->error,sizeof result->error,"%s",error.Message());
        }
        result->stopped = runtime.cancelled;
    } catch (const std::exception &e) { result->error_line=runtime.source_line;snprintf(result->error,sizeof result->error,"%s",e.what()); }
    runtime.drawing.clear();runtime.music.Clear();runtime.camera_translation.Clear();runtime.peeks.clear();
    mt_v32_lowlevel_destroy(runtime.lowlevel);active=nullptr;
    return result->error[0] ? 1 : 0;
}
}
bool ctrlC = false;
