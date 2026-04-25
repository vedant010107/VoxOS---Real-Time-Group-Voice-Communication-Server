#ifndef SERVER_H
#define SERVER_H

#include "protocol.h"

extern int server_on;
extern int server_port;
extern int client_count;

extern client *clients[MAX_CLIENTS];
extern pthread_mutex_t clients_mutex;

extern room *room_head;
extern int room_count;
extern pthread_mutex_t rooms_mutex;

extern ring_buffer *shm_ring;
extern jitter_buffer jitter_buffers[MAX_CLIENTS];
extern pthread_mutex_t jitter_mutex;


extern int users_fd;
extern int rooms_fd;
extern int wal_fd;

extern int tcp_listen_fd;
extern int udp_fd;
extern int epoll_fd;

void auth_init();
void room_manager_init();
void file_db_init();
void network_init(int port);
void client_handler_init();
void audio_mixer_init();
void ipc_manager_init();
void console_display_init();

void shutdown_server();

void network_event_loop();

#endif
