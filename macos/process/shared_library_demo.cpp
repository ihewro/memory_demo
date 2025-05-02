#include <iostream>
#include <vector>
#include <unistd.h>
#include <dlfcn.h>
#include <sys/wait.h>
#include <string>
#include <fstream>
#include <thread>
#include <chrono>
#include <cstdlib>

// 保存父进程PID的全局变量
pid_t parent_pid;

/*
 * 共享库代码段演示程序
 * 
 * 演示多个进程共享的内存页面（如共享库代码段），并使共享内存指标上涨50MB
 * 创建一个大型共享库并让多个进程加载它
 */

// 打印当前进程的内存使用信息
void print_memory_info() {
    char cmd[100];
    sprintf(cmd, "ps -o pid,rss,vsz,command -p %d", getpid());
    std::cout << "执行命令: " << cmd << std::endl;
    system(cmd);

//    sprintf(cmd, "vmmap %d | grep -A 20 'Summary'", getpid());
//    std::cout << "\n执行命令: " << cmd << std::endl;
//    system(cmd);
}

// 创建大型共享库源代码
bool create_large_shared_library(size_t size_mb) {
    // 保存父进程PID
    parent_pid = getpid();
    std::string lib_dir = "/tmp/shared_lib_demo_" + std::to_string(parent_pid);
    std::string mkdir_cmd = "mkdir -p " + lib_dir;
    system(mkdir_cmd.c_str());

    // 创建共享库源代码
    std::string source_path = lib_dir + "/large_lib.cpp";
    std::ofstream source_file(source_path);

    if (!source_file.is_open()) {
        std::cerr << "无法创建源文件: " << source_path << std::endl;
        return false;
    }

    // 写入头部
    source_file << "#include <cstddef>\n";
    source_file << "#include <iostream>\n\n";

    // 生成大量函数以创建大型代码段
    const size_t functions_count = size_mb * 10; // 每个函数约100KB

    for (size_t i = 0; i < functions_count; i++) {
        source_file << "extern \"C\" int large_function_" << i << "() {\n";
        source_file << "    static const char data[] = {\n";

        // 每个函数生成约100KB的常量数据（代码段）
        const size_t data_size = 25000; // 约100KB的数据
        for (size_t j = 0; j < data_size; j++) {
            source_file << "        " << (j % 256) << ",";
            if (j % 10 == 9) source_file << "\n";
        }

        source_file << "    };\n";
        source_file << "    return data[0];\n";
        source_file << "}\n\n";
    }

    // 添加一个导出函数用于测试库是否正常加载
    source_file << "extern \"C\" int test_large_lib() {\n";
    source_file << "    std::cout << \"大型共享库已成功加载!\" << std::endl;\n";
    source_file << "    return 42;\n";
    source_file << "}\n";

    source_file.close();
    std::cout << "已创建共享库源代码: " << source_path << std::endl;

    // 编译共享库
    std::string compile_cmd = "g++ -shared -fPIC -o " + lib_dir + "/liblarge.dylib " + source_path;
    std::cout << "编译共享库: " << compile_cmd << std::endl;

    int result = system(compile_cmd.c_str());
    if (result != 0) {
        std::cerr << "编译共享库失败" << std::endl;
        return false;
    }

    std::cout << "已成功编译共享库: " << lib_dir << "/liblarge.dylib" << std::endl;
    return true;
}

// 加载共享库并调用测试函数
void *load_and_test_library() {
    // 使用父进程PID构建库路径，确保所有进程使用同一个库文件
    std::string lib_path = "/tmp/shared_lib_demo_" + std::to_string(parent_pid) + "/liblarge.dylib";

    // 加载共享库
    void *handle = dlopen(lib_path.c_str(), RTLD_NOW);
    if (!handle) {
        std::cerr << "无法加载共享库: " << dlerror() << std::endl;
        return nullptr;
    }

    // 查找测试函数
    typedef int (*test_func_t)();
    test_func_t test_func = (test_func_t) dlsym(handle, "test_large_lib");

    if (!test_func) {
        std::cerr << "无法找到测试函数: " << dlerror() << std::endl;
        dlclose(handle);
        return nullptr;
    }

    // 调用测试函数
    int result = test_func();
    std::cout << "测试函数返回值: " << result << std::endl;

    return handle;
}

// 清理临时文件
void cleanup_library_files() {
    // 使用父进程PID构建目录路径
    std::string lib_dir = "/tmp/shared_lib_demo_" + std::to_string(parent_pid);
    std::string cleanup_cmd = "rm -rf " + lib_dir;
    system(cleanup_cmd.c_str());
    std::cout << "已清理临时文件: " << lib_dir << std::endl;
}

// 演示共享库代码段
void demo_shared_library_code_segment(size_t size_mb) {
    std::cout << "\n=== 演示共享库代码段 ===" << std::endl;

    // 创建大型共享库
    if (!create_large_shared_library(size_mb)) {
        std::cerr << "创建共享库失败" << std::endl;
        return;
    }

    // 打印父进程的内存信息（加载库前）
    std::cout << "\n父进程内存信息 (加载共享库前):" << std::endl;
    print_memory_info();

    // 父进程加载共享库
    void *parent_handle = load_and_test_library();
    if (!parent_handle) {
        cleanup_library_files();
        return;
    }

    // 打印父进程的内存信息（加载库后）
    std::cout << "\n父进程内存信息 (加载共享库后):" << std::endl;
    print_memory_info();

    // 创建多个子进程来加载同一个共享库
    const int num_children = 3;
    std::vector<pid_t> children;

    for (int i = 0; i < num_children; i++) {
        pid_t pid = fork();

        if (pid == -1) {
            std::cerr << "创建子进程失败" << std::endl;
            break;
        } else if (pid == 0) {
            // 子进程
            std::cout << "\n子进程 " << i+1 << " (PID: " << getpid() << ") 开始运行" << std::endl;

            // 子进程加载共享库
            void* child_handle = load_and_test_library();
            if (child_handle) {
                std::cout << "子进程 " << i+1 << " 成功加载共享库" << std::endl;

                // 打印子进程的内存信息
                std::cout << "\n子进程 " << i+1 << " 内存信息 (加载共享库后):" << std::endl;
                print_memory_info();

                // 运行一段时间以便观察
                std::cout << "子进程 " << i+1 << " 将运行5秒钟，您可以在此期间使用vmmap或活动监视器查看内存使用情况" << std::endl;
                std::this_thread::sleep_for(std::chrono::seconds(5));

//             卸载共享库
                dlclose(child_handle);
            }

            std::cout << "\n按Enter键退出子进程...pid" << getpid() << std::endl;
            std::cin.get();
            std::cout << "子进程 " << i+1 << " 退出" << std::endl;
            exit(0);
        } else {
            // 父进程记录子进程PID
            children.push_back(pid);
            std::cout << "父进程创建了子进程 " << i + 1 << " (PID: " << pid << ")" << std::endl;

            // 稍微延迟，让子进程有时间启动
            std::this_thread::sleep_for(std::chrono::seconds(1));

        }
    }

    // 父进程等待所有子进程完成
    std::cout << "\n父进程等待所有子进程完成..."  << getpid() <<  std::endl;
    std::cout << "您可以在此期间使用vmmap或活动监视器查看内存使用情况" << std::endl;
    std::cout << "注意观察共享库代码段在多个进程间共享的情况" << std::endl;

    std::cout << "\n按Enter键继续...pid" << getpid() << std::endl;
    std::cin.get();

    for (pid_t child_pid: children) {
        int status;
        waitpid(child_pid, &status, 0);
        std::cout << "子进程 (PID: " << child_pid << ") 已退出" << std::endl;
    }

    // 卸载共享库
    if (parent_handle) {
        dlclose(parent_handle);
    }

    // 清理临时文件
    cleanup_library_files();

    std::cout << "共享库代码段演示完成" << std::endl;


}

int main() {
    std::cout << "共享库代码段演示程序" << std::endl;
    std::cout << "当前进程PID: " << getpid() << std::endl;
    std::cout << "\n您可以使用以下命令查看内存使用情况:" << std::endl;
    std::cout << "$ vmmap " << getpid() << " | grep -A 20 'Summary'" << std::endl;
    std::cout << "$ ps -o pid,rss,vsz,command -p " << getpid() << std::endl;

    std::cout << "\n按Enter键开始演示..." << std::endl;
    std::cin.get();

    // 演示共享库代码段
    demo_shared_library_code_segment(50); // 创建约50MB的共享库

    std::cout << "\n演示完成" << std::endl;
    std::cout << "您可以使用上述命令再次检查内存使用情况" << std::endl;

    return 0;
}