#include "driver.h"
#include <sys/mman.h>
#include <sys/prctl.h>
#include <unistd.h>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <cinttypes>

req_obj* g_req = nullptr;

static bool wait_user(int ms){
    for(int i=0;i<ms;i++){
        if(g_req->user){ g_req->user=false; __sync_synchronize(); return true; }
        usleep(1000);
    }
    return false;
}

bool commit_req(sm_req_op op,int ms){
    g_req->op=op; __sync_synchronize();
    g_req->kernel=true; __sync_synchronize();
    return wait_user(ms);
}

bool connect_driver(){
    if(g_req) return true;
    if(prctl(PR_SET_NAME,"LS",0,0,0)!=0){ fprintf(stderr,"设置进程名失败: %s\n",strerror(errno)); return false; }
    size_t sz=sizeof(req_obj);
    void* p=mmap((void*)LS_SHARED_ADDR,sz,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS|MAP_FIXED,-1,0);
    if(p==MAP_FAILED){ fprintf(stderr,"映射共享内存失败: %s\n",strerror(errno)); return false; }
    g_req=(req_obj*)p; memset(g_req,0,sz); printf("共享内存: %p 大小: %zu\n",p,sz);
    if(!wait_user(10000)){
        g_req->op=op_o; g_req->kernel=true;
        if(!wait_user(10000)){ fprintf(stderr,"连接超时：驱动未加载或已有 LS 客户端占用\n"); return false; }
    }
    return true;
}

int select_target(const std::string& target){
    if(target.empty()){ fprintf(stderr,"目标不能为空\n"); return 1; }
    memset(&g_req->proc_info,0,sizeof(g_req->proc_info));
    g_req->pid=0;
    snprintf(g_req->proc_info.name,sizeof(g_req->proc_info.name),"%s",target.c_str());
    if(!commit_req(op_find_process_by_name,5000)){ fprintf(stderr,"查找进程超时\n"); return 1; }
    if(g_req->status!=0){ fprintf(stderr,"查找失败 status=%d target=%s\n",g_req->status,target.c_str()); return 1; }
    g_req->pid=g_req->proc_info.selected_pid;
    printf("内核已选择进程: PID=%d RSS=%" PRIu64 "KB 目标=%s\n",g_req->pid,g_req->proc_info.selected_rss_kb,target.c_str());
    return 0;
}

int load_memory_info(const std::string& target){
    if(select_target(target)) return 1;
    memset(&g_req->mem_info,0,sizeof(g_req->mem_info));
    if(!commit_req(op_m,15000)){ fprintf(stderr,"枚举内存超时\n"); return 1; }
    if(g_req->status!=0){ fprintf(stderr,"枚举内存失败 status=%d\n",g_req->status); return 1; }
    return 0;
}