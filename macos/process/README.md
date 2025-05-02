# 概念
## Footprint
- phys_footprint: 物理内存占用总和=
- \+ internal: 匿名内存，在iOS上始终驻留
- \+ internal_compressed: 被压缩器持有的内部内存
- +iokit_mapped: IOKit映射的总大小
- \-alternate_accounting: IOKit映射中的内部脏页
- \-alternate_accounting_compressed : 被压缩器持有的IOKit映射中的内部脏页
- \+ purgeable_nonvolatile: 可清除的非易失性内存
- \+ purgeable_nonvolatile_compressed: 被压缩器持有的可清除的非易失性内存
- \+ page_table: 页表内存

## purgeable
purgeable（可清除）内存是指可以被系统直接回收的内存，而不是交换到磁盘。当系统内存压力大时，这部分内存会被直接清除掉内容，而不会写入交换文件。一旦被系统回收，内容就无法恢复，应用程序需要重新生成数据。

purgeable_nonvolatile内存适用于以下场景：

1. 缓存可重新生成的数据：如图像缓存、渲染纹理等
2. 媒体处理：视频帧、音频数据等临时缓冲区
3. 游戏资源：非关键游戏资源，可以在需要时重新加载
4. 浏览器缓存：网页内容、已解析的DOM树等
这种内存类型适合存储"昂贵但可重建"的数据，即生成成本高但不是必不可少的数据。系统会在内存压力大时优先回收这类内存，应用可以在需要时重新生成数据，从而在性能和内存使用之间取得平衡。

## 匿名共享内存
匿名共享内存是没有关联文件的共享内存区域，通过mmap系统调用创建(MAP_ANONYMOUS|MAP_SHARED)。

### 使用场景
- 数据库缓存：如Redis、MySQL等使用匿名共享内存存储频繁访问的数据
- 高性能计算：进程间共享大量数据而无需复制
- 实时系统：低延迟的进程间通信
- 图形处理：共享图形缓冲区
- 多线程应用：线程间共享大块数据

### 特点
- 只能在父子进程间共享，不能被无关进程访问
- 在内存指标中计入phys_footprint和internal
- 相比命名共享内存(如POSIX共享内存)，无需创建和管理共享内存对象

## 活动监视器指标
- 内存（footprint 指标）
- 实际（物理）内存
  - 不包含交换到磁盘的部分
- 专用（物理）内存
- 共享（物理）内存
  - 目前没有方式可以准确计算出和监视器中一样的值,因为task_info 并没有提供这个指标，使用vm_map_region采集得到的结果和监视器结果不一致
- 可清除（物理）内存
  - 目前没有找到macOS计算的指标是什么，和vm_map_region采集的结果不一致
- VM 被压缩（物理内存）
  - 目前没有找到macOS计算的指标是什么，和vm_map_region采集的结果不一致

活动监视器报告6个指标：内存、真实内存、真实专用内存、真实共享内存、可清除内存、压缩内存。这些指标的确切含义很难辨别。
- 实际专用内存和实际共享内存列是使用vm_map_reregion（vm_region_TOP_INFO，…）计算的，没有考虑压缩内存。
- “专用内存”和“压缩内存”列是使用task_info（task_VM_info，…）计算的，无法正确考虑子映射、共享内存和可丢弃内存。
- “内存”列计算匿名、不可重用的内存，但不计算IOKit内存。

## 共享内存

共享内存的会计定义不明确。如果一个内存区域被映射到多个进程中（可能多次），它应该计入哪些进程？

在Linux上，一种常见的解决方案是使用比例集大小，它计算驻留大小的1/N，其中N是该区域发生页面错误的其他进程的数量。这具有跨过程添加的良好特性。缺点是它依赖于上下文。例如，如果用户打开更多选项卡，从而导致系统库映射到更多进程，则之前选项卡的PSS将降低（Proportional Set Size，按比例计算的内存集大小），即这使得单个进程的内存使用量统计变得不稳定。

文件支持的共享内存区域通常不值得报告，因为它们通常代表共享系统资源、库和浏览器二进制文件本身，所有这些都不在开发人员的控制范围内。这在不同版本的操作系统中尤其成问题，默认情况下链接到进程中的基础库集差异很大，不受Chrome的控制。

在Chrome中，我们实现了对匿名共享内存区域的所有权跟踪——每个共享内存区域都只计入一个进程，这取决于共享内存区的类型和使用情况。

https://chromium.googlesource.com/chromium/src/+/lkgr/docs/memory/key_concepts.md
# 使用说明

## large_code_segment.cpp
大型代码段演示程序，用于展示代码段在内存中的占用情况：
- 创建一个约100MB的代码段（__TEXT段）
- 使用内联汇编和静态数据确保代码段不被优化
- 提供强制加载所有页面到物理内存的功能
- 可用于观察代码段对内存占用的影响

## mach_shared_memory_demo.cpp
Mach共享内存演示程序（macOS特有）：
- 演示macOS特有的Mach共享内存机制
- 使用Mach API创建内存区域并在父子进程间共享
- 展示共享内存如何影响内存指标（约50MB）
- 包含创建、映射和访问共享内存的完整流程

## shared_library_demo.cpp
共享库代码段演示程序：
- 演示多个进程如何共享同一个库的代码段
- 动态创建并编译一个约50MB的共享库
- 在父进程和多个子进程中加载同一个共享库
- 展示共享库如何影响各进程的内存占用

## xnu_footprint_demo.cpp
XNU内核内存占用指标演示程序：
- 全面演示XNU内核中定义的各种内存占用指标
- 包括internal、internal_compressed、iokit_mapped等指标
- 提供多种内存分配和使用方式的示例
- 展示不同类型内存对phys_footprint的影响

## activity_monitor_demo.cpp

```bash
codesign --force --entitlements /Users/hewro/Documents/mac-m1-doc/memory/macos/process/activity_monitor_demo.entitlements --sign - /Users/hewro/Documents/mac-m1-doc/memory/macos/process/activity_monitor_demo.mm
```

## file_mapping_dirty_demo.cpp
文件映射修改内容演示程序：
- 创建临时文件并通过mmap映射到内存
- 记录修改前的内存指标（包括dirty页面相关统计）
- 修改文件内容，创建脏页
- 记录修改后的内存指标
- 比较dirty指标的变化
- 展示文件映射修改如何影响内存占用指标

## data_segments_demo.cpp
Mach-O文件数据段演示程序：
- 演示__DATA和__DATA_DIRTY段的区别和用途
- __DATA段：包含初始化的全局变量、静态变量和常量数据
  - 在程序启动时从磁盘加载到内存
  - 只读或读写权限，取决于具体子段
  - 包含const全局变量、C++静态类成员等
- __DATA_DIRTY段：包含需要写入的数据
  - 包含可修改的全局变量和静态变量
  - 在内存中具有读写权限
  - 系统会为这些页面创建写时复制(COW)副本
  - 当程序修改这些数据时会产生脏页
- 通过示例代码展示不同类型变量在内存中的分布
- 使用`otool -l`命令分析可执行文件的段结构
- 展示如何使用vmmap工具查看进程内存中的段分布