#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/fs.h"

//来自于grep.c文件
char buf[1024];
int match(char*, char*);
int matchhere(char*, char*);
int matchstar(int, char*, char*);

int
match(char *re, char *text)
{
  if(re[0] == '^')
    return matchhere(re+1, text);
  do{  // must look at empty string
    if(matchhere(re, text))
      return 1;
  }while(*text++ != '\0');
  return 0;
}

// matchhere: search for re at beginning of text
int matchhere(char *re, char *text)
{
  if(re[0] == '\0')
    return 1;
  if(re[1] == '*')
    return matchstar(re[0], re+2, text);
  if(re[0] == '$' && re[1] == '\0')
    return *text == '\0';
  if(*text!='\0' && (re[0]=='.' || re[0]==*text))
    return matchhere(re+1, text+1);
  return 0;
}

// matchstar: search for c*re at beginning of text
int matchstar(int c, char *re, char *text)
{
  do{  // a * matches zero or more instances
    if(matchhere(re, text))
      return 1;
  }while(*text!='\0' && (*text++==c || c=='.'));
  return 0;
}


/*
  find.c
*/

char*
fmtname(char *path)
{
  static char buf[DIRSIZ+1];
  char *p;

  
  for(p=path+strlen(path); p >= path && *p != '/'; p--)
    ;
  p++;
  
  if(strlen(p) >= DIRSIZ)
    return p;
  memset(buf, 0, sizeof(buf));
  memmove(buf, p, strlen(p));
 
  return buf;
}


void 
find(char *path, char *re){
  char buf[512], *p;
    int fd;
    struct dirent de;
    struct stat st;

    // 尝试打开指定路径
    if ((fd = open(path, 0)) < 0) {
        fprintf(2, "find: cannot open %s\n", path);
        return;
    }

    // 获取文件状态信息
    if (fstat(fd, &st) < 0) {
        fprintf(2, "find: cannot stat %s\n", path);
        close(fd);
        return;
    }

    switch (st.type) {
    case T_FILE:
        // 如果是文件且名称匹配，则打印路径
        if (match(re, fmtname(path)))
            printf("%s\n", path);
        break;

    case T_DIR:
        // 检查路径长度是否超出缓冲区限制
        if (strlen(path) + 1 + DIRSIZ + 1 > sizeof buf) {
            printf("find: path too long\n");
            break;
        }
        strcpy(buf, path);
        p = buf + strlen(buf);
        *p++ = '/';

        // 遍历目录中的每个条目
        while (read(fd, &de, sizeof(de)) == sizeof(de)) {
            if (de.inum == 0)
                continue;

            memmove(p, de.name, DIRSIZ);
            p[DIRSIZ] = 0;

            // 获取当前路径的状态信息
            if (stat(buf, &st) < 0) {
                printf("find: cannot stat %s\n", buf);
                continue;
            }

            char *lstname = fmtname(buf);

            // 跳过 "." 和 ".." 目录
            if (strcmp(".", lstname) == 0 || strcmp("..", lstname) == 0) {
                continue;
            } else {
                // 递归查找子目录
                find(buf, re);
            }
        }
        break;
    }
    close(fd);
}

int 
main(int argc, char** argv){
    if(argc != 3){
      printf("Parameters are not enough\n");
    }
    else{
      //在路径path下递归搜索文件 
      find(argv[1], argv[2]);
    }
    exit(0);
}
