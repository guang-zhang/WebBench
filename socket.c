/* $Id: socket.c 1.1 1995/01/01 07:11:14 cthuang Exp $
 *
 * This module has been modified by Radim Kolar for OS/2 emx
 */

/***********************************************************************
  module:       socket.c
  program:      popclient
  SCCS ID:      @(#)socket.c    1.5  4/1/94
  programmer:   Virginia Tech Computing Center
  compiler:     DEC RISC C compiler (Ultrix 4.1)
  environment:  DEC Ultrix 4.3
  description:  UNIX sockets code.
 ***********************************************************************/

#include <arpa/inet.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>

/*
    建立网络通信连接至少要一对端口号(socket)。
    socket本质是编程接口(API)，对TCP/IP的封装，
    TCP/IP也要提供可供程序员做网络开发所用的接口，这就是Socket编程接口；
    HTTP是轿车，提供了封装或者显示数据的具体形式;Socket是发动机，提供了网络通信的能力。
*/
int Socket(const char *host, int clientPort) {
  int sock;
  unsigned long inaddr;
  struct sockaddr_in ad;
  struct hostent *hp;

  /*
    【函数说明】memset() 会将 ptr 所指的内存区域的前 num 个字节的值都设置为
    value，然后返回指向 ptr 的指针。

    参数说明：
    ptr 为要操作的内存的指针。
    value 为要设置的值。你既可以向 value 传递 int 类型的值，也可以传递 char
    类型的值，int 和 char 可以根据 ASCII 码相互转换。
    num 为 ptr 的前 num 个字节，size_t 就是unsigned int。
  */
  memset(&ad, 0, sizeof(ad));
  /* 参数 AF_INET 表示使用 IPv4 地址 */
  ad.sin_family = AF_INET;

  /*
  inet_addr()用来将参数cp 所指的网络地址字符串转换成网络所使用的二进制数字.
  网络地址字符串是以数字和点组成的字符串, 例如:"163. 13. 132. 68".
  */
  inaddr = inet_addr(host);
  /* INADDR_NONE 是个宏定义,代表IpAddress 无效的IP地址。 */
  /* 如果是IP地址 */
  if (inaddr != INADDR_NONE)
    memcpy(&ad.sin_addr, &inaddr, sizeof(inaddr));
  else {
    /* 通过域名获取IP地址 */
    hp = gethostbyname(host);
    if (hp == NULL)
      return -1;
    memcpy(&ad.sin_addr, hp->h_addr, hp->h_length);
  }
  /* htons()用来将参数指定的16位hostshort转换成网络字符顺序.
 */
  ad.sin_port = htons(clientPort);

  /* SOCK_STREAM 表示使用面向连接的数据传输方式 */
  /* 创建套接字 */
  sock = socket(AF_INET, SOCK_STREAM, 0);
  if (sock < 0)
    return sock;
  if (connect(sock, (struct sockaddr *)&ad, sizeof(ad)) < 0)
    return -1;
  return sock;
}
