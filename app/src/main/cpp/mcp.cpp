#include "mcp.h"
#include "driver.h"
#include "symbol.h"
#include "protocol.h"
#include <unistd.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cinttypes>
#include <string>
#include <vector>
#include <map>
#include <memory>

// ====================== 最小 JSON 实现 ======================
namespace js {
struct Value;
using ValuePtr = std::shared_ptr<Value>;
enum Type { NUL, BOOL, NUM, STR, ARR, OBJ };
struct Value {
    Type t=NUL;
    bool b=false;
    double num=0;
    std::string str;
    std::vector<ValuePtr> arr;
    std::vector<std::pair<std::string,ValuePtr>> obj;
    static ValuePtr mknull(){ auto v=std::make_shared<Value>(); v->t=NUL; return v; }
    static ValuePtr mkbool(bool x){ auto v=std::make_shared<Value>(); v->t=BOOL; v->b=x; return v; }
    static ValuePtr mknum(double x){ auto v=std::make_shared<Value>(); v->t=NUM; v->num=x; return v; }
    static ValuePtr mkstr(const std::string& x){ auto v=std::make_shared<Value>(); v->t=STR; v->str=x; return v; }
    static ValuePtr mkarr(){ auto v=std::make_shared<Value>(); v->t=ARR; return v; }
    static ValuePtr mkobj(){ auto v=std::make_shared<Value>(); v->t=OBJ; return v; }
    void set(const std::string& k,ValuePtr v){ obj.push_back({k,v}); }
    ValuePtr get(const std::string& k) const { for(auto& p:obj) if(p.first==k) return p.second; return nullptr; }
    std::string as_str() const { return t==STR?str:std::string(); }
    double as_num() const { return t==NUM?num:0; }
};

struct Parser {
    const char* p; const char* end; bool ok=true;
    Parser(const std::string& s):p(s.c_str()),end(s.c_str()+s.size()){}
    void skip(){ while(p<end&&(*p==' '||*p=='\t'||*p=='\n'||*p=='\r'))p++; }
    ValuePtr parse(){ skip(); return value(); }
    ValuePtr value(){
        skip(); if(p>=end){ok=false;return Value::mknull();}
        char c=*p;
        if(c=='{')return object();
        if(c=='[')return array();
        if(c=='"')return Value::mkstr(string());
        if(c=='t'||c=='f')return boolean();
        if(c=='n'){ p+=4; return Value::mknull(); }
        return number();
    }
    ValuePtr object(){
        auto v=Value::mkobj(); p++; skip();
        if(p<end&&*p=='}'){p++;return v;}
        for(;;){ skip(); if(p>=end||*p!='"'){ok=false;break;} std::string k=string(); skip(); if(p>=end||*p!=':'){ok=false;break;} p++; v->set(k,value()); skip(); if(p<end&&*p==','){p++;continue;} if(p<end&&*p=='}'){p++;break;} ok=false; break; }
        return v;
    }
    ValuePtr array(){
        auto v=Value::mkarr(); p++; skip();
        if(p<end&&*p==']'){p++;return v;}
        for(;;){ v->arr.push_back(value()); skip(); if(p<end&&*p==','){p++;continue;} if(p<end&&*p==']'){p++;break;} ok=false; break; }
        return v;
    }
    std::string string(){
        std::string s; p++;
        while(p<end&&*p!='"'){
            char c=*p++;
            if(c=='\\'&&p<end){ char e=*p++; switch(e){case 'n':s+='\n';break;case 't':s+='\t';break;case 'r':s+='\r';break;case 'b':s+='\b';break;case 'f':s+='\f';break;case '/':s+='/';break;case '\\':s+='\\';break;case '"':s+='"';break;case 'u':{ if(p+4<=end){ int cp=(int)strtol(std::string(p,p+4).c_str(),nullptr,16); p+=4; if(cp<0x80)s+=(char)cp; else if(cp<0x800){s+=(char)(0xC0|(cp>>6));s+=(char)(0x80|(cp&0x3F));} else {s+=(char)(0xE0|(cp>>12));s+=(char)(0x80|((cp>>6)&0x3F));s+=(char)(0x80|(cp&0x3F));} } break; } default:s+=e;} }
            else s+=c;
        }
        if(p<end)p++;
        return s;
    }
    ValuePtr boolean(){ if(*p=='t'){p+=4;return Value::mkbool(true);} p+=5; return Value::mkbool(false); }
    ValuePtr number(){ const char* s=p; while(p<end&&(*p=='-'||*p=='+'||*p=='.'||*p=='e'||*p=='E'||(*p>='0'&&*p<='9')))p++; return Value::mknum(strtod(std::string(s,p).c_str(),nullptr)); }
};

static void dump_str(std::string& o,const std::string& s){
    o+='"';
    for(char c:s){ switch(c){case '"':o+="\\\"";break;case '\\':o+="\\\\";break;case '\n':o+="\\n";break;case '\t':o+="\\t";break;case '\r':o+="\\r";break;case '\b':o+="\\b";break;case '\f':o+="\\f";break;default: if((unsigned char)c<0x20){char buf[8];snprintf(buf,sizeof(buf),"\\u%04x",c);o+=buf;} else o+=c; } }
    o+='"';
}
static void dump(std::string& o,const ValuePtr& v){
    if(!v){ o+="null"; return; }
    switch(v->t){
        case NUL: o+="null"; break;
        case BOOL: o+=v->b?"true":"false"; break;
        case NUM: { double d=v->num; if(d==(long long)d){ char buf[32]; snprintf(buf,sizeof(buf),"%lld",(long long)d); o+=buf; } else { char buf[32]; snprintf(buf,sizeof(buf),"%g",d); o+=buf; } } break;
        case STR: dump_str(o,v->str); break;
        case ARR: { o+='['; for(size_t i=0;i<v->arr.size();i++){ if(i)o+=','; dump(o,v->arr[i]); } o+=']'; } break;
        case OBJ: { o+='{'; for(size_t i=0;i<v->obj.size();i++){ if(i)o+=','; dump_str(o,v->obj[i].first); o+=':'; dump(o,v->obj[i].second); } o+='}'; } break;
    }
}
static std::string serialize(const ValuePtr& v){ std::string o; dump(o,v); return o; }
} // namespace js

// ====================== 输出通道 ======================
static int g_real_out = -1;
static void send_message(const js::ValuePtr& msg){
    std::string s=js::serialize(msg); s+='\n';
    if(g_real_out>=0){ ssize_t w=write(g_real_out,s.data(),s.size()); (void)w; }
}
static js::ValuePtr make_response(const js::ValuePtr& id){
    auto r=js::Value::mkobj();
    r->set("jsonrpc",js::Value::mkstr("2.0"));
    r->set("id", id?id:js::Value::mknull());
    return r;
}
static js::ValuePtr text_content(const std::string& text){
    auto result=js::Value::mkobj();
    auto arr=js::Value::mkarr();
    auto item=js::Value::mkobj();
    item->set("type",js::Value::mkstr("text"));
    item->set("text",js::Value::mkstr(text));
    arr->arr.push_back(item);
    result->set("content",arr);
    return result;
}
static js::ValuePtr error_content(const std::string& text){
    auto result=text_content(text);
    result->set("isError",js::Value::mkbool(true));
    return result;
}

// ====================== 工具实现 ======================
static std::string hex64(uint64_t v){ char buf[32]; snprintf(buf,sizeof(buf),"0x%016" PRIx64,v); return buf; }

static js::ValuePtr reg_obj(uint64_t v){
    auto o=js::Value::mkobj();
    o->set("value",js::Value::mkstr(hex64(v)));
    std::string sym=format_symbol(v);
    if(!sym.empty()) o->set("symbol",js::Value::mkstr(sym));
    return o;
}

static js::ValuePtr record_to_json(const hwbp_record& r){
    auto o=js::Value::mkobj();
    o->set("hit_count",js::Value::mknum((double)r.hit_count));
    auto regs=js::Value::mkobj();
    const uint64_t xs[30]={r.x0,r.x1,r.x2,r.x3,r.x4,r.x5,r.x6,r.x7,r.x8,r.x9,r.x10,r.x11,r.x12,r.x13,r.x14,r.x15,r.x16,r.x17,r.x18,r.x19,r.x20,r.x21,r.x22,r.x23,r.x24,r.x25,r.x26,r.x27,r.x28,r.x29};
    for(int i=0;i<30;i++){ char name[8]; snprintf(name,sizeof(name),"x%d",i); regs->set(name,reg_obj(xs[i])); }
    regs->set("lr",reg_obj(r.lr));
    regs->set("sp",reg_obj(r.sp));
    regs->set("pc",reg_obj(r.pc));
    o->set("registers",regs);
    auto stack=js::Value::mkarr();
    uint32_t count=r.stack_count; if(count>256)count=256;
    for(uint32_t i=0;i<count;i++) stack->arr.push_back(reg_obj(r.stack[i]));
    o->set("stack_count",js::Value::mknum((double)count));
    o->set("stack",stack);
    return o;
}

static js::ValuePtr tool_ping(){
    if(!connect_driver()) return error_content("连接驱动失败");
    if(!commit_req(op_o)) return error_content("ping 超时");
    return text_content("驱动响应正常");
}
static js::ValuePtr tool_info(){
    if(!connect_driver()) return error_content("连接驱动失败");
    memset(&g_req->bp_info,0,sizeof(g_req->bp_info));
    if(!commit_req(op_brps_weps_info)) return error_content("读取超时");
    auto o=js::Value::mkobj();
    o->set("num_brps",js::Value::mknum((double)g_req->bp_info.num_brps));
    o->set("num_wrps",js::Value::mknum((double)g_req->bp_info.num_wrps));
    o->set("max_points",js::Value::mknum((double)MAX_POINTS));
    std::string s=js::serialize(o);
    return text_content(s);
}
static js::ValuePtr tool_find(const js::ValuePtr& args){
    if(!connect_driver()) return error_content("连接驱动失败");
    std::string target=args?args->get("target")?args->get("target")->as_str():"":"";
    if(target.empty()) return error_content("target 不能为空");
    if(select_target(target)) return error_content("查找进程失败");
    auto o=js::Value::mkobj();
    o->set("pid",js::Value::mknum((double)g_req->pid));
    o->set("rss_kb",js::Value::mknum((double)g_req->proc_info.selected_rss_kb));
    o->set("target",js::Value::mkstr(target));
    return text_content(js::serialize(o));
}
static js::ValuePtr tool_read(const js::ValuePtr& args){
    if(!connect_driver()) return error_content("连接驱动失败");
    if(!args) return error_content("缺少参数");
    std::string target=args->get("target")?args->get("target")->as_str():"";
    auto addrv=args->get("addr"); auto sizev=args->get("size");
    if(target.empty()||!addrv) return error_content("需要 target 和 addr");
    uint64_t addr = addrv->t==js::STR ? strtoull(addrv->str.c_str(),nullptr,0) : (uint64_t)addrv->as_num();
    int size = sizev ? (int)sizev->as_num() : 64;
    if(size<1||size>0x1000) return error_content("size 必须 1..4096");
    if(select_target(target)) return error_content("查找进程失败");
    memset(&g_req->rw_info,0,sizeof(g_req->rw_info)); g_req->rw_info.rw_addr=addr; g_req->rw_info.size=size;
    if(!commit_req(op_r,5000)) return error_content("读取超时");
    if(g_req->status<0) return error_content("读取失败");
    std::string hex; char b[4];
    for(int i=0;i<size;i++){ snprintf(b,sizeof(b),"%02x",g_req->rw_info.user_buffer[i]); hex+=b; if(i+1<size)hex+=' '; }
    auto o=js::Value::mkobj();
    o->set("addr",js::Value::mkstr(hex64(addr)));
    o->set("size",js::Value::mknum((double)size));
    o->set("hex",js::Value::mkstr(hex));
    return text_content(js::serialize(o));
}
static js::ValuePtr tool_write(const js::ValuePtr& args){
    if(!connect_driver()) return error_content("连接驱动失败");
    if(!args) return error_content("缺少参数");
    std::string target=args->get("target")?args->get("target")->as_str():"";
    auto addrv=args->get("addr"); auto bytesv=args->get("bytes");
    if(target.empty()||!addrv||!bytesv) return error_content("需要 target/addr/bytes");
    uint64_t addr = addrv->t==js::STR ? strtoull(addrv->str.c_str(),nullptr,0) : (uint64_t)addrv->as_num();
    std::vector<uint8_t> bytes;
    std::string text=bytesv->as_str();
    { size_t pos=0; while(pos<text.size()){ while(pos<text.size()&&isspace((unsigned char)text[pos]))pos++; if(pos>=text.size())break; size_t next=pos; while(next<text.size()&&!isspace((unsigned char)text[next]))next++; std::string item=text.substr(pos,next-pos); char* e=nullptr; errno=0; unsigned long v=strtoul(item.c_str(),&e,16); if(errno||!e||*e||v>0xff) return error_content("bytes 格式无效，例如 \"01 02 ff\""); bytes.push_back((uint8_t)v); pos=next; } }
    if(bytes.empty()||bytes.size()>0x1000) return error_content("字节数必须 1..4096");
    if(select_target(target)) return error_content("查找进程失败");
    memset(&g_req->rw_info,0,sizeof(g_req->rw_info)); g_req->rw_info.rw_addr=addr; g_req->rw_info.size=(int)bytes.size(); memcpy(g_req->rw_info.user_buffer,bytes.data(),bytes.size());
    if(!commit_req(op_w,5000)) return error_content("写入超时");
    if(g_req->status<0) return error_content("写入失败");
    auto o=js::Value::mkobj();
    o->set("addr",js::Value::mkstr(hex64(addr)));
    o->set("written",js::Value::mknum((double)bytes.size()));
    return text_content(js::serialize(o));
}
static js::ValuePtr tool_set_bp(const js::ValuePtr& args){
    if(!connect_driver()) return error_content("连接驱动失败");
    if(!args) return error_content("缺少参数");
    std::string target=args->get("target")?args->get("target")->as_str():"";
    auto addrv=args->get("addr");
    std::string type=args->get("type")?args->get("type")->as_str():"x";
    std::string scope=args->get("scope")?args->get("scope")->as_str():"all";
    int len=args->get("len")?(int)args->get("len")->as_num():4;
    if(target.empty()||!addrv) return error_content("需要 target 和 addr");
    uint64_t addr = addrv->t==js::STR ? strtoull(addrv->str.c_str(),nullptr,0) : (uint64_t)addrv->as_num();
    hwbp_type bt = type=="r"?HWBP_BREAKPOINT_R:type=="w"?HWBP_BREAKPOINT_W:(type=="rw"||type=="wr")?HWBP_BREAKPOINT_RW:HWBP_BREAKPOINT_X;
    hwbp_scope bs = scope=="main"?SCOPE_MAIN_THREAD:scope=="other"?SCOPE_OTHER_THREADS:SCOPE_ALL_THREADS;
    if(!addr||len<1||len>8) return error_content("点位无效");
    memset(&g_req->bp_info,0,sizeof(g_req->bp_info)); memset(&g_req->proc_info,0,sizeof(g_req->proc_info)); g_req->pid=0;
    snprintf(g_req->proc_info.name,sizeof(g_req->proc_info.name),"%s",target.c_str());
    hwbp_point& p0=g_req->bp_info.points[0]; p0.bt=bt; p0.bl=(hwbp_len)len; p0.bs=bs; p0.hit_addr=addr; p0.record_count=0;
    if(!commit_req(op_set_process_hwbp)) return error_content("设置断点超时");
    if(g_req->status!=0) return error_content("设置断点失败");
    memset(&g_req->mem_info,0,sizeof(g_req->mem_info)); commit_req(op_m,15000);
    auto o=js::Value::mkobj();
    o->set("pid",js::Value::mknum((double)g_req->pid));
    o->set("addr",js::Value::mkstr(hex64(addr)));
    o->set("type",js::Value::mkstr(type));
    o->set("len",js::Value::mknum((double)len));
    o->set("scope",js::Value::mkstr(scope));
    o->set("note",js::Value::mkstr("断点已设置，使用 read_hits 读取命中记录"));
    return text_content(js::serialize(o));
}
static js::ValuePtr tool_read_hits(const js::ValuePtr& args){
    if(!connect_driver()) return error_content("连接驱动失败");
    int max_records = args&&args->get("max_records") ? (int)args->get("max_records")->as_num() : 32;
    if(max_records<1) max_records=1;
    if(max_records>0x100) max_records=0x100;
    hwbp_point& p=g_req->bp_info.points[0];
    int c=p.record_count; if(c<0)c=0; if(c>0x100)c=0x100;
    auto o=js::Value::mkobj();
    o->set("record_count",js::Value::mknum((double)c));
    auto arr=js::Value::mkarr();
    int limit=c<max_records?c:max_records;
    for(int i=0;i<limit;i++) arr->arr.push_back(record_to_json(p.records[i]));
    o->set("records",arr);
    if(c>limit) o->set("truncated",js::Value::mknum((double)(c-limit)));
    return text_content(js::serialize(o));
}
static js::ValuePtr tool_remove(){
    if(!connect_driver()) return error_content("连接驱动失败");
    if(!commit_req(op_remove_process_hwbp)) return error_content("删除超时");
    return text_content("已删除当前断点/观察点");
}

// ====================== 工具表与协议处理 ======================
static js::ValuePtr str_prop(const std::string& type,const std::string& desc){
    auto o=js::Value::mkobj(); o->set("type",js::Value::mkstr(type)); o->set("description",js::Value::mkstr(desc)); return o;
}
static js::ValuePtr make_tool(const std::string& name,const std::string& desc,const std::vector<std::pair<std::string,js::ValuePtr>>& props,const std::vector<std::string>& required){
    auto t=js::Value::mkobj();
    t->set("name",js::Value::mkstr(name));
    t->set("description",js::Value::mkstr(desc));
    auto schema=js::Value::mkobj();
    schema->set("type",js::Value::mkstr("object"));
    auto pobj=js::Value::mkobj();
    for(auto& p:props) pobj->set(p.first,p.second);
    schema->set("properties",pobj);
    auto reqarr=js::Value::mkarr();
    for(auto& r:required) reqarr->arr.push_back(js::Value::mkstr(r));
    schema->set("required",reqarr);
    t->set("inputSchema",schema);
    return t;
}
static js::ValuePtr build_tools_list(){
    auto arr=js::Value::mkarr();
    arr->arr.push_back(make_tool("ping","测试与 lsdriver 内核驱动的连通性",{},{}));
    arr->arr.push_back(make_tool("info","查询硬件断点(BRP)与观察点(WRP)槽位数量",{},{}));
    arr->arr.push_back(make_tool("find_process","按包名/进程名/PID 选择目标进程，返回 pid 与 RSS",{{"target",str_prop("string","包名、进程名或 PID")}},{"target"}));
    arr->arr.push_back(make_tool("read_memory","读取目标进程内存，返回十六进制",{{"target",str_prop("string","目标进程")},{"addr",str_prop("string","起始地址，支持 0x 十六进制")},{"size",str_prop("number","读取字节数，1..4096，默认64")}},{"target","addr"}));
    arr->arr.push_back(make_tool("write_memory","写入目标进程内存",{{"target",str_prop("string","目标进程")},{"addr",str_prop("string","起始地址，支持 0x 十六进制")},{"bytes",str_prop("string","空格分隔的十六进制字节，例如 01 02 ff")}},{"target","addr","bytes"}));
    arr->arr.push_back(make_tool("set_breakpoint","在目标进程地址设置硬件断点/观察点",{{"target",str_prop("string","目标进程")},{"addr",str_prop("string","断点地址，支持 0x 十六进制")},{"type",str_prop("string","x执行/r读/w写/rw读写，默认 x")},{"len",str_prop("number","长度 1..8，默认4")},{"scope",str_prop("string","main/other/all，默认 all")}},{"target","addr"}));
    arr->arr.push_back(make_tool("read_hits","读取当前断点的命中记录，含寄存器与内核栈",{{"max_records",str_prop("number","最多返回记录数，默认32")}},{}));
    arr->arr.push_back(make_tool("remove_breakpoint","删除当前断点/观察点",{},{}));
    return arr;
}

static js::ValuePtr dispatch_tool(const std::string& name,const js::ValuePtr& args){
    if(name=="ping") return tool_ping();
    if(name=="info") return tool_info();
    if(name=="find_process") return tool_find(args);
    if(name=="read_memory") return tool_read(args);
    if(name=="write_memory") return tool_write(args);
    if(name=="set_breakpoint") return tool_set_bp(args);
    if(name=="read_hits") return tool_read_hits(args);
    if(name=="remove_breakpoint") return tool_remove();
    return error_content("未知工具: "+name);
}

static void handle_line(const std::string& line){
    js::Parser parser(line);
    js::ValuePtr req=parser.parse();
    if(!parser.ok||!req||req->t!=js::OBJ) return;
    js::ValuePtr id=req->get("id");
    std::string method=req->get("method")?req->get("method")->as_str():"";
    bool is_notification = (id==nullptr);

    if(method=="initialize"){
        auto resp=make_response(id);
        auto result=js::Value::mkobj();
        result->set("protocolVersion",js::Value::mkstr("2024-11-05"));
        auto caps=js::Value::mkobj();
        caps->set("tools",js::Value::mkobj());
        result->set("capabilities",caps);
        auto info=js::Value::mkobj();
        info->set("name",js::Value::mkstr("ls-hwbp"));
        info->set("version",js::Value::mkstr("1.0.0"));
        result->set("serverInfo",info);
        resp->set("result",result);
        send_message(resp);
        return;
    }
    if(method=="notifications/initialized"||method=="initialized"){ return; }
    if(method=="tools/list"){
        auto resp=make_response(id);
        auto result=js::Value::mkobj();
        result->set("tools",build_tools_list());
        resp->set("result",result);
        send_message(resp);
        return;
    }
    if(method=="tools/call"){
        js::ValuePtr params=req->get("params");
        std::string name=params&&params->get("name")?params->get("name")->as_str():"";
        js::ValuePtr args=params?params->get("arguments"):nullptr;
        js::ValuePtr result=dispatch_tool(name,args);
        auto resp=make_response(id);
        resp->set("result",result);
        send_message(resp);
        return;
    }
    if(method=="ping"){ auto resp=make_response(id); resp->set("result",js::Value::mkobj()); send_message(resp); return; }
    if(is_notification) return;
    // 未知方法
    auto resp=make_response(id);
    auto err=js::Value::mkobj();
    err->set("code",js::Value::mknum(-32601));
    err->set("message",js::Value::mkstr("Method not found: "+method));
    resp->set("error",err);
    send_message(resp);
}

int run_mcp_server(){
    // 保存真实 stdout，把驱动层 printf 重定向到 stderr，避免污染 JSON-RPC 流。
    g_real_out = dup(1);
    dup2(2,1);
    setvbuf(stdin,nullptr,_IONBF,0);

    std::string buf;
    char chunk[4096];
    ssize_t n;
    while((n=read(0,chunk,sizeof(chunk)))>0){
        buf.append(chunk,(size_t)n);
        size_t pos;
        while((pos=buf.find('\n'))!=std::string::npos){
            std::string line=buf.substr(0,pos);
            buf.erase(0,pos+1);
            if(!line.empty()) handle_line(line);
        }
    }
    if(!buf.empty()) handle_line(buf);
    return 0;
}