#include <sys/mman.h>
#include <sys/prctl.h>
#include <unistd.h>
#include <signal.h>
#include <ctype.h>
#include <cerrno>
#include <cinttypes>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include <chrono>
#include <thread>
#include <algorithm>
#include <sys/uio.h>
#include <fcntl.h>

static constexpr uintptr_t LS_SHARED_ADDR = 0x2025827000ULL;
static constexpr int MAX_POINTS = 16;

enum hwbp_type { HWBP_BREAKPOINT_EMPTY=0, HWBP_BREAKPOINT_R=1, HWBP_BREAKPOINT_W=2, HWBP_BREAKPOINT_RW=3, HWBP_BREAKPOINT_X=4, HWBP_BREAKPOINT_INVALID=7 };
enum hwbp_len { HWBP_BREAKPOINT_LEN_1=1, HWBP_BREAKPOINT_LEN_2, HWBP_BREAKPOINT_LEN_3, HWBP_BREAKPOINT_LEN_4, HWBP_BREAKPOINT_LEN_5, HWBP_BREAKPOINT_LEN_6, HWBP_BREAKPOINT_LEN_7, HWBP_BREAKPOINT_LEN_8 };
enum hwbp_scope { SCOPE_MAIN_THREAD, SCOPE_OTHER_THREADS, SCOPE_ALL_THREADS };
enum sm_req_op { op_o, op_r, op_w, op_m, op_down, op_move, op_up, op_init_touch, op_brps_weps_info, op_find_process_by_name, op_set_process_hwbp, op_remove_process_hwbp, op_kexit };

struct hwbp_record {
    uint8_t mask[18];
    uint64_t hit_count, pc, lr, sp, orig_x0, syscallno, pstate;
    uint64_t x0,x1,x2,x3,x4,x5,x6,x7,x8,x9,x10,x11,x12,x13,x14,x15,x16,x17,x18,x19,x20,x21,x22,x23,x24,x25,x26,x27,x28,x29;
    uint32_t fpsr, fpcr;
    unsigned __int128 q0,q1,q2,q3,q4,q5,q6,q7,q8,q9,q10,q11,q12,q13,q14,q15,q16,q17,q18,q19,q20,q21,q22,q23,q24,q25,q26,q27,q28,q29,q30,q31;
    uint32_t stack_count;
    uint32_t stack_reserved;
    uint64_t stack[256];
};
struct hwbp_point { hwbp_type bt; hwbp_len bl; hwbp_scope bs; uint64_t hit_addr; int record_count; hwbp_record records[0x100]; };
struct hwbp_info { uint64_t num_brps; uint64_t num_wrps; hwbp_point points[16]; };
static constexpr int MAX_MODULES=1024, MAX_SCAN_REGIONS=16534, MOD_NAME_LEN=256, MAX_SEGS_PER_MODULE=10;
struct segment_info { short index; uint8_t prot; uint64_t start; uint64_t end; };
struct module_info { char name[MOD_NAME_LEN]; int seg_count; segment_info segs[MAX_SEGS_PER_MODULE]; };
struct region_info { uint64_t start; uint64_t end; };
struct memory_info { int module_count; module_info modules[MAX_MODULES]; int region_count; region_info regions[MAX_SCAN_REGIONS]; };
struct virtual_input { int POSITION_X, POSITION_Y; int slot; int x, y; };
struct memory_rw { uint64_t rw_addr; uint8_t user_buffer[0x1000]; int size; };
static constexpr int PROC_NAME_LEN=256;
struct process_select_info { char name[PROC_NAME_LEN]; int selected_pid; uint64_t selected_rss_kb; };
struct req_obj { bool kernel; bool user; sm_req_op op; int status; int pid; process_select_info proc_info; memory_rw rw_info; memory_info mem_info; virtual_input vinput_info; hwbp_info bp_info; };

static req_obj* g_req=nullptr;
static volatile sig_atomic_t g_stop=0;
static void on_sig(int){ g_stop=1; }
static uint64_t parse_u64(const char* s){ char* e=nullptr; errno=0; uint64_t v=strtoull(s,&e,0); if(errno||!e||*e){fprintf(stderr,"数字无效: %s\n",s?s:"(null)"); exit(2);} return v; }
static hwbp_type parse_type(const std::string& s){ if(s=="x")return HWBP_BREAKPOINT_X; if(s=="r")return HWBP_BREAKPOINT_R; if(s=="w")return HWBP_BREAKPOINT_W; if(s=="rw"||s=="wr")return HWBP_BREAKPOINT_RW; fprintf(stderr,"类型无效: %s\n",s.c_str()); exit(2); }
static hwbp_scope parse_scope(const std::string& s){ if(s=="main")return SCOPE_MAIN_THREAD; if(s=="other")return SCOPE_OTHER_THREADS; if(s=="all")return SCOPE_ALL_THREADS; fprintf(stderr,"线程范围无效: %s\n",s.c_str()); exit(2); }
static const char* type_name(hwbp_type t){ switch(t){case HWBP_BREAKPOINT_R:return"r";case HWBP_BREAKPOINT_W:return"w";case HWBP_BREAKPOINT_RW:return"rw";case HWBP_BREAKPOINT_X:return"x";default:return"?";} }
static const char* scope_name(hwbp_scope s){ switch(s){case SCOPE_MAIN_THREAD:return"main";case SCOPE_OTHER_THREADS:return"other";case SCOPE_ALL_THREADS:return"all";default:return"?";} }
static char pc(uint8_t prot,uint8_t bit,char c){ return (prot&bit)?c:'-'; }
static bool wait_user(int ms){ for(int i=0;i<ms;i++){ if(g_req->user){ g_req->user=false; __sync_synchronize(); return true; } usleep(1000);} return false; }
static bool commit_req(sm_req_op op,int ms=5000){ g_req->op=op; __sync_synchronize(); g_req->kernel=true; __sync_synchronize(); return wait_user(ms); }
static bool connect_driver(){
    if(g_req) return true;
    if(prctl(PR_SET_NAME,"LS",0,0,0)!=0){ fprintf(stderr,"设置进程名失败: %s\n",strerror(errno)); return false; }
    size_t sz=sizeof(req_obj); void* p=mmap((void*)LS_SHARED_ADDR,sz,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS|MAP_FIXED,-1,0);
    if(p==MAP_FAILED){ fprintf(stderr,"映射共享内存失败: %s\n",strerror(errno)); return false; }
    g_req=(req_obj*)p; memset(g_req,0,sz); printf("共享内存: %p 大小: %zu\n",p,sz);
    if(!wait_user(10000)){ g_req->op=op_o; g_req->kernel=true; if(!wait_user(10000)){ fprintf(stderr,"连接超时：驱动未加载或已有 LS 客户端占用\n"); return false; }}
    return true;
}
static bool prompt_line(const char* t,char* b,size_t n){ printf("%s",t); fflush(stdout); if(!fgets(b,n,stdin))return false; size_t l=strlen(b); if(l&&b[l-1]=='\n')b[l-1]=0; return true; }
static int prompt_int(const char* t,int d){ char b[128]; if(!prompt_line(t,b,sizeof(b))||!b[0])return d; return (int)parse_u64(b); }
static uint64_t prompt_u64(const char* t,uint64_t d){ char b[128]; if(!prompt_line(t,b,sizeof(b))||!b[0])return d; return parse_u64(b); }
static std::string prompt_str(const char* t,const char* d){ char b[512]; if(!prompt_line(t,b,sizeof(b))||!b[0])return d; return b; }

struct PointSpec { hwbp_type type=HWBP_BREAKPOINT_EMPTY; int len=0; hwbp_scope scope=SCOPE_ALL_THREADS; uint64_t addr=0; };
struct Args { std::string target,module,jsonl,bytes; uint64_t addr=0,offset=0; int seg=-999,len=0,interval=500,duration=0,max_print=32,size=16; hwbp_type type=HWBP_BREAKPOINT_EMPTY; hwbp_scope scope=SCOPE_ALL_THREADS; bool brief=false,all_regs=false; };
static void usage(){
    puts("lsdriver 硬件断点工具");
    puts("  ls-hwbp ping | info | remove");
    puts("  ls-hwbp find --target 包名或PID");
    puts("  ls-hwbp read --target 目标 --addr ADDR --size N");
    puts("  ls-hwbp write --target 目标 --addr ADDR --bytes \"01 02 03 04\"");
    puts("  ls-hwbp monitor --target 目标 --type x|r|w|rw --addr ADDR --len 1..8 --scope main|other|all");
    puts("  ls-hwbp monitor --target 目标 --module lib.so --seg 0 --offset OFF --type x --len 4");
    puts("选项: --interval MS --duration SEC --brief --all-regs --max-print N --jsonl PATH");
}
static Args parse_args(int argc,char** argv,int start=2){
    Args a;
    for(int i=start;i<argc;i++){ std::string k=argv[i]; auto val=[&](){ if(i+1>=argc){fprintf(stderr,"%s 缺少参数\n",k.c_str()); exit(2);} return argv[++i]; };
        if(k=="--target"||k=="--pid"||k=="--name")a.target=val(); else if(k=="--addr")a.addr=parse_u64(val()); else if(k=="--type")a.type=parse_type(val()); else if(k=="--len")a.len=(int)parse_u64(val()); else if(k=="--scope")a.scope=parse_scope(val()); else if(k=="--interval")a.interval=(int)parse_u64(val()); else if(k=="--duration")a.duration=(int)parse_u64(val()); else if(k=="--max-print")a.max_print=(int)parse_u64(val()); else if(k=="--size")a.size=(int)parse_u64(val()); else if(k=="--module")a.module=val(); else if(k=="--seg")a.seg=(int)parse_u64(val()); else if(k=="--offset")a.offset=parse_u64(val()); else if(k=="--bytes")a.bytes=val(); else if(k=="--jsonl")a.jsonl=val(); else if(k=="--brief")a.brief=true; else if(k=="--all-regs")a.all_regs=true; else { fprintf(stderr,"未知参数: %s\n",k.c_str()); exit(2); }}
    return a;
}
static int select_target(const std::string& target){ if(target.empty()){fprintf(stderr,"目标不能为空\n"); return 1;} memset(&g_req->proc_info,0,sizeof(g_req->proc_info)); g_req->pid=0; snprintf(g_req->proc_info.name,sizeof(g_req->proc_info.name),"%s",target.c_str()); if(!commit_req(op_find_process_by_name,5000)){fprintf(stderr,"查找进程超时\n"); return 1;} if(g_req->status!=0){fprintf(stderr,"查找失败 status=%d target=%s\n",g_req->status,target.c_str()); return 1;} g_req->pid=g_req->proc_info.selected_pid; printf("内核已选择进程: PID=%d RSS=%" PRIu64 "KB 目标=%s\n",g_req->pid,g_req->proc_info.selected_rss_kb,target.c_str()); return 0; }
static int load_memory_info(const std::string& target){ if(select_target(target))return 1; memset(&g_req->mem_info,0,sizeof(g_req->mem_info)); if(!commit_req(op_m,15000)){fprintf(stderr,"枚举内存超时\n"); return 1;} if(g_req->status!=0){fprintf(stderr,"枚举内存失败 status=%d\n",g_req->status); return 1;} return 0; }
static uint64_t resolve_addr(const Args& a){ if(a.module.empty())return a.addr; if(load_memory_info(a.target))exit(1); for(int i=0;i<g_req->mem_info.module_count&&i<MAX_MODULES;i++){ module_info& m=g_req->mem_info.modules[i]; if(!strstr(m.name,a.module.c_str()))continue; for(int j=0;j<m.seg_count&&j<MAX_SEGS_PER_MODULE;j++){ segment_info& s=m.segs[j]; if(a.seg!=-999&&s.index!=a.seg)continue; printf("解析模块: %s seg=%d %c%c%c 0x%016" PRIx64 "+0x%" PRIx64 "\n",m.name,s.index,pc(s.prot,1,'r'),pc(s.prot,2,'w'),pc(s.prot,4,'x'),s.start,a.offset); return s.start+a.offset; }} fprintf(stderr,"未找到模块/段: %s seg=%d\n",a.module.c_str(),a.seg); exit(1); }
struct AddrSymbol { std::string name; int seg=-999; uint64_t offset=0; uint64_t start=0; uint64_t end=0; bool ok=false; };
static std::string base_name(const char* path){ const char* p=strrchr(path,'/'); return p?p+1:path; }
static const char* seg_name(int seg,char* buf,size_t n){ if(seg==-1)return "bss"; snprintf(buf,n,"%d",seg); return buf; }
static AddrSymbol resolve_symbol(uint64_t addr){ AddrSymbol best; uint64_t best_size=~0ULL; if(!g_req)return best; memory_info& mi=g_req->mem_info; for(int i=0;i<mi.module_count&&i<MAX_MODULES;i++){ module_info& m=mi.modules[i]; for(int j=0;j<m.seg_count&&j<MAX_SEGS_PER_MODULE;j++){ segment_info& s=m.segs[j]; if(addr>=s.start&&addr<s.end){ uint64_t size=s.end-s.start; if(size<best_size){ best.ok=true; best.name=base_name(m.name); best.seg=s.index; best.offset=addr-s.start; best.start=s.start; best.end=s.end; best_size=size; } } } } return best; }
static std::string format_symbol(uint64_t addr){ AddrSymbol sym=resolve_symbol(addr); char buf[512]; if(!sym.ok) return ""; char segbuf[32]; const char* seg=seg_name(sym.seg,segbuf,sizeof(segbuf)); if(sym.name.rfind("anon:",0)==0 || sym.name.rfind("[anon:",0)==0) snprintf(buf,sizeof(buf),"%s+0x%llx",sym.name.c_str(),(unsigned long long)sym.offset); else if(sym.seg==0) snprintf(buf,sizeof(buf),"%s+0x%llx",sym.name.c_str(),(unsigned long long)sym.offset); else snprintf(buf,sizeof(buf),"%s[%s]+0x%llx",sym.name.c_str(),seg,(unsigned long long)sym.offset); return buf; }
static void print_resolved_value(uint64_t value){ std::string sym=format_symbol(value); if(!sym.empty()) printf(" ← %s",sym.c_str()); }
static void print_reg_line(const char* name,uint64_t value){ printf("%-6s 0x%llx",name,(unsigned long long)value); print_resolved_value(value); puts(""); }
static void print_stack_line(int idx,uint64_t addr){ std::string sym=format_symbol(addr); if(sym.empty()) printf("#%-4d0x%llx\n",idx,(unsigned long long)addr); else printf("#%-4d0x%llx (%s)\n",idx,(unsigned long long)addr,sym.c_str()); }
static void print_kernel_stack(const hwbp_record& r){ puts("\n堆栈:"); uint32_t count=r.stack_count; if(count>256)count=256; if(count==0){ puts("无内核栈记录"); return; } for(uint32_t i=0;i<count;i++)print_stack_line((int)i,r.stack[i]); }
static void print_rec(int point,int idx,const hwbp_record& r,bool brief,bool all){ (void)point; (void)brief; (void)all; printf("\n#%d t:%llu\n",idx+1,(unsigned long long)r.hit_count); puts("----------------------------------------"); print_reg_line("X0",r.x0); print_reg_line("X1",r.x1); print_reg_line("X2",r.x2); print_reg_line("X3",r.x3); print_reg_line("X4",r.x4); print_reg_line("X5",r.x5); print_reg_line("X6",r.x6); print_reg_line("X7",r.x7); print_reg_line("X8",r.x8); print_reg_line("X9",r.x9); print_reg_line("X10",r.x10); print_reg_line("X11",r.x11); print_reg_line("X12",r.x12); print_reg_line("X13",r.x13); print_reg_line("X14",r.x14); print_reg_line("X15",r.x15); print_reg_line("X16",r.x16); print_reg_line("X17",r.x17); print_reg_line("X18",r.x18); print_reg_line("X19",r.x19); print_reg_line("X20",r.x20); print_reg_line("X21",r.x21); print_reg_line("X22",r.x22); print_reg_line("X23",r.x23); print_reg_line("X24",r.x24); print_reg_line("X25",r.x25); print_reg_line("X26",r.x26); print_reg_line("X27",r.x27); print_reg_line("X28",r.x28); print_reg_line("X29",r.x29); print_reg_line("LR",r.lr); print_reg_line("SP",r.sp); print_reg_line("PC",r.pc); print_kernel_stack(r); puts("----------------------------------------"); }
static void append_json(FILE* f,int p,int i,const hwbp_record& r){ if(!f)return; fprintf(f,"{\"point\":%d,\"index\":%d,\"hits\":%" PRIu64 ",\"pc\":\"0x%016" PRIx64 "\",\"lr\":\"0x%016" PRIx64 "\",\"sp\":\"0x%016" PRIx64 "\"}\n",p,i,r.hit_count,r.pc,r.lr,r.sp); fflush(f); }
static int cmd_ping(){ if(!connect_driver())return 1; if(!commit_req(op_o)){fprintf(stderr,"ping 超时\n");return 1;} puts("驱动响应正常"); return 0; }
static int cmd_info(){ if(!connect_driver())return 1; memset(&g_req->bp_info,0,sizeof(g_req->bp_info)); if(!commit_req(op_brps_weps_info)){fprintf(stderr,"读取超时\n");return 1;} printf("执行断点槽位 BRP: %" PRIu64 "\n访问观察点槽位 WRP: %" PRIu64 "\n协议点位上限: %d\n",g_req->bp_info.num_brps,g_req->bp_info.num_wrps,MAX_POINTS); return 0; }
static int cmd_remove(){ if(!connect_driver())return 1; if(!commit_req(op_remove_process_hwbp)){fprintf(stderr,"删除超时\n");return 1;} puts("已删除当前断点/观察点"); return 0; }
static int cmd_find(int argc,char** argv){ if(!connect_driver())return 1; Args a=parse_args(argc,argv); return select_target(a.target); }
static void print_hexdump(uint64_t base,const uint8_t* data,int size){ for(int i=0;i<size;i++){ if((i&15)==0)printf("\n0x%016" PRIx64 ": ",base+i); printf("%02x ",data[i]); } puts(""); }
static int read_memory(const std::string& target,uint64_t addr,int size){ if(size<1){fprintf(stderr,"读取大小无效\n");return 1;} if(select_target(target))return 1; int remain=size; uint64_t cur=addr; while(remain>0){ int n=remain>0x1000?0x1000:remain; memset(&g_req->rw_info,0,sizeof(g_req->rw_info)); g_req->rw_info.rw_addr=cur; g_req->rw_info.size=n; if(!commit_req(op_r,5000)){fprintf(stderr,"读取超时\n");return 1;} if(g_req->status<0){fprintf(stderr,"读取失败 status=%d\n",g_req->status);return 1;} print_hexdump(cur,g_req->rw_info.user_buffer,n); cur+=n; remain-=n;} return 0; }
static bool parse_bytes(const std::string& text,std::vector<uint8_t>& out){ size_t pos=0; while(pos<text.size()){ while(pos<text.size()&&isspace((unsigned char)text[pos]))pos++; if(pos>=text.size())break; size_t next=pos; while(next<text.size()&&!isspace((unsigned char)text[next]))next++; std::string item=text.substr(pos,next-pos); char* end=nullptr; errno=0; unsigned long v=strtoul(item.c_str(),&end,16); if(errno||!end||*end||v>0xff)return false; out.push_back((uint8_t)v); pos=next; } return !out.empty(); }
static int write_memory(const std::string& target,uint64_t addr,const std::vector<uint8_t>& bytes){ if(bytes.empty()||bytes.size()>0x1000){fprintf(stderr,"写入字节数必须是 1..4096\n");return 1;} if(select_target(target))return 1; memset(&g_req->rw_info,0,sizeof(g_req->rw_info)); g_req->rw_info.rw_addr=addr; g_req->rw_info.size=(int)bytes.size(); memcpy(g_req->rw_info.user_buffer,bytes.data(),bytes.size()); if(!commit_req(op_w,5000)){fprintf(stderr,"写入超时\n");return 1;} if(g_req->status<0){fprintf(stderr,"写入失败 status=%d\n",g_req->status);return 1;} printf("写入完成: 0x%016" PRIx64 " size=%zu\n",addr,bytes.size()); return 0; }
static int cmd_read(int argc,char** argv){ if(!connect_driver())return 1; Args a=parse_args(argc,argv); uint64_t addr=resolve_addr(a); return read_memory(a.target,addr,a.size); }
static int cmd_write(int argc,char** argv){ if(!connect_driver())return 1; Args a=parse_args(argc,argv); uint64_t addr=resolve_addr(a); std::vector<uint8_t> bytes; if(!parse_bytes(a.bytes,bytes)){fprintf(stderr,"--bytes 格式无效，例如: \"01 02 ff\"\n");return 2;} return write_memory(a.target,addr,bytes); }
static int set_monitor_connected(Args a){ if(a.target.empty()){fprintf(stderr,"目标不能为空\n");return 1;} if(a.interval<50)a.interval=50; PointSpec s; s.type=a.type; s.len=a.len; s.scope=a.scope; s.addr=resolve_addr(a); if(!s.addr||s.len<1||s.len>8||s.type==HWBP_BREAKPOINT_EMPTY){fprintf(stderr,"点位无效\n");return 1;} memset(&g_req->bp_info,0,sizeof(g_req->bp_info)); memset(&g_req->proc_info,0,sizeof(g_req->proc_info)); g_req->pid=0; snprintf(g_req->proc_info.name,sizeof(g_req->proc_info.name),"%s",a.target.c_str()); hwbp_point& p0=g_req->bp_info.points[0]; p0.bt=s.type; p0.bl=(hwbp_len)s.len; p0.bs=s.scope; p0.hit_addr=s.addr; p0.record_count=0;
    if(!commit_req(op_set_process_hwbp)){fprintf(stderr,"设置断点超时\n");return 1;} if(g_req->status!=0){fprintf(stderr,"设置失败 status=%d\n",g_req->status);return 1;} memset(&g_req->mem_info,0,sizeof(g_req->mem_info)); if(!commit_req(op_m,15000)||g_req->status!=0) fprintf(stderr,"警告：模块枚举失败，命中地址将只显示裸地址\n"); printf("PID=%d RSS=%" PRIu64 "KB\n",g_req->pid,g_req->proc_info.selected_rss_kb); printf("点位#00 %s 0x%016" PRIx64 " len=%d scope=%s\n",type_name(s.type),s.addr,s.len,scope_name(s.scope)); puts(a.duration>0?"正在监控...":"按 Ctrl+C 停止"); FILE* jf=a.jsonl.empty()?nullptr:fopen(a.jsonl.c_str(),"a"); signal(SIGINT,on_sig); signal(SIGTERM,on_sig); int last_count=-1; uint64_t last_hits[0x100]; memset(last_hits,0,sizeof(last_hits)); int elapsed=0; while(!g_stop){ hwbp_point& p=g_req->bp_info.points[0]; int c=p.record_count; if(c<0)c=0; if(c>0x100)c=0x100; bool changed=c!=last_count; for(int i=0;i<c;i++)if(p.records[i].hit_count!=last_hits[i]){changed=true; last_hits[i]=p.records[i].hit_count; append_json(jf,0,i,p.records[i]);} if(changed){ printf("\n========== 点位#00 命中记录: %d ==========" "\n",c); int limit=std::min(c,a.max_print); for(int i=0;i<limit;i++)print_rec(0,i,p.records[i],a.brief,a.all_regs); if(c>limit)printf("...省略%d条\n",c-limit); fflush(stdout); last_count=c; } std::this_thread::sleep_for(std::chrono::milliseconds(a.interval)); elapsed+=a.interval; if(a.duration>0&&elapsed>=a.duration*1000)break; } if(jf)fclose(jf); puts("\n正在删除断点..."); commit_req(op_remove_process_hwbp,2000); return 0; }
static int cmd_monitor(int argc,char** argv){ if(!connect_driver())return 1; return set_monitor_connected(parse_args(argc,argv)); }
static void print_interactive_menu() {
    puts("\n==============================");
    puts(" lsdriver 硬件断点工具");
    puts("==============================");
    puts("1) 测试驱动连接");
    puts("2) 查看硬件断点/观察点数量");
    puts("3) 设置目标进程");
    puts("4) 设置并监控单个断点/观察点");
    puts("5) 读取内存");
    puts("6) 写入内存");
    puts("7) 删除当前断点/观察点");
    puts("8) MCP 模式/接口说明");
    puts("9) 退出");
}

static void print_mcp_help() {
    puts("\nMCP 模式/接口说明");
    puts("当前工具不启动 MCP Server，不监听端口，不生成任何文件。");
    puts("外部 MCP 如需接入，可把下面 CLI 命令封装为工具:");
    puts("  ls-hwbp ping");
    puts("  ls-hwbp info");
    puts("  ls-hwbp find --target 目标");
    puts("  ls-hwbp read --target 目标 --addr 地址 --size 大小");
    puts("  ls-hwbp write --target 目标 --addr 地址 --bytes \"01 02 ff\"");
    puts("  ls-hwbp monitor --target 目标 --type x|r|w|rw --addr 地址 --len 长度 --scope main|other|all");
    puts("  ls-hwbp monitor --target 目标 --module lib.so --seg 0 --offset 偏移 --type x --len 4");
    puts("  ls-hwbp remove");
}

static bool ensure_target(std::string& current_target) {
    std::string prompt = current_target.empty() ? "包名/进程名/PID: " : "包名/进程名/PID，回车复用当前目标: ";
    std::string input = prompt_str(prompt.c_str(), current_target.c_str());
    if (!input.empty()) current_target = input;
    if (current_target.empty()) { puts("当前目标为空"); return false; }
    return true;
}

static void prompt_monitor_common(Args& a, std::string& current_target) {
    if (!ensure_target(current_target)) return;
    a.target = current_target;
    a.interval = prompt_int("刷新间隔毫秒，默认500: ", 500);
    a.brief = true;
}

static int interactive() {
    puts("启动中文交互界面...");

    if (!connect_driver()) {
        return 1;
    }

    std::string current_target;

    for (;;) {
        print_interactive_menu();
        int c = prompt_int("请选择: ", 0);

        if (c == 1) {
            cmd_ping();
        } else if (c == 2) {
            cmd_info();
        } else if (c == 3) {
            if (ensure_target(current_target)) select_target(current_target);
        } else if (c == 4) {
            Args a;
            prompt_monitor_common(a, current_target);
            if (a.target.empty()) continue;
            a.addr = prompt_u64("监控地址，支持十六进制，例如 0x1234: ", 0);
            a.type = parse_type(prompt_str("类型 [x执行/r读/w写/rw读写] 默认 x: ", "x"));
            a.len = prompt_int("长度 [1..8]，执行断点通常填4，默认4: ", 4);
            a.scope = parse_scope(prompt_str("线程范围 [main主线程/other子线程/all全部] 默认 all: ", "all"));
            set_monitor_connected(a);
        } else if (c == 5) {
            if (!ensure_target(current_target)) continue;
            uint64_t addr = prompt_u64("读取地址，支持十六进制，例如 0x1234: ", 0);
            int size = prompt_int("读取大小，默认64: ", 64);
            read_memory(current_target, addr, size);
        } else if (c == 6) {
            if (!ensure_target(current_target)) continue;
            uint64_t addr = prompt_u64("写入地址，支持十六进制，例如 0x1234: ", 0);
            std::string text = prompt_str("写入字节，例如 01 02 03 04: ", "");
            std::vector<uint8_t> bytes;
            if (!parse_bytes(text, bytes)) { puts("字节格式无效"); continue; }
            printf("确认写入 目标=%s 地址=0x%016" PRIx64 " 字节数=%zu，输入 YES 继续: ", current_target.c_str(), addr, bytes.size());
            char confirm[32];
            if (!fgets(confirm, sizeof(confirm), stdin) || strncmp(confirm, "YES", 3) != 0) { puts("已取消写入"); continue; }
            write_memory(current_target, addr, bytes);
        } else if (c == 7) {
            cmd_remove();
        } else if (c == 8) {
            print_mcp_help();
        } else if (c == 9) {
            puts("退出");
            break;
        } else {
            puts("未知选项，请重新输入。");
        }
    }

    return 0;
}

int main(int argc, char** argv) {
    if (argc < 2) {
        return interactive();
    }

    std::string c = argv[1];

    if (c == "menu" || c == "interactive") {
        return interactive();
    }
    if (c == "ping") {
        return cmd_ping();
    }
    if (c == "info") {
        return cmd_info();
    }
    if (c == "find") {
        return cmd_find(argc, argv);
    }
    if (c == "read") {
        return cmd_read(argc, argv);
    }
    if (c == "write") {
        return cmd_write(argc, argv);
    }
    if (c == "remove") {
        return cmd_remove();
    }
    if (c == "monitor") {
        return cmd_monitor(argc, argv);
    }

    usage();
    return 2;
}