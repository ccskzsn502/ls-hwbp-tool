#pragma once
#include <cstdint>
#include <cstdio>
#include "protocol.h"

const char* type_name(hwbp_type t);
const char* scope_name(hwbp_scope s);
char prot_char(uint8_t prot,uint8_t bit,char c);

void print_reg_line(const char* name,uint64_t value);
void print_kernel_stack(const hwbp_record& r);
void print_rec(int point,int idx,const hwbp_record& r,bool brief,bool all);
void print_hexdump(uint64_t base,const uint8_t* data,int size);
void append_json(FILE* f,int p,int i,const hwbp_record& r);