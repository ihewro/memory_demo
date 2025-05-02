#include <iostream>
#include <vector>
#include <unistd.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <string>
#include <fstream>
#include <thread>
#include <chrono>
#include <IOKit/IOKitLib.h>
#include <mach/mach.h>
#include <sys/stat.h>
#include <sys/shm.h>
#include <sys/wait.h>
#include <errno.h>
#include <sys/mman.h>

/*
 * XNU 内核内存占用指标演示程序
 * 
 * 演示 XNU 内核中定义的各种内存占用指标：
 * - phys_footprint: 物理内存占用总和
 * - internal: 匿名内存，在iOS上始终驻留
 * - internal_compressed: 被压缩器持有的内部内存
 * - iokit_mapped: IOKit映射的总大小
 * - alternate_accounting: IOKit映射中的内部脏页
 * - purgeable_nonvolatile: 可清除的非易失性内存
 * - page_table: 页表内存
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

// 用于创建临时文件的辅助函数
std::string create_temp_file(size_t size_mb) {
    std::string filename = "/tmp/memory_demo_" + std::to_string(getpid()) + ".tmp";
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

// 1. 演示internal内存（匿名内存）
void demo_internal_memory(size_t size_mb) {
    std::cout << "\n=== 演示internal内存（匿名内存）===" << std::endl;
    std::cout << "分配" << size_mb << "MB的匿名内存..." << std::endl;
    
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
    std::cout << "这部分内存会计入internal指标" << std::endl;
    print_memory_info();
    std::cout << "按Enter键继续..." << std::endl;
    std::cin.get();
    
    // 释放内存
    free(buffer);
    std::cout << "已释放匿名内存" << std::endl;
}

// 2. 演示文件映射内存（file-backed memory）
void demo_file_mapped_memory(size_t size_mb) {
    std::cout << "\n=== 演示文件映射内存 ===" << std::endl;
    
    // 创建临时文件
    std::string filename = create_temp_file(size_mb);
    if (filename.empty()) return;
    
    // 打开文件进行内存映射
    int fd = open(filename.c_str(), O_RDWR);
    if (fd == -1) {
        std::cerr << "无法打开文件: " << filename << std::endl;
        cleanup_temp_file(filename);
        return;
    }
    
    // 映射文件到内存
    const size_t size_bytes = size_mb * 1024 * 1024;
    void* mapped_memory = mmap(NULL, size_bytes, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    
    if (mapped_memory == MAP_FAILED) {
        std::cerr << "内存映射失败!" << std::endl;
        close(fd);
        cleanup_temp_file(filename);
        return;
    }
    
    std::cout << "已映射" << size_mb << "MB的文件到内存" << std::endl;
    std::cout << "这部分内存不会计入internal指标，而是external" << std::endl;
    
    // 修改映射的内存，使其变为脏页
    char* buffer = (char*)mapped_memory;
    for (size_t i = 0; i < size_bytes; i += 4096) {
        buffer[i] = 'B'; // 修改每个页面的第一个字节
    }
    
    std::cout << "已修改映射内存，创建脏页" << std::endl;
    print_memory_info();
    std::cout << "按Enter键继续..." << std::endl;
    std::cin.get();
    
    // 解除映射并关闭文件
    munmap(mapped_memory, size_bytes);
    close(fd);
    cleanup_temp_file(filename);
    std::cout << "已解除文件映射" << std::endl;
}

// 3. 演示匿名内存映射（anonymous mapping）
void demo_anonymous_mapping(size_t size_mb) {
    std::cout << "\n=== 演示匿名内存映射 ===" << std::endl;
    
    const size_t size_bytes = size_mb * 1024 * 1024;
    void* mapped_memory = mmap(NULL, size_bytes, PROT_READ | PROT_WRITE, 
                             MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    
    if (mapped_memory == MAP_FAILED) {
        std::cerr << "匿名内存映射失败!" << std::endl;
        return;
    }
    
    // 写入数据以确保页面被实际分配
    char* buffer = (char*)mapped_memory;
    for (size_t i = 0; i < size_bytes; i += 4096) {
        buffer[i] = 'C';
    }
    
    std::cout << "已分配并写入" << size_mb << "MB的匿名映射内存" << std::endl;
    std::cout << "这部分内存会计入internal指标" << std::endl;
    print_memory_info();
    std::cout << "按Enter键继续..." << std::endl;
    std::cin.get();
    
    // 解除映射
    munmap(mapped_memory, size_bytes);
    std::cout << "已解除匿名内存映射" << std::endl;
}

// 4. 演示内存压缩（memory compression）
void demo_memory_compression(size_t size_mb) {
    std::cout << "\n=== 演示内存压缩 (internal_compressed) ===" << std::endl;
    
    // 分配大量内存以触发内存压力
    const size_t size_bytes = size_mb * 1024 * 1024;
    char* buffer = (char*)malloc(size_bytes);
    
    if (buffer == nullptr) {
        std::cerr << "内存分配失败!" << std::endl;
        return;
    }
    
    // 写入可压缩的数据（重复模式）
    std::cout << "写入高度可压缩的数据..." << std::endl;
    for (size_t i = 0; i < size_bytes; i++) {
        buffer[i] = 'Z'; // 使用相同的字符使数据高度可压缩
    }
    
    std::cout << "已分配并写入" << size_mb << "MB的高度可压缩内存" << std::endl;
    std::cout << "这部分内存会计入internal指标" << std::endl;
    
    // 创建更多内存压力以触发压缩
    std::cout << "创建内存压力以触发压缩..." << std::endl;
    std::vector<void*> pressure_buffers;
    const size_t pressure_chunk = 50; // 每次分配50MB
    
    for (int i = 0; i < 10; i++) { // 尝试分配额外的500MB
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
    
    std::cout << "此时部分内存可能已被压缩，计入internal_compressed指标" << std::endl;
    print_memory_info();
    std::cout << "按Enter键继续..." << std::endl;
    std::cin.get();
    
    // 释放所有内存
    for (void* ptr : pressure_buffers) {
        free(ptr);
    }
    free(buffer);
    std::cout << "已释放所有内存" << std::endl;
}

// 5. 演示页表内存（page table）
void demo_page_table(size_t size_mb, size_t chunk_size_kb) {
    std::cout << "\n=== 演示页表内存 ===" << std::endl;
    
    const size_t chunk_bytes = chunk_size_kb * 1024;
    const size_t num_chunks = (size_mb * 1024 * 1024) / chunk_bytes;
    
    std::cout << "分配" << num_chunks << "个不连续的内存块，每块" 
              << chunk_size_kb << "KB，总计" << size_mb << "MB" << std::endl;
    
    // 存储所有分配的内存块指针
    std::vector<void*> memory_chunks;
    
    // 分配多个小块内存，增加页表开销
    for (size_t i = 0; i < num_chunks; i++) {
        void* chunk = malloc(chunk_bytes);
        if (chunk != nullptr) {
            // 写入数据以确保页面被实际分配
            char* p = (char*)chunk;
            for (size_t j = 0; j < chunk_bytes; j += 4096) {
                p[j] = 'T';
            }
            memory_chunks.push_back(chunk);
        } else {
            std::cerr << "内存分配失败，已分配" << i << "个块" << std::endl;
            break;
        }
    }
    
    std::cout << "成功分配" << memory_chunks.size() << "个内存块" << std::endl;
    std::cout << "这种分散的内存分配会增加页表开销，计入page_table指标" << std::endl;
    print_memory_info();
    std::cout << "按Enter键继续..." << std::endl;
    std::cin.get();
    
    // 释放所有内存块
    for (void* chunk : memory_chunks) {
        free(chunk);
    }
    std::cout << "已释放所有内存块" << std::endl;
}

// 6. 演示IOKit内存映射
void demo_iokit_mapping() {
    std::cout << "\n=== 演示IOKit内存映射 (iokit_mapped 和 alternate_accounting) ===" << std::endl;
    
    // 使用Metal框架间接创建IOKit内存映射
    std::cout << "使用Metal框架间接创建IOKit内存映射..." << std::endl;
    
    // 创建一个临时Metal程序来触发IOKit映射
    std::string metal_code = "#include <metal_stdlib>\n"
    "using namespace metal;\n\n"
    "kernel void test_kernel(device float* buffer [[buffer(0)]],\n"
    "                       uint id [[thread_position_in_grid]]) {\n"
    "    buffer[id] = float(id);\n"
    "}\n";
    
    // 将Metal代码写入临时文件
    std::string metal_file = "/tmp/test_kernel_" + std::to_string(getpid()) + ".metal";
    std::ofstream metal_out(metal_file);
    if (!metal_out.is_open()) {
        std::cerr << "无法创建Metal源文件" << std::endl;
        return;
    }
    metal_out << metal_code;
    metal_out.close();
    
    // 编译Metal代码
    std::string compile_cmd = "xcrun -sdk macosx metal -c " + metal_file + " -o /tmp/test_kernel.air";
    std::cout << "编译Metal代码..." << std::endl;
    int compile_result = system(compile_cmd.c_str());
    
    if (compile_result != 0) {
        std::cerr << "Metal编译失败" << std::endl;
        remove(metal_file.c_str());
        return;
    }
    
    // 创建Metal库
    std::string lib_cmd = "xcrun -sdk macosx metallib /tmp/test_kernel.air -o /tmp/test_kernel.metallib";
    std::cout << "创建Metal库..." << std::endl;
    int lib_result = system(lib_cmd.c_str());
    
    if (lib_result != 0) {
        std::cerr << "创建Metal库失败" << std::endl;
        remove(metal_file.c_str());
        remove("/tmp/test_kernel.air");
        return;
    }
    
    // 使用Objective-C运行时调用Metal API
    std::string run_metal_cmd = "/usr/bin/osascript -e '"
    "tell application \"Terminal\" "
    "do script \""
    "cat > /tmp/run_metal.m << EOL"
    "#import <Foundation/Foundation.h>"
    "#import <Metal/Metal.h>"
    ""
    "int main() {"
    "    @autoreleasepool {"
    "        // 获取默认Metal设备"
    "        id<MTLDevice> device = MTLCreateSystemDefaultDevice();"
    "        if (!device) {"
    "            NSLog(@\\\"无法获取Metal设备\\\");"
    "            return 1;"
    "        }"
    ""
    "        // 加载Metal库"
    "        NSError *error = nil;"
    "        id<MTLLibrary> library = [device newLibraryWithFile:@\\\"/tmp/test_kernel.metallib\\\" error:&error];"
    "        if (!library) {"
    "            NSLog(@\\\"加载Metal库失败: %@\\\", error);"
    "            return 1;"
    "        }"
    ""
    "        // 获取内核函数"
    "        id<MTLFunction> function = [library newFunctionWithName:@\\\"test_kernel\\\"];"
    "        if (!function) {"
    "            NSLog(@\\\"获取内核函数失败\\\");"
    "            return 1;"
    "        }"
    ""
    "        // 创建计算管道"
    "        id<MTLComputePipelineState> pipelineState = [device newComputePipelineStateWithFunction:function error:&error];"
    "        if (!pipelineState) {"
    "            NSLog(@\\\"创建计算管道失败: %@\\\", error);"
    "            return 1;"
    "        }"
    ""
    "        // 创建命令队列"
    "        id<MTLCommandQueue> commandQueue = [device newCommandQueue];"
    "        if (!commandQueue) {"
    "            NSLog(@\\\"创建命令队列失败\\\");"
    "            return 1;"
    "        }"
    ""
    "        // 创建缓冲区 (这将触发IOKit内存映射)"
    "        const NSUInteger bufferSize = 256 * 1024 * 1024; // 256MB"
    "        id<MTLBuffer> buffer = [device newBufferWithLength:bufferSize options:MTLResourceStorageModeShared];"
    "        if (!buffer) {"
    "            NSLog(@\\\"创建缓冲区失败\\\");"
    "            return 1;"
    "        }"
    ""
    "        NSLog(@\\\"成功创建Metal缓冲区，大小: %lu MB\\\", bufferSize / 1024 / 1024);"
    "        NSLog(@\\\"这将在底层创建IOKit内存映射，计入iokit_mapped指标\\\");"
    ""
    "        // 创建命令缓冲区"
    "        id<MTLCommandBuffer> commandBuffer = [commandQueue commandBuffer];"
    "        if (!commandBuffer) {"
    "            NSLog(@\\\"创建命令缓冲区失败\\\");"
    "            return 1;"
    "        }"
    ""
    "        // 创建计算命令编码器"
    "        id<MTLComputeCommandEncoder> computeEncoder = [commandBuffer computeCommandEncoder];"
    "        if (!computeEncoder) {"
    "            NSLog(@\\\"创建计算命令编码器失败\\\");"
    "            return 1;"
    "        }"
    ""
    "        // 设置计算管道"
    "        [computeEncoder setComputePipelineState:pipelineState];"
    "        [computeEncoder setBuffer:buffer offset:0 atIndex:0];"
    ""
    "        // 设置线程组大小"
    "        MTLSize gridSize = MTLSizeMake(bufferSize / sizeof(float), 1, 1);"
    "        MTLSize threadGroupSize = MTLSizeMake(pipelineState.maxTotalThreadsPerThreadgroup, 1, 1);"
    "        [computeEncoder dispatchThreads:gridSize threadsPerThreadgroup:threadGroupSize];"
    ""
    "        // 结束编码"
    "        [computeEncoder endEncoding];"
    ""
    "        // 提交命令缓冲区"
    "        [commandBuffer commit];"
    "        [commandBuffer waitUntilCompleted];"
    ""
    "        // 验证结果"
    "        float *data = (float *)[buffer contents];"
    "        NSLog(@\\\"缓冲区前10个元素:\\\");"
    "        for (int i = 0; i < 10; i++) {"
    "            NSLog(@\\\"%d: %f\\\", i, data[i]);"
    "        }"
    ""
    "        NSLog(@\\\"Metal操作完成，IOKit内存映射已创建\\\");"
    "        NSLog(@\\\"按Enter键继续...\\\");"
    "        char c;"
    "        scanf(\\\"%c\\\", &c);"
    ""
    "        // 释放资源 (ARC会自动处理)"
    "    }"
    "    return 0;"
    "}"
    "EOL"
    ""
    "clang -framework Foundation -framework Metal -fobjc-arc /tmp/run_metal.m -o /tmp/run_metal"
    "/tmp/run_metal"
    "\" "
    "end tell"
    "'";
    
    std::cout << "运行Metal程序创建IOKit内存映射..." << std::endl;
    system(run_metal_cmd.c_str());
    
    // 清理临时文件
    remove(metal_file.c_str());
    remove("/tmp/test_kernel.air");
    remove("/tmp/test_kernel.metallib");
    remove("/tmp/run_metal.m");
    remove("/tmp/run_metal");
    
    std::cout << "Metal程序已完成，IOKit内存映射演示结束" << std::endl;
    std::cout << "在实际应用中，图形和计算应用会通过Metal、OpenGL或其他框架" << std::endl;
    std::cout << "间接创建IOKit内存映射，这些映射会计入iokit_mapped指标" << std::endl;
    std::cout << "当这些映射包含脏页时，会同时计入alternate_accounting指标" << std::endl;
    
    print_memory_info();
}

// 7. 演示匿名共享内存（anonymous shared memory）
void demo_anonymous_shared_memory(size_t size_mb) {
    std::cout << "\n=== 演示匿名共享内存 (anonymous shared memory) ===" << std::endl;
    std::cout << "匿名共享内存是没有关联文件的共享内存区域，常用于进程间通信" << std::endl;
    std::cout << "使用场景：数据库缓存、高性能计算、实时系统、图形处理等" << std::endl;
    
    // 创建匿名共享内存
    const size_t size_bytes = size_mb * 1024 * 1024;
    void* shared_memory = mmap(NULL, size_bytes, PROT_READ | PROT_WRITE, 
                             MAP_ANONYMOUS | MAP_SHARED, -1, 0);
    
    if (shared_memory == MAP_FAILED) {
        std::cerr << "匿名共享内存映射失败: " << strerror(errno) << std::endl;
        return;
    }
    
    std::cout << "已创建并映射" << size_mb << "MB的匿名共享内存" << std::endl;
    
    // 写入数据到共享内存
    char* buffer = (char*)shared_memory;
    for (size_t i = 0; i < size_bytes; i += 4096) {
        buffer[i] = 'A'; // 写入每个页面的第一个字节
    }
    
    std::cout << "已写入数据到匿名共享内存" << std::endl;
    std::cout << "匿名共享内存可以被父子进程共享，但不能被无关进程访问" << std::endl;
    std::cout << "在内存指标中，匿名共享内存会计入phys_footprint和internal" << std::endl;
    
    print_memory_info();
    
    // 创建一个子进程来演示匿名共享内存的共享特性
    pid_t pid = fork();
    
    if (pid == -1) {
        std::cerr << "创建子进程失败" << std::endl;
    } else if (pid == 0) {
        // 子进程
        std::cout << "\n子进程 (PID: " << getpid() << ") 访问匿名共享内存" << std::endl;
        std::cout << "子进程读取匿名共享内存的第一个字节: '" << buffer[0] << "'" << std::endl;
        
        // 修改共享内存中的数据
        for (size_t i = 0; i < size_bytes; i += 4096) {
            buffer[i] = 'B'; // 子进程修改每个页面的第一个字节
        }
        
        std::cout << "子进程修改匿名共享内存的第一个字节为: '" << buffer[0] << "'" << std::endl;
        std::cout << "按Enter键退出子进程..." << std::endl;
        std::cin.get();
        std::cout << "子进程退出" << std::endl;
        exit(0);
    } else {
        // 父进程
        // 等待子进程完成
        int status;
        waitpid(pid, &status, 0);
        
        std::cout << "\n父进程检查匿名共享内存" << std::endl;
        std::cout << "父进程读取匿名共享内存的第一个字节 (应该被子进程修改): '" << buffer[0] << "'" << std::endl;
    }
    
    std::cout << "按Enter键继续..." << std::endl;
    std::cin.get();
    
    // 解除映射
    munmap(shared_memory, size_bytes);
    
    std::cout << "已解除匿名共享内存映射" << std::endl;
}

// 8. 演示共享内存（shared memory）
void demo_shared_memory(size_t size_mb) {
    std::cout << "\n=== 演示共享内存 (shared memory) ===" << std::endl;
    
    // 创建共享内存名称
    std::string shm_name = "/xnu_footprint_demo_" + std::to_string(getpid());
    
    // 创建共享内存对象
    const size_t size_bytes = size_mb * 1024 * 1024;
    int fd = shm_open(shm_name.c_str(), O_CREAT | O_RDWR, 0666);
    
    if (fd == -1) {
        std::cerr << "无法创建共享内存对象: " << strerror(errno) << std::endl;
        return;
    }
    
    // 设置共享内存大小
    if (ftruncate(fd, size_bytes) == -1) {
        std::cerr << "无法设置共享内存大小: " << strerror(errno) << std::endl;
        close(fd);
        shm_unlink(shm_name.c_str());
        return;
    }
    
    // 映射共享内存到进程地址空间
    void* shared_memory = mmap(NULL, size_bytes, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    
    if (shared_memory == MAP_FAILED) {
        std::cerr << "共享内存映射失败: " << strerror(errno) << std::endl;
        close(fd);
        shm_unlink(shm_name.c_str());
        return;
    }
    
    std::cout << "已创建并映射" << size_mb << "MB的共享内存" << std::endl;
    
    // 写入数据到共享内存
    char* buffer = (char*)shared_memory;
    for (size_t i = 0; i < size_bytes; i += 4096) {
        buffer[i] = 'S'; // 写入每个页面的第一个字节
    }
    
    std::cout << "已写入数据到共享内存" << std::endl;
    std::cout << "共享内存可以被多个进程访问，这是进程间通信的有效方式" << std::endl;
    std::cout << "在macOS上，共享内存会计入phys_footprint，但只会在第一个映射它的进程中计入internal" << std::endl;
    std::cout << "后续映射此内存的进程不会增加系统总体内存使用量" << std::endl;
    
    print_memory_info();
    
    // 创建一个子进程来演示共享内存的共享特性
    pid_t pid = fork();
    
    if (pid == -1) {
        std::cerr << "创建子进程失败" << std::endl;
    } else if (pid == 0) {
        // 子进程
        std::cout << "\n子进程 (PID: " << getpid() << ") 访问共享内存" << std::endl;
        std::cout << "子进程读取共享内存的第一个字节: '" << buffer[0] << "'" << std::endl;
        
        // 修改共享内存中的一些数据
        // buffer[0] = 'C'; // 子进程修改
        for (size_t i = 0; i < size_bytes; i += 4096) {
            buffer[i] = 'C'; // 写入每个页面的第一个字节
        }
        
        std::cout << "子进程修改共享内存的第一个字节为: '" << buffer[0] << "',size:" << size_bytes<< std::endl;
        std::cout << "按Enter键退出子进程..." << std::endl;
        std::cin.get();
        std::cout << "子进程退出" << std::endl;
        exit(0);
    } else {
        // 父进程
        // 等待子进程完成
        int status;
        waitpid(pid, &status, 0);
        
        std::cout << "\n父进程检查共享内存" << std::endl;
        std::cout << "父进程读取共享内存的第一个字节 (应该被子进程修改): '" << buffer[0] << "'" << std::endl;
    }
    
    std::cout << "按Enter键继续..." << std::endl;
    std::cin.get();
    
    // 解除映射并清理共享内存
    munmap(shared_memory, size_bytes);
    close(fd);
    shm_unlink(shm_name.c_str());
    
    std::cout << "已解除映射并删除共享内存对象" << std::endl;
}

// 8. 演示共享内存（在活动监视器中显示为"共享内存"）
void demo_sysv_shared_memory(size_t size_mb) {
    std::cout << "\n=== 演示System V共享内存 (在活动监视器中显示为\"共享内存\") ===" << std::endl;
    
    // 使用System V IPC共享内存API
    // 从较小的大小开始尝试，macOS上System V IPC可能有严格限制
    const size_t initial_size = 64 * 1024; // 从64KB开始尝试
    
    // 清理可能存在的旧共享内存段
    std::cout << "尝试清理可能存在的旧共享内存段..." << std::endl;
    
    // 检查系统IPC限制
    std::cout << "检查系统IPC限制..." << std::endl;
    int check_result = system("ipcs -l");
    if (check_result != 0) {
        std::cout << "无法获取IPC限制信息，继续尝试..." << std::endl;
    }
    
    // 创建唯一的key - 尝试多种方法
    key_t key = IPC_PRIVATE; // 首先尝试IPC_PRIVATE
    std::cout << "使用IPC_PRIVATE作为键..." << std::endl;
    
    // 尝试获取并删除已存在的共享内存段
    int existing_shmid = shmget(key, 0, 0666);
    if (existing_shmid != -1) {
        std::cout << "发现已存在的共享内存段 (ID: " << existing_shmid << ")，尝试删除..." << std::endl;
        if (shmctl(existing_shmid, IPC_RMID, NULL) != -1) {
            std::cout << "成功删除已存在的共享内存段" << std::endl;
        }
    }
    
    // 创建共享内存段 - 从小尺寸开始，尝试多种配置
    int shmid = -1;
    size_t current_size = initial_size;
    
    // 尝试不同的标志组合
    int flags_options[][2] = {
        {IPC_CREAT | 0666, 0},
        {IPC_CREAT | IPC_EXCL | 0666, 0},
        {IPC_CREAT | 0600, 0},
        {IPC_CREAT | IPC_EXCL | 0600, 0}
    };
    
    // 尝试不同的大小
    size_t size_options[] = {64 * 1024, 32 * 1024, 16 * 1024, 8 * 1024, 4 * 1024};
    
    // 尝试所有组合
    bool success = false;
    for (size_t i = 0; i < sizeof(size_options)/sizeof(size_options[0]) && !success; i++) {
        current_size = size_options[i];
        std::cout << "尝试分配 " << (current_size / 1024.0) << "KB 共享内存..." << std::endl;
        
        for (size_t j = 0; j < sizeof(flags_options)/sizeof(flags_options[0]) && !success; j++) {
            shmid = shmget(key, current_size, flags_options[j][0]);
            if (shmid != -1) {
                success = true;
                std::cout << "成功创建共享内存，大小: " << (current_size / 1024.0) 
                          << "KB，标志: 0x" << std::hex << flags_options[j][0] 
                          << std::dec << std::endl;
                break;
            } else {
                std::cerr << "  尝试标志0x" << std::hex << flags_options[j][0] 
                          << std::dec << " 失败: " << strerror(errno) << std::endl;
            }
        }
    }
    
    // 如果所有尝试都失败，尝试使用POSIX共享内存作为备选方案
    if (!success) {
        std::cerr << "所有System V共享内存尝试都失败" << std::endl;
        std::cout << "诊断信息:" << std::endl;
        system("ipcs -m"); // 显示当前共享内存段
        std::cout << "\n系统限制:" << std::endl;
        system("sysctl kern.sysv"); // 显示System V IPC相关的系统限制
        
        std::cout << "\n切换到POSIX共享内存作为备选方案..." << std::endl;
        demo_shared_memory(1); // 尝试分配1MB POSIX共享内存
        return;
    }
    
    // 附加到共享内存段
    void* shared_memory = shmat(shmid, NULL, 0);
    if (shared_memory == (void*)-1) {
        std::cerr << "无法附加到System V共享内存: " << strerror(errno) << std::endl;
        // 删除共享内存段
        shmctl(shmid, IPC_RMID, NULL);
        
        // 尝试POSIX共享内存作为备选方案
        std::cout << "切换到POSIX共享内存作为备选方案..." << std::endl;
        demo_shared_memory(1); // 尝试分配1MB POSIX共享内存
        return;
    }
    
    std::cout << "已创建" << (current_size / 1024 / 1024) << "MB的System V共享内存 (ID: " << shmid << ")" << std::endl;
    
    // 写入数据到共享内存以确保它被实际使用
    char* buffer = (char*)shared_memory;
    // 使用实际分配到的内存大小，而不是最初请求的大小
    for (size_t i = 0; i < current_size; i += 4096) {
        buffer[i] = 'V'; // 写入每个页面的第一个字节
    }
    
    std::cout << "已写入数据到共享内存" << std::endl;
    std::cout << "此时在macOS活动监视器中，此进程的\"共享内存\"指标应该增加了约" << (current_size / 1024 / 1024) << "MB" << std::endl;
    std::cout << "请打开活动监视器查看\"共享内存\"指标的变化" << std::endl;
    
    print_memory_info();
    
    // 创建一个子进程来演示共享内存的共享特性
    pid_t pid = fork();
    
    if (pid == -1) {
        std::cerr << "创建子进程失败" << std::endl;
    } else if (pid == 0) {
        // 子进程
        std::cout << "\n子进程 (PID: " << getpid() << ") 访问共享内存" << std::endl;
        std::cout << "子进程读取共享内存的第一个字节: '" << buffer[0] << "'" << std::endl;
        
        // 修改共享内存中的一些数据
        // 使用实际分配到的内存大小，而不是最初请求的大小
        for (size_t i = 0; i < current_size; i += 4096) {
            buffer[i] = 'C'; // 写入每个页面的第一个字节
        }
        
        std::cout << "子进程修改共享内存的第一个字节为: '" << buffer[0] << "'" << std::endl;
        std::cout << "按Enter键退出子进程..." << std::endl;
        std::cin.get();
        std::cout << "子进程退出" << std::endl;
        
        // 子进程分离共享内存但不删除它
        shmdt(shared_memory);
        exit(0);
    } else {
        // 父进程
        // 等待子进程完成
        int status;
        waitpid(pid, &status, 0);
        
        std::cout << "\n父进程检查共享内存" << std::endl;
        std::cout << "父进程读取共享内存的第一个字节 (应该被子进程修改): '" << buffer[0] << "'" << std::endl;
    }
    
    std::cout << "按Enter键继续..." << std::endl;
    std::cin.get();
    
    // 分离共享内存
    if (shmdt(shared_memory) == -1) {
        std::cerr << "无法分离System V共享内存: " << strerror(errno) << std::endl;
    }
    
    // 删除共享内存段
    if (shmctl(shmid, IPC_RMID, NULL) == -1) {
        std::cerr << "无法删除System V共享内存: " << strerror(errno) << std::endl;
    }
    
    std::cout << "已删除System V共享内存" << std::endl;
}

// 9. 演示可清除内存（purgeable memory）
void demo_purgeable_memory(size_t size_mb) {
    std::cout << "\n=== 演示可清除内存 (purgeable_nonvolatile) ===" << std::endl;
    
    // 使用malloc分配内存
    const size_t size_bytes = size_mb * 1024 * 1024;
    char* buffer = (char*)malloc(size_bytes);
    
    if (buffer == nullptr) {
        std::cerr << "内存分配失败!" << std::endl;
        return;
    }
    
    // 写入数据以确保页面被实际分配
    for (size_t i = 0; i < size_bytes; i += 4096) {
        buffer[i] = 'P';
    }
    
    std::cout << "已分配并写入" << size_mb << "MB的内存" << std::endl;
    std::cout << "在实际应用中，可以使用vm_purgable_control API将内存标记为可清除" << std::endl;
    std::cout << "这部分内存会计入purgeable_nonvolatile指标" << std::endl;
    std::cout << "当内存压力增加时，系统可以回收这部分内存而不需要将其写入磁盘" << std::endl;
    
    print_memory_info();
    std::cout << "按Enter键继续..." << std::endl;
    std::cin.get();
    
    // 释放内存
    free(buffer);
    std::cout << "已释放内存" << std::endl;
}

// 主函数
int main() {
    std::cout << "XNU内核内存占用指标演示程序" << std::endl;
    std::cout << "当前进程PID: " << getpid() << std::endl;
    std::cout << "\n在每个演示之间，您可以使用以下命令查看内存使用情况:" << std::endl;
    std::cout << "$ sudo memory_pressure" << std::endl;
    std::cout << "$ ps -o pid,rss,vsz,command -p " << getpid() << std::endl;
    std::cout << "$ vmmap " << getpid() << " | grep -A 20 'Summary'" << std::endl;
    
    // 打印初始内存信息
    print_memory_info();
    
    std::cout << "\n按Enter键开始演示..." << std::endl;
    std::cin.get();
    
    // 1. 演示internal内存（匿名内存）
    // demo_internal_memory(50); // 分配50MB

    // 2. 演示文件映射内存
    // demo_file_mapped_memory(50); // 映射50MB文件

    // // 3. 演示匿名内存映射
    // demo_anonymous_mapping(50); // 分配50MB
    //
    // // 4. 演示内存压缩
    // demo_memory_compression(100); // 分配100MB可压缩内存
    //
    // // 5. 演示页表内存
    // demo_page_table(50, 64); // 分配50MB，每块64KB
    //
    // // 6. 演示IOKit内存映射
    // demo_iokit_mapping();
    
    // 7. 演示匿名共享内存
    demo_anonymous_shared_memory(50); // 分配50MB匿名共享内存
    
    // 8. 演示共享内存(POSIX)
    // demo_shared_memory(50); // 分配50MB共享内存
    
    // 8. 演示System V共享内存(在活动监视器中显示为"共享内存")
    // demo_sysv_shared_memory(1); // 尝试分配1MB共享内存，减小初始尝试大小
    
    // 9. 演示可清除内存
    // demo_purgeable_memory(50); // 分配50MB
    
    std::cout << "\n所有演示已完成" << std::endl;
    std::cout << "您可以使用上述命令再次检查内存使用情况" << std::endl;
    
    return 0;
}