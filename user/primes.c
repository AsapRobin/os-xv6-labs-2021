#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

void
select_prime_num(int count, int select_num[]) {
    if (count == 0) {
        return;
    }
    int first_num = select_num[0];
    printf("prime %d\n", first_num);

    int P[2];
    pipe(P);

    char buff[4];

    if (fork() == 0) {  // 子进程
        close(P[0]);
        for (int i = 1; i < count; i++) {  
            write(P[1], (char *)&select_num[i], sizeof(select_num[i]));
        }
        close(P[1]);
        exit(0);
    } else {  // 父进程
        close(P[1]);
        int left_num[34];
        int index = 0;  // 从0开始，因为我们需要从头开始放入新数组
        while (read(P[0], buff, 4) != 0) {
            int temp = *((int *)buff);
            if (temp % first_num != 0) {  // 筛选出不能被first_num整除的数
                left_num[index] = temp;
                index++;
            }
        }

        // 递归调用处理剩余的数
        select_prime_num(index, left_num);

        close(P[0]);
        wait(0);
    }
}

int 
main() {
    int select_num[] = {2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20,21,22,23,24,25,26,27,28,29,30,31,32,33,34};
    int count = sizeof(select_num) / sizeof(select_num[0]);

    select_prime_num(count, select_num);
    exit(0);
}



