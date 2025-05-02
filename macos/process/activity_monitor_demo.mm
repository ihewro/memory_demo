#include <iostream>
#include <vector>
#include <unistd.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <thread>
#include <chrono>
#include <sys/stat.h>
#include <sys/shm.h>
#include <sys/wait.h>
#include <errno.h>
#include <Foundation/Foundation.h>
#include "memory_info.h"

/*
 * macOS活动监视器内存指标演示程序
 * 
 * 演示活动监视器中各个内存指标的变化：
 * - 内存（footprint 指标）
 * - 实际（物理）内存
 * - 专用（物理）内存
 * - 共享（物理）内存
 * - 可清除（物理）内存
 * - VM 被压缩（物理内存）
 */

// 等待用户按Enter键继续
void wait_for_user() {
    std::cout << "\n按Enter键继续..." << std::endl;
    std::cin.get();
}

// 1. 演示专用内存（匿名内存）- 影响"专用内存"指标
void demo_private_memory(size_t size_mb) {
    std::cout << "\n=== 演示专用内存（匿名内存）===" << std::endl;
    std::cout << "分配" << size_mb << "MB的匿名内存..." << std::endl;
    std::cout << "这将增加活动监视器中的\"专用内存\"指标" << std::endl;
    
    // 使用malloc分配内存
    const size_t size_bytes = size_mb * 1024 * 1024;
    char* buffer = (char*)malloc(size_bytes);
    
    if (buffer == nullptr) {
        std::cerr << "内存分配失败!" << std::endl;
        return;
    }
    
    // 写入数据以确保页面被实际分配
    for (size_t i = 0; i < size_bytes; i += 4096) {
        buffer[i] = 'A';
    }
    
    std::cout << "已分配并写入" << size_mb << "MB的匿名内存" << std::endl;
    print_memory_info();
    std::cout << "请在活动监视器中观察\"专用内存\"指标的增加" << std::endl;
    wait_for_user();
    
    // 释放内存
    free(buffer);
    std::cout << "已释放匿名内存" << std::endl;
}

// 2. 演示共享内存 - 影响"共享内存"指标
void demo_shared_memory(size_t size_mb) {
    std::cout << "\n=== 演示共享内存 ===" << std::endl;
    std::cout << "创建" << size_mb << "MB的共享内存..." << std::endl;
    std::cout << "这将增加活动监视器中的\"共享内存\"指标" << std::endl;
    
    // 创建共享内存
    const size_t size_bytes = size_mb * 1024 * 1024;
    void* shared_memory = mmap(NULL, size_bytes, PROT_READ | PROT_WRITE, 
                             MAP_ANONYMOUS | MAP_SHARED, -1, 0);
    
    if (shared_memory == MAP_FAILED) {
        std::cerr << "共享内存创建失败: " << strerror(errno) << std::endl;
        return;
    }
    
    // 写入数据以确保页面被实际分配
    char* buffer = (char*)shared_memory;
    for (size_t i = 0; i < size_bytes; i += 4096) {
        buffer[i] = 'S';
    }
    
    std::cout << "已创建并写入" << size_mb << "MB的共享内存" << std::endl;
    print_memory_info();
    std::cout << "请在活动监视器中观察\"共享内存\"指标的增加" << std::endl;
    
    // 创建子进程来演示共享特性
    pid_t pid = fork();
    
    if (pid == -1) {
        std::cerr << "创建子进程失败" << std::endl;
    } else if (pid == 0) {
        // 子进程
        std::cout << "\n子进程访问共享内存" << std::endl;
        std::cout << "子进程读取共享内存的第一个字节: '" << buffer[0] << "'" << std::endl;
        
        // 修改共享内存
        for (size_t i = 0; i < size_bytes; i += 4096) {
            buffer[i] = 'C';
        }
        
        std::cout << "子进程修改共享内存的第一个字节为: '" << buffer[0] << "'" << std::endl;
        std::cout << "请在活动监视器中观察子进程的内存使用情况" << std::endl;
        std::cout << "注意：共享内存在多个进程间共享，不会重复计算总内存使用量" << std::endl;
        wait_for_user();
        exit(0);
    } else {
        // 父进程
        wait_for_user();
        
        // 等待子进程完成
        int status;
        waitpid(pid, &status, 0);
        
        std::cout << "\n父进程检查共享内存" << std::endl;
        std::cout << "父进程读取共享内存的第一个字节 (应该被子进程修改): '" << buffer[0] << "'" << std::endl;
    }
    
    wait_for_user();
    
    // 解除映射
    munmap(shared_memory, size_bytes);
    std::cout << "已解除共享内存映射" << std::endl;
}

// 3. 演示可清除内存 - 影响"可清除内存"指标
void demo_purgeable_memory(size_t size_mb) {
    std::cout << "\n=== 演示可清除内存 ===" << std::endl;
    std::cout << "创建" << size_mb << "MB的可清除内存..." << std::endl;
    std::cout << "这将增加活动监视器中的\"可清除内存\"指标" << std::endl;
    
    // 使用Mach VM接口创建可清除内存
    vm_address_t address = 0;
    const size_t size_bytes = size_mb * 1024 * 1024;

    // 分配内存
    kern_return_t kr = vm_allocate(mach_task_self(), &address, size_bytes, VM_FLAGS_ANYWHERE | VM_FLAGS_PURGABLE);
    if (kr != KERN_SUCCESS) {
        std::cerr << "内存分配失败: " << mach_error_string(kr) << std::endl;
        return;
    }

    // 写入数据以确保页面被实际分配
    char* buffer = (char*)address;
    for (size_t i = 0; i < size_bytes; i += 4096) {
        buffer[i] = 'P';
    }

    // 将内存标记为可清除
    int state = VM_PURGABLE_NONVOLATILE; // 非易失性可清除
    vm_purgable_control(mach_task_self(), address, VM_PURGABLE_SET_STATE, &state);

    std::cout << "已分配并标记" << size_mb << "MB的可清除内存" << std::endl;
    print_memory_info();
    std::cout << "请在活动监视器中观察\"可清除内存\"指标的增加" << std::endl;
    wait_for_user();

    // 演示内存压力下的可清除内存行为
    std::cout << "\n现在创建内存压力，观察可清除内存的行为..." << std::endl;
    std::cout << "系统可能会回收可清除内存而不写入交换文件" << std::endl;

    // 分配大量内存以创建内存压力
    std::vector<void*> pressure_buffers;
    const size_t pressure_chunk = 100; // 每次分配100MB

    for (int i = 0; i < 5; i++) { // 尝试分配额外的500MB
        void* pressure = malloc(pressure_chunk * 1024 * 1024);
        if (pressure != nullptr) {
            // 写入数据以确保页面被实际分配
            char* p = (char*)pressure;
            for (size_t j = 0; j < pressure_chunk * 1024 * 1024; j += 4096) {
                p[j] = 'X';
            }
            pressure_buffers.push_back(pressure);
            std::cout << "已分配额外" << pressure_chunk << "MB内存，总计: "
                      << (i+1) * pressure_chunk << "MB" << std::endl;
        } else {
            std::cout << "无法分配更多内存，可能已触发内存回收" << std::endl;
            break;
        }

        // 短暂暂停，让系统有时间进行内存管理
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }

    // 尝试访问可清除内存，检查是否被回收
    std::cout << "\n尝试访问可清除内存..." << std::endl;
    char first_byte = buffer[0];
    std::cout << "可清除内存的第一个字节: '" << first_byte << "'" << std::endl;
    if (first_byte != 'P') {
        std::cout << "内容已被系统回收，这是可清除内存的特性" << std::endl;
    } else {
        std::cout << "内容仍然存在，系统尚未回收此内存" << std::endl;
    }

    print_memory_info();
    std::cout << "请在活动监视器中再次观察\"可清除内存\"指标的变化" << std::endl;
    wait_for_user();

    // 释放所有内存
    for (void* ptr : pressure_buffers) {
        free(ptr);
    }
    vm_deallocate(mach_task_self(), address, size_bytes);
    std::cout << "已释放所有内存" << std::endl;
}

// 4. 演示压缩内存 - 影响"VM 被压缩"指标
void demo_compressed_memory(size_t size_mb) {
    std::cout << "\n=== 演示压缩内存 ===" << std::endl;
    std::cout << "创建" << size_mb << "MB的高度可压缩内存..." << std::endl;
    std::cout << "这将增加活动监视器中的\"VM 被压缩\"指标" << std::endl;
    
    // 分配内存
    const size_t size_bytes = size_mb * 1024 * 1024;
    char* buffer = (char*)malloc(size_bytes);
    
    if (buffer == nullptr) {
        std::cerr << "内存分配失败!" << std::endl;
        return;
    }
    
    // 写入高度可压缩的数据（重复模式）
    for (size_t i = 0; i < size_bytes; i++) {
        buffer[i] = 'Z'; // 使用相同的字符使数据高度可压缩
    }
    
    std::cout << "已分配并写入" << size_mb << "MB的高度可压缩内存" << std::endl;
    print_memory_info();
    
    // 创建内存压力以触发压缩
    std::cout << "\n创建内存压力以触发压缩..." << std::endl;
    std::vector<void*> pressure_buffers;
    const size_t pressure_chunk = 100; // 每次分配100MB
    
    for (int i = 0; i < 5; i++) { // 尝试分配额外的500MB
        void* pressure = malloc(pressure_chunk * 1024 * 1024);
        if (pressure != nullptr) {
            // 写入数据以确保页面被实际分配
            char* p = (char*)pressure;
            for (size_t j = 0; j < pressure_chunk * 1024 * 1024; j += 4096) {
                p[j] = 'P';
            }
            pressure_buffers.push_back(pressure);
            std::cout << "已分配额外" << pressure_chunk << "MB内存，总计: " 
                      << (i+1) * pressure_chunk << "MB" << std::endl;
        } else {
            std::cout << "无法分配更多内存，可能已触发压缩" << std::endl;
            break;
        }
        
        // 短暂暂停，让系统有时间进行压缩
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
    
    std::cout << "此时部分内存可能已被压缩，计入\"VM 被压缩\"指标" << std::endl;
    print_memory_info();
    std::cout << "请在活动监视器中观察\"VM 被压缩\"指标的增加" << std::endl;
    wait_for_user();
    
    // 释放所有内存
    for (void* ptr : pressure_buffers) {
        free(ptr);
    }
    free(buffer);
    std::cout << "已释放所有内存" << std::endl;
}

// 5. 演示匿名共享内存对RPRVT的影响 - 验证匿名共享内存会增加专用内存指标，并在fork后转移到共享内存
void demo_anon_shared_memory_rprvt(size_t size_mb) {
    std::cout << "\n=== 演示匿名共享内存对RPRVT的影响 ===" << std::endl;
    std::cout << "分配" << size_mb << "MB的匿名共享内存..." << std::endl;
    std::cout << "这将增加活动监视器中的\"专用内存\"指标(RPRVT)" << std::endl;
    
    // 创建匿名共享内存
    const size_t size_bytes = size_mb * 1024 * 1024;
    void* shared_memory = mmap(NULL, size_bytes, PROT_READ | PROT_WRITE, 
                             MAP_ANONYMOUS | MAP_SHARED, -1, 0);
    
    if (shared_memory == MAP_FAILED) {
        std::cerr << "匿名共享内存创建失败: " << strerror(errno) << std::endl;
        return;
    }
    
    // 写入数据以确保页面被实际分配
    char* buffer = (char*)shared_memory;
    for (size_t i = 0; i < size_bytes; i += 4096) {
        buffer[i] = 'R'; // 'R' for RPRVT
    }
    
    std::cout << "已分配并写入" << size_mb << "MB的匿名共享内存" << std::endl;
    print_memory_info();
    std::cout << "请在活动监视器中观察\"专用内存\"指标(RPRVT)的增加" << std::endl;
    std::cout << "注意：尽管使用了MAP_SHARED标志，但匿名共享内存在没有fork的情况下会计入专用内存" << std::endl;
    wait_for_user();
    
    // 创建子进程来演示共享特性
    std::cout << "\n现在创建子进程，观察内存从专用内存转移到共享内存..." << std::endl;
    pid_t pid = fork();
    
    if (pid == -1) {
        std::cerr << "创建子进程失败" << std::endl;
    } else if (pid == 0) {
        // 子进程
        std::cout << "\n子进程访问共享内存" << std::endl;
        std::cout << "子进程读取共享内存的第一个字节: '" << buffer[0] << "'" << std::endl;
        
        // 修改共享内存
        for (size_t i = 0; i < size_bytes; i += 4096) {
            buffer[i] = 'S'; // 'S' for Shared
        }
        
        std::cout << "子进程修改共享内存的第一个字节为: '" << buffer[0] << "'" << std::endl;
        print_memory_info();
        std::cout << "请在活动监视器中观察子进程的内存使用情况" << std::endl;
        std::cout << "注意：此时匿名共享内存已从专用内存(RPRVT)转移到共享内存统计中" << std::endl;
        wait_for_user();
        exit(0);
    } else {
        // 父进程
        std::cout << "父进程创建了子进程 (PID: " << pid << ")" << std::endl;
        std::cout << "请在活动监视器中观察父进程的内存使用情况" << std::endl;
        std::cout << "注意：此时匿名共享内存已从专用内存(RPRVT)转移到共享内存统计中" << std::endl;
        print_memory_info();
        wait_for_user();
        
        // 等待子进程完成
        int status;
        waitpid(pid, &status, 0);
        
        std::cout << "\n父进程检查共享内存" << std::endl;
        std::cout << "父进程读取共享内存的第一个字节 (应该被子进程修改): '" << buffer[0] << "'" << std::endl;
        print_memory_info();
        wait_for_user();
    }
    
    // 解除映射
    munmap(shared_memory, size_bytes);
    std::cout << "已解除匿名共享内存映射" << std::endl;
}

// 6. 演示实际内存 - 影响"实际内存"指标（综合演示）
// 7. 演示NSPurgeableData可清除内存 - 影响"可清除内存"指标
void demo_purgeable_data(size_t size_mb) {
    std::cout << "\n=== 演示NSPurgeableData可清除内存 ===" << std::endl;
    std::cout << "创建" << size_mb << "MB的可清除内存..." << std::endl;
    std::cout << "这将增加活动监视器中的\"可清除内存\"指标" << std::endl;
    
    @autoreleasepool {
        // 创建可清除数据
        const size_t size_bytes = size_mb * 1024 * 1024;
        NSPurgeableData* purgeable_data = [[NSPurgeableData alloc] initWithLength:size_bytes];
        if (!purgeable_data) {
            std::cerr << "NSPurgeableData创建失败!" << std::endl;
            return;
        }
        
        // 确保内存访问权限
        if (![purgeable_data beginContentAccess]) {
            std::cerr << "无法访问NSPurgeableData内容!" << std::endl;
            return;
        }
        
        // 写入数据
        char* buffer = (char*)[purgeable_data mutableBytes];
        if (!buffer) {
            std::cerr << "无法获取NSMutableData的内存缓冲区!" << std::endl;
            [purgeable_data endContentAccess];
            return;
        }
        
        for (size_t i = 0; i < size_bytes; i += 4096) {
            buffer[i] = 'N'; // 'N' for NSPurgeableData
        }
        
        std::cout << "已分配并写入" << size_mb << "MB的可清除数据" << std::endl;
        print_memory_info();
        std::cout << "请在活动监视器中观察\"可清除内存\"指标的增加" << std::endl;
        wait_for_user();
        
        // 标记内容为可清除
        [purgeable_data endContentAccess];
        
        // 创建内存压力
        std::cout << "\n创建内存压力，观察可清除内存的行为..." << std::endl;
        std::vector<void*> pressure_buffers;
        const size_t pressure_chunk = 100; // 每次分配100MB
        
        for (int i = 0; i < 5; i++) {
            void* pressure = malloc(pressure_chunk * 1024 * 1024);
            if (pressure != nullptr) {
                char* p = (char*)pressure;
                for (size_t j = 0; j < pressure_chunk * 1024 * 1024; j += 4096) {
                    p[j] = 'X';
                }
                pressure_buffers.push_back(pressure);
                std::cout << "已分配额外" << pressure_chunk << "MB内存，总计: "
                          << (i+1) * pressure_chunk << "MB" << std::endl;
            } else {
                std::cout << "无法分配更多内存，可能已触发内存回收" << std::endl;
                break;
            }
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
        
        // 尝试访问可清除数据
        std::cout << "\n尝试访问可清除数据..." << std::endl;
        if ([purgeable_data beginContentAccess]) {
            buffer = (char*)[purgeable_data mutableBytes];
            if (buffer) {
                std::cout << "可清除数据的第一个字节: '" << buffer[0] << "'" << std::endl;
            } else {
                std::cout << "无法获取内存缓冲区，数据可能已被回收" << std::endl;
            }
            [purgeable_data endContentAccess];
        } else {
            std::cout << "数据已被系统回收，这是可清除内存的特性" << std::endl;
        }
        
        print_memory_info();
        std::cout << "请在活动监视器中再次观察\"可清除内存\"指标的变化" << std::endl;
        wait_for_user();
        
        // 释放内存压力
        for (void* ptr : pressure_buffers) {
            free(ptr);
        }
        
        // NSPurgeableData会在autoreleasepool结束时自动释放
    }
    
    std::cout << "已释放所有内存" << std::endl;
}

void demo_real_memory(size_t size_mb) {
    std::cout << "\n=== 演示实际内存 ===" << std::endl;
    std::cout << "实际内存是进程使用的物理内存总量，包括专用、共享和可清除内存" << std::endl;
    std::cout << "分配多种类型的内存，总计" << size_mb << "MB..." << std::endl;
    
    const size_t part_size = size_mb / 3; // 将总大小分成三部分
    const size_t size_bytes = part_size * 1024 * 1024;
    
    // 1. 分配专用内存
    std::cout << "\n分配" << part_size << "MB的专用内存..." << std::endl;
    char* private_buffer = (char*)malloc(size_bytes);
    if (private_buffer == nullptr) {
        std::cerr << "专用内存分配失败!" << std::endl;
        return;
    }
    for (size_t i = 0; i < size_bytes; i += 4096) {
        private_buffer[i] = 'A';
    }
    
    // 2. 分配共享内存
    std::cout << "分配" << part_size << "MB的共享内存..." << std::endl;
    void* shared_memory = mmap(NULL, size_bytes, PROT_READ | PROT_WRITE, 
                             MAP_ANONYMOUS | MAP_SHARED, -1, 0);
    if (shared_memory == MAP_FAILED) {
        std::cerr << "共享内存分配失败!" << std::endl;
        free(private_buffer);
        return;
    }
    char* shared_buffer = (char*)shared_memory;
    for (size_t i = 0; i < size_bytes; i += 4096) {
        shared_buffer[i] = 'S';
    }
    
    // 3. 分配可清除内存
    std::cout << "分配" << part_size << "MB的可清除内存..." << std::endl;
    vm_address_t purgeable_address = 0;
    kern_return_t kr = vm_allocate(mach_task_self(), &purgeable_address, size_bytes, VM_FLAGS_ANYWHERE);
    if (kr != KERN_SUCCESS) {
        std::cerr << "可清除内存分配失败!" << std::endl;
        free(private_buffer);
        munmap(shared_memory, size_bytes);
        return;
    }
    char* purgeable_buffer = (char*)purgeable_address;
    for (size_t i = 0; i < size_bytes; i += 4096) {
        purgeable_buffer[i] = 'P';
    }
    int state = VM_PURGABLE_NONVOLATILE;
    vm_purgable_control(mach_task_self(), purgeable_address, VM_PURGABLE_SET_STATE, &state);
    
    std::cout << "\n已分配三种类型的内存，总计" << (part_size * 3) << "MB" << std::endl;
    print_memory_info();
    std::cout << "请在活动监视器中观察\"实际内存\"指标的增加" << std::endl;
    std::cout << "注意：实际内存是所有物理内存使用的总和" << std::endl;
    wait_for_user();
    
    // 释放所有内存
    free(private_buffer);
    munmap(shared_memory, size_bytes);
    vm_deallocate(mach_task_self(), purgeable_address, size_bytes);
    std::cout << "已释放所有内存" << std::endl;
}

// 8. 演示私有文件映射内存 - 影响"专用内存"和"footprint"指标
void demo_private_file_mapping(size_t size_mb) {
    std::cout << "\n=== 演示私有文件映射内存 ===" << std::endl;
    std::cout << "创建并映射" << size_mb << "MB的文件(私有模式)..." << std::endl;
    std::cout << "这将增加活动监视器中的\"专用内存\"和\"footprint\"指标" << std::endl;
    
    // 创建临时文件
    const size_t size_bytes = size_mb * 1024 * 1024;
    char temp_filename[] = "/tmp/private_mapping_test_XXXXXX";
    int fd = mkstemp(temp_filename);
    
    if (fd == -1) {
        std::cerr << "临时文件创建失败: " << strerror(errno) << std::endl;
        return;
    }
    
    // 扩展文件大小
    if (ftruncate(fd, size_bytes) == -1) {
        std::cerr << "文件大小调整失败: " << strerror(errno) << std::endl;
        close(fd);
        unlink(temp_filename);
        return;
    }
    
    // 写入一些数据到文件
    char* write_buffer = (char*)malloc(size_bytes);
    if (write_buffer == nullptr) {
        std::cerr << "内存分配失败!" << std::endl;
        close(fd);
        unlink(temp_filename);
        return;
    }
    
    // 填充数据
    for (size_t i = 0; i < size_bytes; i++) {
        write_buffer[i] = 'F'; // 'F' for File
    }
    
    // 写入文件
    if (write(fd, write_buffer, size_bytes) != size_bytes) {
        std::cerr << "文件写入失败: " << strerror(errno) << std::endl;
        free(write_buffer);
        close(fd);
        unlink(temp_filename);
        return;
    }
    
    free(write_buffer);
    
    // 显示映射前的内存状态
    std::cout << "\n映射前的内存状态:" << std::endl;
    print_memory_info();
    
    // 私有映射文件 (MAP_PRIVATE)
    void* mapped_memory = mmap(NULL, size_bytes, PROT_READ | PROT_WRITE, 
                              MAP_PRIVATE, fd, 0);
    
    if (mapped_memory == MAP_FAILED) {
        std::cerr << "文件映射失败: " << strerror(errno) << std::endl;
        close(fd);
        unlink(temp_filename);
        return;
    }
    
    // 读取映射内存以确保页面被加载
    char* buffer = (char*)mapped_memory;
    volatile char sum = 0; // 防止编译器优化掉读取操作
    
    std::cout << "读取映射内存中..." << std::endl;
    for (size_t i = 0; i < size_bytes; i += 4096) {
        sum += buffer[i]; // 读取每个页面的第一个字节
    }
    
    // 显示映射后的内存状态
    std::cout << "\n映射并读取后的内存状态:" << std::endl;
    print_memory_info();
    std::cout << "请在活动监视器中观察\"专用内存\"和\"footprint\"指标的增加" << std::endl;
    std::cout << "注意：私有映射在读取时会将文件内容加载到物理内存中" << std::endl;
    wait_for_user();
    
    // 修改映射内存以测试写时复制行为
    std::cout << "\n修改映射内存以测试写时复制(Copy-on-Write)行为..." << std::endl;
    for (size_t i = 0; i < size_bytes; i += 4096) {
        buffer[i] = 'M'; // 'M' for Modified
    }
    
    // 显示修改后的内存状态
    std::cout << "\n修改映射内存后的内存状态:" << std::endl;
    print_memory_info();
    std::cout << "请在活动监视器中观察\"专用内存\"指标的进一步增加" << std::endl;
    std::cout << "注意：私有映射在写入时会触发写时复制，分配新的物理页面" << std::endl;
    wait_for_user();
    
    // 解除映射并清理
    munmap(mapped_memory, size_bytes);
    close(fd);
    unlink(temp_filename);
    std::cout << "已解除映射并删除临时文件" << std::endl;
}

// 9. 演示共享文件映射内存 - 影响"共享内存"和"footprint"指标
void demo_shared_file_mapping(size_t size_mb) {
    std::cout << "\n=== 演示共享文件映射内存 ===" << std::endl;
    std::cout << "创建并映射" << size_mb << "MB的文件(共享模式)..." << std::endl;
    std::cout << "这将增加活动监视器中的\"共享内存\"和\"footprint\"指标" << std::endl;
    
    // 创建临时文件
    const size_t size_bytes = size_mb * 1024 * 1024;
    char temp_filename[] = "/tmp/shared_mapping_test_XXXXXX";
    int fd = mkstemp(temp_filename);
    
    if (fd == -1) {
        std::cerr << "临时文件创建失败: " << strerror(errno) << std::endl;
        return;
    }
    
    // 扩展文件大小
    if (ftruncate(fd, size_bytes) == -1) {
        std::cerr << "文件大小调整失败: " << strerror(errno) << std::endl;
        close(fd);
        unlink(temp_filename);
        return;
    }
    
    // 写入一些数据到文件
    char* write_buffer = (char*)malloc(size_bytes);
    if (write_buffer == nullptr) {
        std::cerr << "内存分配失败!" << std::endl;
        close(fd);
        unlink(temp_filename);
        return;
    }
    
    // 填充数据
    for (size_t i = 0; i < size_bytes; i++) {
        write_buffer[i] = 'S'; // 'S' for Shared
    }
    
    // 写入文件
    if (write(fd, write_buffer, size_bytes) != size_bytes) {
        std::cerr << "文件写入失败: " << strerror(errno) << std::endl;
        free(write_buffer);
        close(fd);
        unlink(temp_filename);
        return;
    }
    
    free(write_buffer);
    
    // 显示映射前的内存状态
    std::cout << "\n映射前的内存状态:" << std::endl;
    print_memory_info();
    
    // 共享映射文件 (MAP_SHARED)
    void* mapped_memory = mmap(NULL, size_bytes, PROT_READ | PROT_WRITE, 
                              MAP_SHARED, fd, 0);
    
    if (mapped_memory == MAP_FAILED) {
        std::cerr << "文件映射失败: " << strerror(errno) << std::endl;
        close(fd);
        unlink(temp_filename);
        return;
    }
    
    // 读取映射内存以确保页面被加载
    char* buffer = (char*)mapped_memory;
    volatile char sum = 0; // 防止编译器优化掉读取操作
    
    std::cout << "读取映射内存中..." << std::endl;
    for (size_t i = 0; i < size_bytes; i += 4096) {
        sum += buffer[i]; // 读取每个页面的第一个字节
    }
    
    // 显示映射后的内存状态
    std::cout << "\n映射并读取后的内存状态:" << std::endl;
    print_memory_info();
    std::cout << "请在活动监视器中观察\"共享内存\"和\"footprint\"指标的增加" << std::endl;
    std::cout << "注意：共享映射在读取时会将文件内容加载到物理内存中，但计入共享内存" << std::endl;
    wait_for_user();
    
    // 创建子进程来演示共享特性
    std::cout << "\n创建子进程来演示共享文件映射的特性..." << std::endl;
    pid_t pid = fork();
    
    if (pid == -1) {
        std::cerr << "创建子进程失败" << std::endl;
    } else if (pid == 0) {
        // 子进程
        std::cout << "\n子进程访问共享文件映射" << std::endl;
        std::cout << "子进程读取共享映射的第一个字节: '" << buffer[0] << "'" << std::endl;
        
        // 修改共享映射
        for (size_t i = 0; i < size_bytes; i += 4096) {
            buffer[i] = 'C'; // 'C' for Child
        }
        
        std::cout << "子进程修改共享映射的第一个字节为: '" << buffer[0] << "'" << std::endl;
        print_memory_info();
        std::cout << "请在活动监视器中观察子进程的内存使用情况" << std::endl;
        std::cout << "注意：共享文件映射在多个进程间共享，不会重复计算总内存使用量" << std::endl;
        wait_for_user();
        
        // 将修改写回文件
        if (msync(mapped_memory, size_bytes, MS_SYNC) == -1) {
            std::cerr << "内存同步到文件失败: " << strerror(errno) << std::endl;
        } else {
            std::cout << "子进程已将修改同步到文件" << std::endl;
        }
        
        exit(0);
    } else {
        // 父进程
        std::cout << "父进程创建了子进程 (PID: " << pid << ")" << std::endl;
        std::cout << "请在活动监视器中观察父进程的内存使用情况" << std::endl;
        print_memory_info();
        wait_for_user();
        
        // 等待子进程完成
        int status;
        waitpid(pid, &status, 0);
        
        std::cout << "\n父进程检查共享映射" << std::endl;
        std::cout << "父进程读取共享映射的第一个字节 (应该被子进程修改): '" << buffer[0] << "'" << std::endl;
        print_memory_info();
        wait_for_user();
    }
    
    // 解除映射并清理
    munmap(mapped_memory, size_bytes);
    close(fd);
    unlink(temp_filename);
    std::cout << "已解除映射并删除临时文件" << std::endl;
}

// 主函数
int main() {
    std::cout << "=== macOS活动监视器内存指标演示程序 ===" << std::endl;
    std::cout << "此程序将演示不同类型的内存分配如何影响活动监视器中的各项指标" << std::endl;
    std::cout << "请在运行过程中打开活动监视器，观察内存指标的变化" << std::endl;
    std::cout << "活动监视器路径: /Applications/Utilities/Activity Monitor.app" << std::endl;
    std::cout << "\n初始内存状态:" << std::endl;
    print_memory_info();
    wait_for_user();
    
    // 演示各种内存类型
//     demo_private_memory(100);    // 100MB专用内存
//     demo_shared_memory(100);     // 100MB共享内存
//     demo_purgeable_memory(100);  // 100MB可清除内存
//     demo_anon_shared_memory_rprvt(500); // 500MB匿名共享内存，验证RPRVT指标
//     demo_purgeable_data(100);    // 100MB NSPurgeableData可清除内存
//     demo_compressed_memory(200); // 200MB压缩内存
//     demo_real_memory(150);       // 150MB实际内存（综合演示）
    demo_private_file_mapping(100); // 100MB私有文件映射
    demo_shared_file_mapping(100);  // 100MB共享文件映射
    
    std::cout << "\n=== 演示完成 ===" << std::endl;
    std::cout << "最终内存状态:" << std::endl;
    print_memory_info();
    
    return 0;
}