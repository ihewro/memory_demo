#ifndef PROCESS_MEMORY_MONITOR_H
#define PROCESS_MEMORY_MONITOR_H

#include <windows.h>
#include <string>

/**
 * Windows进程内存指标监控
 * 
 * 提供Windows进程内存使用情况的监控功能，包括以下指标：
 * - 虚拟地址已使用大小
 * - 已提交内存 (Committed)
 * - 私有提交 (Private Bytes)
 * - 总工作集 (Total Working Set)
 * - 私有工作集 (Private Working Set)
 * - 可共享工作集
 * - 已共享工作集
 */

/**
 * 格式化内存大小输出
 * 
 * @param bytes 内存字节数
 * @return 格式化后的内存大小字符串，如"10.24 MB"
 */
std::string formatMemorySize(SIZE_T bytes);

/**
 * 打印当前进程的内存使用信息
 * 
 * 获取并显示当前进程的各项内存使用指标，包括工作集、提交内存等
 */
void printProcessMemoryInfo();

#endif // PROCESS_MEMORY_MONITOR_H