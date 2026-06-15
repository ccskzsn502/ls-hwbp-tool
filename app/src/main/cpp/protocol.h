#pragma once
#include <cstdint>

// 与内核 lsdriver 共享内存协议保持严格一致，禁止改动字段顺序与大小。
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
