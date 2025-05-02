#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <unistd.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <mach/mach.h>
#include <sys/stat.h>

/*
 * 文件映射修改内容演示程序
 * 
 * 演示通过mmap映射文件并修改内容，观察dirty指标的变化：
 * - 创建临时文件并映射到内存
 * - 记录修改前的内存指标
 * - 修改文件内容，创建脏页
 * - 记录修改后的内存指标
 * - 比较dirty指标的变化
 */

// 获取详细的内存使用信息，包括dirty页面统计
void print_detailed_memory_info() {
    task_t task = mach_task_self();
    
    // 获取基本任务信息
    task_basic_info_data_t basic_info;
    mach_msg_type_number_t count = TASK_BASIC_INFO_COUNT;
    kern_return_t kr = task_info(task, TASK_BASIC_INFO, (task_info_t)&basic_info, &count);
    if (kr != KERN_SUCCESS) {
        std::cerr << "获取基本任务信息失败" << std::endl;
        return;
    }
    
    // 获取扩展任务信息，包括phys_footprint
    task_vm_info_data_t vm_info;
    count = TASK_VM_INFO_COUNT;
    kr = task_info(task, TASK_VM_INFO, (task_info_t)&vm_info, &count);
    if (kr != KERN_SUCCESS) {
        std::cerr << "获取VM任务信息失败" << std::endl;
        return;
    }
    
    std::cout << "内存使用详情:" << std::endl;
    std::cout << "  - 常驻内存大小 (RSS): " << (basic_info.resident_size / 1024 / 1024) << " MB" << std::endl;
    std::cout << "  - 虚拟内存大小 (VSZ): " << (basic_info.virtual_size / 1024 / 1024) << " MB" << std::endl;
    std::cout << "  - 物理内存占用 (phys_footprint): " << (vm_info.phys_footprint / 1024 / 1024) << " MB" << std::endl;
    
    // 输出dirty页面相关信息
    std::cout << "  - 内部内存 (internal): " << (vm_info.internal / 1024 / 1024) << " MB" << std::endl;
    std::cout << "  - 压缩内存: " << (vm_info.compressed / 1024 / 1024) << " MB" << std::endl;
    
    // 在macOS中，dirty页面通常会计入internal指标
    // 对于文件映射，修改后的页面会变为dirty，并最终写回文件
    std::cout << "  - 脏页指标: 在macOS中，脏页通常计入internal指标" << std::endl;
}

// 创建临时文件
std::string create_temp_file(size_t size_mb) {
    std::string filename = "/tmp/file_mapping_demo_" + std::to_string(getpid()) + ".tmp";
    std::ofstream file(filename, std::ios::binary);
    
    if (!file.is_open()) {
        std::cerr << "无法创建临时文件: " << filename << std::endl;
        return "";
    }
    
    // 写入指定大小的数据
    const size_t chunk_size = 1024 * 1024; // 1MB
    std::vector<char> buffer(chunk_size, 'A');
    
    for (size_t i = 0; i < size_mb; i++) {
        file.write(buffer.data(), buffer.size());
    }
    
    file.close();
    std::cout << "创建了" << size_mb << "MB的临时文件: " << filename << std::endl;
    return filename;
}

// 清理临时文件
void cleanup_temp_file(const std::string& filename) {
    if (!filename.empty()) {
        if (remove(filename.c_str()) == 0) {
            std::cout << "已删除临时文件: " << filename << std::endl;
        } else {
            std::cerr << "无法删除临时文件: " << filename << std::endl;
        }
    }
}

int main() {
    std::cout << "=== 文件映射修改内容演示程序 ===" << std::endl;
    std::cout << "此程序演示通过mmap映射文件并修改内容，观察dirty指标的变化" << std::endl;
    
    const size_t file_size_mb = 500; // 文件大小，单位MB
    
    // 创建临时文件
    std::string filename = create_temp_file(file_size_mb);
    if (filename.empty()) return 1;
    
    // 打开文件进行内存映射
    int fd = open(filename.c_str(), O_RDWR);
    if (fd == -1) {
        std::cerr << "无法打开文件: " << filename << std::endl;
        cleanup_temp_file(filename);
        return 1;
    }
    
    // 获取文件大小
    struct stat sb;
    if (fstat(fd, &sb) == -1) {
        std::cerr << "无法获取文件信息" << std::endl;
        close(fd);
        cleanup_temp_file(filename);
        return 1;
    }
    
    const size_t file_size = sb.st_size;
    
    // 映射文件到内存
    void* mapped_memory = mmap(NULL, file_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (mapped_memory == MAP_FAILED) {
        std::cerr << "内存映射失败!" << std::endl;
        close(fd);
        cleanup_temp_file(filename);
        return 1;
    }
    
    std::cout << "\n已映射" << (file_size / 1024 / 1024) << "MB的文件到内存" << std::endl;
    std::cout << "映射地址: " << mapped_memory << std::endl;
    
    // 打印修改前的内存指标
    std::cout << "\n修改前的内存指标:" << std::endl;
    print_detailed_memory_info();
    
    std::cout << "\n按Enter键开始修改文件内容..." << std::endl;
    std::cin.get();
    
    // 修改映射的内存，使其变为脏页
    char* buffer = (char*)mapped_memory;
    std::cout << "修改文件内容，创建脏页..." << std::endl;
    
    // 修改每个页面的第一个字节，创建脏页
    for (size_t i = 0; i < file_size; i += 4096) {
        buffer[i] = 'B'; // 修改每个页面的第一个字节
    }
    
    // 确保修改被写入
    // if (msync(mapped_memory, file_size, MS_SYNC) != 0) {
    //     std::cerr << "msync失败: " << strerror(errno) << std::endl;
    // }
    
    std::cout << "文件内容已修改，创建了脏页" << std::endl;
    
    // 打印修改后的内存指标
    std::cout << "\n修改后的内存指标:" << std::endl;
    print_detailed_memory_info();
    
    std::cout << "\n按Enter键结束程序..." << std::endl;
    std::cin.get();
    
    // 解除映射并关闭文件
    munmap(mapped_memory, file_size);
    close(fd);
    cleanup_temp_file(filename);
    
    std::cout << "程序结束" << std::endl;
    return 0;
}