/*
 * (C) Radim Kolar 1997-2004
 * This is free software, see GNU Public License version 2 for
 * details.
 *
 * Simple forking WWW Server benchmark:
 *
 * Usage:
 *   webbench --help
 *
 * Return codes:
 *    0 - sucess
 *    1 - benchmark failed (server is not on-line)
 *    2 - bad param
 *    3 - internal error, fork failed
 *
 */
#include "socket.c"
#include <getopt.h>
#include <rpc/types.h>
#include <signal.h>
#include <strings.h>
#include <sys/param.h>
#include <time.h>
#include <unistd.h>

/* values */
volatile int timerexpired = 0;
int speed = 0;
int failed = 0; //压力测试过程错误计数
int bytes = 0;
/* globals */
int http10 = 1; /* 0 - http/0.9, 1 - http/1.0, 2 - http/1.1 */
/* Allow: GET, HEAD, OPTIONS, TRACE */
#define METHOD_GET 0
#define METHOD_HEAD 1
#define METHOD_OPTIONS 2
#define METHOD_TRACE 3
/* Web Bench版本号 */
#define PROGRAM_VERSION "1.5"
int method = METHOD_GET;
int clients = 1;
int force = 0;
int force_reload = 0;
int proxyport = 80;
char *proxyhost = NULL;
int benchtime = 30;
/* internal */
int mypipe[2];
char host[MAXHOSTNAMELEN];
#define REQUEST_SIZE 2048
char request[REQUEST_SIZE];

/*
 Linux系统下，需要大量的命令行选项，如果自己手动解析他们的话实在是有违软件复用的思想，不过还好，GNU
 C
 library留给我们一个解析命令行的接口(X/Open规范)，好好使用它可以使你的程序改观不少。
  使用getopt_long()需要引入头文件：#include<getopt.h>
  现在我们使用一个例子来说明它的使用。
  一个应用程序需要如下的短选项和长选项
  短选项       长选项                 作用
  -h           --help             输出程序命令行参数说明然后退出
  -o filename  --output filename  给定输出文件名
  -v           --version          显示程序当前版本后退出
  为了使用getopt_long()函数，我们需要先确定两个结构：
  1.一个字符串，包括所需要的短选项字符，如果选项后有参数，字符后加一个":"符号。本例中，这个字符串应该为"ho:v"。(因为-o后面有参数filename,所以字符后面需要加":")。
  2.
 一个包含长选项字符串的结构体数组，每一个结构体包含4个域，第一个域为长选项字符串，第二个域是一个标识，只能为0或1，分别代表没有选项或有选项。第三个域永远为NULL。第四个选项域为对应的短选项字符串。结构体数组的最后一个元素全部位NULL和0，标识结束。在本例中，它应为以下的样子：
  const struct option long_options[] = {
      {"help", 0, NULL, 'h'},
      {"output", 1, NULL, 'o'},
      {"version", 0, NULL, 'v'},
      {NULL, 0, NULL, 0}
  };
  */
static const struct option long_options[] = {
    {"force", no_argument, &force, 1},
    {"reload", no_argument, &force_reload, 1},
    {"time", required_argument, NULL, 't'},
    {"help", no_argument, NULL, '?'},
    {"http09", no_argument, NULL, '9'},
    {"http10", no_argument, NULL, '1'},
    {"http11", no_argument, NULL, '2'},
    {"get", no_argument, &method, METHOD_GET},
    {"head", no_argument, &method, METHOD_HEAD},
    {"options", no_argument, &method, METHOD_OPTIONS},
    {"trace", no_argument, &method, METHOD_TRACE},
    {"version", no_argument, NULL, 'V'},
    {"proxy", required_argument, NULL, 'p'},
    {"clients", required_argument, NULL, 'c'},
    {NULL, 0, NULL, 0}};

/*
  C语言中使用静态函数的好处：
  1、静态函数会被自动分配在一个一直使用的存储区，直到退出应用程序实例，避免了调用函数时压栈出栈，速度快很多。
  2、关键字“static”，译成中文就是“静态的”，所以内部函数又称静态函数。但此处“static”的含义不是指存储方式，而是指对函数的作用域仅局限于本文件。
  使用内部函数的好处是：不同的人编写不同的函数时，不用担心自己定义的函数，是否会与其它文件中的函数同名，因为同名也没有关系。
*/
/* prototypes */
static void benchcore(const char *host, const int port, const char *request);
static int bench(void);
/* 创建请求报头 */
static void build_request(const char *url);
/* 信号处理函数 */
static void alarm_handler(int signal) { timerexpired = 1; }

/* 使用说明 */
static void usage(void) {
  fprintf(
      stderr,
      "webbench [option]... URL\n"
      "  -f|--force               Don't wait for reply from server.\n"
      "  -r|--reload              Send reload request - Pragma: no-cache.\n"
      "  -t|--time <sec>          Run benchmark for <sec> seconds. Default "
      "30.\n"
      "  -p|--proxy <server:port> Use proxy server for request.\n"
      "  -c|--clients <n>         Run <n> HTTP clients at once. Default one.\n"
      "  -9|--http09              Use HTTP/0.9 style requests.\n"
      "  -1|--http10              Use HTTP/1.0 protocol.\n"
      "  -2|--http11              Use HTTP/1.1 protocol.\n"
      "  --get                    Use GET request method.\n"
      "  --head                   Use HEAD request method.\n"
      "  --options                Use OPTIONS request method.\n"
      "  --trace                  Use TRACE request method.\n"
      "  -?|-h|--help             This information.\n"
      "  -V|--version             Display program version.\n");
};
int main(int argc, char *argv[]) {
  int opt = 0;
  int options_index = 0;
  char *tmp = NULL;

  if (argc == 1) {
    usage();
    return 2;
  }

  /* 读取配置信息 */
  while ((opt = getopt_long(argc, argv, "912Vfrt:p:c:?h", long_options,
                            &options_index)) != EOF) {
    switch (opt) {
    case 0:
      break;
    case 'f':
      force = 1;
      break;
    case 'r':
      force_reload = 1;
      break;
    case '9':
      http10 = 0;
      break;
    case '1':
      http10 = 1;
      break;
    case '2':
      http10 = 2;
      break;
    case 'V':
      printf(PROGRAM_VERSION "\n");
      exit(0);
    case 't':
      benchtime = atoi(optarg);
      break;
    case 'p':
      /* proxy server parsing server:port */
      tmp = strrchr(optarg, ':');
      proxyhost = optarg;
      if (tmp == NULL) {
        break;
      }
      if (tmp == optarg) {
        fprintf(stderr, "Error in option --proxy %s: Missing hostname.\n",
                optarg);
        return 2;
      }
      if (tmp == optarg + strlen(optarg) - 1) {
        fprintf(stderr, "Error in option --proxy %s Port number is missing.\n",
                optarg);
        return 2;
      }
      *tmp = '\0';
      proxyport = atoi(tmp + 1);
      break;
    case ':':
    case 'h':
    case '?':
      usage();
      return 2;
      break;
    case 'c':
      clients = atoi(optarg);
      break;
    }
  }

  if (optind == argc) {
    fprintf(stderr, "webbench: Missing URL!\n");
    usage();
    return 2;
  }

  if (clients == 0)
    clients = 1;
  if (benchtime == 0)
    benchtime = 60;
  /* Copyright */
  fprintf(stderr,
          "Webbench - Simple Web Benchmark " PROGRAM_VERSION "\n"
          "Copyright (c) Radim Kolar 1997-2004, GPL Open Source Software.\n");
  /* 创建请求报头 */
  build_request(argv[optind]);
  /* 打印压力测试基本信息 */
  printf("\nBenchmarking: ");
  switch (method) {
  case METHOD_GET:
  default:
    printf("GET");
    break;
  case METHOD_OPTIONS:
    printf("OPTIONS");
    break;
  case METHOD_HEAD:
    printf("HEAD");
    break;
  case METHOD_TRACE:
    printf("TRACE");
    break;
  }
  printf(" %s", argv[optind]);
  switch (http10) {
  case 0:
    printf(" (using HTTP/0.9)");
    break;
  case 2:
    printf(" (using HTTP/1.1)");
    break;
  }
  printf("\n");
  if (clients == 1)
    printf("1 client");
  else
    printf("%d clients", clients);

  printf(", running %d sec", benchtime);
  if (force)
    printf(", early socket close");
  if (proxyhost != NULL)
    printf(", via proxy server %s:%d", proxyhost, proxyport);
  if (force_reload)
    printf(", forcing reload");
  printf(".\n");
  return bench();
}

/* 创建请求报头 */
void build_request(const char *url) {
  char tmp[10];
  int i;

  bzero(host, MAXHOSTNAMELEN);
  bzero(request, REQUEST_SIZE);

  if (force_reload && proxyhost != NULL && http10 < 1)
    http10 = 1;
  if (method == METHOD_HEAD && http10 < 1)
    http10 = 1;
  if (method == METHOD_OPTIONS && http10 < 2)
    http10 = 2;
  if (method == METHOD_TRACE && http10 < 2)
    http10 = 2;

  switch (method) {
  default:
  case METHOD_GET:
    strcpy(request, "GET");
    break;
  case METHOD_HEAD:
    strcpy(request, "HEAD");
    break;
  case METHOD_OPTIONS:
    strcpy(request, "OPTIONS");
    break;
  case METHOD_TRACE:
    strcpy(request, "TRACE");
    break;
  }

  strcat(request, " ");

  if (NULL == strstr(url, "://")) {
    fprintf(stderr, "\n%s: is not a valid URL.\n", url);
    exit(2);
  }
  if (strlen(url) > 1500) {
    fprintf(stderr, "URL is too long.\n");
    exit(2);
  }
  if (proxyhost == NULL)
    if (0 != strncasecmp("http://", url, 7)) {
      fprintf(stderr, "\nOnly HTTP protocol is directly supported, set --proxy "
                      "for others.\n");
      exit(2);
    }
  /* protocol/host delimiter */
  i = strstr(url, "://") - url + 3;
  /* printf("%d\n",i); */

  if (strchr(url + i, '/') == NULL) {
    fprintf(stderr, "\nInvalid URL syntax - hostname don't ends with '/'.\n");
    exit(2);
  }
  if (proxyhost == NULL) {
    /* get port from hostname */
    if (index(url + i, ':') != NULL &&
        index(url + i, ':') < index(url + i, '/')) {
      strncpy(host, url + i, strchr(url + i, ':') - url - i);
      bzero(tmp, 10);
      strncpy(tmp, index(url + i, ':') + 1,
              strchr(url + i, '/') - index(url + i, ':') - 1);
      /* printf("tmp=%s\n",tmp); */
      proxyport = atoi(tmp);
      if (proxyport == 0)
        proxyport = 80;
    } else {
      strncpy(host, url + i, strcspn(url + i, "/"));
    }
    // printf("Host=%s\n",host);
    strcat(request + strlen(request), url + i + strcspn(url + i, "/"));
  } else {
    // printf("ProxyHost=%s\nProxyPort=%d\n",proxyhost,proxyport);
    strcat(request, url);
  }
  if (http10 == 1)
    strcat(request, " HTTP/1.0");
  else if (http10 == 2)
    strcat(request, " HTTP/1.1");
  strcat(request, "\r\n");
  if (http10 > 0)
    strcat(request, "User-Agent: WebBench " PROGRAM_VERSION "\r\n");
  if (proxyhost == NULL && http10 > 0) {
    strcat(request, "Host: ");
    strcat(request, host);
    strcat(request, "\r\n");
  }
  if (force_reload && proxyhost != NULL) {
    strcat(request, "Pragma: no-cache\r\n");
  }
  if (http10 > 1)
    strcat(request, "Connection: close\r\n");
  /* add empty line at end */
  if (http10 > 0)
    strcat(request, "\r\n");
  // printf("Req=%s\n",request);
}

/* vraci system rc error kod */
static int bench(void) {
  int i, j, k;
  pid_t pid = 0;
  FILE *f;

  /* check avaibility of target server */
  i = Socket(proxyhost == NULL ? host : proxyhost, proxyport);
  if (i < 0) {
    fprintf(stderr, "\nConnect to server failed. Aborting benchmark.\n");
    return 1;
  }
  close(i);

  /*
    pipe我们用中文叫做管道。
    管道是半双工的，数据只能向一个方向流动；需要双方通信时，需要建立起两个管道；
    一个进程在由pipe()创建管道后，一般再fork一个子进程，然后通过管道实现父子进程间的通信（因此也不难推出，只要两个进程中存在亲缘关系，这里的亲缘关系指的是具有共同的祖先，都可以采用管道方式来进行通信）
    数据的读出和写入：一个进程向管道中写的内容被管道另一端的进程读出。写入的内容每次都添加在管道缓冲区的末尾，并且每次都是从缓冲区的头部读出数据。
  */
  if (pipe(mypipe)) {
    perror("pipe failed.");
    return 3;
  }

  /* 创建指定数量进程的方法 */
  for (i = 0; i < clients; i++) {
    /*
      一个进程，包括代码、数据和分配给进程的资源。fork（）函数通过系统调用创建一个与原来进程几乎完全相同的进程，也就是两个进程可以做完全相同的事，但如果初始参数或者传入的变量不同，两个进程也可以做不同的事。
      一个进程调用fork（）函数后，系统先给新的进程分配资源，例如存储数据和代码的空间。然后把原来的进程的所有值都复制到新的新进程中，只有少数值与原来的进程的值不同。相当于克隆了一个自己。
      -----------------------------------------------------------------------
      fork调用的一个奇妙之处就是它仅仅被调用一次，却能够返回两次，它可能有三种不同的返回值：
      1）在父进程中，fork返回新创建子进程的进程ID；
      2）在子进程中，fork返回0；
      3）如果出现错误，fork返回一个负值；
    */
    pid = fork(); //应该为fpid
    /*
      ***fork返回值fpid为指向子进程的ID（子进程没有子进程，fpid为0）***
      ***父进程fork之后，自身仍然是那个父进程（只是fpid变成了指向其创建的子进程1）以及创建的子进程1，然后两个进程继续执行***
      ***子进程fork之后，自身会变成父进程（fpid有指向了其创建的子进程2）以及创建一个子进程2，然后两个进程继续执行***
      fork出一个子进程（fpid为0的子进程）以及父进程之后，子进程跳出循环执行操作，父进程继续进入循环创建子进程，最后得到40个子进程以及1个父进程，共41个进程
    */
    if (pid <= (pid_t)0) {
      /* child process or error*/
      sleep(1); /* make childs faster */
      break;
    }
  }

  if (pid < (pid_t)0) {
    fprintf(stderr, "problems forking worker no. %d\n", i);
    perror("fork failed.");
    return 3;
  }

  /*
   * C中如何模拟HTTP请求进行压力测试，为什么子进程以及父进程的处理要进行区分
   */
  if (pid == (pid_t)0) {
    /* 如果是子线程 */
    if (proxyhost == NULL)
      benchcore(host, proxyport, request);
    else
      benchcore(proxyhost, proxyport, request);

    /*
    结果写入到管道，实现父子进程间的通信
    由描述字fd[0]表示，称其为管道读端；另一端则只能用于写，由描述字fd[1]来表示
    */
    f = fdopen(mypipe[1], "w");
    if (f == NULL) {
      perror("open pipe for writing failed.");
      return 3;
    }
    /* fprintf(stderr,"Child - %d %d\n",speed,failed); */
    fprintf(f, "%d %d %d\n", speed, failed, bytes);
    fclose(f);
    return 0;
  } else {
    f = fdopen(mypipe[0], "r");
    if (f == NULL) {
      perror("open pipe for reading failed.");
      return 3;
    }
    /*
    setvbuf()用来设定文件流的缓冲区
    参数】stream为文件流指针，buf为缓冲区首地址，type为缓冲区类型，size为缓冲区内字节的数量。
    参数类型type说明如下：
    _IOFBF
    (满缓冲)：在这种情况下，当填满标准I/O缓存后才进行实际I/O操作。全缓冲的典型代表是对磁盘文件的读写。
    _IOLBF
    (行缓冲)：在这种情况下，当在输入和输出中遇到换行符时，执行真正的I/O操作。这时，我们输入的字符先存放在缓冲区，等按下回车键换行时才进行实际的I/O操作。典型代表是标准输入(stdin)和标准输出(stdout)。
    _IONBF
    (无缓冲)：也就是不进行缓冲，标准出错情况stderr是典型代表，这使得出错信息可以直接尽快地显示出来。

    实际意义在于：用户打开一个文件后，可以建立自己的文件缓冲区，而不必使用fopen()函数打开文件时设定的默认缓冲区。
    这样就可以让用户自己来控制缓冲区，包括改变缓冲区大小、定时刷新缓冲区、改变缓冲区类型、删除流中默认的缓冲区、为不带缓冲区的流开辟缓冲区等。
    */
    setvbuf(f, NULL, _IONBF, 0);
    speed = 0;
    failed = 0;
    bytes = 0;

    while (1) {
      /*
      从管道中读取子线程中的记录
      成功则返回被赋值的参数的个数
      */
      pid = fscanf(f, "%d %d %d", &i, &j, &k);
      if (pid < 2) {
        fprintf(stderr, "Some of our childrens died.\n");
        break;
      }
      speed += i;
      failed += j;
      bytes += k;
      /* fprintf(stderr,"*Knock* %d %d read=%d\n",speed,failed,pid); */
      /* 直到没有client */
      if (--clients == 0)
        break;
    }
    fclose(f);

    printf("\nSpeed=%d pages/min, %d bytes/sec.\nRequests: %d susceed, %d "
           "failed.\n",
           (int)((speed + failed) / (benchtime / 60.0f)),
           (int)(bytes / (float)benchtime), speed, failed);
  }
  return i;
}

/*
  压力测试函数，每个client在限定时间内不断发起请求
  @param host 主机名
  @param port 端口
  @param req  请求报头
*/
void benchcore(const char *host, const int port, const char *req) {
  int rlen;
  char buf[1500];
  int s, i;

  /*===通过信号定时设置===*/
  /*
  定义函数：int sigaction(int signum, const struct sigaction *act, struct
sigaction *oldact);
  函数说明：sigaction()会依参数signum 指定的信号编号来设置该信号的处理函数。
参数signum 可以指定SIGKILL 和SIGSTOP 以外的所有信号。
  SIGKILL (确认杀死) 当用户通过kill -9命令向进程发送信号时，可靠的终止进程
  SIGSTOP (停止) 作业控制信号,暂停停止(stopped)进程的执行. 本信号不能被阻塞,
  处理或忽略.
  SIGALRM (超时) alarm函数使用该信号，时钟定时器超时响应
  */
  struct sigaction sa;

  /* setup alarm signal handler */
  sa.sa_handler = alarm_handler;
  sa.sa_flags = 0;
  if (sigaction(SIGALRM, &sa, NULL))
    exit(3);

  /*
  alarm()用来设置信号SIGALRM 在经过参数seconds
  指定的秒数后传送给目前的进程.如果参数seconds 为0, 则之前设置的闹钟会被取消,
  并将剩下的时间返回.
  */
  alarm(benchtime);

  rlen = strlen(req);

nexttry:
  while (1) {
    //过期了
    if (timerexpired) {
      if (failed > 0) {
        /* fprintf(stderr,"Correcting failed by signal\n"); */
        failed--;
      }
      return;
    }

    // socket建立连接后，建立连接过程即为三次握手
    s = Socket(host, port);
    if (s < 0) {
      failed++;
      continue;
    }
    /* 将报头信息（字符串）写入socket，发起请求 */
    if (rlen != write(s, req, rlen)) {
      failed++;
      close(s);
      continue;
    }
    /* 0 - http/0.9, 1 - http/1.0, 2 - http/1.1 */
    if (http10 == 0)
      if (shutdown(s, 1)) {
        failed++;
        close(s);
        continue;
      }
    if (force == 0) {
      /* 从socket中读取所有响应数据 */
      while (1) {
        if (timerexpired) {
          break;
        }

        /*
        read()会把参数fd 所指的文件传送count 个字节到buf 指针所指的内存中.
        若参数count为0, 则read()不会有作用并返回0.
        返回值为实际读取到的字节数, 如果返回0,
        表示已到达文件尾或是无可读取的数据,此外文件读写位置会随读取到的字节移动.
        每次读取1500字节数，并不是一次全读取的
        */
        i = read(s, buf, 1500);
        if (i < 0) {
          //响应失败，重新创建连接
          failed++;
          close(s);
          goto nexttry;
        } else if (i == 0) {
          //已全部读取，跳出
          break;
        } else {
          //统计流量
          bytes += i;
        }
      }
    }
    /* close：关闭socket，若文件顺利关闭则返回0, 发生错误时返回-1. */
    if (close(s)) {
      failed++;
      continue;
    }
    speed++;
  }
}
