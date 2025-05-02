#include <iostream>
#include <vector>
#include <unistd.h>
#include <mach/mach.h>
#include <string>
#include <thread>
#include <chrono>

/*
 * Mach-O文件数据段演示程序
 * 
 * 演示__DATA和__DATA_DIRTY段的区别和用途：
 * - __DATA段：包含初始化的全局变量、静态变量和常量数据
 *   - 在程序启动时从磁盘加载到内存
 *   - 只读或读写权限，取决于具体子段
 * - __DATA_DIRTY段：包含需要写入的数据
 *   - 包含可修改的全局变量和静态变量
 *   - 在内存中具有读写权限
 *   - 当程序修改这些数据时会产生脏页
 */

// 常量数据 - 通常位于__DATA.__const段
const int kConstantValue = 42;
const char* kConstantString = "这是一个常量字符串";

// 只读数据 - 通常位于__DATA.__const段
static const std::vector<int> kReadOnlyVector = {1, 2, 3, 4, 5};

// 初始化的全局变量 - 通常位于__DATA_DIRTY.__data段
int gGlobalVariable = 100;
static int gStaticVariable = 200;

// 未初始化的全局变量 - 通常位于__DATA_DIRTY.__bss段
int gUninitializedVariable;
static int gUninitializedStaticVariable;

// 类的静态成员变量
class DemoClass {
public:
    // 常量静态成员 - 通常位于__DATA.__const段
    static const int kClassConstant = 500;
    
    // 可修改的静态成员 - 通常位于__DATA_DIRTY.__data段
    static int sClassVariable;
};

// 静态成员变量的定义
int DemoClass::sClassVariable = 600;

// 打印当前进程的内存使用信息
void print_memory_info() {
    task_t task = mach_task_self();
    task_basic_info_data_t info;
    mach_msg_type_number_t count = TASK_BASIC_INFO_COUNT;
    
    kern_return_t kr = task_info(task, TASK_BASIC_INFO, (task_info_t)&info, &count);
    if (kr != KERN_SUCCESS) {
        std::cerr << "获取任务信息失败" << std::endl;
        return;
    }
    
    std::cout << "当前进程内存使用情况:" << std::endl;
    std::cout << "  - 常驻内存大小 (RSS): " << (info.resident_size / 1024 / 1024) << " MB" << std::endl;
    std::cout << "  - 虚拟内存大小 (VSZ): " << (info.virtual_size / 1024 / 1024) << " MB" << std::endl;
}

// 修改全局变量以创建脏页
void modify_global_variables() {
    std::cout << "\n=== 修改全局变量以创建脏页 ===" << std::endl;
    
    // 修改前的值
    std::cout << "修改前的值:" << std::endl;
    std::cout << "  gGlobalVariable = " << gGlobalVariable << std::endl;
    std::cout << "  gStaticVariable = " << gStaticVariable << std::endl;
    std::cout << "  DemoClass::sClassVariable = " << DemoClass::sClassVariable << std::endl;
    
    // 修改全局变量
    gGlobalVariable = 101;
    gStaticVariable = 201;
    DemoClass::sClassVariable = 601;
    
    // 修改后的值
    std::cout << "\n修改后的值:" << std::endl;
    std::cout << "  gGlobalVariable = " << gGlobalVariable << std::endl;
    std::cout << "  gStaticVariable = " << gStaticVariable << std::endl;
    std::cout << "  DemoClass::sClassVariable = " << DemoClass::sClassVariable << std::endl;
    
    std::cout << "\n这些修改会导致__DATA_DIRTY段中的页面变为脏页" << std::endl;
    std::cout << "而__DATA.__const段中的常量数据保持不变" << std::endl;
}

// 显示如何查看段信息的命令
void show_segment_commands() {
    std::cout << "\n=== 查看Mach-O文件段信息的命令 ===" << std::endl;
    std::cout << "编译此程序后，可以使用以下命令查看段信息:" << std::endl;
    std::cout << "1. 查看所有段和节:" << std::endl;
    std::cout << "   otool -l data_segments_demo" << std::endl;
    std::cout << "\n2. 查看数据段内容:" << std::endl;
    std::cout << "   otool -s __DATA __data data_segments_demo" << std::endl;
    std::cout << "   otool -s __DATA_DIRTY __data data_segments_demo" << std::endl;
    std::cout << "\n3. 查看进程内存中的段分布:" << std::endl;
    std::cout << "   vmmap $(pgrep data_segments_demo)" << std::endl;
}

// 主函数
int main() {
    std::cout << "=== Mach-O文件数据段演示程序 ===" << std::endl;
    
    // 打印进程ID，方便用户使用vmmap查看
    std::cout << "进程ID: " << getpid() << std::endl;
    
    // 打印变量地址，帮助理解内存布局
    std::cout << "\n=== 变量地址 ===" << std::endl;
    std::cout << "常量数据 (__DATA.__const):" << std::endl;
    std::cout << "  &kConstantValue = " << &kConstantValue << std::endl;
    std::cout << "  &kConstantString = " << static_cast<const void*>(&kConstantString) << std::endl;
    std::cout << "  &kReadOnlyVector = " << &kReadOnlyVector << std::endl;
    std::cout << "  &DemoClass::kClassConstant = " << &DemoClass::kClassConstant << std::endl;
    
    std::cout << "\n可修改的全局数据 (__DATA_DIRTY.__data):" << std::endl;
    std::cout << "  &gGlobalVariable = " << &gGlobalVariable << std::endl;
    std::cout << "  &gStaticVariable = " << &gStaticVariable << std::endl;
    std::cout << "  &DemoClass::sClassVariable = " << &DemoClass::sClassVariable << std::endl;
    
    std::cout << "\n未初始化的数据 (__DATA_DIRTY.__bss):" << std::endl;
    std::cout << "  &gUninitializedVariable = " << &gUninitializedVariable << std::endl;
    std::cout << "  &gUninitializedStaticVariable = " << &gUninitializedStaticVariable << std::endl;
    
    // 打印内存使用信息
    print_memory_info();
    
    // 修改全局变量以创建脏页
    modify_global_variables();
    
    // 显示如何查看段信息的命令
    show_segment_commands();
    
    std::cout << "\n按Enter键退出程序..." << std::endl;
    std::cin.get();
    
    return 0;
}