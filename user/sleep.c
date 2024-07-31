#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

int
main(int argc, char *argv[])
{
  if(argc!=2){
    printf("Please enter the parameters!\n");//判断是否遗漏参数
    exit(1);
  }
  else{
    int duration=atoi(argv[1]);
    sleep(duration);//sleep 系统调用函数
  }

  exit(0);//确保'main'函数调用'exit()'来退出程序

}