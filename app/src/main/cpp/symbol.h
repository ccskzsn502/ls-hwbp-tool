#pragma once
#include <cstdint>
#include <string>

struct AddrSymbol { std::string name; int seg=-999; uint64_t offset=0; uint64_t start=0; uint64_t end=0; bool ok=false; };

// 基于 g_req->mem_info 已枚举的模块段，将地址解析到最贴近的模块段。
AddrSymbol resolve_symbol(uint64_t addr);

// 返回 "libxxx.so+0x偏移" 形式；无法解析返回空字符串。
std::string format_symbol(uint64_t addr);