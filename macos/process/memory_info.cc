#include "memory_info.h"
#include <mach/mach_vm.h>
#include <vector>
#include <unistd.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <string>
#include <fstream>
#include <thread>
#include <chrono>
#include <mach/mach.h>
#include <sys/stat.h>
#include <sys/shm.h>
#include <sys/wait.h>
#include <errno.h>

// 格式化内存大小输出
std::string formatMemorySize(uint64_t bytes) {
    const char *units[] = {"B", "KB", "MB", "GB"};
    int unitIndex = 0;
    double size = bytes;

    while (size >= 1024.0 && unitIndex < 3) {
        size /= 1024.0;
        unitIndex++;
    }

    char buffer[32];
    snprintf(buffer, sizeof(buffer), "%.2f %s", size, units[unitIndex]);
    return std::string(buffer);
}

// 获取进程共享内存大小
uint64_t getProcessSharedMemory(pid_t pid) {
    // 1. 获取进程的Mach任务端口
    task_t task;
    kern_return_t kr = task_for_pid(mach_task_self(), pid, &task);
    if (kr != KERN_SUCCESS) {
        std::cerr << "无法获取进程任务端口: " << mach_error_string(kr) << std::endl;
        return 0;
    }

    // 2. 获取进程的内存区域信息
    mach_vm_size_t shared_memory = 0;

    // 获取进程的内存区域地址
    mach_vm_address_t address = 0;
    mach_vm_size_t size = 0;
    uint32_t depth = 1;

    while (true) {
        // 内存区域基本信息
        vm_region_submap_info_data_64_t info;
        mach_msg_type_number_t count = VM_REGION_SUBMAP_INFO_COUNT_64;
        mach_vm_address_t next_address = address;

        kr = mach_vm_region_recurse(task,
                                    &next_address,
                                    &size,
                                    &depth,
                                    (vm_region_recurse_info_t) &info,
                                    &count);

        if (kr != KERN_SUCCESS) {
            break; // 已遍历完所有内存区域
        }

        // 检查该内存区域是否为共享内存
        if (info.share_mode != SM_PRIVATE) {
            // 共享内存的标准: 非私有的内存区域
            // SM_COW: 写时复制
            // SM_SHARED: 共享
            // SM_TRUESHARED: 真共享
            // SM_PRIVATE_ALIASED: 私有别名
            // SM_SHARED_ALIASED: 共享别名
            shared_memory += size;
        }

        // 移动到下一个内存区域
        address = next_address + size;
    }

    // 释放任务端口
    mach_port_deallocate(mach_task_self(), task);

    return shared_memory;
}

// 打印当前进程的内存使用信息
void print_memory_info() {
    task_t task = mach_task_self();
//    task_t task;
    // 获取其他进程的信息，需要sudo权限
//    kern_return_t kr_ = task_for_pid(mach_task_self(),57130, &task);
//    if (kr_ != KERN_SUCCESS) {
//        std::cerr << "获取任务信息失败1" << std::endl;
//        return;
//    }
    task_vm_info_data_t vm_info;
    mach_msg_type_number_t count = TASK_VM_INFO_COUNT;
    kern_return_t kr = task_info(task, TASK_VM_INFO, (task_info_t) &vm_info, &count);
    if (kr != KERN_SUCCESS) {
        std::cerr << "获取任务信息失败2" << std::endl;
        return;
    }

    // 计算Real Private Memory和Real Shared Memory
    vm_size_t real_private_memory = 0;
    vm_size_t real_shared_memory = 0;

    vm_address_t address = 0;
    vm_size_t size;
    mach_port_t object_name;

    while (true) {
        vm_region_top_info_data_t info;
        mach_msg_type_number_t info_count = VM_REGION_TOP_INFO_COUNT;

        kr = vm_region_64(task, &address, &size,
                          VM_REGION_TOP_INFO,
                          (vm_region_info_t) &info,
                          &info_count, &object_name);

        if (kr != KERN_SUCCESS) {
            break;
        }

        // 计算私有和共享内存
        if (info.share_mode == SM_COW && info.ref_count == 1) {
            info.share_mode = SM_PRIVATE;
        }

        if (info.share_mode == SM_PRIVATE) {
            real_private_memory += info.private_pages_resident * vm_page_size;
        } else if ((info.share_mode == SM_COW || info.share_mode == SM_SHARED) && info.ref_count > 1) {
            // 只统计引用计数大于1的共享内存，这样才与活动监视器显示一致
            // 活动监视器只计算实际被多个进程共享的内存
//            std::cout << "shared_pages_resident:" << (info.shared_pages_resident * vm_page_size /  info.ref_count/1024)
//                    << "KB,count:" << info.ref_count << ", range: 0x" << std::hex << address << "-0x" << (address + size) << std::dec << std::endl;
            real_shared_memory += (info.shared_pages_resident * vm_page_size / info.ref_count);
        }

        address += size;
    }

    // real_shared_memory =  vm_info.external ;
    // real_shared_memory = getProcessSharedMemory(getpid());

    std::cout << "当前进程内存使用情况:" << std::endl;
    std::cout << "  - 物理内存占用 (phys_footprint): " << (vm_info.phys_footprint / 1024 / 1024) << " MB" << std::endl;
    std::cout << "  - 常驻内存大小 (internal): " << (vm_info.internal / 1024 / 1024) << " MB" << std::endl;
    // 计算压缩内存大小，包括已压缩页面和正在压缩的页面
    vm_size_t total_compressed = vm_info.compressed;
    vm_size_t compressed_count = 0;
    while (true) {
        vm_region_top_info_data_t info;
        mach_msg_type_number_t info_count = VM_REGION_TOP_INFO_COUNT;
        mach_port_t object_name;
        vm_address_t region_address = address;
        vm_size_t region_size;

        kr = vm_region_64(task, &region_address, &region_size,
                          VM_REGION_TOP_INFO,
                          (vm_region_info_t) &info,
                          &info_count, &object_name);

        if (kr != KERN_SUCCESS) {
            break;
        }

        // 统计压缩页面
        // compressed_count += info.comppessressed_pages_resident;
        address = region_address + region_size;
    }
    total_compressed += (compressed_count * vm_page_size);
    std::cout << "  - 压缩内存 (compressed): " << (total_compressed / 1024 / 1024) << " MB" << std::endl;
    std::cout << "  - 压缩内存 (compressed): " << (vm_info.compressed / 1024 / 1024) << " MB" << std::endl;
    std::cout << "  - 可清除内存 (purgeable_nonvolatile): "
              << ((vm_info.ledger_purgeable_nonvolatile) /
                  1024 / 1024)
              << " MB" << std::endl;
    std::cout << "  - 可清除内存 (purgeable_nonvolatile1): "
              << ((vm_info.purgeable_volatile_resident + vm_info.purgeable_volatile_virtual) /
                  1024 / 1024)
              << " MB" << std::endl;
    std::cout << "  - 实际私有内存 (real private): " << (real_private_memory / 1024 / 1024) << " MB" << std::endl;
    std::cout << "  - 实际共享内存 (real shared): " << (real_shared_memory / 1024) << " KB" << std::endl;
}