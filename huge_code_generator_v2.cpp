#include <iostream>
#include <fstream>
#include <string>
#include <sstream>

// 这个程序用于生成一个包含大量代码的C++文件
// 生成的文件将包含约100MB的代码段

int main() {
    std::ofstream outFile("/Users/hewro/Documents/文档/memory/huge_code_v2.cpp");
    
    if (!outFile.is_open()) {
        std::cerr << "无法创建输出文件!" << std::endl;
        return 1;
    }
    
    // 写入头文件和必要的声明
    outFile << "#include <iostream>\n";
    outFile << "#include <string>\n\n";
    outFile << "// 这个文件包含约100MB的代码\n\n";
    
    // 生成大量的常量字符串定义
    // 每个字符串约1KB，需要生成约100,000个字符串来达到100MB
    const int NUM_STRINGS = 100000; // 增加到100000个字符串
    const int STRING_SIZE = 1000;
    
    for (int i = 0; i < NUM_STRINGS; i++) {
        std::stringstream ss;
        ss << "const char* string_" << i << " = \"";
        
        // 生成一个大约1KB的字符串
        for (int j = 0; j < STRING_SIZE; j++) {
            // 使用可打印字符，避免转义序列
            ss << (char)('A' + (i + j) % 26);
        }
        
        ss << "\";\n";
        outFile << ss.str();
    }
    
    // 生成大量的函数定义
    // 每个函数约1KB，再生成约50,000个函数来增加代码段大小
    const int NUM_FUNCTIONS = 100000; // 增加到100000个函数
    
    outFile << "\n// 函数声明\n";
    for (int i = 0; i < NUM_FUNCTIONS; i++) {
        outFile << "std::string func_" << i << "();\n";
    }
    
    outFile << "\n// 函数定义\n";
    for (int i = 0; i < NUM_FUNCTIONS; i++) {
        outFile << "std::string func_" << i << "() {\n";
        outFile << "    return std::string(string_" << (i % NUM_STRINGS) << ", 10);\n";
        outFile << "}\n\n";
    }
    
    // 添加一个使用这些函数的main函数
    outFile << "int main() {\n";
    outFile << "    std::cout << \"程序已启动，代码段大小约为100MB\" << std::endl;\n";
    outFile << "    std::string result;\n\n";
    outFile << "    // 调用一些函数以防止编译器优化\n";
    
    // 只调用一小部分函数，但足以防止编译器完全优化
    for (int i = 0; i < 100; i++) {
        int idx = i * (NUM_FUNCTIONS / 100);
        outFile << "    result += func_" << idx << "();\n";
    }
    
    outFile << "\n    std::cout << \"结果长度: \" << result.length() << std::endl;\n";
    outFile << "    std::cout << \"按Enter键退出程序...\" << std::endl;\n";
    outFile << "    std::cin.get();\n";
    outFile << "    return 0;\n";
    outFile << "}\n";
    
    outFile.close();
    
    std::cout << "已生成huge_code_v2.cpp文件，包含约100MB的代码" << std::endl;
    std::cout << "请使用g++编译该文件：g++ -o huge_program huge_code_v2.cpp" << std::endl;
    std::cout << "注意：编译这个文件可能需要较长时间和较大的内存" << std::endl;
    
    return 0;
}