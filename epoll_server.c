#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/epoll.h>
#include <arpa/inet.h>
#include <string.h>
#include <fcntl.h>
#include <dirent.h>
#include <ctype.h>
#include <time.h>
#include "epoll_server.h"

#define MAXSIZE 2000

// 16进制数转化为10进制
int hexit(char c)
{
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;

    return 0;
}

/*
 *  这里的内容是处理%20之类的东西！是"解码"过程。
 *  %20 URL编码中的‘ ’(space)
 *  %21 '!' %22 '"' %23 '#' %24 '$'
 *  %25 '%' %26 '&' %27 ''' %28 '('......
 *  相关知识html中的‘ ’(space)是&nbsp
 */
void encode_str(char* to, int tosize, const char* from)
{
    int tolen;

    for (tolen = 0; *from != '\0' && tolen + 4 < tosize; ++from) 
    {
        if (isalnum(*from) || strchr("/_.-~", *from) != (char*)0) 
        {
            *to = *from;
            ++to;
            ++tolen;
        } 
        else 
        {
            sprintf(to, "%%%02x", (int) *from & 0xff);
            to += 3;
            tolen += 3;
        }

    }
    *to = '\0';
}

// 将url编码的结果给解码
void decode_str(char *to, char *from)
{
    for ( ; *from != '\0'; ++to, ++from  ) 
    {
        if (from[0] == '%' && isxdigit(from[1]) && isxdigit(from[2])) 
        { 
            *to = hexit(from[1])*16 + hexit(from[2]);
            from += 2;                      
        } 
        else
        {
            *to = *from;
        }

    }
    *to = '\0';
}


int init_listen_fd(int port, int epfd)
{
    // 创建监听的套接字
    int lfd = socket(AF_INET, SOCK_STREAM, 0);
    if (lfd == -1)
    {
        perror("socket error");
        return 0;
    }

    // 绑定IP和端口
    struct sockaddr_in server;
    memset(&server, 0, sizeof(server));
    server.sin_addr.s_addr = htonl(INADDR_ANY);
    server.sin_family = AF_INET;
    server.sin_port = htons(port);
    // 端口复用
    int flag = 1;
    setsockopt(lfd, SOL_SOCKET, SO_REUSEADDR, &flag, sizeof(flag));
    int ret = bind(lfd, (struct sockaddr *)&server, sizeof(server));
    if (ret == -1)
    {
        perror("bind error");
        return 0;
    }

    // listen  监听
    ret = listen(lfd, 64);
    if (ret == -1)
    {
        perror("listen error");
        return 0;
    }
    // lfd 添加到epoll树上
    struct epoll_event ev;
    ev.events = EPOLLIN;
    ev.data.fd = lfd;
    ret = epoll_ctl(epfd, EPOLL_CTL_ADD, lfd, &ev);
    if (ret == -1)
    {
        perror("epoll_ctl error");
        return 0;
    }

    return lfd;
}

// 断开连接的函数
void disconnect(int cfd, int epfd)
{
    int ret = epoll_ctl(epfd, EPOLL_CTL_DEL, cfd, NULL);
    if (ret == -1)
    {
        perror("EPOLL_CTL_DEL error");
        return ;
    }
    close(cfd);
}

// 接收连接请求
void do_accept(int lfd, int epfd)
{
    // 创建客户端的地址，作为传出参数
    struct sockaddr_in client;
    socklen_t len = sizeof(client);
    int cfd = accept(lfd, (struct sockaddr *)&client, &len);
    if (cfd == -1)
    {
        perror("accpet error");
        return;
    }
    // 打印客户端信息
    char ip[64] = {0};
    printf("New Client, IP: %s, Port: %d, cfd=%d\n", 
           inet_ntop(AF_INET, &client.sin_addr.s_addr, ip, sizeof(ip)), 
           ntohs(client.sin_port), cfd);
    // 设置cfd非阻塞,边沿非阻塞模式
    int flag = fcntl(cfd, F_GETFL);
    flag |= O_NONBLOCK;
    fcntl(cfd, F_SETFL, flag);
    // 得到新的节点挂载树上
    struct epoll_event ev;
    ev.data.fd = cfd;
    ev.events = EPOLLIN | EPOLLET;
    int ret = epoll_ctl(epfd, EPOLL_CTL_ADD, cfd, &ev);
    if (ret == -1)
    {
        perror("epoll_ctl add cfd error");
        return;
    }
}

// 解析http请求消息的每一行内容
int get_line(int sock, char *buf, int size)
{
    int i = 0;
    char c = '\0';
    int n;
    while ((i < size - 1) && (c != '\n'))
    {
        n = recv(sock, &c, 1, 0);
        if (n > 0)
        {
            if (c == '\r')
            {
                n = recv(sock, &c, 1, MSG_PEEK);
                if ((n > 0) && (c == '\n'))
                {
                    recv(sock, &c, 1, 0);
                }
                else
                {
                    c = '\n';
                }
            }
            buf[i] = c;
            i++;
        }
        else
        {
            c = '\n';
        }
    }
    buf[i] = '\0';

    return i;
}

// 发送报头
void send_respond_head(int cfd, int no, const char *desp, const char *type, long len)
{
    char buff[1024] = {0};
    // 状态行
    sprintf(buff, "http/1.1 %d %s\r\n", no, desp);
    printf("%s", buff);
    send(cfd, buff, strlen(buff), 0);
    memset(buff, 0, sizeof(buff));
    // 消息报头
    sprintf(buff, "Content-Type:%s\r\n", type);
    sprintf(buff + strlen(buff), "Content-Length:%ld\r\n", len);
    printf("%s", buff);
    send(cfd, buff, strlen(buff), 0);
    // 空行
    send(cfd, "\r\n", 2, 0);
    printf("\n");
    printf("========THE END==========\n");
}

// 发送文件
void send_file(int cfd, const char *filename)
{
    int fd = open(filename, O_RDONLY);
    if (fd == -1)
    {
        send_respond_head(cfd, 404, "File No Found!", ".html", -1);
        send_file(cfd, "404.html");
    }
    char buff[4096] = {0};
    int len = 0;
    while ((len = read(fd, buff, sizeof(buff))) > 0)
    {
        write(cfd, buff, len);
        memset(buff, 0, len);
        usleep(1000);
    }
    if (len == -1)
    {
        perror("read file error");
        return;
    }

    close(fd);
}

void send_dir(int cfd, const char *filename)
{
    // 构建html页面
    char buff[4096] = {0};
    sprintf(buff, "<html><head><title>目录名: %s </title></head>", filename);
    sprintf(buff+strlen(buff), "<body><h1>当前目录: %s</h1><table border=\"1\">", filename);
    sprintf(buff+strlen(buff), "<tr><td>Dir</td><td>Size</td><td>Time</td></tr>");

    char enstr[1024] = {0};
    char path[1024] = {0};
    char timestr[64] = {0};
    // 读取当前目录文件
    // 目录项的数组
    struct dirent **ptr;
    int num = scandir(filename, &ptr, NULL, alphasort);
    int i = 0;
    for (i = 0; i < num; i++)
    {
        char *name = ptr[i]->d_name;
        // 拼结文件的完整路径
        sprintf(path, "%s/%s", filename, name);
        //printf("path=%s ========\n", path);
        struct stat st;
        stat(path, &st);
        // url编码
        encode_str(enstr, sizeof(enstr), name);
        // 得到文件的修改时间
        strftime(timestr, sizeof(timestr), "%d  %b   %Y  %H:%M", localtime(&st.st_mtime));
        // 如果是文件
        if (S_ISREG(st.st_mode))
        {
            sprintf(buff+strlen(buff), "<tr><td><a href=\"%s\">%s</a></td><td>%ld</td><td>%s</td></tr>",
                enstr, name, (long)st.st_size, timestr);
        }
        // 如果是目录
        else if (S_ISDIR(st.st_mode))
        {
            sprintf(buff+strlen(buff), "<tr><td><a href=\"%s/\">%s/</a></td><td>%ld</td><td>%s</td></tr>",
                enstr, name, (long)st.st_size, timestr);
        }
        send(cfd, buff, strlen(buff), 0);
        memset(buff, 0, sizeof(buff));
    }
    sprintf(buff+strlen(buff), "</table></body></html>");
    send(cfd, buff, sizeof(buff), 0);
    printf("Dir message send ok!\n");
}

// http get 请求
void http_request(const char *request, int cfd)
{
    // 拆分http请求行
    char method[12];
    char path[1024];
    char protocol[12];
    sscanf(request, "%[^ ] %[^ ] %[^ ]", method, path, protocol); // 正则表达式匹配

    printf("\n～～～～～响应报文～～～～～\n");
    printf("======响应行======\n");
    // 转码 将不能识别的中文乱码 -> 中文
    decode_str(path, path);
    //printf("docode str %s\n", path);
    // 处理path
    char *file = path + 1;
    // 如果没有指定访问资源，显示目录中的资源内容
    if (strcmp(path, "/") == 0)
    {
        file = "./";
    }

    // 获取文件的属性
    struct stat st;
    int ret = stat(file, &st);
    if (ret == -1)
    {
        send_respond_head(cfd, 404, "File Not Found", ".html", -1);
        send_file(cfd, "404.html");
    }
    // 判断是目录还是文件
    // 如果是目录
    if (S_ISDIR(st.st_mode))
    {
        // 发送消息报头
        send_respond_head(cfd, 200, "OK", get_file_type(".html"), -1);
        // 发送目录消息
        send_dir(cfd, file);
    }
    // 如果是文件
    else if (S_ISREG(st.st_mode))
    {
        // 发送消息报头
        send_respond_head(cfd, 200, "OK", get_file_type(file), st.st_size);
        // 发送文件的内容
        send_file(cfd, file);
    }
}

//  读取通信数据
void do_read(int cfd, int epfd)
{
    char line[1024] = {0};
    int len = get_line(cfd, line, sizeof(line));
    if (len == 0)
    {
        printf("客户端断开了连接!\n");
        disconnect(cfd, epfd);
    }
    else
    {
        printf("～～～～～请求报文～～～～～\n");
        printf("======请求行======\n");
        printf("%s", line);
        printf("======请求头======\n");
        while (len)
        {
            char buff[1024] = {0};
            len = get_line(cfd, buff, sizeof(buff));
            write(STDOUT_FILENO, buff, len);
        }
        printf("========THE END==========\n");
    }
    // 请求行的格式 GET /XX HTTP/1.1
    if (strncasecmp("get", line, 3) == 0)
    {
        // 处理get请求
        http_request(line, cfd);
        // 关闭套接字，cfd从epoll树上删除
        disconnect(cfd, epfd);
    }
}

// 创建epoll
void epoll_run(int port)
{
    // 创建树的根节点
    int epfd = epoll_create(MAXSIZE);
    if (epfd == -1)
    {
        perror("epoll_create error");
        return ;
    }

    // 添加要监听的节点
    // 先添加监听的lfd
    int lfd = init_listen_fd(port, epfd);

    // 委托内核检测添加到树上的节点
    struct epoll_event all[MAXSIZE];
    while (1)
    {
        // 阻塞等待事件的发生
        int ret = epoll_wait(epfd, all, MAXSIZE, -1);
        if (ret == -1)
        {
            perror("epoll_wait error");
            return;
        }
        // 遍历每个发送的事件
        int i = 0;
        for (i = 0; i < ret; i++)
        {
            
            struct epoll_event *pev = &all[i];
            // 如果事件不是读事件，跳过 
            if (!(pev->events & EPOLLIN))
            {
                continue;
            }
            // 接收连接请求
            if (pev->data.fd == lfd)
            {
                do_accept(lfd, epfd);
            }
            // 读取通信数据
            else
            {
                do_read(pev->data.fd, epfd);
            }
        }
    }
}

// 通过文件名获取文件的类型
const char *get_file_type(const char *name)
{
    char* dot;

    // 自右向左查找‘.’字符, 如不存在返回NULL
    dot = strrchr(name, '.');   
    if (dot == NULL)
        return "text/plain; charset=utf-8";
    if (strcmp(dot, ".html") == 0 || strcmp(dot, ".htm") == 0)
        return "text/html; charset=utf-8";
    if (strcmp(dot, ".jpg") == 0 || strcmp(dot, ".jpeg") == 0)
        return "image/jpeg";
    if (strcmp(dot, ".gif") == 0)
        return "image/gif";
    if (strcmp(dot, ".png") == 0)
        return "image/png";
    if (strcmp(dot, ".css") == 0)
        return "text/css";
    if (strcmp(dot, ".au") == 0)
        return "audio/basic";
    if (strcmp( dot, ".wav" ) == 0)
        return "audio/wav";
    if (strcmp(dot, ".avi") == 0)
        return "video/x-msvideo";
    if (strcmp(dot, ".mov") == 0 || strcmp(dot, ".qt") == 0)
        return "video/quicktime";
    if (strcmp(dot, ".mpeg") == 0 || strcmp(dot, ".mpe") == 0)
        return "video/mpeg";
    if (strcmp(dot, ".vrml") == 0 || strcmp(dot, ".wrl") == 0)
        return "model/vrml";
    if (strcmp(dot, ".midi") == 0 || strcmp(dot, ".mid") == 0)
        return "audio/midi";
    if (strcmp(dot, ".mp3") == 0)
        return "audio/mpeg";
    if (strcmp(dot, ".ogg") == 0)
        return "application/ogg";
    if (strcmp(dot, ".pac") == 0)
        return "application/x-ns-proxy-autoconfig";

    return "text/plain; charset=utf-8";
}
