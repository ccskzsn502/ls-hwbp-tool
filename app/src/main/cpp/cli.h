#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include "protocol.h"

struct PointSpec { hwbp_type type=HWBP_BREAKPOINT_EMPTY; int len=0; hwbp_scope scope=SCOPE_ALL_THREADS; uint64_t addr=0; };
struct Args { std::string target,module,jsonl,bytes; uint64_t addr=0,offset=0; int seg=-999,len=0,interval=500,duration=0,max_print=32,size=16; hwbp_type type=HWBP_BREAKPOINT_EMPTY; hwbp_scope scope=SCOPE_ALL_THREADS; bool brief=false,all_regs=false; };

uint64_t parse_u64(const char* s);
hwbp_type parse_type(const std::string& s);
hwbp_scope parse_scope(const std::string& s);
bool parse_bytes(const std::string& text,std::vector<uint8_t>& out);

void usage();
Args parse_args(int argc,char** argv,int start=2);
uint64_t resolve_addr(const Args& a);

int read_memory(const std::string& target,uint64_t addr,int size);
int write_memory(const std::string& target,uint64_t addr,const std::vector<uint8_t>& bytes);
int set_monitor_connected(Args a);

int cmd_ping();
int cmd_info();
int cmd_remove();
int cmd_find(int argc,char** argv);
int cmd_read(int argc,char** argv);
int cmd_write(int argc,char** argv);
int cmd_monitor(int argc,char** argv);

int run_interactive();