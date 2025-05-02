cmake_minimum_required(VERSION 3.10)
project(windows_process_memory_monitor)

set(CMAKE_CXX_STANDARD 17)

# 添加可执行文件
add_executable(process_memory_monitor process_memory_monitor.cpp)
add_executable(memory_metrics_demo process_memory_monitor.cpp memory_metrics_demo.cpp)
add_executable(memory_allocation_demo process_memory_monitor.cpp memory_allocation_demo.cpp)

# 添加Windows特定库
if(WIN32)
    target_link_libraries(process_memory_monitor psapi)
endif()

# 添加编译选项
target_compile_options(process_memory_monitor PRIVATE /W4)

# 输出构建信息
message(STATUS "配置完成，可以使用以下命令构建项目:")
message(STATUS "  mkdir build && cd build")
message(STATUS "  cmake ..")
message(STATUS "  cmake --build .")