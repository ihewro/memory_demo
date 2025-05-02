#include <iostream>
#include <windows.h>
#include <psapi.h>
#include <string>
#include <iomanip>

/*
 * Windows进程内存指标监控程序
 * 
 * 演示Windows任务管理器中各个内存指标：
 * - 虚拟地址已使用大小
 * - 已提交内存 (Committed)
 * - 私有提交 (Private Bytes)
 * - 总工作集 (Total Working Set)
 * - 私有工作集 (Private Working Set)
 * - 可共享工作集
 * - 已共享工作集
 */

// 格式化内存大小输出
std::string formatMemorySize(SIZE_T bytes) {
    const char* units[] = {"B", "KB", "MB", "GB"};
    int unitIndex = 0;
    double size = static_cast<double>(bytes);

    while (size >= 1024.0 && unitIndex < 3) {
        size /= 1024.0;
        unitIndex++;
    }

    char buffer[32];
    snprintf(buffer, sizeof(buffer), "%.2f %s", size, units[unitIndex]);
    return std::string(buffer);
}

// 打印当前进程的内存使用信息
void printProcessMemoryInfo() {
    HANDLE hProcess = GetCurrentProcess();
    PROCESS_MEMORY_COUNTERS_EX pmc;
    
    if (!GetProcessMemoryInfo(hProcess, (PROCESS_MEMORY_COUNTERS*)&pmc, sizeof(pmc))) {
        std::cerr << "获取进程内存信息失败，错误码: " << GetLastError() << std::endl;
        return;
    }

    // 获取工作集信息
    DWORD dwWorkingSetInfoSize = 0;
    PSAPI_WORKING_SET_INFORMATION wsInfo;
    
    // 首先获取所需的缓冲区大小
    if (!QueryWorkingSet(hProcess, &wsInfo, sizeof(PSAPI_WORKING_SET_INFORMATION))) {
        if (GetLastError() != ERROR_BAD_LENGTH) {
            std::cerr << "查询工作集信息失败，错误码: " << GetLastError() << std::endl;
            return;
        }
        
        // 获取所需的缓冲区大小
        // 增加额外的缓冲区空间以确保足够
        dwWorkingSetInfoSize = sizeof(PSAPI_WORKING_SET_INFORMATION) + 
                              ((pmc.WorkingSetSize / 4096) + 100) * sizeof(PSAPI_WORKING_SET_BLOCK);
        
        PSAPI_WORKING_SET_INFORMATION* pWSInfo = (PSAPI_WORKING_SET_INFORMATION*)malloc(dwWorkingSetInfoSize);
        if (!pWSInfo) {
            std::cerr << "内存分配失败" << std::endl;
            return;
        }
        
        // 确保缓冲区清零
        memset(pWSInfo, 0, dwWorkingSetInfoSize);
        
        // 再次查询工作集信息
        if (!QueryWorkingSet(hProcess, pWSInfo, dwWorkingSetInfoSize)) {
            std::cerr << "查询工作集详细信息失败，错误码: " << GetLastError() << std::endl;
            free(pWSInfo);
            return;
        }
        
        // 计算共享和私有页面数量
        SIZE_T sharedPages = 0;
        SIZE_T privatePages = 0;
        
        for (ULONG_PTR i = 0; i < pWSInfo->NumberOfEntries; i++) {
            // 检查页面是否共享
            if (pWSInfo->WorkingSetInfo[i].Shared) {
                sharedPages++;
            } else {
                privatePages++;
            }
        }
        
        SIZE_T sharedWorkingSetSize = sharedPages * 4096; // 4KB页面大小
        SIZE_T privateWorkingSetSize = privatePages * 4096;
        
        // 输出内存指标
        std::cout << "当前进程内存使用情况:" << std::endl;
        std::cout << "  - 虚拟地址已使用大小: " << formatMemorySize(pmc.PrivateUsage + pmc.WorkingSetSize) << std::endl;
        std::cout << "  - 已提交内存 (Committed): " << formatMemorySize(pmc.PagefileUsage) << std::endl;
        std::cout << "  - 私有提交 (Private Bytes): " << formatMemorySize(pmc.PrivateUsage) << std::endl;
        std::cout << "  - 总工作集 (Total Working Set): " << formatMemorySize(pmc.WorkingSetSize) << std::endl;
        std::cout << "  - 私有工作集 (Private Working Set): " << formatMemorySize(privateWorkingSetSize) << std::endl;
        std::cout << "  - 可共享工作集: " << formatMemorySize(pmc.WorkingSetSize - privateWorkingSetSize) << std::endl;
        std::cout << "  - 已共享工作集: " << formatMemorySize(sharedWorkingSetSize) << std::endl;
        
        free(pWSInfo);
    } else {
        // 这种情况不太可能发生，因为工作集通常很大
        std::cout << "当前进程内存使用情况:" << std::endl;
        std::cout << "  - 虚拟地址已使用大小: " << formatMemorySize(pmc.PrivateUsage + pmc.WorkingSetSize) << std::endl;
        std::cout << "  - 已提交内存 (Committed): " << formatMemorySize(pmc.PagefileUsage) << std::endl;
        std::cout << "  - 私有提交 (Private Bytes): " << formatMemorySize(pmc.PrivateUsage) << std::endl;
        std::cout << "  - 总工作集 (Total Working Set): " << formatMemorySize(pmc.WorkingSetSize) << std::endl;
        std::cout << "  - 私有工作集 (Private Working Set): " << formatMemorySize(pmc.WorkingSetSize) << " (估计值)" << std::endl;
        std::cout << "  - 可共享工作集: " << formatMemorySize(0) << " (无法获取详细信息)" << std::endl;
        std::cout << "  - 已共享工作集: " << formatMemorySize(0) << " (无法获取详细信息)" << std::endl;
    }
}

// 示例函数，展示如何使用printProcessMemoryInfo
void runMemoryMonitorDemo() {
    std::cout << "Windows进程内存指标监控程序" << std::endl;
    std::cout << "--------------------------------" << std::endl;
    
    // 分配一些内存以便观察
    const int MB = 1024 * 1024;
    char* buffer1 = new char[50 * MB]; // 分配50MB内存
    memset(buffer1, 1, 50 * MB); // 写入内存以确保它被提交
    
    // 打印内存信息
    printProcessMemoryInfo();
    
    std::cout << "\n按Enter键退出..." << std::endl;
    std::cin.get();
    
    // 释放内存
    delete[] buffer1;
}