#include <iostream>
#include <windows.h>
#include <psapi.h>
#include <string>
#include <fstream>
#include <vector>
#include "process_memory_monitor.h"

/**
 * Windows进程内存分配方式演示程序
 * 
 * 本程序演示四种不同的内存分配方式，并观察各种内存指标的变化：
 * 1. 匿名私有内存 - 通过VirtualAlloc或new分配的私有内存
 * 2. 匿名共享内存 - 通过CreateFileMapping创建的共享内存（不基于文件）
 * 3. 文件映射私有内存 - 通过MapViewOfFile映射文件到内存（私有方式）
 * 4. 文件映射共享内存 - 通过MapViewOfFile映射文件到内存（共享方式）
 */

// 等待用户按Enter继续
void waitForUser() {
    std::cout << "\n按Enter键继续..." << std::endl;
    std::cin.get();
}

// 创建临时文件用于文件映射演示
std::string createTempFile(size_t sizeBytes) {
    char tempPath[MAX_PATH];
    char tempFileName[MAX_PATH];
    
    // 获取临时文件路径
    GetTempPathA(MAX_PATH, tempPath);
    GetTempFileNameA(tempPath, "mem", 0, tempFileName);
    
    // 创建并填充临时文件
    std::ofstream file(tempFileName, std::ios::binary);
    if (!file) {
        std::cerr << "创建临时文件失败" << std::endl;
        return "";
    }
    
    // 写入数据到文件
    std::vector<char> buffer(4096, 'F'); // 4KB块
    for (size_t i = 0; i < sizeBytes; i += buffer.size()) {
        size_t writeSize = (i + buffer.size() > sizeBytes) ? (sizeBytes - i) : buffer.size();
        file.write(buffer.data(), writeSize);
    }
    
    file.close();
    return std::string(tempFileName);
}

// 1. 演示匿名私有内存分配
void demoAnonymousPrivate(size_t sizeMB) {
    std::cout << "\n=== 演示匿名私有内存分配 ===" << std::endl;
    std::cout << "分配" << sizeMB << "MB的匿名私有内存..." << std::endl;
    std::cout << "这种分配方式会影响'私有工作集'和'私有提交'指标" << std::endl;
    
    // 打印分配前的内存信息
    std::cout << "\n分配前:" << std::endl;
    printProcessMemoryInfo();
    
    // 使用VirtualAlloc分配私有内存
    const size_t sizeBytes = sizeMB * 1024 * 1024;
    void* privateMem = VirtualAlloc(NULL, sizeBytes, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    
    if (privateMem == NULL) {
        std::cerr << "匿名私有内存分配失败，错误码: " << GetLastError() << std::endl;
        return;
    }
    
    // 访问内存以确保它进入工作集
    char* buffer = static_cast<char*>(privateMem);
    for (size_t i = 0; i < sizeBytes; i += 4096) { // 每页4KB
        buffer[i] = 'P';
    }
    
    // 打印分配后的内存信息
    std::cout << "\n分配后:" << std::endl;
    printProcessMemoryInfo();
    
    std::cout << "已分配并访问" << sizeMB << "MB的匿名私有内存" << std::endl;
    std::cout << "请观察'私有工作集'和'私有提交'指标的增加" << std::endl;
    waitForUser();
    
    // 释放内存
    VirtualFree(privateMem, 0, MEM_RELEASE);
}

// 2. 演示匿名共享内存分配
void demoAnonymousShared(size_t sizeMB) {
    std::cout << "\n=== 演示匿名共享内存分配 ===" << std::endl;
    std::cout << "分配" << sizeMB << "MB的匿名共享内存..." << std::endl;
    std::cout << "这种分配方式会影响'可共享工作集'指标" << std::endl;
    
    // 打印分配前的内存信息
    std::cout << "\n分配前:" << std::endl;
    printProcessMemoryInfo();
    
    const size_t sizeBytes = sizeMB * 1024 * 1024;
    
    // 创建一个匿名共享内存映射对象
    HANDLE hMapFile = CreateFileMapping(
        INVALID_HANDLE_VALUE,    // 使用分页文件（匿名）
        NULL,                    // 默认安全属性
        PAGE_READWRITE,          // 读写权限
        0,                       // 最大大小高32位
        static_cast<DWORD>(sizeBytes), // 最大大小低32位
        "AnonymousSharedDemo");  // 映射名称
    
    if (hMapFile == NULL) {
        std::cerr << "创建匿名共享内存映射失败，错误码: " << GetLastError() << std::endl;
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
    
    std::cout << "已分配并访问" << sizeMB << "MB的匿名共享内存" << std::endl;
    std::cout << "请观察'可共享工作集'指标的增加" << std::endl;
    waitForUser();
    
    // 清理
    UnmapViewOfFile(pBuf);
    CloseHandle(hMapFile);
}

// 3. 演示文件映射私有内存分配
void demoFileMappedPrivate(size_t sizeMB) {
    std::cout << "\n=== 演示文件映射私有内存分配 ===" << std::endl;
    std::cout << "分配" << sizeMB << "MB的文件映射私有内存..." << std::endl;
    std::cout << "这种分配方式会影响'私有工作集'指标" << std::endl;
    
    // 打印分配前的内存信息
    std::cout << "\n分配前:" << std::endl;
    printProcessMemoryInfo();
    
    const size_t sizeBytes = sizeMB * 1024 * 1024;
    
    // 创建临时文件
    std::string tempFileName = createTempFile(sizeBytes);
    if (tempFileName.empty()) {
        return;
    }
    
    // 打开文件
    HANDLE hFile = CreateFileA(
        tempFileName.c_str(),    // 文件名
        GENERIC_READ | GENERIC_WRITE, // 读写权限
        0,                       // 不共享
        NULL,                    // 默认安全属性
        OPEN_EXISTING,           // 打开已存在的文件
        FILE_ATTRIBUTE_NORMAL,   // 正常文件属性
        NULL);                   // 无模板
    
    if (hFile == INVALID_HANDLE_VALUE) {
        std::cerr << "打开文件失败，错误码: " << GetLastError() << std::endl;
        DeleteFileA(tempFileName.c_str());
        return;
    }
    
    // 创建文件映射对象
    HANDLE hMapFile = CreateFileMapping(
        hFile,                   // 文件句柄
        NULL,                    // 默认安全属性
        PAGE_READONLY,           // 只读权限
        0,                       // 最大大小高32位
        0,                       // 使用文件大小
        NULL);                   // 无名称
    
    if (hMapFile == NULL) {
        std::cerr << "创建文件映射失败，错误码: " << GetLastError() << std::endl;
        CloseHandle(hFile);
        DeleteFileA(tempFileName.c_str());
        return;
    }
    
    // 以私有方式映射视图
    LPVOID pBuf = MapViewOfFile(
        hMapFile,                // 内存映射文件句柄
        FILE_MAP_COPY,           // 私有映射（写时复制）
        0,                       // 文件偏移高32位
        0,                       // 文件偏移低32位
        sizeBytes);              // 映射大小
    
    if (pBuf == NULL) {
        std::cerr << "映射视图失败，错误码: " << GetLastError() << std::endl;
        CloseHandle(hMapFile);
        CloseHandle(hFile);
        DeleteFileA(tempFileName.c_str());
        return;
    }
    
    // 修改内存以触发写时复制
    char* buffer = static_cast<char*>(pBuf);
    for (size_t i = 0; i < sizeBytes; i += 4096) { // 每页4KB
        buffer[i] = 'P'; // 修改会导致私有副本
    }
    
    // 打印分配后的内存信息
    std::cout << "\n分配后:" << std::endl;
    printProcessMemoryInfo();
    
    std::cout << "已分配并修改" << sizeMB << "MB的文件映射私有内存" << std::endl;
    std::cout << "请观察'私有工作集'指标的增加" << std::endl;
    waitForUser();
    
    // 清理
    UnmapViewOfFile(pBuf);
    CloseHandle(hMapFile);
    CloseHandle(hFile);
    DeleteFileA(tempFileName.c_str());
}

// 4. 演示文件映射共享内存分配
void demoFileMappedShared(size_t sizeMB) {
    std::cout << "\n=== 演示文件映射共享内存分配 ===" << std::endl;
    std::cout << "分配" << sizeMB << "MB的文件映射共享内存..." << std::endl;
    std::cout << "这种分配方式会影响'可共享工作集'指标" << std::endl;
    
    // 打印分配前的内存信息
    std::cout << "\n分配前:" << std::endl;
    printProcessMemoryInfo();
    
    const size_t sizeBytes = sizeMB * 1024 * 1024;
    
    // 创建临时文件
    std::string tempFileName = createTempFile(sizeBytes);
    if (tempFileName.empty()) {
        return;
    }
    
    // 打开文件
    HANDLE hFile = CreateFileA(
        tempFileName.c_str(),    // 文件名
        GENERIC_READ | GENERIC_WRITE, // 读写权限
        FILE_SHARE_READ | FILE_SHARE_WRITE, // 共享
        NULL,                    // 默认安全属性
        OPEN_EXISTING,           // 打开已存在的文件
        FILE_ATTRIBUTE_NORMAL,   // 正常文件属性
        NULL);                   // 无模板
    
    if (hFile == INVALID_HANDLE_VALUE) {
        std::cerr << "打开文件失败，错误码: " << GetLastError() << std::endl;
        DeleteFileA(tempFileName.c_str());
        return;
    }
    
    // 创建文件映射对象
    HANDLE hMapFile = CreateFileMapping(
        hFile,                   // 文件句柄
        NULL,                    // 默认安全属性
        PAGE_READWRITE,          // 读写权限
        0,                       // 最大大小高32位
        0,                       // 使用文件大小
        "FileMappedSharedDemo"); // 映射名称
    
    if (hMapFile == NULL) {
        std::cerr << "创建文件映射失败，错误码: " << GetLastError() << std::endl;
        CloseHandle(hFile);
        DeleteFileA(tempFileName.c_str());
        return;
    }
    
    // 以共享方式映射视图
    LPVOID pBuf = MapViewOfFile(
        hMapFile,                // 内存映射文件句柄
        FILE_MAP_ALL_ACCESS,     // 读写权限（共享）
        0,                       // 文件偏移高32位
        0,                       // 文件偏移低32位
        sizeBytes);              // 映射大小
    
    if (pBuf == NULL) {
        std::cerr << "映射视图失败，错误码: " << GetLastError() << std::endl;
        CloseHandle(hMapFile);
        CloseHandle(hFile);
        DeleteFileA(tempFileName.c_str());
        return;
    }
    
    // 访问内存以确保页面被加载到物理内存
    char* buffer = static_cast<char*>(pBuf);
    for (size_t i = 0; i < sizeBytes; i += 4096) { // 每页4KB
        buffer[i] = 'S';
    }
    
    // 打印分配后的内存信息
    std::cout << "\n分配后:" << std::endl;
    printProcessMemoryInfo();
    
    std::cout << "已分配并访问" << sizeMB << "MB的文件映射共享内存" << std::endl;
    std::cout << "请观察'可共享工作集'指标的增加" << std::endl;
    waitForUser();
    
    // 清理
    UnmapViewOfFile(pBuf);
    CloseHandle(hMapFile);
    CloseHandle(hFile);
    DeleteFileA(tempFileName.c_str());
}

int main() {
    std::cout << "Windows进程内存分配方式演示程序" << std::endl;
    std::cout << "--------------------------------" << std::endl;
    std::cout << "本程序将演示四种不同的内存分配方式对各种内存指标的影响：" << std::endl;
    std::cout << "1. 匿名私有内存 - 通过VirtualAlloc或new分配的私有内存" << std::endl;
    std::cout << "2. 匿名共享内存 - 通过CreateFileMapping创建的共享内存（不基于文件）" << std::endl;
    std::cout << "3. 文件映射私有内存 - 通过MapViewOfFile映射文件到内存（私有方式）" << std::endl;
    std::cout << "4. 文件映射共享内存 - 通过MapViewOfFile映射文件到内存（共享方式）" << std::endl;
    std::cout << "\n每种分配方式将分配约50MB的内存" << std::endl;
    std::cout << "在每次分配之间，程序会暂停，以便您观察指标变化" << std::endl;
    
    // 打印初始内存信息
    std::cout << "\n初始内存状态:" << std::endl;
    printProcessMemoryInfo();
    waitForUser();
    
    // 演示四种内存分配方式
    demoAnonymousPrivate(50);
    demoAnonymousShared(50);
    demoFileMappedPrivate(50);
    demoFileMappedShared(50);
    
    std::cout << "\n所有演示已完成" << std::endl;
    std::cout << "最终内存状态:" << std::endl;
    printProcessMemoryInfo();
    
    std::cout << "\n按Enter键退出..." << std::endl;
    std::cin.get();
    
    return 0;
}