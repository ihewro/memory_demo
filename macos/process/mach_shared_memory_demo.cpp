#include <iostream>
#include <vector>
#include <unistd.h>
#include <mach/mach.h>
#include <mach/mach_vm.h>
#include <sys/wait.h>
#include <string>
#include <thread>
#include <chrono>

/*
 * Mach共享内存演示程序
 * 
 * 演示macOS特有的Mach共享内存机制，并使共享内存指标上涨50MB
 * 使用Mach API创建内存区域并在父子进程间共享
 */

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

// 创建Mach共享内存区域
bool create_mach_shared_memory(vm_address_t* memory, size_t size, mach_port_t* memory_port) {
    // 分配内存区域
    kern_return_t kr = vm_allocate(mach_task_self(), memory, size, VM_FLAGS_ANYWHERE);
    if (kr != KERN_SUCCESS) {
        std::cerr << "无法分配内存区域: " << mach_error_string(kr) << std::endl;
        return false;
    }
    
    // 填充内存区域以确保页面被实际分配
    char* buffer = reinterpret_cast<char*>(*memory);
    for (size_t i = 0; i < size; i += 4096) {
        buffer[i] = 'M'; // 'M' for Mach
    }
    
    // 创建内存对象
    mach_port_t mem_port;
    kr = mach_make_memory_entry_64(
        mach_task_self(),
        (memory_object_size_t*)&size,
        *memory,
        VM_PROT_READ | VM_PROT_WRITE,
        &mem_port,
        MACH_PORT_NULL
    );
    
    if (kr != KERN_SUCCESS) {
        std::cerr << "无法创建内存对象: " << mach_error_string(kr) << std::endl;
        vm_deallocate(mach_task_self(), *memory, size);
        return false;
    }
    
    *memory_port = mem_port;
    return true;
}

// 映射已存在的Mach共享内存区域
bool map_mach_shared_memory(mach_port_t memory_port, size_t size, vm_address_t* mapped_address) {
    kern_return_t kr = vm_map(
        mach_task_self(),
        mapped_address,
        size,
        0,  // 对齐
        VM_FLAGS_ANYWHERE,
        memory_port,
        0,  // 偏移量
        FALSE,  // 副本
        VM_PROT_READ | VM_PROT_WRITE,
        VM_PROT_READ | VM_PROT_WRITE,
        VM_INHERIT_SHARE
    );
    
    if (kr != KERN_SUCCESS) {
        std::cerr << "无法映射共享内存: " << mach_error_string(kr) << std::endl;
        return false;
    }
    
    return true;
}

// 演示Mach共享内存
void demo_mach_shared_memory(size_t size_mb) {
    std::cout << "\n=== 演示Mach共享内存 (macOS特有) ===" << std::endl;
    
    const size_t size_bytes = size_mb * 1024 * 1024;
    vm_address_t memory = 0;
    mach_port_t memory_port = MACH_PORT_NULL;
    
    // 创建共享内存区域
    if (!create_mach_shared_memory(&memory, size_bytes, &memory_port)) {
        std::cerr << "创建Mach共享内存失败" << std::endl;
        return;
    }
    
    std::cout << "已创建" << size_mb << "MB的Mach共享内存区域" << std::endl;
    std::cout << "内存地址: 0x" << std::hex << memory << std::dec << std::endl;
    
    // 打印父进程的内存信息
    std::cout << "\n父进程内存信息 (创建共享内存后):" << std::endl;
    print_memory_info();
    
    // 创建子进程
    pid_t pid = fork();
    
    if (pid == -1) {
        std::cerr << "创建子进程失败" << std::endl;
        mach_port_deallocate(mach_task_self(), memory_port);
        vm_deallocate(mach_task_self(), memory, size_bytes);
        return;
    } else if (pid == 0) {
        // 子进程
        std::cout << "\n子进程 (PID: " << getpid() << ") 开始运行" << std::endl;
        
        // 在子进程中映射共享内存
        vm_address_t child_memory = 0;
        if (!map_mach_shared_memory(memory_port, size_bytes, &child_memory)) {
            std::cerr << "子进程映射共享内存失败" << std::endl;
            exit(1);
        }
        
        std::cout << "子进程成功映射共享内存，地址: 0x" << std::hex << child_memory << std::dec << std::endl;
        
        // 读取共享内存中的数据
        char* buffer = reinterpret_cast<char*>(child_memory);
        std::cout << "子进程读取共享内存第一个字节: '" << buffer[0] << "'" << std::endl;
        
        // 修改共享内存中的数据
        for (size_t i = 0; i < size_bytes; i += 4096) {
            buffer[i] = 'C'; // 'C' for Child
        }
        
        std::cout << "子进程修改了共享内存内容" << std::endl;
        
        // 打印子进程的内存信息
        std::cout << "\n子进程内存信息 (映射共享内存后):" << std::endl;
        print_memory_info();
        
        // 运行一段时间以便观察
        std::cout << "子进程将运行10秒钟，您可以在此期间使用vmmap或活动监视器查看内存使用情况" << std::endl;
        std::cout << "命令: vmmap " << getpid() << " | grep -A 20 'Summary'" << std::endl;
        std::this_thread::sleep_for(std::chrono::seconds(10));
        
        // 解除映射
        vm_deallocate(mach_task_self(), child_memory, size_bytes);
        mach_port_deallocate(mach_task_self(), memory_port);
        
        std::cout << "子进程退出" << std::endl;
        exit(0);
    } else {
        // 父进程
        std::cout << "父进程创建了子进程 (PID: " << pid << ")" << std::endl;
        std::cout << "父进程将等待子进程完成..." << std::endl;
        std::cout << "您可以在此期间使用vmmap或活动监视器查看内存使用情况" << std::endl;
        std::cout << "命令: vmmap " << getpid() << " | grep -A 20 'Summary'" << std::endl;
        
        // 等待子进程完成
        int status;
        waitpid(pid, &status, 0);
        
        // 检查共享内存是否被子进程修改
        char* buffer = reinterpret_cast<char*>(memory);
        std::cout << "\n父进程检查共享内存" << std::endl;
        std::cout << "父进程读取共享内存第一个字节 (应该被子进程修改): '" << buffer[0] << "'" << std::endl;
        
        // 清理资源
        mach_port_deallocate(mach_task_self(), memory_port);
        vm_deallocate(mach_task_self(), memory, size_bytes);
        
        std::cout << "父进程已释放共享内存资源" << std::endl;
    }
}

int main() {
    std::cout << "Mach共享内存演示程序 (macOS特有)" << std::endl;
    std::cout << "当前进程PID: " << getpid() << std::endl;
    std::cout << "\n您可以使用以下命令查看内存使用情况:" << std::endl;
    std::cout << "$ vmmap " << getpid() << " | grep -A 20 'Summary'" << std::endl;
    std::cout << "$ ps -o pid,rss,vsz,command -p " << getpid() << std::endl;
    
    // 打印初始内存信息
    std::cout << "\n初始内存信息:" << std::endl;
    print_memory_info();
    
    std::cout << "\n按Enter键开始演示..." << std::endl;
    std::cin.get();
    
    // 演示Mach共享内存
    demo_mach_shared_memory(50); // 分配50MB共享内存
    
    std::cout << "\n演示完成" << std::endl;
    std::cout << "您可以使用上述命令再次检查内存使用情况" << std::endl;
    
    return 0;
}