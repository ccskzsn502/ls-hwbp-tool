#include "report.h"
#include "symbol.h"
#include <cinttypes>
#include <string>

const char* type_name(hwbp_type t){ switch(t){case HWBP_BREAKPOINT_R:return"r";case HWBP_BREAKPOINT_W:return"w";case HWBP_BREAKPOINT_RW:return"rw";case HWBP_BREAKPOINT_X:return"x";default:return"?";} }
const char* scope_name(hwbp_scope s){ switch(s){case SCOPE_MAIN_THREAD:return"main";case SCOPE_OTHER_THREADS:return"other";case SCOPE_ALL_THREADS:return"all";default:return"?";} }
char prot_char(uint8_t prot,uint8_t bit,char c){ return (prot&bit)?c:'-'; }

static void print_resolved_value(uint64_t value){ std::string sym=format_symbol(value); if(!sym.empty()) printf(" ← %s",sym.c_str()); }

void print_reg_line(const char* name,uint64_t value){ printf("%-6s 0x%llx",name,(unsigned long long)value); print_resolved_value(value); puts(""); }

static void print_stack_line(int idx,uint64_t addr){ std::string sym=format_symbol(addr); if(sym.empty()) printf("#%-4d0x%llx\n",idx,(unsigned long long)addr); else printf("#%-4d0x%llx (%s)\n",idx,(unsigned long long)addr,sym.c_str()); }

void print_kernel_stack(const hwbp_record& r){
    puts("\n堆栈:");
    uint32_t count=r.stack_count; if(count>256)count=256;
    if(count==0){ puts("无内核栈记录"); return; }
    for(uint32_t i=0;i<count;i++) print_stack_line((int)i,r.stack[i]);
}

void print_rec(int point,int idx,const hwbp_record& r,bool brief,bool all){
    (void)point; (void)brief; (void)all;
    printf("\n#%d t:%llu\n",idx+1,(unsigned long long)r.hit_count);
    puts("----------------------------------------");
    print_reg_line("X0",r.x0); print_reg_line("X1",r.x1); print_reg_line("X2",r.x2); print_reg_line("X3",r.x3);
    print_reg_line("X4",r.x4); print_reg_line("X5",r.x5); print_reg_line("X6",r.x6); print_reg_line("X7",r.x7);
    print_reg_line("X8",r.x8); print_reg_line("X9",r.x9); print_reg_line("X10",r.x10); print_reg_line("X11",r.x11);
    print_reg_line("X12",r.x12); print_reg_line("X13",r.x13); print_reg_line("X14",r.x14); print_reg_line("X15",r.x15);
    print_reg_line("X16",r.x16); print_reg_line("X17",r.x17); print_reg_line("X18",r.x18); print_reg_line("X19",r.x19);
    print_reg_line("X20",r.x20); print_reg_line("X21",r.x21); print_reg_line("X22",r.x22); print_reg_line("X23",r.x23);
    print_reg_line("X24",r.x24); print_reg_line("X25",r.x25); print_reg_line("X26",r.x26); print_reg_line("X27",r.x27);
    print_reg_line("X28",r.x28); print_reg_line("X29",r.x29);
    print_reg_line("LR",r.lr); print_reg_line("SP",r.sp); print_reg_line("PC",r.pc);
    print_kernel_stack(r);
    puts("----------------------------------------");
}

void print_hexdump(uint64_t base,const uint8_t* data,int size){
    for(int i=0;i<size;i++){ if((i&15)==0)printf("\n0x%016" PRIx64 ": ",base+i); printf("%02x ",data[i]); }
    puts("");
}

void append_json(FILE* f,int p,int i,const hwbp_record& r){
    if(!f)return;
    fprintf(f,"{\"point\":%d,\"index\":%d,\"hits\":%" PRIu64 ",\"pc\":\"0x%016" PRIx64 "\",\"lr\":\"0x%016" PRIx64 "\",\"sp\":\"0x%016" PRIx64 "\"}\n",p,i,r.hit_count,r.pc,r.lr,r.sp);
    fflush(f);
}