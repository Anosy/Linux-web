#ifndef _EPOLL_SERVER_H
#define _EPOLL_SERVER_H

int hexit(char c);
void encode_str(char* to, int tosize, const char* from);
void decode_str(char *to, char *from);
int init_listen_fd(int port, int epfd);
void epoll_run(int port);
void do_accpet(int cfd, int epfd);
void disconnect(int cfd, int epfd);
int get_line(int sock, char *buff, int size);
void send_respond_head(int cfd, int no, const char *desp, const char *type, long len);
void send_file(int cfd, const char *filename);
void http_request(const char *request, int cfd);
void do_read(int cfd, int efd);
void send_dir(int cfd, const char *filename);
const char *get_file_type(const char *name);

#endif
