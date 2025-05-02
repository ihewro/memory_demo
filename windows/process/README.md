# 指标定义
- 虚拟地址已经使用的大小
  - 进程申请的虚拟地址空间大小 = Reserve（保留但未提交） + Commited。
- 已提交 committed
  - 进程地址空间的一种状态，表示系统承诺这部分地址空间可以映射到物理内存上
  - 注意⚠️：在 Windows 任务管理器上的“已提交”不包含共享部分，即值为 private bytes 的值
- 私有提交 private bytes
  - 私有的已提交部分
  - ⚠️注意：该指标包含未被分配内存的部分，因此不是私有工作集+私有交换到磁盘部分之和

- 总工作集 Total WS
  - #4 专用物理内存 +  #7 共享的物理内存

- 私有工作集 private workingset
  - 私有的物理内存
  - 注意⚠️：在任务管理器默认看到的是这个指标
- 可共享工作集
- 已共享工作集

# 进程内存分配方式
- 匿名私有 - 通过VirtualAlloc或new分配的私有内存
- 匿名共享 - 通过CreateFileMapping创建的共享内存（不基于文件）
- 文件映射私有 - 通过MapViewOfFile映射文件到内存（私有方式）
- 文件映射共享 - 通过MapViewOfFile映射文件到内存（共享方式）

## 内存分配方式演示

可以通过运行`memory_allocation_demo.cpp`来观察不同内存分配方式对系统内存指标的影响：

```bash
# 编译
g++ memory_allocation_demo.cpp process_memory_monitor.cpp -o memory_allocation_demo.exe

# 运行
./memory_allocation_demo.exe
```

该演示程序会分别展示四种内存分配方式，并在每次分配前后显示内存指标的变化，帮助理解不同分配方式对系统内存的影响。