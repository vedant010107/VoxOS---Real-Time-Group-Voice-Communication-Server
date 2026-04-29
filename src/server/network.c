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

    int opt=1;// immediate binding if the prev server crashed using reuseaddr
    setsockopt(tcp_listen_fd,SOL_SOCKET,SO_REUSEADDR,&opt,sizeof(opt));

    int flags=fcntl(tcp_listen_fd,F_GETFL,0);
    fcntl(tcp_listen_fd,F_SETFL,flags|O_NONBLOCK);// to add this nonblock flag which immediately returns instead of waiting

    struct sockaddr_in tcp_addr;
    memset(&tcp_addr,0,sizeof(tcp_addr));// for bind to work as padding of 0
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
    if(udp_fd==-1)
    {
        log_message("ERROR","Failed to create UDP socket: %s",strerror(errno));
        close(tcp_listen_fd);
        exit(1);
    }

    flags=fcntl(udp_fd,F_GETFL,0);
    fcntl(udp_fd,F_SETFL,flags|O_NONBLOCK);

    struct sockaddr_in udp_addr;
    memset(&udp_addr,0,sizeof(udp_addr));
    udp_addr.sin_family=AF_INET;
    udp_addr.sin_addr.s_addr=INADDR_ANY;
    udp_addr.sin_port=htons(port);

    if(bind(udp_fd,(struct sockaddr *)&udp_addr,sizeof(udp_addr))==-1)
    {
        log_message("ERROR","Failed to bind UDP socket: %s",strerror(errno));
        close(tcp_listen_fd);
        close(udp_fd);
        exit(1);
    }

    log_message("INFO","UDP Server listening on port %d",port);

    epoll_fd=epoll_create1(0);
    if(epoll_fd==-1)
    {
        log_message("ERROR","Failed to create epoll: %s",strerror(errno));
        close(tcp_listen_fd);
        close(udp_fd);
        exit(1);
    }

    struct epoll_event ev;
    ev.events=EPOLLIN;
    ev.data.fd=tcp_listen_fd;
    if(epoll_ctl(epoll_fd,EPOLL_CTL_ADD,tcp_listen_fd,&ev)==-1)
    {
        log_message("ERROR","Failed to add TCP listen socket to epoll: %s",strerror(errno));
        exit(1);        
    }

    ev.events=EPOLLIN;
    ev.data.fd=udp_fd;
    if(epoll_ctl(epoll_fd,EPOLL_CTL_ADD,udp_fd,&ev)==-1)
    {
        log_message("ERROR","Failed to add UDP socket to epoll: %s",strerror(errno));
        exit(1);
    }

    log_message("INFO","epoll initialization complete.Server is ready .");
}

static void accept_new_client()
{
    struct sockaddr_in client_addr;
    socklen_t addr_len=sizeof(client_addr);

    int client_fd=accept(tcp_listen_fd,(struct sockaddr *)&client_addr,&addr_len);
    if(client_fd==-1)
    {
        if(errno!=EAGAIN && errno!=EWOULDBLOCK)
        {
            log_message("ERROR","Failed to accept new client: %s",strerror(errno));
        }
        return;
    }

    int flags=fcntl(client_fd,F_GETFL,0);
    fcntl(client_fd,F_SETFL,flags|O_NONBLOCK);

    pthread_mutex_lock(&clients_mutex);

    if(client_count>=MAX_CLIENTS)
    {
        log_message("WARN","Server full. Rejecting connection from %s",inet_ntoa(client_addr.sin_addr));
        close(client_fd);
        pthread_mutex_unlock(&clients_mutex);
        return; 
    }

    int slot=-1;
    for(int i=0;i<MAX_CLIENTS;i++)
    {
        if(clients[i]==NULL)
        {
            slot=i;
            break;
        }
    }

    if(slot==-1)// mostly doesnt happen ,but just in case yk
    {
        close(client_fd);
        pthread_mutex_unlock(&clients_mutex);
        return;
    }

    client *new_client=malloc(sizeof(client));
    if(new_client==NULL)
    {
        log_message("ERROR","Failed to allocate memory for new client");
        close(client_fd);
        pthread_mutex_unlock(&clients_mutex);
        return;
    }

    memset(new_client,0,sizeof(client));
    new_client->fd=client_fd;
    new_client->is_active=1;
    new_client->user_id=-1; // not assigned yet
    new_client->role=ROLE_GUEST; // default role
    new_client->is_mute=0; // not muted by default
    new_client->current_room=NULL;
    clients[slot]=new_client;
    client_count++;

    pthread_mutex_unlock(&clients_mutex);

    struct epoll_event ev;

    ev.events=EPOLLIN;
    ev.data.fd=client_fd;
    epoll_ctl(epoll_fd,EPOLL_CTL_ADD,client_fd,&ev);

    log_message("INFO","New client connected from %s. Assigned slot %d, fd %d" ,inet_ntoa(client_addr.sin_addr),slot,client_fd);

    char welcome_msg[]="Welcome to VoxOS! Please login .\n";
    send(client_fd,welcome_msg,strlen(welcome_msg),0);
}

static void handle_tcp_command(int client_fd)
{
    char buffer[MAX_PACKET_SIZE];
    ssize_t bytes=recv(client_fd,buffer,sizeof(buffer)-1,0);

    if(bytes<=0)
    {
        if(bytes==0)
        {
            log_message("INFO","Client fd %d disconnected",client_fd);
        }
        else if(errno!=EAGAIN && errno!=EWOULDBLOCK)
        {
            log_message("ERROR","Failed to read from client fd %d: %s",client_fd,strerror(errno));
        }
        disconnect_client(client_fd);
        return;
    }

    buffer[bytes]='\0';

    pthread_mutex_lock(&clients_mutex);
    client *c=NULL;
    for(int i=0;i<MAX_CLIENTS;i++)
    {
        if(clients[i]!=NULL && clients[i]->fd==client_fd)
        {
            c=clients[i];
             break;
        }
    }

    pthread_mutex_unlock(&clients_mutex);

    if(c==NULL)
    {
        log_message("WARN","Client fd %d not found in clients list",client_fd);
        return;
    }

    process_client_commnand(c,buffer);
}

static void handle_udp_packer()
{
    audio_packet pkt;
    struct sockaddr_in sender_addr;
    socklen_t addr_len=sizeof(sender_addr);

    ssize_t bytes=recvfrom(udp_fd,&pkt,sizeof(pkt),0,(struct sockaddr *)&sender_addr,&addr_len);

    if(bytes<=0)
    {
        if(errno!=EAGAIN && errno!=EWOULDBLOCK)
        {
            log_message("ERROR","Failed to read UDP packet: %s",strerror(errno));
        }
        return;
    }

    if(shm_ring!=NULL)
    {
        ring_buffer_push(shm_ring,&pkt);
    }
}

void disconnect_client(int client_fd)
{
    pthread_mutex_lock(&clients_mutex);
    for(int i=0;i<MAX_CLIENTS;i++)
    {
        if(clients[i]!=NULL && clients[i]->fd==client_fd)
        {
            client *c=clients[i];
            if(c->current_room!=NULL)
            {
                remove_client_from_room(c->current_room,c);
            }
            epoll_ctl(epoll_fd,EPOLL_CTL_DEL,client_fd,NULL);
            close(c->fd);
            free(c);
            clients[i]=NULL;
            client_count--;
            log_message("INFO","Client fd %d disconnected and cleaned up",client_fd);
            break;
        }
    }
    pthread_mutex_unlock(&clients_mutex);
}


void network_event_loop()
{
    struct epoll_event events[MAX_EVENTS];

    log_message("INFO","Entering main network event loop");

    while(server_on)
    {
        int n=epoll_wait(epoll_fd,events,MAX_EVENTS,500);

        if(n==-1)
        {
            if(errno==EINTR)
            {
                continue; // interrupted by signal, just retry
            }
            log_message("ERROR","epoll_wait failed: %s",strerror(errno));
            break;
        }

        for(int i=0;i<n;i++)
        {
            int fd=events[i].data.fd;
            if(fd==tcp_listen_fd)
            {
                accept_new_client();
            }
            else if(fd==udp_fd)
            {
                handle_udp_packet();
            }
            else
            {
                if(events[i].events & (EPOLLIN | EPOLLHUP | EPOLLERR))
                {
                    handle_tcp_command(fd);
                }
            }
        }
    }
    log_message("INFO","Exiting network event loop");
}

void send_response(int client_fd,response_packet *resp)
{
    ssize_t sent=send(client_fd,resp,sizeof(response_packet),0);
    if(sent==-1)
    {
        log_message("ERROR","Failed to send response to client fd %d: %s",client_fd,strerror(errno));
    }

}