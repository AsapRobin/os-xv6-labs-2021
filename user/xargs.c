#include "kernel/types.h"
#include "user/user.h"
#include "kernel/param.h"

int
main(int argc, char *argv[]) {
    
    // 将命令行参数存储到参数数组中
    char* line_parameter[MAXARG]; // 参数数组
    int para_count = 0; // 用于记录参数的数量
    for (int i = 1; i < argc; ++i) {
        line_parameter[para_count++] = argv[i];
    }
    int origin_index = para_count; // 记录初始参数数量的位置

    char ch;
    char *line; // 当前行的指针
    char buf[512]; // 用于存储当前行的缓冲区
    line = buf;
    int line_index = 0; // 当前行的索引

    // 从标准输入逐字符读取输入
    while (read(0, &ch, 1) > 0) {
        if (ch == '\n') {
            // 如果读到换行符，表示一行结束
            line[line_index] = '\0';
            line_index = 0;

            // 将当前行添加到参数数组中
            line_parameter[para_count++] = line;
            line_parameter[para_count] = 0;

            // 创建子进程
            if (fork()==0) {
                // 子进程执行命令
                exec(argv[1], line_parameter);
                
            } else {
                // 父进程等待子进程完成
                wait(0);
                para_count = origin_index; // 重置参数数量
            }
        } else if (ch == ' ') {
            // 如果读到空格，表示一个参数结束
            line[line_index] = '\0';
            line_index = 0;
            line_parameter[para_count++] = line;

            // 重置行缓冲区
            char buf[512];
            line = buf;
        } else {
            // 继续读取当前参数
            line[line_index++] = ch;
        }
    }

    exit(0);
}








