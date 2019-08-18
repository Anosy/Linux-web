# 本项目主要基于epoll实现高性能的web服务器
## 语言C/C++

### 主体
1.**全部函数简介**
int hexit(char c);<br>
void encode_str(char* to, int tosize, const char* from);<br>
void decode_str(char *to, char *from);<br>
int init_listen_fd(int port, int epfd);<br>
void epoll_run(int port);<br>
void do_accpet(int cfd, int epfd);<br>
void disconnect(int cfd, int epfd);<br>
int get_line(int sock, char *buff, int size);<br>
void send_respond_head(int cfd, int no, const char *desp, const char *type, long len);<br>
void send_file(int cfd, const char *filename);<br>
void http_request(const char *request, int cfd);<br>
void do_read(int cfd, int efd);<br>
void send_dir(int cfd, const char *filename);<br>
const char *get_file_type(const char *name);<br>

<br>
**2.整体流程与函数**
1.void epoll_run(int port)<br>
利用epoll_create创建树的根节点<br>
调用init_listen_fd来创建套接字，并且将其与服务器的绑定，监听，然后添加到epoll树上<br>
使用while循环，并且调用epoll_wait不断的等待事件的发生。<br>
如果发生的事件为接收连接请求，则调用do_accept<br>
如果发生的事件为通信请求，则调用do_read<br>
2.int init_listen_fd(int port, int epfd) 创建通信套记者添加到epoll树上<br>
创建套接字<br>
绑定服务器地址和端口<br>
利用setsockopt来实现端口复用<br>
监听套接字<br>
将连接套接字利用epoll_ctl给挂到epoll树上<br>
3.void do_accept(int lfd, int epfd)<br>
调用accpet接收客户端的连接请求，并且得到通信描述符<br>
将通信的文件描述符给设定为边缘非阻塞模式<br>
然后将其给加入到epoll树上等待检测事件<br>
4.void do_read(int cfd, int epfd)<br>
按行读取通信文件描述符，如果读取的文件描述读取的内容为空，则判断为客户端断开连接<br>
由于开启了边缘触发模式，因此可以使用while循环来不断的读取数据，直到没有数据为止。<br>
读取到数据，获得请求行，如果是get方法，那么调用http_request处理get请求<br>
5.void http_request(const char *request, int cfd)<br>
获取请求行，这里使用了正则表达式匹配，分别得到请求方法，请求文件，请求的协议<br>
使用url解码，因为在浏览器发送请求的时候，会将中文给通过url编码给转换成%xx的形式，因此需要将其给转换成中文，否则显示乱码<br>
获取文件的属性，并且判断该文件是目录还是文件，然后分别调用发送目录的函数send_dir, send_file，另外需要都需要发送响应头部分<br>
6.void send_respond_head(int cfd, int no, const char *desp, const char *type, long len)<br>
使用sprintf拼接出报头，包括状态行，消息报头，空行<br>
7.void send_dir(int cfd, const char *filename)<br>
使用html的table元素来保存目录<br>
使用scandir来读取目录下的文件，另外该函数读取的目录可能出现中文，因此还需要继续url编码将其给转换成浏览器能够识别的编码<br>
循环的判断每个文件，并且将每个文件给保存到目录中，并且调用strftime来显示时间<br>
传入herf为文件或者文件夹的地址<br>
8.void send_file(int cfd, const char *filename)<br>
循环读取文件，并且边读取变发送，但是需要延迟1ms，否则将会无法完整的发送文件<br>
9.void no_found(int cfd)<br>
找不到文件或者目录，则调用该函数发送html文件，以及发送响应行404<br>

<br>
**3.难点和解决**<br>
1.为什么使用边缘触发模式？<br>
如果使用水平触发模式，循环下次调用epoll_wait的时候将会继续对该通信的文件描述符再次进行水平触发，直到缓冲区中的数据全部读取完毕，调用epoll_wait资源消耗大。
此外水平触发很少使用循环读取。<br>
如果使用边缘阻塞模式，如果缓冲区没有发生事件，即使缓冲区还有数据，但是如果不使用循环读取，使得缓冲区还存在数据，则会影响下一次读取的内容。虽然可以使用while(recv)不断的读取文件中的内容，
但是读取到最后将会导致阻塞等待缓冲区的数据，因此将不会继续循环，运行其他的文件描述符的事件检测包括新的连接。<br>
如果使用边缘非阻塞模式，可以调用循环读取，但缓冲区没有数据的时候就会触发errno，对其进行判断即可，然后进行下一次的循环epoll_wait等待其他事件是否发生。
这样就不需要多次调用epoll_wait提高效率。<br>
另外需要指出的是边缘非阻塞模式下不适用于通信的文件描述符，因为这回导致高并发情况下，一部分客户端连接不上的情况。<br>
因此，对于监听的文件描述符最好使用水平触发模式(默认模式)，对于通信的文件描述符最好使用边缘触发模式<br>
2.为什么需要使用编码和解码函数<br>
解码：因为浏览器向服务器发送请求的时候，将会把中文url编码转换成%xx的形式，因此需要将其给转换成中文，来显示<br><br>
编码：读取目录后存在中文的文件地址，那么在将文件作为herf发送给浏览器的时候，需要将其给转换成url编码的形式，否则将无法访问地址<br>

<br>
**4.效果图**<br>
![](https://github.com/Anosy/Linux-web/blob/master/result/main_index.jpg)
![](https://github.com/Anosy/Linux-web/blob/master/result/code.jpg)
![](https://github.com/Anosy/Linux-web/blob/master/result/picture.jpg)
![](https://github.com/Anosy/Linux-web/blob/master/result/music.jpg)
![](https://github.com/Anosy/Linux-web/blob/master/result/404.jpg)
![](https://github.com/Anosy/Linux-web/blob/master/result/log.jpg)
