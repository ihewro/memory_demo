#include <iostream>
#include <vector>
#include <unistd.h>

// 这个程序使用内联汇编和大量静态数据来创建一个约100MB的代码段

// 定义一个大型静态数组，但这次放在代码段中
static const char __attribute__((section("__TEXT,__text"))) huge_data[100 * 1024 * 1024] = {1};

// 使用内联汇编来确保编译器不会优化掉代码段
void ensure_code_segment() {
    // 使用架构无关的空操作，避免使用特定架构的NOP指令
    asm volatile("" ::: "memory"); // 内存屏障，防止优化
    // 引用huge_data以确保它不会被优化掉
    asm volatile("" : : "r"(huge_data));
}

// 添加一个函数来强制访问整个数组，确保所有页面都被加载到物理内存中
void force_load_all_pages() {
    volatile int sum = 0;
    const size_t array_size = 100 * 1024 * 1024;
    
    std::cout << "正在强制加载所有内存页..." << std::endl;
    
    // 方法1：完全访问每个字节
    for (size_t i = 0; i < array_size; i++) {
        sum += huge_data[i];
        // 每处理1MB数据显示一次进度
        if (i > 0 && i % (1024 * 1024) == 0) {
            std::cout << "已加载 " << (i / (1024 * 1024)) << "MB/100MB" << std::endl;
        }
    }
    
    // 使用sum防止编译器优化掉整个循环
    std::cout << "内存页加载完成，校验和: " << sum << std::endl;
}

int main() {
    std::cout << "程序已启动，代码段大小约为100MB" << std::endl;
    
    // 调用函数以确保代码段被保留
    ensure_code_segment();
    
    // 输出数组的第一个字节
    std::cout << "数组第一个字节: " << (int)huge_data[0] << std::endl;
    pid_t pid = getpid();
    std::cout << "当前进程的 PID 是: " << pid << std::endl;
    
    // 显示当前内存使用情况
    std::cout << "强制加载前，请检查内存使用情况..." << std::endl;
    std::cout << "按Enter键继续..." << std::endl;
    std::cin.get();
    
    // 强制加载所有页面到物理内存
    force_load_all_pages();
    
    std::cout << "所有页面已加载到物理内存，请再次检查内存使用情况" << std::endl;
    std::cout << "按Enter键退出程序..." << std::endl;
    std::cin.get();
    
    return 0;
}