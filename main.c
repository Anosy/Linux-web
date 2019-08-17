#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include "epoll_server.h"

int main(int argc, char *argv[])
{
    if (argc < 3)
    {
        printf("Please input port path!\n");
        return 0;
    }
    int port = atoi(argv[1]);
    int ret = chdir(argv[2]);
    if (ret == -1)
    {
        perror("chdir error");
        return 0;
    }

    epoll_run(port);

    return 0;
}

