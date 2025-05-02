#ifndef MEMORY_INFO_H
#define MEMORY_INFO_H

#include <iostream>
#include <string>
#include <mach/mach.h>
#include <mach/mach_vm.h>

// 格式化内存大小输出
std::string formatMemorySize(uint64_t bytes);

// 获取进程共享内存大小
uint64_t getProcessSharedMemory(pid_t pid);

// 打印当前进程的内存使用信息
void print_memory_info();

#endif // MEMORY_INFO_H