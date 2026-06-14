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
struct Args { std::string target,module,filter,jsonl; uint64_t addr=0,offset=0; int seg=-999,len=0,interval=500,duration=0,max_print=32,size=16; hwbp_type type=HWBP_BREAKPOINT_EMPTY; hwbp_scope scope=SCOPE_ALL_THREADS; bool brief=false,all_regs=false,regions=false; std::vector<PointSpec> points; };
static void usage(){
    puts("lsdriver 完整硬件断点工具");
    puts("  ls-hwbp ping | info | remove");
    puts("  ls-hwbp find --target 包名或PID");
    puts("  ls-hwbp modules --target 目标 [--filter so名] [--regions]");
    puts("  ls-hwbp read --target 目标 --addr ADDR --size N");
    puts("  ls-hwbp monitor --target 目标 --type x|r|w|rw --addr ADDR --len 1..8 --scope main|other|all");
    puts("  ls-hwbp monitor --target 目标 --module lib.so --seg 0 --offset OFF --type x --len 4");
    puts("  ls-hwbp monitor --target 目标 --point x:0xADDR:4:all --point rw:0xADDR:8:all");
    puts("选项: --interval MS --duration SEC --brief --all-regs --max-print N --jsonl PATH");
}
static Args parse_args(int argc,char** argv,int start=2){
    Args a;
    for(int i=start;i<argc;i++){ std::string k=argv[i]; auto val=[&](){ if(i+1>=argc){fprintf(stderr,"%s 缺少参数\n",k.c_str()); exit(2);} return argv[++i]; };
        if(k=="--target"||k=="--pid"||k=="--name")a.target=val(); else if(k=="--addr")a.addr=parse_u64(val()); else if(k=="--type")a.type=parse_type(val()); else if(k=="--len")a.len=(int)parse_u64(val()); else if(k=="--scope")a.scope=parse_scope(val()); else if(k=="--interval")a.interval=(int)parse_u64(val()); else if(k=="--duration")a.duration=(int)parse_u64(val()); else if(k=="--max-print")a.max_print=(int)parse_u64(val()); else if(k=="--size")a.size=(int)parse_u64(val()); else if(k=="--module")a.module=val(); else if(k=="--filter")a.filter=val(); else if(k=="--seg")a.seg=(int)parse_u64(val()); else if(k=="--offset")a.offset=parse_u64(val()); else if(k=="--jsonl")a.jsonl=val(); else if(k=="--brief")a.brief=true; else if(k=="--all-regs")a.all_regs=true; else if(k=="--regions")a.regions=true; else if(k=="--point"){ std::string s=val(); std::vector<std::string> p; size_t pos=0; while(true){ size_t n=s.find(':',pos); p.push_back(s.substr(pos,n==std::string::npos?std::string::npos:n-pos)); if(n==std::string::npos)break; pos=n+1;} if(p.size()<3||p.size()>4){fprintf(stderr,"--point 格式 type:addr:len[:scope]\n"); exit(2);} PointSpec ps; ps.type=parse_type(p[0]); ps.addr=parse_u64(p[1].c_str()); ps.len=(int)parse_u64(p[2].c_str()); ps.scope=p.size()==4?parse_scope(p[3]):SCOPE_ALL_THREADS; a.points.push_back(ps); } else { fprintf(stderr,"未知参数: %s\n",k.c_str()); exit(2); }}
    return a;
}
static int select_target(const std::string& target){ if(target.empty()){fprintf(stderr,"目标不能为空\n"); return 1;} memset(&g_req->proc_info,0,sizeof(g_req->proc_info)); g_req->pid=0; snprintf(g_req->proc_info.name,sizeof(g_req->proc_info.name),"%s",target.c_str()); if(!commit_req(op_find_process_by_name,5000)){fprintf(stderr,"查找进程超时\n"); return 1;} if(g_req->status!=0){fprintf(stderr,"查找失败 status=%d target=%s\n",g_req->status,target.c_str()); return 1;} g_req->pid=g_req->proc_info.selected_pid; printf("内核已选择进程: PID=%d RSS=%" PRIu64 "KB 目标=%s\n",g_req->pid,g_req->proc_info.selected_rss_kb,target.c_str()); return 0; }
static int load_memory_info(const std::string& target){ if(select_target(target))return 1; memset(&g_req->mem_info,0,sizeof(g_req->mem_info)); if(!commit_req(op_m,15000)){fprintf(stderr,"枚举内存超时\n"); return 1;} if(g_req->status!=0){fprintf(stderr,"枚举内存失败 status=%d\n",g_req->status); return 1;} return 0; }
static uint64_t resolve_addr(const Args& a){ if(a.module.empty())return a.addr; if(load_memory_info(a.target))exit(1); for(int i=0;i<g_req->mem_info.module_count&&i<MAX_MODULES;i++){ module_info& m=g_req->mem_info.modules[i]; if(!strstr(m.name,a.module.c_str()))continue; for(int j=0;j<m.seg_count&&j<MAX_SEGS_PER_MODULE;j++){ segment_info& s=m.segs[j]; if(a.seg!=-999&&s.index!=a.seg)continue; printf("解析模块: %s seg=%d %c%c%c 0x%016" PRIx64 "+0x%" PRIx64 "\n",m.name,s.index,pc(s.prot,1,'r'),pc(s.prot,2,'w'),pc(s.prot,4,'x'),s.start,a.offset); return s.start+a.offset; }} fprintf(stderr,"未找到模块/段: %s seg=%d\n",a.module.c_str(),a.seg); exit(1); }
static void print_pair(const char* a,uint64_t av,const char* b,uint64_t bv){ printf("  %-3s=0x%016" PRIx64 "  %-3s=0x%016" PRIx64 "\n",a,av,b,bv); }
static void print_rec(int point,int idx,const hwbp_record& r,bool brief,bool all){ printf("\n点位#%02d 记录#%03d hits=%" PRIu64 "\n",point,idx,r.hit_count); printf("  pc=0x%016" PRIx64 " lr=0x%016" PRIx64 " sp=0x%016" PRIx64 " pstate=0x%016" PRIx64 "\n",r.pc,r.lr,r.sp,r.pstate); print_pair("x0",r.x0,"x1",r.x1); print_pair("x2",r.x2,"x3",r.x3); print_pair("x4",r.x4,"x5",r.x5); print_pair("x6",r.x6,"x7",r.x7); if(brief)return; print_pair("x8",r.x8,"x9",r.x9); print_pair("x10",r.x10,"x11",r.x11); print_pair("x12",r.x12,"x13",r.x13); print_pair("x14",r.x14,"x15",r.x15); if(all){ print_pair("x16",r.x16,"x17",r.x17); print_pair("x18",r.x18,"x19",r.x19); print_pair("x20",r.x20,"x21",r.x21); print_pair("x22",r.x22,"x23",r.x23); print_pair("x24",r.x24,"x25",r.x25); print_pair("x26",r.x26,"x27",r.x27); print_pair("x28",r.x28,"x29",r.x29); printf("  fpsr=0x%08x fpcr=0x%08x\n",r.fpsr,r.fpcr); }}
static void append_json(FILE* f,int p,int i,const hwbp_record& r){ if(!f)return; fprintf(f,"{\"point\":%d,\"index\":%d,\"hits\":%" PRIu64 ",\"pc\":\"0x%016" PRIx64 "\",\"lr\":\"0x%016" PRIx64 "\",\"sp\":\"0x%016" PRIx64 "\"}\n",p,i,r.hit_count,r.pc,r.lr,r.sp); fflush(f); }
static int cmd_ping(){ if(!connect_driver())return 1; if(!commit_req(op_o)){fprintf(stderr,"ping 超时\n");return 1;} puts("驱动响应正常"); return 0; }
static int cmd_info(){ if(!connect_driver())return 1; memset(&g_req->bp_info,0,sizeof(g_req->bp_info)); if(!commit_req(op_brps_weps_info)){fprintf(stderr,"读取超时\n");return 1;} printf("执行断点槽位 BRP: %" PRIu64 "\n访问观察点槽位 WRP: %" PRIu64 "\n协议点位上限: %d\n",g_req->bp_info.num_brps,g_req->bp_info.num_wrps,MAX_POINTS); return 0; }
static int cmd_remove(){ if(!connect_driver())return 1; if(!commit_req(op_remove_process_hwbp)){fprintf(stderr,"删除超时\n");return 1;} puts("已删除当前断点/观察点"); return 0; }
static int cmd_find(int argc,char** argv){ if(!connect_driver())return 1; Args a=parse_args(argc,argv); return select_target(a.target); }
static int cmd_modules(int argc,char** argv){ if(!connect_driver())return 1; Args a=parse_args(argc,argv); if(load_memory_info(a.target))return 1; printf("模块数量: %d\n",g_req->mem_info.module_count); for(int i=0;i<g_req->mem_info.module_count&&i<MAX_MODULES;i++){ module_info& m=g_req->mem_info.modules[i]; if(!a.filter.empty()&&!strstr(m.name,a.filter.c_str()))continue; printf("\n[%03d] %s segs=%d\n",i,m.name,m.seg_count); for(int j=0;j<m.seg_count&&j<MAX_SEGS_PER_MODULE;j++){ segment_info& s=m.segs[j]; printf("  seg=%2d %c%c%c 0x%016" PRIx64 "-0x%016" PRIx64 " size=0x%" PRIx64 "\n",s.index,pc(s.prot,1,'r'),pc(s.prot,2,'w'),pc(s.prot,4,'x'),s.start,s.end,s.end>s.start?s.end-s.start:0); }} if(a.regions){ printf("\n扫描区域: %d\n",g_req->mem_info.region_count); for(int i=0;i<g_req->mem_info.region_count&&i<MAX_SCAN_REGIONS;i++){ region_info& r=g_req->mem_info.regions[i]; printf("[%04d] 0x%016" PRIx64 "-0x%016" PRIx64 "\n",i,r.start,r.end); }} return 0; }
static int cmd_read(int argc,char** argv){ if(!connect_driver())return 1; Args a=parse_args(argc,argv); uint64_t addr=resolve_addr(a); if(select_target(a.target))return 1; int remain=a.size; uint64_t cur=addr; while(remain>0){ int n=remain>0x1000?0x1000:remain; memset(&g_req->rw_info,0,sizeof(g_req->rw_info)); g_req->rw_info.rw_addr=cur; g_req->rw_info.size=n; if(!commit_req(op_r,5000)){fprintf(stderr,"读取超时\n");return 1;} if(g_req->status<0){fprintf(stderr,"读取失败 status=%d\n",g_req->status);return 1;} for(int i=0;i<n;i++){ if(((cur-addr+i)&15)==0)printf("\n0x%016" PRIx64 ": ",cur+i); printf("%02x ",g_req->rw_info.user_buffer[i]); } cur+=n; remain-=n;} puts(""); return 0; }
static int set_monitor_connected(Args a){ if(a.target.empty()){fprintf(stderr,"目标不能为空\n");return 1;} if(a.interval<50)a.interval=50; if(a.points.empty()){ PointSpec p; p.type=a.type; p.len=a.len; p.scope=a.scope; p.addr=resolve_addr(a); a.points.push_back(p); } if(a.points.size()>MAX_POINTS){fprintf(stderr,"最多 %d 个点位\n",MAX_POINTS);return 1;} memset(&g_req->bp_info,0,sizeof(g_req->bp_info)); memset(&g_req->proc_info,0,sizeof(g_req->proc_info)); g_req->pid=0; snprintf(g_req->proc_info.name,sizeof(g_req->proc_info.name),"%s",a.target.c_str()); for(size_t i=0;i<a.points.size();i++){ PointSpec& s=a.points[i]; if(!s.addr||s.len<1||s.len>8||s.type==HWBP_BREAKPOINT_EMPTY){fprintf(stderr,"点位%zu无效\n",i);return 1;} hwbp_point& p=g_req->bp_info.points[i]; p.bt=s.type; p.bl=(hwbp_len)s.len; p.bs=s.scope; p.hit_addr=s.addr; p.record_count=0; }
    if(!commit_req(op_set_process_hwbp)){fprintf(stderr,"设置断点超时\n");return 1;} if(g_req->status!=0){fprintf(stderr,"设置失败 status=%d\n",g_req->status);return 1;} printf("PID=%d RSS=%" PRIu64 "KB\n",g_req->pid,g_req->proc_info.selected_rss_kb); for(size_t i=0;i<a.points.size();i++)printf("点位#%02zu %s 0x%016" PRIx64 " len=%d scope=%s\n",i,type_name(a.points[i].type),a.points[i].addr,a.points[i].len,scope_name(a.points[i].scope)); puts(a.duration>0?"正在监控...":"按 Ctrl+C 停止"); FILE* jf=a.jsonl.empty()?nullptr:fopen(a.jsonl.c_str(),"a"); signal(SIGINT,on_sig); signal(SIGTERM,on_sig); int last_count[MAX_POINTS]; uint64_t last_hits[MAX_POINTS][0x100]; memset(last_count,0xff,sizeof(last_count)); memset(last_hits,0,sizeof(last_hits)); int elapsed=0; while(!g_stop){ for(size_t pi=0;pi<a.points.size();pi++){ hwbp_point& p=g_req->bp_info.points[pi]; int c=p.record_count; if(c<0)c=0; if(c>0x100)c=0x100; bool changed=c!=last_count[pi]; for(int i=0;i<c;i++)if(p.records[i].hit_count!=last_hits[pi][i]){changed=true; last_hits[pi][i]=p.records[i].hit_count; append_json(jf,(int)pi,i,p.records[i]);} if(changed){ printf("\n========== 点位#%02zu 命中记录: %d ==========" "\n",pi,c); int limit=std::min(c,a.max_print); for(int i=0;i<limit;i++)print_rec((int)pi,i,p.records[i],a.brief,a.all_regs); if(c>limit)printf("...省略%d条\n",c-limit); fflush(stdout); last_count[pi]=c; }} std::this_thread::sleep_for(std::chrono::milliseconds(a.interval)); elapsed+=a.interval; if(a.duration>0&&elapsed>=a.duration*1000)break; } if(jf)fclose(jf); puts("\n正在删除断点..."); commit_req(op_remove_process_hwbp,2000); return 0; }
static int cmd_monitor(int argc,char** argv){ if(!connect_driver())return 1; return set_monitor_connected(parse_args(argc,argv)); }
static void print_interactive_menu() {
    puts("\n==============================");
    puts(" lsdriver 硬件断点工具");
    puts("==============================");
    puts("1) 测试驱动连接");
    puts("2) 查看硬件断点/观察点数量");
    puts("3) 查找进程");
    puts("4) 查看模块列表");
    puts("5) 设置并监控单个断点/观察点");
    puts("6) 设置并监控多个断点/观察点");
    puts("7) 读取内存提示");
    puts("8) 删除当前断点/观察点");
    puts("9) 退出");
}

static void prompt_monitor_common(Args& a) {
    a.target = prompt_str("包名/进程名/PID: ", "");
    a.interval = prompt_int("刷新间隔毫秒，默认500: ", 500);
    a.brief = true;
}

static int interactive() {
    puts("启动中文交互界面...");

    if (!connect_driver()) {
        return 1;
    }

    for (;;) {
        print_interactive_menu();
        int c = prompt_int("请选择: ", 0);

        if (c == 1) {
            cmd_ping();
        } else if (c == 2) {
            cmd_info();
        } else if (c == 3) {
            Args a;
            a.target = prompt_str("包名/进程名/PID: ", "");
            select_target(a.target);
        } else if (c == 4) {
            std::string target = prompt_str("包名/进程名/PID: ", "");
            char* av[4] = {
                (char*)"ls-hwbp",
                (char*)"modules",
                (char*)"--target",
                (char*)target.c_str(),
            };
            cmd_modules(4, av);
        } else if (c == 5) {
            Args a;
            prompt_monitor_common(a);
            a.addr = prompt_u64("监控地址，支持十六进制，例如 0x1234: ", 0);
            a.type = parse_type(prompt_str("类型 [x执行/r读/w写/rw读写] 默认 x: ", "x"));
            a.len = prompt_int("长度 [1..8]，执行断点通常填4，默认4: ", 4);
            a.scope = parse_scope(prompt_str("线程范围 [main主线程/other子线程/all全部] 默认 all: ", "all"));

            if (!a.target.empty()) {
                set_monitor_connected(a);
            }
        } else if (c == 6) {
            Args a;
            prompt_monitor_common(a);

            int n = prompt_int("点位数量，最多16个，默认2: ", 2);
            if (n < 1) {
                n = 1;
            }
            if (n > MAX_POINTS) {
                n = MAX_POINTS;
            }

            for (int i = 0; i < n; i++) {
                PointSpec p;
                char prompt[128];

                snprintf(prompt, sizeof(prompt), "点位%d 地址: ", i);
                p.addr = prompt_u64(prompt, 0);

                snprintf(prompt, sizeof(prompt), "点位%d 类型 [x执行/r读/w写/rw读写] 默认 x: ", i);
                p.type = parse_type(prompt_str(prompt, "x"));

                snprintf(prompt, sizeof(prompt), "点位%d 长度 [1..8] 默认4: ", i);
                p.len = prompt_int(prompt, 4);

                snprintf(prompt, sizeof(prompt), "点位%d 线程范围 [main主线程/other子线程/all全部] 默认 all: ", i);
                p.scope = parse_scope(prompt_str(prompt, "all"));

                a.points.push_back(p);
            }

            if (!a.target.empty()) {
                set_monitor_connected(a);
            }
        } else if (c == 7) {
            puts("交互读取暂未开放，请使用命令行 read 子命令。");
        } else if (c == 8) {
            cmd_remove();
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
    if (c == "modules") {
        return cmd_modules(argc, argv);
    }
    if (c == "read") {
        return cmd_read(argc, argv);
    }
    if (c == "remove") {
        return cmd_remove();
    }
    if (c == "monitor" || c == "multi") {
        return cmd_monitor(argc, argv);
    }

    usage();
    return 2;
}