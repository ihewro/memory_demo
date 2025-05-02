#include <iostream>
#include <windows.h>
#include <psapi.h>
#include <string>
#include <vector>
#include <thread>
#include <chrono>
#include "process_memory_monitor.h"

/**
 * Windows进程内存指标演示程序
 * 
 * 本程序通过不同的内存分配方式，分别演示各种Windows内存指标的增长：
 * - 虚拟地址已使用大小
 * - 已提交内存 (Committed)
 * - 私有提交 (Private Bytes)
 * - 总工作集 (Total Working Set)
 * - 私有工作集 (Private Working Set)
 * - 可共享工作集
 * - 已共享工作集
 */

// 等待用户按Enter继续
void waitForUser() {
    std::cout << "\n按Enter键继续..." << std::endl;
    std::cin.get();
}

// 1. 演示虚拟地址保留 - 影响"虚拟地址已使用大小"指标
void demoVirtualAddressReserve(size_t sizeMB) {
    std::cout << "\n=== 演示虚拟地址保留 (Reserve) ===" << std::endl;
    std::cout << "保留" << sizeMB << "MB的虚拟地址空间..." << std::endl;
    std::cout << "这将增加'虚拟地址已使用大小'指标，但不会增加'已提交'指标" << std::endl;
    
    // 打印分配前的内存信息
    std::cout << "\n分配前:" << std::endl;
    printProcessMemoryInfo();
    
    // 使用VirtualAlloc保留虚拟地址空间，但不提交
    const size_t sizeBytes = sizeMB * 1024 * 1024;
    void* reservedMem = VirtualAlloc(NULL, sizeBytes, MEM_RESERVE, PAGE_NOACCESS);
    
    if (reservedMem == NULL) {
        std::cerr << "虚拟地址保留失败，错误码: " << GetLastError() << std::endl;
        return;
    }
    
    // 打印分配后的内存信息
    std::cout << "\n分配后:" << std::endl;
    printProcessMemoryInfo();
    
    std::cout << "已保留" << sizeMB << "MB的虚拟地址空间" << std::endl;
    std::cout << "请观察'虚拟地址已使用大小'指标的增加" << std::endl;
    waitForUser();
    
    // 释放保留的内存
    VirtualFree(reservedMem, 0, MEM_RELEASE);
}

// 2. 演示内存提交 - 影响"已提交内存"和"私有提交"指标
void demoCommittedMemory(size_t sizeMB) {
    std::cout << "\n=== 演示内存提交 (Commit) ===" << std::endl;
    std::cout << "提交" << sizeMB << "MB的内存..." << std::endl;
    std::cout << "这将增加'已提交内存'和'私有提交'指标" << std::endl;
    
    // 打印分配前的内存信息
    std::cout << "\n分配前:" << std::endl;
    printProcessMemoryInfo();
    
    // 使用VirtualAlloc保留并提交内存
    const size_t sizeBytes = sizeMB * 1024 * 1024;
    void* committedMem = VirtualAlloc(NULL, sizeBytes, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    
    if (committedMem == NULL) {
        std::cerr << "内存提交失败，错误码: " << GetLastError() << std::endl;
        return;
    }
    
    // 打印分配后的内存信息
    std::cout << "\n分配后:" << std::endl;
    printProcessMemoryInfo();
    
    std::cout << "已提交" << sizeMB << "MB的内存" << std::endl;
    std::cout << "请观察'已提交内存'和'私有提交'指标的增加" << std::endl;
    waitForUser();
    
    // 释放提交的内存
    VirtualFree(committedMem, 0, MEM_RELEASE);
}

// 3. 演示私有工作集 - 影响"私有工作集"和"总工作集"指标
void demoPrivateWorkingSet(size_t sizeMB) {
    std::cout << "\n=== 演示私有工作集 (Private Working Set) ===" << std::endl;
    std::cout << "分配并访问" << sizeMB << "MB的内存..." << std::endl;
    std::cout << "这将增加'私有工作集'和'总工作集'指标" << std::endl;
    
    // 打印分配前的内存信息
    std::cout << "\n分配前:" << std::endl;
    printProcessMemoryInfo();
    
    // 分配内存并访问以确保它进入工作集
    const size_t sizeBytes = sizeMB * 1024 * 1024;
    char* buffer = new char[sizeBytes];
    
    // 写入数据以确保页面被加载到物理内存
    for (size_t i = 0; i < sizeBytes; i += 4096) { // 每页4KB
        buffer[i] = 'A';
    }
    
    // 打印分配后的内存信息
    std::cout << "\n分配后:" << std::endl;
    printProcessMemoryInfo();
    
    std::cout << "已分配并访问" << sizeMB << "MB的内存" << std::endl;
    std::cout << "请观察'私有工作集'和'总工作集'指标的增加" << std::endl;
    waitForUser();
    
    // 释放内存
    delete[] buffer;
}

// 4. 演示共享内存 - 影响"可共享工作集"和"已共享工作集"指标
void demoSharedMemory(size_t sizeMB) {
    std::cout << "\n=== 演示共享内存 (Shared Memory) ===" << std::endl;
    std::cout << "创建" << sizeMB << "MB的内存映射文件..." << std::endl;
    std::cout << "这将增加'可共享工作集'指标" << std::endl;
    
    // 打印分配前的内存信息
    std::cout << "\n分配前:" << std::endl;
    printProcessMemoryInfo();
    
    const size_t sizeBytes = sizeMB * 1024 * 1024;
    
    // 创建一个内存映射文件
    HANDLE hMapFile = CreateFileMapping(
        INVALID_HANDLE_VALUE,    // 使用分页文件
        NULL,                    // 默认安全属性
        PAGE_READWRITE,          // 读写权限
        0,                       // 最大大小高32位
        static_cast<DWORD>(sizeBytes), // 最大大小低32位
        "SharedMemoryDemo");   // 映射文件名称
    
    if (hMapFile == NULL) {
        std::cerr << "创建内存映射文件失败，错误码: " << GetLastError() << std::endl;
        return;
    }
    
    // 映射视图
    LPVOID pBuf = MapViewOfFile(
        hMapFile,                // 内存映射文件句柄
        FILE_MAP_ALL_ACCESS,     // 读写权限
        0,                       // 文件偏移高32位
        0,                       // 文件偏移低32位
        sizeBytes);              // 映射大小
    
    if (pBuf == NULL) {
        std::cerr << "映射视图失败，错误码: " << GetLastError() << std::endl;
        CloseHandle(hMapFile);
        return;
    }
    
    // 写入数据以确保页面被加载到物理内存
    char* buffer = static_cast<char*>(pBuf);
    for (size_t i = 0; i < sizeBytes; i += 4096) { // 每页4KB
        buffer[i] = 'S';
    }
    
    // 打印分配后的内存信息
    std::cout << "\n分配后:" << std::endl;
    printProcessMemoryInfo();
    
    std::cout << "已创建" << sizeMB << "MB的内存映射文件" << std::endl;
    std::cout << "请观察'可共享工作集'指标的增加" << std::endl;
    waitForUser();
    
    // 清理
    UnmapViewOfFile(pBuf);
    CloseHandle(hMapFile);
}

int main() {
    std::cout << "Windows进程内存指标演示程序" << std::endl;
    std::cout << "--------------------------------" << std::endl;
    std::cout << "本程序将演示不同内存分配方式对各种内存指标的影响" << std::endl;
    std::cout << "每种分配方式将增加约100MB的相应内存指标" << std::endl;
    std::cout << "在每次分配之间，程序会暂停，以便您观察指标变化" << std::endl;
    
    // 打印初始内存信息
    std::cout << "\n初始内存状态:" << std::endl;
    printProcessMemoryInfo();
    waitForUser();
    
    // 1. 演示虚拟地址保留 - 影响"虚拟地址已使用大小"指标
    demoVirtualAddressReserve(100);
    
    // 2. 演示内存提交 - 影响"已提交内存"和"私有提交"指标
    demoCommittedMemory(100);
    
    // 3. 演示私有工作集 - 影响"私有工作集"和"总工作集"指标
    demoPrivateWorkingSet(100);
    
    // 4. 演示共享内存 - 影响"可共享工作集"和"已共享工作集"指标
    demoSharedMemory(100);
    
    std::cout << "\n所有演示已完成" << std::endl;
    std::cout << "最终内存状态:" << std::endl;
    printProcessMemoryInfo();
    
    std::cout << "\n按Enter键退出..." << std::endl;
    std::cin.get();
    
    return 0;
}