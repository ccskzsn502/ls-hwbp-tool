#include "symbol.h"
#include "driver.h"
#include "protocol.h"
#include <cstdio>
#include <cstring>

static std::string base_name(const char* path){ const char* p=strrchr(path,'/'); return p?p+1:path; }
static const char* seg_name(int seg,char* buf,size_t n){ if(seg==-1)return "bss"; snprintf(buf,n,"%d",seg); return buf; }

AddrSymbol resolve_symbol(uint64_t addr){
    AddrSymbol best; uint64_t best_size=~0ULL;
    if(!g_req) return best;
    memory_info& mi=g_req->mem_info;
    for(int i=0;i<mi.module_count&&i<MAX_MODULES;i++){
        module_info& m=mi.modules[i];
        for(int j=0;j<m.seg_count&&j<MAX_SEGS_PER_MODULE;j++){
            segment_info& s=m.segs[j];
            if(addr>=s.start&&addr<s.end){
                uint64_t size=s.end-s.start;
                if(size<best_size){
                    best.ok=true; best.name=base_name(m.name); best.seg=s.index;
                    best.offset=addr-s.start; best.start=s.start; best.end=s.end; best_size=size;
                }
            }
        }
    }
    return best;
}

std::string format_symbol(uint64_t addr){
    AddrSymbol sym=resolve_symbol(addr);
    char buf[512];
    if(!sym.ok) return "";
    char segbuf[32];
    const char* seg=seg_name(sym.seg,segbuf,sizeof(segbuf));
    if(sym.name.rfind("anon:",0)==0 || sym.name.rfind("[anon:",0)==0)
        snprintf(buf,sizeof(buf),"%s+0x%llx",sym.name.c_str(),(unsigned long long)sym.offset);
    else if(sym.seg==0)
        snprintf(buf,sizeof(buf),"%s+0x%llx",sym.name.c_str(),(unsigned long long)sym.offset);
    else
        snprintf(buf,sizeof(buf),"%s[%s]+0x%llx",sym.name.c_str(),seg,(unsigned long long)sym.offset);
    return buf;
}