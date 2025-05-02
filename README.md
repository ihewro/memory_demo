# 前言

进程和系统的内存指标很多，不同工具（任务管理器/活动监视器...）中的指标定义并不明确，不同操作系统存在名称相似但含义差异很大的术语。
本文抛砖引玉介绍现Windows & macOS 操作系统中的内存指标、概念，以便读者后续在使用软件分析、更进一步的内存文章阅读中有更清晰的认知。

# 摘要

暂时无法在飞书文档外展示此内容

- 进程内存Windows 上主要关注 private bytes（私有提交）、private workingset（私有物理内存），macOS 关注 footprint
	- 可以按照地址空间位置（数据段/代码段/heap/内存映射/stack），访问权限（私有/共享），页面位置（物理/交换），进行分类
		
	- Windows 上进程页面可按照提交状态（reserve / commtited / 工作集）分类
		
	- macOS 上进程内存可按照是否可丢弃（dirty/clean）进行分类
		
- 系统内存中主要关注可用物理内存、总内存指标
	- Windows ：
		- 可用物理内存分为“备用”&“空闲”两部分，已使用物理内存包含“活跃”&“已修改”两部分
			
		- 提交和提交上限
			- Windows上存在commit limit限制，即允许进程暂时不分配内存，但承诺commit部分一定能成功分配到内存
				
			- 因此 Windows 的 oom 一般触发时机是 commit 申请过程，而其它操作系统触发时机一般是对申请内存进行读写（此时才会真正的分配内存）过程中
				
	- macOS 的可用物理内存（free）不包含“已缓存文件”，这部分内存可以在内存压力大的时候被移除，可折算入可用内存
		

# 基础概念

## **通用术语**

- **虚拟地址**
  - 每个进程的虚拟地址空间大小是 2^X（X 是操作系统的位数）

- **虚拟内存**
  - 在不同的环境下有不同的语义：
    - 虚拟内存技术（支持换页的内存管理方式）
    - 进程：
      - 进程虚拟地址空间（进程虚存）
      - 进程被换入到磁盘上的大小（已使用的交换空间）
      - 进程committed大小（private bytes 指标）
    - 系统：
      - 系统 pagefile （交换空间）
      - 系统 committed 大小（系统虚存）
  - > 尽量避免直接使用虚拟内存的概念，本文未特别说明下，虚拟内存术语为系统的交换空间大小。
- **交换空间**
  - 操作系统通过换页将内存交换到磁盘上，其它等价术语有 pagefile，交换文件。

- **保护模式 & 实模式**
  - 进程通过访问虚拟地址，由操作系统进行地址映射的方式是保护模式，直接操作物理地址方式是实模式。

- **文件映射**
  - 文件映射将磁盘文件和内存地址映射起来，读写文件就好像是直接操作内存地址
  - > Note：文件映射和普通的 fread/fwrite 读写文件接口不同，是文件IO的一种方式

- **写时复制 (copy on write)**
  - （可读可写）私有的页面当被多个进程共享，内核把进程地址空间中这些内存页面标记为"*copy-on-write"*（页面数据库管理该信息）此时这些页面变成只读状态，*当*进程尝试对这块地址空间写入数据，会创建私有的匿名副本。
  - > 具体场景：
    > - fork 创建子进程，操作系统会将父进程的内存页映射到子进程的地址空间，但是初始时这些内存页是共享的，也就是只读的。当父进程或者子进程尝试去写这些内存页时，操作系统才会实际地为需要写入的内存页创建一个私有副本
    > - mmap(fd, MAP\_PRIVATE)，表示对于任何对映射区域的修改是私有的，也就是说这些修改不会反映回基础文件。当修改映射的内容的时候，会创建一个私有匿名内存副本

## 进程内存分类

### 按照地址空间的位置分类

- 代码段
	- TEXT 代码
		
- 数据段
	- READONLY\_DATA 常量数据
		
	- DATA 静态数据（已初始化）
		
	- BSS 静态数据（未初始化）
		
- HEAP 堆（低到高）
	
- MEM\_MAPPED 内存映射（低到高）
	
- STACK 栈（固定大小，高到低）
	

### 按照内容分类

- 共享 Sharable/Shared:
	- Mapped File：file-backup 共享文件映射（MEM\_MAPPED）
		
	- Sharable Memory：memory-backup 匿名共享内存（MEM\_MAPPED）
		
- 私有:
	- Heap：堆内存（HEAP）
		
	- Stack：栈内存（STACK）
		
	- Static：静态数据（DATA/BSS）
		
	- Process/Thread Environment Table（无法直接访问）
		
- Image（FrameWorks）：可执行文件（TEXT/READONLY\_DATA/...）
	
- Page Table：内核维护的 当前进程页表（无法直接访问）
	

### 按照页面映射方式分类

- 匿名映射：虚拟地址不和特定的磁盘/设备文件关联
	

> 注意⚠️：匿名内存即使被换出到磁盘上仍然是匿名内存

- 文件映射 ：虚拟地址和特定磁盘/设备文件关联，通过虚拟地址可以直接读写文件
	

> 注意⚠️：读写过程中会使用到物理内存作为页面高速缓存来提高速度
> 文件读写和文件映射不是等价的概念

页面映射方式（匿名/文件映射）和页面访问权限（私有/共享）四象限：

- **Anonymous 匿名映射 + Private 私有（专用）内存 (#1)**
  - stack 栈
  - Static：静态数据（DATA/BSS）
  - [malloc](https://en.cppreference.com/w/c/memory/malloc) c标准库接口：**动态内存申请方式**
    - > malloc只能用来分配++匿名私有++内存，会根据申请内存大小决定具体的内存申请方式
    - macos/linux: sbrk/ brk 在 heap 上申请内存（brk 是调整堆的结尾增加/减少从而线性大小改变）
      - > 注意⚠️：这里的堆是操作系统中进程地址空间中的概念，和广义的动态内存分配在"堆

| **页面映射方式\\页面权限** | Private 私有（专用）内存 | Shared 共享内存 |
| :-- | :-- | :-- |
| Anonymous 匿名映射 | **#1** | **#2** |\
||||\
|| - stack 栈 | - macos/linux： |\
|| 	 | 	- POSIX ++[shm\*](https://pubs.opengroup.org/onlinepubs/007908799/xsh/sysshm.h.html)++（这个星号表示以 shm 为前缀的函数，如 shm\_open shmat shmctl shmdt shmget ） |\
|| - Static：静态数据（DATA/BSS） | 		 |\
|| 	 | 	- [mmap](https://man7.org/linux/man-pages/man2/mmap.2.html#DESCRIPTION)(MAP\_SHARED, MAP\_ANONYMOUS) |\
|| - [malloc](https://en.cppreference.com/w/c/memory/malloc) c标准库接口：**动态****内存****申请方式** | 		 |\
|| 	 | - windows: [VirtualAlloc](https://learn.microsoft.com/zh-cn/windows/win32/api/memoryapi/nf-memoryapi-virtualalloc) |\
|| 	> malloc只能用来分配++匿名私有++内存，会根据申请内存大小决定具体的内存申请方式 | 	 |\
|| 	 ||\
|| 	- macos/linux: sbrk/ brk 在 heap 上申请内存（brk 是调整堆的结尾增加/减少从而线性大小改变） ||\
|| 		 |\
|| 		> 注意⚠️：这里的堆是操作系统中进程地址空间中的概念，和广义的动态内存分配在“堆”上，不是同一个概念，广义的“堆”指的是全部的动态内存的区域(包括mmap分配的内存区域)。 |\
|| 		 |\
|| 		> 注意⚠️：这里的堆是操作系统中进程地址空间中的概念，和广义的动态内存分配在“堆”上，不是同一个概念，广义的“堆”指的是全部的动态内存的区域(包括mmap分配的内存区域)。 |\
|| 		 |\
|| 	- windows：HeapAlloc |\
|| 		 |\
|| - 平台接口 |\
|| 	- macos/linux：[mmap](https://man7.org/linux/man-pages/man2/mmap.2.html#DESCRIPTION)(MAP\_SHARED, MAP\_ANONYMOUS) |\
|| 		 |\
|| 	 |\
|| 	> mmap 可以进行文件映射或者是匿名内存映射，也可以是申请私有或者共享，这里是匿名私有映射 |\
|| 	 |\
|| 	- windows：[VirtualAlloc](https://learn.microsoft.com/zh-cn/windows/win32/api/memoryapi/nf-memoryapi-virtualalloc) / CreateFileMapping+MapViewOfFile |\
|| 		 |\
|| 	 |\
|| 	> CreateFileMapping 也可以创建匿名映射 |
| file-backed 文件映射 | **#3** | **#4** |\
||||\
|| - macos/Linux：[mmap](https://man7.org/linux/man-pages/man2/mmap.2.html#DESCRIPTION)(PRIVATE, fd) | - [mmap](https://man7.org/linux/man-pages/man2/mmap.2.html#DESCRIPTION)(SHARED, fd) |\
|| 	 | 	 |\
|| - Windows：CreateFileMapping+MapViewOfFile | - Windows：CreateFileMapping+MapViewOfFile |\
|| 	 | 	 |\
||| - 可执行文件 ：Image（FrameWorks）：可执行文件（TEXT/READONLY\_DATA/...） |\
|||

### 按照页面映射位置分类

- 物理内存
	
- 交换空间
	

**页面映射位置（物理/交换）和页面访问类型（私有/共享）四象限:** 
暂时无法在飞书文档外展示此内容

# 进程内存

## Windows

### 指标

> 工具：[vmmap](https://learn.microsoft.com/zh-cn/sysinternals/downloads/vmmap)、任务管理器、资源监视器、[Process Explorer](https://learn.microsoft.com/en-us/sysinternals/downloads/process-explorer)、[Process Hacker](https://systeminformer.sourceforge.io/downloads)

Windows 引入了“工作集”概念来表示进程虚拟地址空间映射的++物理++++内存++++（++++不包含交换到磁盘部分即 page file++++）++的大小

> 在 macOS 仍然被命名为进程的物理内存，在 Linux 上则使用“resident size” 驻留集术语。

Windows 上进程地址空间有“Reserve 保留”和“Commit 提交”两个重要概念。

- “Reserve”仅分配虚拟地址空间，系统不关心大小。
	
- “Commit”成功不代表已分配物理内存，只表示最终要用，且只要成功，系统承诺需要时能映射到物理内存（通过 commit limit 和 committed 保证）。
	

> 在 macOS/Linux 上无完全一致的概念对应，是因为不同操作系统的内存管理有区别，比如 macOS 上交换空间是整个磁盘区域，Linux 上允许通过 vm.overcommit\_memory 来过度承诺 commit。这也是为什么会在Windows上会出现物理内存充足但是oom情况

> 举个例子，你需要办事预定酒席，可能需要 100 个餐位，但目前确定的人数只有 50 个人，你在自己的小本子上记上可能预定 100 个餐位，打电话给老板的时候只说你暂时只预定 50 个餐位。那这 100 个餐位就是 reserve 的大小，只占据了进程的虚拟地址空间，其中已经确定的 50 个餐位就是 committed 部分。其中 committed 成功后，人并不一定到餐馆了。


| 序号 | 名词 | 解释 |
| :-- | :-- | :-- |
| 1 | 虚拟地址已经使用的大小 | 进程申请的虚拟地址空间大小 = Reserve（保留但未提交） + Commited。 |
| 2 | 已提交 committed | 进程地址空间的一种状态，表示系统承诺这部分地址空间可以映射到物理内存上 |\
||||\
||| > 注意⚠️：在 Windows 任务管理器上的“已提交”不包含共享部分，即值为 private bytes 的值 |
| 3 | 私有提交 private bytes | 私有的已提交部分 |\
||||\
||| > ⚠️注意：该指标包含未被分配内存的部分，因此不是私有工作集+私有交换到磁盘部分之和 |
| 4 | 总工作集 Total WS | #4 专用物理内存 + #7 共享的物理内存 |
| 5 | 私有工作集 private workingset | 私有的物理内存 |\
||||\
||| > 注意⚠️：在任务管理器默认看到的是这个指标 |
| 6 | 可共享工作集 | 支持多个进程共享访问的物理内存 |
| 7 | 已共享工作集 | 已共享工作集是"实际已被共享"的内存，即这部分内存已被多个进程占用 |

### Demo



## macOS

### 指标

macOS 在已有的“共享/私有”，“物理/交换”进程内存分类上，根据“是否可丢弃”增加一个新的维度：dirty/clean：

- clean：这部分内容随时被++丢弃++后续再通过 page fault 重新读取文件
	- mmap 文件映射
		
	- malloc 申请但还未分配的内存
		
	- 代码文件中的\_\_TEXT、\_\_DATA\_CONST 区域
		
- dirty：这部分内容不能直接丢弃，而只能交换到磁盘或者被压缩。匿名内存
	- All heap Allocations 在堆上分配的内存（malloc / array / NSCache/ String...)
		
	- Framework 的\_\_DATA 和 \_\_DATA\_DIRTY 部分
		

> 注意⚠️：未特别说明情况下，dirty 不包含 dirty compressed（即内存压缩以及被换出到磁盘的部分）（vmmap 中的定义），而广义的 dirty 和 footprint 概念“接近”。

> 注意⚠️：dirty 的概念有点类似 Windows 上的“已修改”，但完全不一样。Windows 上的“已修改”是系统内存中的概念（队列），表示的是从进程中移除的工作集，并且这部分工作集内容是“dirty”的。


| 序号 | 名词 | 解释 |
| :-- | :-- | :-- |
| 1 | **内存****（****footprint** **指标）** | ++internal + internal\_compressed++ \+ iokit 持有的内存 + purgeable\_nonvolatile 内存 + 页表 |\
||||\
||| > Physical footprint: This is the sum of: + (internal（匿名内存） - alternate\_accounting（iokit\_mappiing 中dirty 部分）) + (internal\_compressed（匿名内存被压缩或者被交换） - alternate\_accounting\_compressed（iokit\_mappiing 中dirty 且被压缩或被交换的部分）) + iokit\_mapped（IOKit持有的内存，一般和设备访问、图像处理等有关，和文件映射不是一个概念） + purgeable\_nonvolatile（可清理且非易失内存物理内存） + purgeable\_nonvolatile\_compressed（可清理且非易失内存被压缩或被交换） + page\_table |\
||||\
||| > 可以看到 footprint 概念和广义的 dirty 概念接近，但footprint 除了私有匿名内存以外，还额外有：匿名共享内存、purgeable\_nonvolatile、iokit\_mapped |\
||| > Note：不包含文件映射的内容 |\
||||\
||| > **注意⚠️：活动监视器中默认****内存****是该指标** |\
||||\
||| > Note：macos上malloc 分配的内存是不可清理类型，与此相对应的有一类是可清理内存，通过特定接口申请，一般用于缓存的场景，可以被清理，其中非易失类型只在内存压力非常大的时候才会被清理 |\
||||\
||| |
| 2 | 实际（物理）内存 | 占用的物理内存，包含私有和共享两部分 |\
||||\
|| > Real Memory | > NOTE：不包含被交换到磁盘的部分 |\
|| > RSIZE | > \*不包含被压缩部分 |
| 3 | 专用（物理）内存 | 私有的物理内存 |\
|||\
|| > Real Private Memory |\
|| > RPRVT |
| 4 | 共享（物理）内存 | 共享的物理内存 |\
||||\
|| > Real Shared Memory | > 专用内存和共享内存指标在 mac 上看的比较少，还看到过负数的 bug 情况 |\
|| > RSHRD |  |\
|||\
|||
| 5 | 可清除（物理）内存 | 这个概念关注较少，是使用 NSPurgeableData 的数据结构 |\
|||\
|| > Purgeable Memory |
| 6 | VM 被压缩（物理内存） | 内存压缩器压缩**前**的内存大小 |\
||||\
|| > Compressed Memory | > Windows 也有内存压缩，但是没有进程维度的数据 |



### Demo


### 工具


排查内存的软件有 xcode / instruments，命令行有 vmmap / footprint / leap / heap / malloc\_histroy。开启**MallocStackLogging**之后，可以通过命令行工具看到heap地址空间对应的分配堆栈。

## 术语对比

暂时无法在飞书文档外展示此内容
我们关注进程的内存，主要关注私有部分，即私有物理内存和私有的写入在磁盘空间的内容，这两部分的总和 mac 上是 footprint（内存），而 Windows 上没有这样的概念与之对应。Windows 上更多的是关注私有物理内存的大小。

| macOS | Windows |
| :-- | :-- |
| 内存 footprint | \- |\
||\
| > 注意⚠️：footprint 中会包含共享的匿名内存，和 private bytes 不完全一致，在“一致性指标”中还会提到 |
| \- | 私有工作集 private bytes |\
|||\
|| > 注意⚠️：该指标是私有的 committed 大小，包含了未被分配物理内存的部分，在“一致性指标”中还会提到 |
| 实际（物理）内存 | 进程工作集 Working Set |
| 专用（物理）内存 | 私有工作集 Private Working Set |
| 共享（物理）内存 | 共享工作集 Sharable Working Set |

## 一致性指标

进程内存可以按照不同的维度进程分类，比如按照访问权限（私有/共享），所在位置（物理/交换），是否可丢弃（dirty/clean），是否提交（reseve/commit/free）等。

真正关心的进程内存大小是**当进程被终止的时候，物理内存 和 page file 能释放的大小总和**。chromium 提出了[统一内存指标](https://docs.google.com/document/d/1_WmgE1F5WUrhwkPqJis3dWyOiUmQKvpXp5cd4w86TvA/edit?tab=t.0#heading=h.72p7m75zec96) 的方案，即用来衡量一个进程的内存占用情况是**私有内存**。
它的定义如下：++私有的 & 匿名（非文件映射）& 不可丢弃++、存在物理内存上或者磁盘上或者被压缩。该指标即私有的 footprint 指标。

各个操作系统中没有直接提供一个 API 来告知一个进程的非共享的内存（物理+page file）是多少，因此 chromium 最终选择的替代接口：

- macOS：footprint，这部分相比较理想指标会多计入以下内容：
	- 5～10MB 的共享匿名内存
		
	- Non-volatile IOKit pages：GPU 进程>500MB，前台的渲染进程和 chrome 进程有～30MB，这部分是共享内存
		
	- Non-volatile CoreAnimation pages：chrome 进程 ～25MB 这部分是共享的内存映射，但只有 Window server 和 chrome 进程使用，因此计入私有也是符合预期的。
		
	<br>
	
- Windows：private bytes，这部分相比较理想指标会多计入以下内容：
	- 已 commit 但是未分配内存的部分
		
	
	> Windows 上没有办法直接获取到进程在物理内存和磁盘内存大小之和
	

飞书中进程的内存上报指标中，mac 上使用 footprint，Windows 上使用 private workingset，和系统的“活动监视器”和“任务管理器”保持一致，同时 Windows 上还额外上报了 private byte 字段。

# 系统内存

## Windows

### 指标

> 工具：[vmmap](https://learn.microsoft.com/zh-cn/sysinternals/downloads/vmmap)、任务管理器、资源管理器、[Process Explorer](https://learn.microsoft.com/en-us/sysinternals/downloads/process-explorer)、[Process Hacker](https://systeminformer.sourceforge.io/downloads)

Windows 系统物理内存管理中有两个主要概念：“已提交 committed” 和 提交上限（commit limit）概念。

- committed ：表示系统已经承诺进程允许写入物理内存的大小
	
- commit limited ：为物理内存+pagefile 空间大小，因为支持换页，系统可以将一部分内存交换到 page file 上以兑现 committed 部分在物理内存上分配的承诺。
	

| 序号 | 名词 | 解释 |
| :-- | :-- | :-- |
| 1 | 已安装内存 | 内存条的大小 |
| 2 | 总内存 | 已安装 - 为硬件保留的内存 |
| 3 | 已使用（内部可能包含已压缩） | 活跃的被使用的内存 |\
||||\
||| > 注意⚠️：Windows10 上默认开启内存压缩，已压缩也属于已使用的一部分 |
| 4 | **可用** | 总内存 - 使用中 ，即 #11 备用 + #12 真正可用（free） |
| 5 | **已提交** | 映射到物理内存或者交换文件中的地址空间总和。 |
| 6 | **已****缓存** | #10 已修改 + #11 备用 |
| 7 | 分页缓冲池 | 内核模式使用的内存区域之一，这部分内存可以换入到磁盘，一般注册表会表示这部分的内存 |
| 8 | 非分页缓冲池 | 内核模式使用的内存区域之一，这部分内存不能换入到磁盘，如进程和线程对象、中断处理程序代码等 |
| 9 | 为硬件保留的内存 | 为 BIOS 和其他外围设备的某些驱动程序保留使用的内存，这部分内存操作系统无法使用和操作的，和驱动程序或者内核 kernel 占用内存不同。 |
| 10 | 已修改 modified | #6 已缓存子集。进程已释放文件缓存中已经被修改的部分，需要回写到磁盘后才能被利用。 |
| 11 | 备用 standby | 已被释放、没被修改，可以直接再利用（包含预读取部分） |
| 12 | 空闲 free | 完全未被使用的内存 |


### Demo


## macOS

### 指标

<br>


| 序号 | 名词 | 解释 |
| :-- | :-- | :-- |
| 1 | 物理内存 | 物理内存上限（和 windows 上已安装内存一致） |
| 2 | 已使用内存 | 使用的物理内存： |\
||||\
||| - App 内存：用户程序使用的物理内存 |\
||| 	 |\
||| - 联动内存：系统内核使用的内存，类似 Windows 上的非分页内存，不可被换出到磁盘上 |\
||| 	 |\
||| - 被压缩：压缩后的大小 |
| 3 | 已缓存文件 | 文件映射内存大小 + 可清理 puregeable 内存大小 |\
||||\
||| > 注意⚠️：这部分一定程度可以被视作“可用物理内存”，尽管它没有包含在 host\_statistics64 接口中 free 字段中。这也是为什么 macOS 上可用物理内存尽管很低，但是内存压力指示仍然是绿色的原因之一。 |
| 4 | 已使用的交换 | 交换空间占据的磁盘大小 |\
||||\
||| > 注意⚠️：macOS 上没有预先设定 page file 的大小，而是启动磁盘的所有剩余空间都可以用于交换空间 |


### Demo



## 术语对比

暂时无法在飞书文档外展示此内容

| macOS | Windows |
| :-- | :-- |
| 总物理内存 | 已安装内存 |
| 总物理内存-已使用内存 | 可用物理内存 |\
||\
| > 注意⚠️：free 可用内存没有包含“已缓存”内容 |
| 已使用内存 | 使用的物理内存 |
| 已缓存 | 备用 |\
||\
| > 注意⚠️：和 Windows 的“已缓存”概念不一致， |
| 已使用的交换 | PageFile |\
|||\
| > 注意⚠️：与 Linux 和 Windows 不同，OSX 不使用预先分配的磁盘分区作为后备存储。相反，它使用机器引导分区上的所有可用空间 |

# Q&A

## Windows 内存不足就容易卡，macOS 为什么不会

网络上有这样一句话，“windows是要多少用多少，而os x是有多少用多少。”这句话是说macOS上会更多的利用内存做缓存（比如预读或者保留缓存），而Windows上则更倾向于更多的可用物理内存。但实际上现代的内存管理机制大同小异，包含内存压缩、预读、缓存机制每个操作系统都是有的。
导致这样的观念深入人心推测可能有两方面因素：

- windows 设备硬件良莠不齐，比如磁盘、cpu频率，大基数的用户的设备相比mac 设备的硬件都要差很多。而内存不足可能会触发内存频繁压缩、解压缩，或者IO花费时间在交换文件换入、换出上，就很容易导致卡、慢甚至oom 的问题。即在设备差的情况下，可用物理内存不足确实会导致卡顿问题。
	
- 相较Windows，macOS并没有直接在活动监视器提供系统可用物理内存百分比值，因此Windows用户一旦遇到卡慢就会看到内存占用百分比很高，从而直接将两者挂钩。
	

## 主动清理内存能让系统更快吗

在Windows系统中可以清理进程工作集（EmptyWorkingSet）、清空已修改队列（Empty Modified List）来让可用物理内存看上去更多（备用属于可用一部分）。
正如前文提到的，这个机制有一定的意义，比如在硬件差的设备应该尽量保存充足内存避免额外的性能消耗。比如某些进程内存泄漏占用大量物理内存，并不是一无是处。但是在其他一些场景反而会加剧换入换出，比如清理的内存后续马上使用。
因此这个问题不能简单回答，这些工作理论上应该由操作系统更智能的自动完成，操作系统应该考虑各种场景下的内存管理，从而减少用户心智。

## macos 进程内存分配有对应 reserve & commit 概念吗

- macos 上没有等价reserve 概念：
	- 根本原因是mac上没有commit 概念，因此和windows上先申请虚拟内存，再提交两阶段不同，mac申请内存只有一个阶段
		
- macos 上没有等价commit 概念：
	- unix 变体中通常是允许过度commit，即不存在Windows上的commit limit 限制，commit limit 限制本质上为了承诺进程确保需要的时候就能用，但是也可能会被滥用，比如进程大量commit 却不使用。
		

## windows 物理内存还剩余很多，为什么程序仍然 OOM

正如前文多次提到，Windows 存在commit limit 限制，并且存在commit 不分配内存机制
	

# 参考链接

- [统一内存指标](https://docs.google.com/document/d/1_WmgE1F5WUrhwkPqJis3dWyOiUmQKvpXp5cd4w86TvA/edit?tab=t.0#heading=h.72p7m75zec96)