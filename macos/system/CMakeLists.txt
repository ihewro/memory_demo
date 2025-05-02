cmake_minimum_required(VERSION 3.10)
project(system_memory_monitor)

set(CMAKE_CXX_STANDARD 17)

# 查找需要的包
find_library(COREFOUNDATION_LIBRARY CoreFoundation)

# 添加可执行文件
add_executable(activity_monitor_demo activity_monitor_demo.mm)

# 添加编译选项
target_compile_options(activity_monitor_demo PRIVATE -Wall -Wextra -x objective-c++)

# 链接必要的库
target_link_libraries(activity_monitor_demo ${COREFOUNDATION_LIBRARY})

# 输出构建信息
message(STATUS "配置完成，可以使用以下命令构建项目:")
message(STATUS "  mkdir -p build && cd build")
message(STATUS "  cmake ..")
message(STATUS "  make")