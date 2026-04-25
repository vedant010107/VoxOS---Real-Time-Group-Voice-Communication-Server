#include "server.h"
#include <signal.h>

int server_on=1;
int server_port=DEFAULT_PORT;
int client_count=0;


client *clients[MAX_CLIENTS];
pthread_mutex_t clients_mutex = PTHREAD_MUTEX_INITIALIZER;


room *room_head=NULL;
int room_count=0;
pthread_mutex_t rooms_mutex = PTHREAD_MUTEX_INITIALIZER;

ring_buffer *shm_ring=NULL;
jitter_buffer jitter_buffers[MAX_CLIENTS];
pthread_mutex_t jitter_mutex = PTHREAD_MUTEX_INITIALIZER;

int users_fd=-1;
int rooms_fd=-1;
int wal_fd=-1;


int tcp_listen_fd=-1;
int udp_fd=-1;
int epoll_fd=-1;

void handle_signal(int sig)
{
    if(sig==SIGINT || sig==SIGTERM)
    {
        log_message("INFO","Shutting down server...");
        server_on=0;
    }
}

int main(int argc,char * argv)
{   
    if(argc>1)
    {
        server_port=atoi(argv[1]);
        
    }

    signal(SIGINT,handle_signal);
    signal(SIGPIPE,SIG_IGN);

    log_message("INFO","Starting VoxOS Server on port %d",server_port);

    file_db_init();
    auth_init();
    room_manager_init();
    network_init(server_port);
    client_handler_init();
    ipc_manager_init();
    audio_mixer_init();
    console_display_init();

    log_message("INFO","Server initialization complete. Waiting for clients...");

    network_event_loop();

    shutdown_server();
    return 0;

}

void shutdown_server()
{
    log_message("INFO","Shutting down server...");

    pthread_mutex_lock(&clients_mutex);
    for(int i=0;i<MAX_CLIENTS;i++)
    {
        if(clients[i] && clients[i]->is_active)
        {
            close(clients[i]->fd);
            free(clients[i]);
            clients[i]=NULL;
        }
    }
    pthread_mutex_unlock(&clients_mutex);

    if(tcp_listen_fd!=-1) close(tcp_listen_fd);
    if(udp_fd!=-1) close(udp_fd);
    if(epoll_fd!=-1) close(epoll_fd);

    if(users_fd!=-1) close(users_fd);
    if(rooms_fd!=-1) close(rooms_fd);
    if(wal_fd!=-1) close(wal_fd);

    ipc_manager_cleanup();

    pthread_mutex_destroy(&clients_mutex);
    pthread_mutex_destroy(&rooms_mutex);
    pthread_mutex_destroy(&jitter_mutex);

    log_message("INFO","Server shutdown complete. Byee :)");

}