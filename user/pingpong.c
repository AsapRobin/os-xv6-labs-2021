#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

int
main(int argc,char** argv){
    int child_fd[2],parent_fd[2];
    char buf[20]={0};
    //创建两个管道
    pipe(child_fd);
    pipe(parent_fd);

    //pid为0表示子进程
    if(fork()==0){
        close(parent_fd[1]);//在子进程读之前，关闭父进程用于写的管道
        read(parent_fd[0],buf,4);//通过parent_fd[0]进行读
        printf("%d: received %s\n",getpid(),buf);

        close(child_fd[0]);//关闭子进程用于读的管道
        write(child_fd[1],"pong",sizeof(buf));//读完之后，写一个pong在child[1]中
        exit(0);
    }
    //父进程
    else{
        close(parent_fd[0]);//在写之前关闭用于读的管道
        write(parent_fd[1],"ping",4);//通过parent_fd[1]进行写

        close(child_fd[1]);//关闭子进程用于写的管道
        read(child_fd[0],buf,sizeof(buf));//通过child_fd[0]进行读
        printf("%d: received %s\n",getpid(),buf);
        exit(0);
    }

}