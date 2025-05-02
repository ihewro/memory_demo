#include <iostream>
#include <mach/mach_init.h>
#include <mach/mach_host.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/sysctl.h>
#include <sys/xattr.h>

std::string ParseMB(double bytes) {
    return std::to_string(bytes * 1.0 / 1024 / 1024) + " MB";
}

std::string ParseGB(double bytes) {
    return std::to_string(bytes * 1.0 / 1024 / 1024 / 1024) + " GB";
}

uint32_t GetSystemMemoryPressure() {
    uint32_t memory_pressure = 0;
    vm_size_t page_size;
    mach_port_t mach_port = mach_host_self();
    mach_msg_type_number_t count = HOST_VM_INFO64_COUNT;
    vm_statistics64_data_t vm_stat;
    if (KERN_SUCCESS == host_page_size(mach_port, &page_size) &&
        KERN_SUCCESS == host_statistics64(mach_port, HOST_VM_INFO64,
                                          (host_info64_t) &vm_stat, &count)) {
        long long useAndCache = (vm_stat.active_count + vm_stat.inactive_count +
                                 vm_stat.speculative_count + vm_stat.wire_count +
                                 vm_stat.compressor_page_count) *
                                page_size;

        std::cout << "page_size:" << page_size << std::endl;
        std::cout << "------------------------- 已使用 ----------------" << std::endl;
        std::cout << "internal_page_count:" << ParseGB(vm_stat.internal_page_count * page_size)
                  << std::endl;

        std::cout << "active_count:"
                  << ParseGB((vm_stat.active_count) * page_size)
                  << std::endl;
        std::cout << "inactive_count:"
                  << ParseGB((vm_stat.inactive_count) * page_size)
                  << std::endl;

        std::cout << "active_count + inactive_count:"
                  << ParseGB((vm_stat.inactive_count + vm_stat.active_count) * page_size)
                  << std::endl;

        std::cout << "purgeable_count:"
                  << ParseGB(vm_stat.purgeable_count * page_size)
                  << std::endl;
        // app 内存
        std::cout << "App 内存:"
                  << ParseGB((vm_stat.internal_page_count - vm_stat.purgeable_count) * page_size)
                  << std::endl;
        // 联动
        std::cout << "联动内存:" << ParseGB(vm_stat.wire_count * page_size)
                  << std::endl;
        std::cout << "App + 已缓存内存:" << ParseGB(
                (vm_stat.internal_page_count + vm_stat.external_page_count - vm_stat.speculative_count) * page_size)
                  << std::endl;
        std::cout << "App 内存 + 联动内存+ purgeable_count:"
                  << ParseGB((vm_stat.internal_page_count - vm_stat.purgeable_count + vm_stat.wire_count +
                              vm_stat.purgeable_count) * page_size)
                  << std::endl;

        std::cout << "已使用=App +联动内存+被压缩" <<  ParseGB(
                (vm_stat.internal_page_count- vm_stat.purgeable_count + vm_stat.wire_count + vm_stat.compressor_page_count) * page_size) << std::endl;

        long long cache =
                (vm_stat.purgeable_count + vm_stat.external_page_count) * page_size;

        long long used = useAndCache - cache;

        long long free = useAndCache + vm_stat.free_count * page_size - used;

        long long pressure =
                (vm_stat.wire_count + vm_stat.compressor_page_count) * page_size;
        std::cout << "------------------------- 压缩 ----------------" << std::endl;

        std::cout << "Pages stored in compressor:"
                  << vm_stat.total_uncompressed_pages_in_compressor * page_size * 1.0 / 1024 / 1024 / 1024 << "GB"
                  << std::endl;

        // 压缩内存
        std::cout << "被压缩:"
                  << ParseGB(vm_stat.compressor_page_count * page_size)
                  << std::endl;

        std::cout << "------------------------- 缓存 ----------------" << std::endl;
        // 已缓存
        std::cout << "external_page_count:" << ParseGB(vm_stat.external_page_count * page_size)
                  << std::endl;
        std::cout << "已缓存文件"
                  << ParseGB((vm_stat.external_page_count + vm_stat.purgeable_count) * page_size)
                  << std::endl;
        std::cout << "pressure:" << ParseGB(pressure) << std::endl;
        std::cout << "free + used:" << ParseGB(free + used) << std::endl;
        std::cout << "useAndCache:" << ParseGB(useAndCache) << std::endl;
        std::cout << "------------------------- 空闲 ----------------" << std::endl;
        std::cout << "free:" << ParseGB(vm_stat.free_count * page_size) << std::endl;
        std::cout << "speculative_count:" << ParseGB(vm_stat.speculative_count * page_size) << std::endl;
        std::cout << "------------------------- 交换 ----------------" << std::endl;
        std::cout << "swapouts:" << ParseGB(vm_stat.swapouts * page_size) << std::endl;
        // MIB for the vm.swapusage sysctl
        int mib[] = {CTL_VM, VM_SWAPUSAGE};
        struct xsw_usage swap;
        size_t len = sizeof(swap);

        //调用 sysctl
        if (sysctl(mib, 2, &swap, &len, NULL, 0) < 0) {
            perror("sysctl");
            exit(1);
        }
        // 已交换内存
        std::cout << "已使用的交换:" << ParseGB(swap.xsu_used) << std::endl;

        std::cout << "------------------------- 总内存 ----------------" << std::endl;

        std::cout << "总内存:"
                  << ParseGB((vm_stat.inactive_count + vm_stat.active_count + vm_stat.free_count +
                              vm_stat.compressor_page_count + vm_stat.wire_count) * page_size)
                  << std::endl;

        std::cout << "总内存:"
                  << ParseGB((vm_stat.internal_page_count + vm_stat.external_page_count + vm_stat.free_count +
                              vm_stat.compressor_page_count + vm_stat.wire_count) * page_size)
                  << std::endl;

        memory_pressure = pressure * 1.0 / (free + used) * 100;
    }
    return memory_pressure;
}

#define AVAILABLE_MEMORY 35
#define AVAILABLE_NON_COMPRESSED_MEMORY 20
#define VM_PAGE_COMPRESSOR_COMPACT_THRESHOLD            (((AVAILABLE_MEMORY) * 10) / 20)

int main() {

    while (1) {
        std::cout << GetSystemMemoryPressure() << std::endl;
        usleep(2000000);
    }
    return 0;
}