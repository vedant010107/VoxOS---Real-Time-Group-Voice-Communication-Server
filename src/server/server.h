#ifndef SERVER_H
#define SERVER_H

#include "protocol.h"
#include <unistd.h>    // for usleep

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

void auth_init(void);
void room_manager_init(void);
void file_db_init(void);
void network_init(int port);
void client_handler_init(void);
void audio_mixer_init(void);
void ipc_manager_init(void);
void console_display_init(void);

void shutdown_server(void);
void network_event_loop(void);

int  ring_buffer_push(ring_buffer *ring, audio_packet *pkt);
int  ring_buffer_pop(ring_buffer *ring, audio_packet *pkt);
void ipc_manager_cleanup(void);

int  db_read_room(int room_id, room_record *rec);
int  db_write_room(int room_id, room_record *rec);
int  db_read_room_raw(int room_id, room_record *rec);
int  db_next_free_room_id(void);
int  db_read_user_by_name(const char *username, user_record *rec);
void wal_begin(int record_id, room_record *old_rec, room_record *new_rec);
void wal_commit(int record_id);
void wal_rollback(int record_id);
void wal_recover(void);

int  room_create(const char *name, const char *password, client *creator);
int  room_delete(int room_id);
int  room_join(int room_id, const char *password, client *c);
int  room_leave(client *c);
room *room_find_by_name(const char *name);
room *room_find_by_id(int room_id);
void room_list_formatted(char *buffer, int buffer_size);
void remove_client_from_room(room *r, client *c);

int  auth_authenticate(const char *username, const char *password, int *role_out);
int  auth_check_permission(int role, int command);
int  auth_add_user(const char *username, const char *password, const char *role);
int  auth_promote_user(const char *username, const char *new_role);

void log_message(const char *level, const char *fmt, ...);
void sha256_hash(const char *input, char output[65]);
int  safe_memcmp(const void *a, const void *b, size_t size);
long long get_time_ms(void);

void send_response(int client_fd, response_packet *resp);
void disconnect_client(int client_fd);
void process_client_command(client *c, const char *raw_command);

#endif