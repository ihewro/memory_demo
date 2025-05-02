#include <fcntl.h>
#include <fstream>
#include <getopt.h>
#include <iomanip>
#include <iostream>
#import <mach/mach_error.h>
#include <mach/mach_host.h>
#include <mach/mach_init.h>
#import <mach/task.h>
#import <mach/vm_map.h>
#include <sstream>
#include <stdlib.h>
#include <string>
#include <sys/mman.h>
#include <sys/sysctl.h>
#include <sys/xattr.h>
#include <unistd.h>
#include <vector>

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
    kern_return_t kr = task_info(task, TASK_VM_INFO, (task_info_t)&vm_info, &count);
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
                          (vm_region_info_t)&info,
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
//                      << "KB,count:" << info.ref_count << ", range: 0x" << std::hex << address << "-0x" << (address + size) << std::dec << std::endl;
            real_shared_memory += (info.shared_pages_resident * vm_page_size /  info.ref_count);
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
                          (vm_region_info_t)&info,
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
    std::cout << "  - 可清除内存 (purgeable_nonvolatile1): "
              << ((vm_info.ledger_purgeable_nonvolatile) /
                  1024 / 1024)
              << " MB" << std::endl;
    std::cout << "  - 可清除内存 (purgeable_volatile_resident): "
              << (vm_info.purgeable_volatile_resident)
              << " B" << std::endl;
    std::cout << "  - 可清除内存 (ledger_purgeable_volatile): "
              << (vm_info.ledger_purgeable_volatile)
              << " B" << std::endl;
    std::cout << "  - 实际私有内存 (real private): " << (real_private_memory / 1024 / 1024) << " MB" << std::endl;
    std::cout << "  - 实际共享内存 (real shared): " << (real_shared_memory / 1024 ) << " KB" << std::endl;
}

// 等待用户按Enter键继续
void wait_for_user() {
    std::cout << "\n按Enter键继续..." << std::endl;
    std::cin.get();
}
// 格式化输出函数
std::string ParseMB(double bytes) {
    std::stringstream ss;
    ss << std::fixed << std::setprecision(2) << (bytes * 1.0 / 1024 / 1024);
    return ss.str() + " MB";
}

std::string ParseGB(double bytes) {
    std::stringstream ss;
    ss << std::fixed << std::setprecision(2) << (bytes * 1.0 / 1024 / 1024 / 1024);
    return ss.str() + " GB";
}

// 内存指标结构体
struct MemoryMetrics {
    long long appMemory;          // App内存
    long long wiredMemory;        // 联动内存
    long long compressedMemory;   // 被压缩内存
    long long cachedFiles;        // 已缓存文件
    long long freeMemory;         // 空闲内存
    long long swapUsed;           // 已使用的交换
    long long totalMemory;        // 总内存
    vm_statistics64_data_t vm_stat;// 原始数据
    vm_size_t page_size;
};

// 获取系统内存指标
MemoryMetrics GetSystemMemoryMetrics() {
    MemoryMetrics metrics = {0};
    vm_size_t page_size;
    mach_port_t mach_port = mach_host_self();
    mach_msg_type_number_t count = HOST_VM_INFO64_COUNT;
    vm_statistics64_data_t vm_stat;

    if (KERN_SUCCESS == host_page_size(mach_port, &page_size) &&
        KERN_SUCCESS == host_statistics64(mach_port, HOST_VM_INFO64,
                                          (host_info64_t) &vm_stat, &count)) {

        // 计算各项指标
        metrics.vm_stat = vm_stat;
        metrics.page_size = page_size;
        metrics.appMemory = (vm_stat.internal_page_count - vm_stat.purgeable_count) * page_size;
        metrics.wiredMemory = vm_stat.wire_count * page_size;
        metrics.compressedMemory = vm_stat.compressor_page_count * page_size;
        metrics.cachedFiles = (vm_stat.external_page_count + vm_stat.purgeable_count) * page_size;
        metrics.freeMemory = vm_stat.free_count * page_size;
        metrics.totalMemory = (vm_stat.internal_page_count + vm_stat.external_page_count +
                              vm_stat.free_count + vm_stat.compressor_page_count +
                              vm_stat.wire_count) * page_size;

        // 获取交换内存使用情况
        int mib[] = {CTL_VM, VM_SWAPUSAGE};
        struct xsw_usage swap;
        size_t len = sizeof(swap);

        if (sysctl(mib, 2, &swap, &len, NULL, 0) >= 0) {
            metrics.swapUsed = swap.xsu_used;
        }
    }

    return metrics;
}

// 打印内存指标
void PrintMemoryMetrics(const MemoryMetrics& metrics, bool detailed = false) {
    std::cout << "┌────────────────────────────────────┬───────────────────┐" << std::endl;
    std::cout << "│ 系统内存状态                         │                   │" << std::endl;
    std::cout << "├────────────────────────────────────┼───────────────────┤" << std::endl;
    std::cout << "│ " << std::setw(30) << std::left << "App 内存" << "│ " << std::setw(15) << std::left << ParseGB(metrics.appMemory) << " │" << std::endl;
    std::cout << "│ " << std::setw(30) << std::left << "联动内存" << "│ " << std::setw(15) << std::left << ParseGB(metrics.wiredMemory) << " │" << std::endl;
    std::cout << "│ " << std::setw(30) << std::left << "被压缩" << "│ " << std::setw(15) << std::left << ParseGB(metrics.compressedMemory) << " │" << std::endl;
    std::cout << "├────────────────────────────────────┼───────────────────┤" << std::endl;
    std::cout << "│ " << std::setw(30) << std::left << "purgeable" << "│ " << std::setw(15) << std::left << ParseMB(metrics.vm_stat.purgeable_count * metrics.page_size) << " │" << std::endl;
    std::cout << "│ " << std::setw(30) << std::left << "compressor_page_count" << "│ " << std::setw(15) << std::left << ParseGB(metrics.vm_stat.compressor_page_count * metrics.page_size) << " │" << std::endl;
    std::cout << "│ " << std::setw(30) << std::left << "total_uncompressed_pages" << "│ " << std::setw(15) << std::left << ParseGB(metrics.vm_stat.total_uncompressed_pages_in_compressor * metrics.page_size) << " │" << std::endl;
    std::cout << "│ " << std::setw(30) << std::left << "external_page_count" << "│ " << std::setw(15) << std::left << ParseMB(metrics.vm_stat.external_page_count * metrics.page_size) << " │" << std::endl;
    std::cout << "│ " << std::setw(30) << std::left << "speculative_count" << "│ " << std::setw(15) << std::left << ParseMB(metrics.vm_stat.speculative_count * metrics.page_size) << " │" << std::endl;
    std::cout << "├────────────────────────────────────┼───────────────────┤" << std::endl;
    std::cout << "│ " << std::setw(30) << std::left << "已缓存文件" << "│ " << std::setw(15) << std::left << ParseGB(metrics.cachedFiles) << " │" << std::endl;
    std::cout << "│ " << std::setw(30) << std::left << "空闲内存" << "│ " << std::setw(15) << std::left << ParseGB(metrics.freeMemory) << " │" << std::endl;
    std::cout << "│ " << std::setw(30) << std::left << "已使用的交换" << "│ " << std::setw(15) << std::left << ParseGB(metrics.swapUsed) << " │" << std::endl;
    std::cout << "├────────────────────────────────────┼───────────────────┤" << std::endl;
    std::cout << "│ " << std::setw(30) << std::left << "总内存" << "│ " << std::setw(15) << std::left << ParseGB(metrics.totalMemory) << " │" << std::endl;
    std::cout << "└────────────────────────────────────┴───────────────────┘" << std::endl;
}

// 打印指标变化
void PrintMetricsChange(const std::string& label, const std::string& before, const std::string& after) {
    std::cout << "│ " << std::setw(30) << std::left << label
              << "│ " << std::setw(15) << std::left << before
              << "│ " << std::setw(15) << std::left << after << " │" << std::endl;
}

// 分配匿名私有内存
void* AllocatePrivateMemory(size_t size) {
    return mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
}

// 分配匿名共享内存
void* AllocateSharedMemory(size_t size) {
    return mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANON, -1, 0);
}

// 分配可清除内存 (purgeable)
void* AllocatePurgeableMemory(size_t size) {
    void* ptr = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
    if (ptr != MAP_FAILED) {
        // 在macOS中，我们可以使用madvise来标记内存为可清除
        if (madvise(ptr, size, MADV_FREE) != 0) {
            munmap(ptr, size);
            return MAP_FAILED;
        }

        // 写入数据以确保分配
        memset(ptr, 0x42, size);

        // 再次标记为可清除
        madvise(ptr, size, MADV_FREE);
    }
    return ptr;
}

void* AlloctePugealeMemory2(size_t size_bytes){
    // 使用Mach VM接口创建可清除内存
    vm_address_t address = 0;

    // 分配内存
    kern_return_t kr = vm_allocate(mach_task_self(), &address, size_bytes, VM_FLAGS_ANYWHERE | VM_FLAGS_PURGABLE);
    if (kr != KERN_SUCCESS) {
        std::cerr << "内存分配失败: " << mach_error_string(kr) << std::endl;
        return nullptr;
    }

    // 写入数据以确保页面被实际分配
    char* buffer = (char*)address;
    for (size_t i = 0; i < size_bytes; i += 4096) {
        buffer[i] = 'P';
    }

    // 将内存标记为可清除
//    int state = VM_PURGABLE_VOLATILE; // 非易失性可清除
    int state = VM_PURGABLE_NONVOLATILE; // 非易失性可清除
    vm_purgable_control(mach_task_self(), address, VM_PURGABLE_SET_STATE, &state);
    std::cout << "已分配并标记可清除内存" << std::endl;

    return buffer;
//
//    vm_deallocate(mach_task_self(), address, size_bytes);
//    std::cout << "已释放所有内存" << std::endl;
}

// 增加文件缓存
bool IncreaseCachedFiles(size_t size) {
    // 创建临时文件
    std::string tempFile = "/tmp/memory_cache_test_" + std::to_string(getpid());
    std::ofstream file(tempFile, std::ios::binary);
    if (!file) {
        return false;
    }

    // 写入数据
    std::vector<char> buffer(1024 * 1024, 'A'); // 1MB 缓冲区
    for (size_t i = 0; i < size / buffer.size(); i++) {
        file.write(buffer.data(), buffer.size());
    }
    file.close();

    // 读取文件以确保进入缓存
    std::ifstream readFile(tempFile, std::ios::binary);
    if (!readFile) {
        unlink(tempFile.c_str());
        return false;
    }

    char readBuffer[1024];
    while (readFile.read(readBuffer, sizeof(readBuffer))) {}
    readFile.close();

    // 不删除文件，让系统自行管理缓存
    return true;
}

// 测试匿名私有内存分配
void TestPrivateMemory() {
    const size_t size = 200 * 1024 * 1024; // 100MB

    std::cout << "\n=== 测试分配匿名私有内存 (500MB) ===" << std::endl;

    // 获取分配前指标
    MemoryMetrics before = GetSystemMemoryMetrics();

    // 分配内存
    void* ptr = AllocatePrivateMemory(size);
    if (ptr == MAP_FAILED) {
        std::cerr << "分配匿名私有内存失败" << std::endl;
        return;
    }

    // 确保内存被实际分配 (写入数据)
    memset(ptr, 0x42, size);

    // 获取分配后指标
    MemoryMetrics after = GetSystemMemoryMetrics();

    // 打印对比结果
    std::cout << "┌────────────────────────────────────┬─────────────────┬─────────────────┐" << std::endl;
    std::cout << "│ 指标                               │ 分配前          │ 分配后          │" << std::endl;
    std::cout << "├────────────────────────────────────┼─────────────────┼─────────────────┤" << std::endl;
    PrintMetricsChange("App 内存", ParseGB(before.appMemory), ParseGB(after.appMemory));
    std::cout << "└────────────────────────────────────┴─────────────────┴─────────────────┘" << std::endl;

    std::cout << "变化: App内存 " << ParseMB(after.appMemory - before.appMemory) << " (预期约500MB)" << std::endl;

    // 释放内存
    munmap(ptr, size);
}

// 测试匿名共享内存分配
void TestSharedMemory() {
    const size_t size = 500 * 1024 * 1024; // 100MB

    std::cout << "\n=== 测试分配匿名共享内存 (500MB) ===" << std::endl;

    // 获取分配前指标
    MemoryMetrics before = GetSystemMemoryMetrics();

    // 分配内存
    void* ptr = AllocateSharedMemory(size);
    if (ptr == MAP_FAILED) {
        std::cerr << "分配匿名共享内存失败" << std::endl;
        return;
    }

    // 确保内存被实际分配 (写入数据)
    memset(ptr, 0x42, size);

    // 获取分配后指标
    MemoryMetrics after = GetSystemMemoryMetrics();

    // 打印对比结果
    std::cout << "┌────────────────────────────────────┬─────────────────┬─────────────────┐" << std::endl;
    std::cout << "│ 指标                               │ 分配前          │ 分配后          │" << std::endl;
    std::cout << "├────────────────────────────────────┼─────────────────┼─────────────────┤" << std::endl;
    PrintMetricsChange("App 内存", ParseGB(before.appMemory), ParseGB(after.appMemory));
    std::cout << "└────────────────────────────────────┴─────────────────┴─────────────────┘" << std::endl;

    std::cout << "变化: App内存 " << ParseMB(after.appMemory - before.appMemory) << " (预期约500MB)" << std::endl;
    wait_for_user();

    // 释放内存
    munmap(ptr, size);
}

// 测试可清除内存分配
void TestPurgeableMemory() {
    const size_t size = 500 * 1024 * 1024; // 100MB

    std::cout << "\n=== 测试分配可清除内存 (500MB) ===" << std::endl;

    // 获取分配前指标
    MemoryMetrics before = GetSystemMemoryMetrics();
    print_memory_info();
    // 分配内存
//    void* ptr = AllocatePurgeableMemory(size);
    void* ptr = AlloctePugealeMemory2(size);
    if (ptr == MAP_FAILED) {
        std::cerr << "分配可清除内存失败" << std::endl;
        return;
    }

    // 确保内存被实际分配 (写入数据)
//    memset(ptr, 0x42, size);

    sleep(1);
    // 获取分配后指标
    MemoryMetrics after = GetSystemMemoryMetrics();

    // 打印对比结果
    std::cout << "┌────────────────────────────────────┬─────────────────┬─────────────────┐" << std::endl;
    std::cout << "│ 指标                                │ 分配前          │ 分配后          │" << std::endl;
    std::cout << "├────────────────────────────────────┼─────────────────┼─────────────────┤" << std::endl;
    PrintMetricsChange("App 内存", ParseGB(before.appMemory), ParseGB(after.appMemory));
    PrintMetricsChange("已缓存文件", ParseGB(before.cachedFiles), ParseGB(after.cachedFiles));
    PrintMetricsChange("purgeable_count", ParseGB(before.vm_stat.purgeable_count*before.page_size), ParseGB(after.vm_stat.purgeable_count*after.page_size));
    std::cout << "└────────────────────────────────────┴─────────────────┴─────────────────┘" << std::endl;

    std::cout << "变化:\n";
    std::cout << "已缓存文件: " << ParseMB(after.cachedFiles - before.cachedFiles) << " (预期增加约500MB)" << std::endl;
    std::cout << "App内存: " << ParseMB(after.appMemory - before.appMemory) << " (预期变化不大)" << std::endl;
    print_memory_info();

    wait_for_user();
    // 释放内存
    munmap(ptr, size);
}

// 测试私有文件映射
void TestFileMapPrivate(const std::string& filePath = "") {
     size_t size = 500 * 1024 * 1024; // 500MB

    std::cout << "\n=== 测试私有文件映射 (500MB) ===" << std::endl;

    // 使用用户指定的文件路径或创建临时文件
    std::string tempFile;
    bool useExistingFile = !filePath.empty();
    
    if (useExistingFile) {
        tempFile = filePath;
        std::cout << "使用用户指定的文件: " << tempFile << std::endl;
        
        // 检查文件是否存在
        std::ifstream checkFile(tempFile);
        if (!checkFile) {
            std::cerr << "指定的文件不存在" << std::endl;
            return;
        }
        
        // 检查文件大小
        checkFile.seekg(0, std::ios::end);
        size_t fileSize = checkFile.tellg();
        checkFile.close();
        size = fileSize;
        
//        if (fileSize < size) {
//            std::cerr << "指定的文件大小不足500MB，实际大小: " << (fileSize / 1024 / 1024) << "MB" << std::endl;
//            return;
//        }
    } else {
        return;
    }

    // 获取映射前指标
    MemoryMetrics before = GetSystemMemoryMetrics();
    print_memory_info();

    // 打开文件进行映射
    int fd = open(tempFile.c_str(), O_RDONLY);

    if (fd == -1) {
        std::cerr << "打开文件失败" << std::endl;
        return;
    }

    // 私有映射文件
    void* ptr = mmap(NULL, size, PROT_READ , MAP_PRIVATE, fd, 0);
    if (ptr == MAP_FAILED) {
        std::cerr << "映射文件失败" << std::endl;
        close(fd);
        return;
    }

    // 读取整个映射区域以触发页面加载
    volatile char* data = (char*)ptr;
    for (size_t i = 0; i < size; i += 4096) {
        char c = data[i];
        (void)c; // 防止编译器优化掉读取操作
    }

    // 获取映射后指标
    MemoryMetrics after = GetSystemMemoryMetrics();

    // 打印对比结果
    std::cout << "┌────────────────────────────────────┬─────────────────┬─────────────────┐" << std::endl;
    std::cout << "│ 指标                               │ 映射前          │ 映射后          │" << std::endl;
    std::cout << "├────────────────────────────────────┼─────────────────┼─────────────────┤" << std::endl;
    PrintMetricsChange("App 内存", ParseGB(before.appMemory), ParseGB(after.appMemory));
    PrintMetricsChange("已缓存文件", ParseGB(before.cachedFiles), ParseGB(after.cachedFiles));
    std::cout << "└────────────────────────────────────┴─────────────────┴─────────────────┘" << std::endl;

    std::cout << "变化:\n";
    std::cout << "已缓存文件: " << ParseMB(after.cachedFiles - before.cachedFiles) << " (预期增加约500MB)" << std::endl;
    std::cout << "App内存: " << ParseMB(after.appMemory - before.appMemory) << " (预期变化不大)" << std::endl;
    print_memory_info();

    wait_for_user();

    // 清理资源
    munmap(ptr, size);
    close(fd);
}

// 测试增加文件缓存
void TestFileCache() {
    const size_t size = 500 * 1024 * 1024; // 500MB

    std::cout << "\n=== 测试增加文件缓存 (500MB) ===" << std::endl;

    // 获取操作前指标
    MemoryMetrics before = GetSystemMemoryMetrics();

    // 增加文件缓存
    if (!IncreaseCachedFiles(size)) {
        std::cerr << "增加文件缓存失败" << std::endl;
        return;
    }

    // 获取操作后指标
    MemoryMetrics after = GetSystemMemoryMetrics();

    // 打印对比结果
    std::cout << "┌────────────────────────────────────┬─────────────────┬─────────────────┐" << std::endl;
    std::cout << "│ 指标                               │ 操作前          │ 操作后          │" << std::endl;
    std::cout << "├────────────────────────────────────┼─────────────────┼─────────────────┤" << std::endl;
    PrintMetricsChange("已缓存文件", ParseGB(before.cachedFiles), ParseGB(after.cachedFiles));
    std::cout << "└────────────────────────────────────┴─────────────────┴─────────────────┘" << std::endl;

    std::cout << "变化: 已缓存文件 " << ParseMB(after.cachedFiles - before.cachedFiles) << " (预期约500MB)" << std::endl;
}

// 持续监控内存状态
void MonitorMemory() {
    std::cout << "进入内存监控模式，按Ctrl+C退出..." << std::endl;

    while (true) {
        MemoryMetrics metrics = GetSystemMemoryMetrics();
        PrintMemoryMetrics(metrics);
        std::cout << std::endl;
        sleep(1);
        system("clear"); // 清屏以便更新显示
    }
}


// 测试文件映射 (MAP_SHARED) 不修改内容
void TestFileMapSharedNoModify(const std::string& filePath = "") {
    size_t size = 500 * 1024 * 1024; // 500MB

    std::cout << "\n=== 测试私有文件映射 (500MB) ===" << std::endl;

    // 使用用户指定的文件路径或创建临时文件
    std::string tempFile;
    bool useExistingFile = !filePath.empty();

    if (useExistingFile) {
        tempFile = filePath;
        std::cout << "使用用户指定的文件: " << tempFile << std::endl;

        // 检查文件是否存在
        std::ifstream checkFile(tempFile);
        if (!checkFile) {
            std::cerr << "指定的文件不存在" << std::endl;
            return;
        }

        // 检查文件大小
        checkFile.seekg(0, std::ios::end);
        size_t fileSize = checkFile.tellg();
        checkFile.close();
        size = fileSize;


    } else {
        return;
    }

    // 获取映射前指标
    MemoryMetrics before = GetSystemMemoryMetrics();
    print_memory_info();

    // 打开文件进行映射
    int fd = open(tempFile.c_str(), O_RDONLY);

    if (fd == -1) {
        std::cerr << "打开文件失败" << std::endl;
        return;
    }

    // 私有映射文件
    void* ptr = mmap(NULL, size, PROT_READ , MAP_SHARED, fd, 0);
    if (ptr == MAP_FAILED) {
        std::cerr << "映射文件失败" << std::endl;
        close(fd);
        return;
    }

    // 读取整个映射区域以触发页面加载
    volatile char* data = (char*)ptr;
    for (size_t i = 0; i < size; i += 4096) {
        char c = data[i];
        (void)c; // 防止编译器优化掉读取操作
    }

    // 获取写入后内存指标
    std::cout << "\n读入内存状态:" << std::endl;
    MemoryMetrics after = GetSystemMemoryMetrics();
    PrintMemoryMetrics(after);
    print_memory_info();


    // 打印对比结果
    std::cout << "\n┌────────────────────────────────────┬─────────────────┬─────────────────┬─────────────────┐" << std::endl;
    std::cout << "│ 指标                               │ 映射前          │ 映射后但写入前  │ 写入后          │" << std::endl;
    std::cout << "├────────────────────────────────────┼─────────────────┼─────────────────┼─────────────────┤" << std::endl;
    std::cout << "│ " << std::setw(30) << std::left << "App 内存"
              << "│ " << std::setw(15) << std::left << ParseGB(before.appMemory)
              << "│ " << std::setw(15) << std::left << ParseGB(after.appMemory) << " │" << std::endl;
    std::cout << "│ " << std::setw(30) << std::left << "已缓存文件"
              << "│ " << std::setw(15) << std::left << ParseGB(before.cachedFiles)
              << "│ " << std::setw(15) << std::left << ParseGB(after.cachedFiles) << " │" << std::endl;
    std::cout << "│ " << std::setw(30) << std::left << "external_page_count"
              << "│ " << std::setw(15) << std::left << ParseMB(before.vm_stat.external_page_count * before.page_size)
              << "│ " << std::setw(15) << std::left << ParseMB(after.vm_stat.external_page_count * after.page_size) << " │" << std::endl;
    std::cout << "└────────────────────────────────────┴─────────────────┴─────────────────┴─────────────────┘" << std::endl;

    std::cout << "\n变化分析:" << std::endl;
    std::cout << "写入后:" << std::endl;
    std::cout << "  - 已缓存文件: " << ParseMB(after.cachedFiles - before.cachedFiles) << " (预期增加约500MB)" << std::endl;
    std::cout << "  - App内存: " << ParseMB(after.appMemory - before.appMemory) << " (预期变化不大)" << std::endl;
    std::cout << "  - external_page_count: " << ParseMB((after.vm_stat.external_page_count - before.vm_stat.external_page_count) * before.page_size) << " (预期增加500MB)" << std::endl;

    std::cout << "\n注意: 使用MAP_SHARED映射文件但不修改内容时," << std::endl;
    std::cout << "系统会在写入数据后将文件内容缓存在页面缓存中,这部分内存会计入'已缓存文件'而非'App内存'" << std::endl;

    wait_for_user();

    // 解除映射并清理
    munmap(ptr, size);
    close(fd);
    std::cout << "已解除映射" << std::endl;
}

// 测试共享文件映射 (MAP_SHARED) 并修改内容
void TestFileMapSharedModify(const std::string& filePath = "") {
    size_t size = 500 * 1024 * 1024; // 500MB

    std::cout << "\n=== 测试私有文件映射 (500MB) ===" << std::endl;

    // 使用用户指定的文件路径或创建临时文件
    std::string tempFile;
    bool useExistingFile = !filePath.empty();

    if (useExistingFile) {
        tempFile = filePath;
        std::cout << "使用用户指定的文件: " << tempFile << std::endl;

        // 检查文件是否存在
        std::ifstream checkFile(tempFile);
        if (!checkFile) {
            std::cerr << "指定的文件不存在" << std::endl;
            return;
        }

        // 检查文件大小
        checkFile.seekg(0, std::ios::end);
        size_t fileSize = checkFile.tellg();
        checkFile.close();
        size = fileSize;


    } else {
        return;
    }

    // 获取映射前指标
    MemoryMetrics before = GetSystemMemoryMetrics();
    print_memory_info();

    // 打开文件进行映射（不截断文件内容）
    int fd = open(tempFile.c_str(), O_RDWR, 0644);

    if (fd == -1) {
        std::cerr << "打开文件失败" << std::endl;
        return;
    }

    // 共享映射文件
    void* mapped_memory = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (mapped_memory == MAP_FAILED) {
        std::cerr << "映射文件失败" << std::endl;
        close(fd);
        return;
    }

    // 获取映射后但写入前内存指标
    std::cout << "\n映射后但写入前内存状态:" << std::endl;
    MemoryMetrics after_map = GetSystemMemoryMetrics();
    PrintMemoryMetrics(after_map);
    print_memory_info();

    // 通过映射区域填充文件内容并修改
    std::cout << "通过映射区域填充文件内容并修改..." << std::endl;
    char* mem = (char*)mapped_memory;
    for (size_t i = 0; i < size; i += 4096) {
        mem[i] = 'B'; // 写入并修改每个页面的第一个字节
    }

    // 确保修改被写入
    msync(mapped_memory, size, MS_SYNC);

    // 获取写入后内存指标
    std::cout << "\n写入后内存状态:" << std::endl;
    MemoryMetrics after = GetSystemMemoryMetrics();
    PrintMemoryMetrics(after);
    print_memory_info();

    // 打印对比结果
    std::cout << "\n┌────────────────────────────────────┬─────────────────┬─────────────────┬─────────────────┐" << std::endl;
    std::cout << "│ 指标                               │ 映射前          │ 映射后但写入前  │ 写入后          │" << std::endl;
    std::cout << "├────────────────────────────────────┼─────────────────┼─────────────────┼─────────────────┤" << std::endl;
    std::cout << "│ " << std::setw(30) << std::left << "App 内存"
              << "│ " << std::setw(15) << std::left << ParseGB(before.appMemory)
              << "│ " << std::setw(15) << std::left << ParseGB(after_map.appMemory)
              << "│ " << std::setw(15) << std::left << ParseGB(after.appMemory) << " │" << std::endl;
    std::cout << "│ " << std::setw(30) << std::left << "已缓存文件"
              << "│ " << std::setw(15) << std::left << ParseGB(before.cachedFiles)
              << "│ " << std::setw(15) << std::left << ParseGB(after_map.cachedFiles)
              << "│ " << std::setw(15) << std::left << ParseGB(after.cachedFiles) << " │" << std::endl;
    std::cout << "│ " << std::setw(30) << std::left << "external_page_count"
              << "│ " << std::setw(15) << std::left << ParseMB(before.vm_stat.external_page_count * before.page_size)
              << "│ " << std::setw(15) << std::left << ParseMB(after_map.vm_stat.external_page_count * after_map.page_size)
              << "│ " << std::setw(15) << std::left << ParseMB(after.vm_stat.external_page_count * after.page_size) << " │" << std::endl;
    std::cout << "└────────────────────────────────────┴─────────────────┴─────────────────┴─────────────────┘" << std::endl;

    std::cout << "\n变化分析:" << std::endl;
    std::cout << "映射后但写入前:" << std::endl;
    std::cout << "  - 已缓存文件: " << ParseMB(after_map.cachedFiles - before.cachedFiles) << " (预期变化不大)" << std::endl;
    std::cout << "  - App内存: " << ParseMB(after_map.appMemory - before.appMemory) << " (预期变化不大)" << std::endl;
    std::cout << "  - external_page_count: " << ParseMB((after_map.vm_stat.external_page_count - before.vm_stat.external_page_count) * before.page_size) << " (预期变化不大)" << std::endl;

    std::cout << "写入后:" << std::endl;
    std::cout << "  - 已缓存文件: " << ParseMB(after.cachedFiles - after_map.cachedFiles) << " (预期增加约500MB)" << std::endl;
    std::cout << "  - App内存: " << ParseMB(after.appMemory - after_map.appMemory) << " (预期增加)" << std::endl;
    std::cout << "  - external_page_count: " << ParseMB((after.vm_stat.external_page_count - after_map.vm_stat.external_page_count) * before.page_size) << " (预期增加)" << std::endl;

    std::cout << "\n注意: 使用MAP_SHARED映射文件并修改内容时," << std::endl;
    std::cout << "系统会在写入数据后将文件内容缓存在页面缓存中,这部分内存会计入'已缓存文件'" << std::endl;
    std::cout << "当页面被修改时,它们会同时影响'已缓存文件'和'App内存',因为修改的页面需要被写回文件" << std::endl;
    std::cout << "通过先映射再写入的方式,我们可以清晰观察到缓存内存的变化过程" << std::endl;

    wait_for_user();

    // 解除映射并清理
    munmap(mapped_memory, size);
    close(fd);
    std::cout << "已解除映射并删除临时文件" << std::endl;
}

// 显示帮助信息
void ShowHelp(const char* programName) {
    std::cout << "用法: " << programName << " [选项]" << std::endl;
    std::cout << "选项:" << std::endl;
    std::cout << "  -h, --help       显示此帮助信息" << std::endl;
    std::cout << "  -m, --monitor    持续监控系统内存状态" << std::endl;
    std::cout << "  -p, --private    测试私有文件映射 (500MB)" << std::endl;
    std::cout << "  -i, --file       指定用于测试的文件路径" << std::endl;
    std::cout << "  -s, --shared     测试分配500MB匿名共享内存" << std::endl;
    std::cout << "  -g, --purgeable  测试分配500MB可清除内存" << std::endl;
    std::cout << "  -c, --cache      测试增加500MB文件缓存" << std::endl;
    std::cout << "  -f, --file-map   测试文件映射 (MAP_SHARED) 不修改内容" << std::endl;
    std::cout << "  -w, --file-write 测试文件映射 (MAP_SHARED) 并修改内容" << std::endl;
    std::cout << "  -a, --all        执行所有测试" << std::endl;
}

int main(int argc, char* argv[]) {
    // 打印初始内存状态
    std::cout << "初始内存状态:" << std::endl;
    MemoryMetrics initial = GetSystemMemoryMetrics();
    PrintMemoryMetrics(initial);
    print_memory_info();
    wait_for_user();

    // 定义命令行选项
    static struct option long_options[] = {
        {"help", no_argument, 0, 'h'},
        {"monitor", no_argument, 0, 'm'},
        {"private", no_argument, 0, 'p'},
        {"shared", no_argument, 0, 's'},
        {"purgeable", no_argument, 0, 'g'},
        {"cache", no_argument, 0, 'c'},
        {"file-map-private", no_argument, 0, 'v'},
        {"file-map", no_argument, 0, 'f'},
        {"file-write", no_argument, 0, 'w'},
        {"all", no_argument, 0, 'a'},
        {"file", required_argument, 0, 'i'},
        {0, 0, 0, 0}
    };

    // 如果没有参数，显示当前内存状态
    if (argc == 1) {
        MemoryMetrics metrics = GetSystemMemoryMetrics();
        PrintMemoryMetrics(metrics);
        return 0;
    }

    // 解析命令行参数
    int opt;
    int option_index = 0;
    bool runPrivate = false;
    bool runPrivateMap = false;
    bool runShared = false;
    bool runPurgeable = false;
    bool runCache = false;
    bool runFileMap = false;
    bool runFileMapModify = false;
    std::string userFilePath = "";

    while ((opt = getopt_long(argc, argv, "vhmpsagcfwi:", long_options, &option_index)) != -1) {
        switch (opt) {
            case 'h':
                ShowHelp(argv[0]);
                return 0;
            case 'm':
                MonitorMemory();
                return 0;
            case 'p':
                runPrivate = true;
                break;
            case 'v':
                runPrivateMap = true;
                break;
            case 's':
                runShared = true;
                break;
            case 'g':
                runPurgeable = true;
                break;
            case 'c':
                runCache = true;
                break;
            case 'f':
                runFileMap = true;
                break;
            case 'w':
                runFileMapModify = true;
                break;
            case 'a':
                runPrivateMap = runPrivate = runShared = runPurgeable = runCache = runFileMap = runFileMapModify = true;
                break;
            case 'i':
                userFilePath = optarg;
                break;
            default:
                ShowHelp(argv[0]);
                return 1;
        }
    }

    // 执行选定的测试
    if (runPrivate) {
        TestPrivateMemory();
    }

    if (runPrivateMap){
        TestFileMapPrivate(userFilePath);
    }

    if (runShared) {
        TestSharedMemory();
    }

    if (runPurgeable) {
        TestPurgeableMemory();
    }

    if (runCache) {
        TestFileCache();
    }

    if (runFileMap) {
        TestFileMapSharedNoModify(userFilePath);
    }

    if (runFileMapModify) {
        TestFileMapSharedModify(userFilePath);
    }


    return 0;
}
