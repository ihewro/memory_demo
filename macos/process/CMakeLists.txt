cmake_minimum_required(VERSION 3.28)
project(process)

set(CMAKE_CXX_STANDARD 17)

# 查找需要的包
find_library(IOKIT_LIBRARY IOKit)
find_library(COREFOUNDATION_LIBRARY CoreFoundation)
find_library(FOUNDATION_LIBRARY Foundation)

# 添加内存信息库
add_library(memory_info memory_info.cc)
target_link_libraries(memory_info ${FOUNDATION_LIBRARY} ${COREFOUNDATION_LIBRARY})

# 添加可执行文件
add_executable(xnu_footprint_demo xnu_footprint_demo.cpp)
add_executable(large_code_segment large_code_segment.cpp)
add_executable(shared_library_demo shared_library_demo.cpp)
add_executable(mach_shared_memory_demo mach_shared_memory_demo.cpp)
add_executable(activity_monitor_demo activity_monitor_demo.mm)
add_executable(file_mapping_dirty_demo file_mapping_dirty_demo.cpp)

# 为需要 IOKit 的目标添加链接库
target_link_libraries(xnu_footprint_demo ${IOKIT_LIBRARY} ${COREFOUNDATION_LIBRARY})

# 添加编译选项
target_compile_options(xnu_footprint_demo PRIVATE -Wall -Wextra)
target_compile_options(large_code_segment PRIVATE -Wall -Wextra)
target_compile_options(activity_monitor_demo PRIVATE -Wall -Wextra -x objective-c++)
target_link_libraries(activity_monitor_demo memory_info ${FOUNDATION_LIBRARY} ${COREFOUNDATION_LIBRARY} ${IOKIT_LIBRARY})

# 添加线程库支持
find_package(Threads REQUIRED)
target_link_libraries(xnu_footprint_demo Threads::Threads)
target_link_libraries(activity_monitor_demo Threads::Threads)

# 输出构建信息
message(STATUS "配置完成，可以使用以下命令构建项目:")
message(STATUS "  mkdir -p build && cd build")
message(STATUS "  cmake ..")
message(STATUS "  make")
