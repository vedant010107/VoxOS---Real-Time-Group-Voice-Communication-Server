#include "server.h"

#include <errno.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/epoll.h>


#define MAX_EVENTS 64
#define UDP_BUFFER_SIZE 65536 // 64KB


void network_init(int port)
{
    tcp_listen_fd=socket(AF_INET,SOCK_STREAM,0);
    if (tcp_listen_fd==-1)
    {
        log_message("ERROR","Failed to create TCP socket: %s",strerror(errno));
        exit(1);
    }

    int opt=1;
    setsockopt(tcp_listen_fd,SOL_SOCKET,SO_REUSEADDR,&opt,sizeof(opt));

    int flags=fcntl(tcp_listen_fd,F_GETFL,0);
    fcntl(tcp_listen_fd,F_SETFL,flags|O_NONBLOCK);

    struct sockaddr_in tcp_addr;
    memset(&tcp_addr,0,sizeof(tcp_addr));
    tcp_addr.sin_family=AF_INET;
    tcp_addr.sin_addr.s_addr=INADDR_ANY;
    tcp_addr.sin_port=htons(port);

    if(bind(tcp_listen_fd,(struct sockaddr *)&tcp_addr,sizeof(tcp_addr))==-1)
    {
        log_message("ERROR","Failed to bind TCP socket: %s",strerror(errno));
        close(tcp_listen_fd);
        exit(1);
    }


    if(listen(tcp_listen_fd,SOMAXCONN)==-1)
    {
        log_message("ERROR","Failed to listen on TCP socket: %s",strerror(errno));
        close(tcp_listen_fd);
        exit(1);
    }

    log_message("INFO","TCP Server listening on port %d",port);

    udp_fd=socket(AF_INET,SOCK_DGRAM,0);
    
}