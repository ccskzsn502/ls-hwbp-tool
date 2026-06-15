#pragma once
#include <cstdint>
#include <string>
#include "protocol.h"

// 全局共享内存请求对象（连接成功后有效）。
extern req_obj* g_req;

// 连接驱动并映射共享内存。重复调用安全。
bool connect_driver();

// 底层通信：提交一个操作并等待内核应答。
bool commit_req(sm_req_op op, int ms = 5000);

// 选择目标进程（按包名/进程名/PID 文本）。成功后 g_req->pid 有效。返回 0 成功。
int select_target(const std::string& target);

// 枚举目标进程的模块/内存段信息到 g_req->mem_info。返回 0 成功。
int load_memory_info(const std::string& target);