#include "cli.h"
#include "driver.h"
#include "symbol.h"
#include "report.h"
#include <unistd.h>
#include <signal.h>
#include <ctype.h>
#include <cerrno>
#include <cinttypes>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <chrono>
#include <thread>
#include <algorithm>
#include <string>

static volatile sig_atomic_t g_stop=0;
static void on_sig(int){ g_stop=1; }

uint64_t parse_u64(const char* s){ char* e=nullptr; errno=0; uint64_t v=strtoull(s,&e,0); if(errno||!e||*e){fprintf(stderr,"数字无效: %s\n",s?s:"(null)"); exit(2);} return v; }
hwbp_type parse_type(const std::string& s){ if(s=="x")return HWBP_BREAKPOINT_X; if(s=="r")return HWBP_BREAKPOINT_R; if(s=="w")return HWBP_BREAKPOINT_W; if(s=="rw"||s=="wr")return HWBP_BREAKPOINT_RW; fprintf(stderr,"类型无效: %s\n",s.c_str()); exit(2); }
hwbp_scope parse_scope(const std::string& s){ if(s=="main")return SCOPE_MAIN_THREAD; if(s=="other")return SCOPE_OTHER_THREADS; if(s=="all")return SCOPE_ALL_THREADS; fprintf(stderr,"线程范围无效: %s\n",s.c_str()); exit(2); }
bool parse_bytes(const std::string& text,std::vector<uint8_t>& out){ size_t pos=0; while(pos<text.size()){ while(pos<text.size()&&isspace((unsigned char)text[pos]))pos++; if(pos>=text.size())break; size_t next=pos; while(next<text.size()&&!isspace((unsigned char)text[next]))next++; std::string item=text.substr(pos,next-pos); char* end=nullptr; errno=0; unsigned long v=strtoul(item.c_str(),&end,16); if(errno||!end||*end||v>0xff)return false; out.push_back((uint8_t)v); pos=next; } return !out.empty(); }

static bool prompt_line(const char* t,char* b,size_t n){ printf("%s",t); fflush(stdout); if(!fgets(b,n,stdin))return false; size_t l=strlen(b); if(l&&b[l-1]=='\n')b[l-1]=0; return true; }
static int prompt_int(const char* t,int d){ char b[128]; if(!prompt_line(t,b,sizeof(b))||!b[0])return d; return (int)parse_u64(b); }
static uint64_t prompt_u64(const char* t,uint64_t d){ char b[128]; if(!prompt_line(t,b,sizeof(b))||!b[0])return d; return parse_u64(b); }
static std::string prompt_str(const char* t,const char* d){ char b[512]; if(!prompt_line(t,b,sizeof(b))||!b[0])return d; return b; }

void usage(){
    puts("lsdriver 硬件断点工具");
    puts("  ls-hwbp ping | info | remove | mcp");
    puts("  ls-hwbp find --target 包名或PID");
    puts("  ls-hwbp read --target 目标 --addr ADDR --size N");
    puts("  ls-hwbp write --target 目标 --addr ADDR --bytes \"01 02 03 04\"");
    puts("  ls-hwbp monitor --target 目标 --type x|r|w|rw --addr ADDR --len 1..8 --scope main|other|all");
    puts("  ls-hwbp monitor --target 目标 --module lib.so --seg 0 --offset OFF --type x --len 4");
    puts("选项: --interval MS --duration SEC --brief --all-regs --max-print N --jsonl PATH");
}

Args parse_args(int argc,char** argv,int start){
    Args a;
    for(int i=start;i<argc;i++){ std::string k=argv[i]; auto val=[&](){ if(i+1>=argc){fprintf(stderr,"%s 缺少参数\n",k.c_str()); exit(2);} return argv[++i]; };
        if(k=="--target"||k=="--pid"||k=="--name")a.target=val(); else if(k=="--addr")a.addr=parse_u64(val()); else if(k=="--type")a.type=parse_type(val()); else if(k=="--len")a.len=(int)parse_u64(val()); else if(k=="--scope")a.scope=parse_scope(val()); else if(k=="--interval")a.interval=(int)parse_u64(val()); else if(k=="--duration")a.duration=(int)parse_u64(val()); else if(k=="--max-print")a.max_print=(int)parse_u64(val()); else if(k=="--size")a.size=(int)parse_u64(val()); else if(k=="--module")a.module=val(); else if(k=="--seg")a.seg=(int)parse_u64(val()); else if(k=="--offset")a.offset=parse_u64(val()); else if(k=="--bytes")a.bytes=val(); else if(k=="--jsonl")a.jsonl=val(); else if(k=="--brief")a.brief=true; else if(k=="--all-regs")a.all_regs=true; else { fprintf(stderr,"未知参数: %s\n",k.c_str()); exit(2); }}
    return a;
}

uint64_t resolve_addr(const Args& a){
    if(a.module.empty())return a.addr;
    if(load_memory_info(a.target))exit(1);
    for(int i=0;i<g_req->mem_info.module_count&&i<MAX_MODULES;i++){ module_info& m=g_req->mem_info.modules[i]; if(!strstr(m.name,a.module.c_str()))continue; for(int j=0;j<m.seg_count&&j<MAX_SEGS_PER_MODULE;j++){ segment_info& s=m.segs[j]; if(a.seg!=-999&&s.index!=a.seg)continue; printf("解析模块: %s seg=%d %c%c%c 0x%016" PRIx64 "+0x%" PRIx64 "\n",m.name,s.index,prot_char(s.prot,1,'r'),prot_char(s.prot,2,'w'),prot_char(s.prot,4,'x'),s.start,a.offset); return s.start+a.offset; }}
    fprintf(stderr,"未找到模块/段: %s seg=%d\n",a.module.c_str(),a.seg); exit(1);
}

int read_memory(const std::string& target,uint64_t addr,int size){
    if(size<1){fprintf(stderr,"读取大小无效\n");return 1;}
    if(select_target(target))return 1;
    int remain=size; uint64_t cur=addr;
    while(remain>0){ int n=remain>0x1000?0x1000:remain; memset(&g_req->rw_info,0,sizeof(g_req->rw_info)); g_req->rw_info.rw_addr=cur; g_req->rw_info.size=n; if(!commit_req(op_r,5000)){fprintf(stderr,"读取超时\n");return 1;} if(g_req->status<0){fprintf(stderr,"读取失败 status=%d\n",g_req->status);return 1;} print_hexdump(cur,g_req->rw_info.user_buffer,n); cur+=n; remain-=n;}
    return 0;
}

int write_memory(const std::string& target,uint64_t addr,const std::vector<uint8_t>& bytes){
    if(bytes.empty()||bytes.size()>0x1000){fprintf(stderr,"写入字节数必须是 1..4096\n");return 1;}
    if(select_target(target))return 1;
    memset(&g_req->rw_info,0,sizeof(g_req->rw_info)); g_req->rw_info.rw_addr=addr; g_req->rw_info.size=(int)bytes.size(); memcpy(g_req->rw_info.user_buffer,bytes.data(),bytes.size());
    if(!commit_req(op_w,5000)){fprintf(stderr,"写入超时\n");return 1;} if(g_req->status<0){fprintf(stderr,"写入失败 status=%d\n",g_req->status);return 1;}
    printf("写入完成: 0x%016" PRIx64 " size=%zu\n",addr,bytes.size()); return 0;
}

int set_monitor_connected(Args a){
    if(a.target.empty()){fprintf(stderr,"目标不能为空\n");return 1;} if(a.interval<50)a.interval=50;
    PointSpec s; s.type=a.type; s.len=a.len; s.scope=a.scope; s.addr=resolve_addr(a);
    if(!s.addr||s.len<1||s.len>8||s.type==HWBP_BREAKPOINT_EMPTY){fprintf(stderr,"点位无效\n");return 1;}
    memset(&g_req->bp_info,0,sizeof(g_req->bp_info)); memset(&g_req->proc_info,0,sizeof(g_req->proc_info)); g_req->pid=0; snprintf(g_req->proc_info.name,sizeof(g_req->proc_info.name),"%s",a.target.c_str());
    hwbp_point& p0=g_req->bp_info.points[0]; p0.bt=s.type; p0.bl=(hwbp_len)s.len; p0.bs=s.scope; p0.hit_addr=s.addr; p0.record_count=0;
    if(!commit_req(op_set_process_hwbp)){fprintf(stderr,"设置断点超时\n");return 1;} if(g_req->status!=0){fprintf(stderr,"设置失败 status=%d\n",g_req->status);return 1;}
    memset(&g_req->mem_info,0,sizeof(g_req->mem_info)); if(!commit_req(op_m,15000)||g_req->status!=0) fprintf(stderr,"警告：模块枚举失败，命中地址将只显示裸地址\n");
    printf("PID=%d RSS=%" PRIu64 "KB\n",g_req->pid,g_req->proc_info.selected_rss_kb);
    printf("点位#00 %s 0x%016" PRIx64 " len=%d scope=%s\n",type_name(s.type),s.addr,s.len,scope_name(s.scope));
    puts(a.duration>0?"正在监控...":"按 Ctrl+C 停止");
    FILE* jf=a.jsonl.empty()?nullptr:fopen(a.jsonl.c_str(),"a");
    signal(SIGINT,on_sig); signal(SIGTERM,on_sig);
    int last_count=-1; uint64_t last_hits[0x100]; memset(last_hits,0,sizeof(last_hits)); int elapsed=0;
    while(!g_stop){ hwbp_point& p=g_req->bp_info.points[0]; int c=p.record_count; if(c<0)c=0; if(c>0x100)c=0x100; bool changed=c!=last_count; for(int i=0;i<c;i++)if(p.records[i].hit_count!=last_hits[i]){changed=true; last_hits[i]=p.records[i].hit_count; append_json(jf,0,i,p.records[i]);} if(changed){ printf("\n========== 点位#00 命中记录: %d ==========" "\n",c); int limit=std::min(c,a.max_print); for(int i=0;i<limit;i++)print_rec(0,i,p.records[i],a.brief,a.all_regs); if(c>limit)printf("...省略%d条\n",c-limit); fflush(stdout); last_count=c; } std::this_thread::sleep_for(std::chrono::milliseconds(a.interval)); elapsed+=a.interval; if(a.duration>0&&elapsed>=a.duration*1000)break; }
    if(jf)fclose(jf);
    puts("\n正在删除断点...");
    commit_req(op_remove_process_hwbp,2000);
    return 0;
}

int cmd_ping(){ if(!connect_driver())return 1; if(!commit_req(op_o)){fprintf(stderr,"ping 超时\n");return 1;} puts("驱动响应正常"); return 0; }
int cmd_info(){ if(!connect_driver())return 1; memset(&g_req->bp_info,0,sizeof(g_req->bp_info)); if(!commit_req(op_brps_weps_info)){fprintf(stderr,"读取超时\n");return 1;} printf("执行断点槽位 BRP: %" PRIu64 "\n访问观察点槽位 WRP: %" PRIu64 "\n协议点位上限: %d\n",g_req->bp_info.num_brps,g_req->bp_info.num_wrps,MAX_POINTS); return 0; }
int cmd_remove(){ if(!connect_driver())return 1; if(!commit_req(op_remove_process_hwbp)){fprintf(stderr,"删除超时\n");return 1;} puts("已删除当前断点/观察点"); return 0; }
int cmd_find(int argc,char** argv){ if(!connect_driver())return 1; Args a=parse_args(argc,argv); return select_target(a.target); }
int cmd_read(int argc,char** argv){ if(!connect_driver())return 1; Args a=parse_args(argc,argv); uint64_t addr=resolve_addr(a); return read_memory(a.target,addr,a.size); }
int cmd_write(int argc,char** argv){ if(!connect_driver())return 1; Args a=parse_args(argc,argv); uint64_t addr=resolve_addr(a); std::vector<uint8_t> bytes; if(!parse_bytes(a.bytes,bytes)){fprintf(stderr,"--bytes 格式无效，例如: \"01 02 ff\"\n");return 2;} return write_memory(a.target,addr,bytes); }
int cmd_monitor(int argc,char** argv){ if(!connect_driver())return 1; return set_monitor_connected(parse_args(argc,argv)); }

static void print_interactive_menu(){
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
    puts("8) 启动 MCP 模式");
    puts("9) 退出");
}

static int start_mcp_bridge(){
    puts("\n正在启动 MCP 桥接服务...");
    int rc = system("pidfile=/data/local/tmp/ls-hwbp-mcp-nc.pid; "
                    "if [ -r $pidfile ]; then pid=$(cat $pidfile); if kill -0 $pid 2>/dev/null; then exit 0; fi; fi; "
                    "nohup sh -c 'while true; do /system/bin/nc -l -s 127.0.0.1 -p 37651 /data/local/tmp/ls-hwbp mcp; sleep 1; done' >/data/local/tmp/ls-hwbp-mcp-nc.log 2>&1 & "
                    "echo $! > $pidfile");
    if (rc != 0) {
        puts("启动 MCP 桥接服务失败，请检查 /system/bin/nc 和 /data/local/tmp/ls-hwbp");
        return 1;
    }
    puts("MCP 桥接服务已启动: 127.0.0.1:37651");
    puts("现在可以回到 Operit 里使用 LS HWBP / ls_hwbp 工具。若重启手机，需要重新选择 8 启动一次。");
    return 0;
}

static bool ensure_target(std::string& current_target){
    std::string prompt = current_target.empty() ? "包名/进程名/PID: " : "包名/进程名/PID，回车复用当前目标: ";
    std::string input = prompt_str(prompt.c_str(), current_target.c_str());
    if (!input.empty()) current_target = input;
    if (current_target.empty()) { puts("当前目标为空"); return false; }
    return true;
}

static void prompt_monitor_common(Args& a, std::string& current_target){
    if (!ensure_target(current_target)) return;
    a.target = current_target;
    a.interval = prompt_int("刷新间隔毫秒，默认500: ", 500);
    a.brief = true;
}

int run_interactive(){
    puts("启动中文交互界面...");
    if (!connect_driver()) return 1;
    std::string current_target;
    for (;;) {
        print_interactive_menu();
        int c = prompt_int("请选择: ", 0);
        if (c == 1) { cmd_ping(); }
        else if (c == 2) { cmd_info(); }
        else if (c == 3) { if (ensure_target(current_target)) select_target(current_target); }
        else if (c == 4) {
            Args a; prompt_monitor_common(a, current_target);
            if (a.target.empty()) continue;
            a.addr = prompt_u64("监控地址，支持十六进制，例如 0x1234: ", 0);
            a.type = parse_type(prompt_str("类型 [x执行/r读/w写/rw读写] 默认 x: ", "x"));
            a.len = prompt_int("长度 [1..8]，执行断点通常填4，默认4: ", 4);
            a.scope = parse_scope(prompt_str("线程范围 [main主线程/other子线程/all全部] 默认 all: ", "all"));
            set_monitor_connected(a);
        }
        else if (c == 5) {
            if (!ensure_target(current_target)) continue;
            uint64_t addr = prompt_u64("读取地址，支持十六进制，例如 0x1234: ", 0);
            int size = prompt_int("读取大小，默认64: ", 64);
            read_memory(current_target, addr, size);
        }
        else if (c == 6) {
            if (!ensure_target(current_target)) continue;
            uint64_t addr = prompt_u64("写入地址，支持十六进制，例如 0x1234: ", 0);
            std::string text = prompt_str("写入字节，例如 01 02 03 04: ", "");
            std::vector<uint8_t> bytes;
            if (!parse_bytes(text, bytes)) { puts("字节格式无效"); continue; }
            printf("确认写入 目标=%s 地址=0x%016" PRIx64 " 字节数=%zu，输入 YES 继续: ", current_target.c_str(), addr, bytes.size());
            char confirm[32];
            if (!fgets(confirm, sizeof(confirm), stdin) || strncmp(confirm, "YES", 3) != 0) { puts("已取消写入"); continue; }
            write_memory(current_target, addr, bytes);
        }
        else if (c == 7) { cmd_remove(); }
        else if (c == 8) { start_mcp_bridge(); }
        else if (c == 9) { puts("退出"); break; }
        else { puts("未知选项，请重新输入。"); }
    }
    return 0;
}