#ifndef PROTOCOL_H
#define PROTOCOL_H

#include <stdint.h>
#include <pthread.h>

// Server constants
#define MAX_CLIENTS 30
#define MAX_ROOMS 16
#define MAX_USERNAME_LEN 32
#define MAX_PASSWORD_LEN 64
#define MAX_ROLE_LEN  16
#define MAX_ROOM_NAME_LEN 32
#define MAX_PACKET_SIZE  4096
#define AUDIO_PAYLOAD_SIZE  512
#define JITTER_BUFFER_SIZE 20
#define MAX_SPEAKERS_PER_ROOM 5
#define RING_BUFFER_CAPACITY  64
#define DEFAULT_PORT 8080

// Roles
#define ROLE_ADMIN 1
#define ROLE_MODERATOR 2
#define ROLE_USER 3
#define ROLE_GUEST 4

// Command opcodes
#define CMD_LOGIN 1
#define CMD_LOGOUT 2
#define CMD_CREATE 3
#define CMD_JOIN 4
#define CMD_LEAVE 5 
#define CMD_LIST 6
#define CMD_KICK 7
#define CMD_MUTE 8
#define CMD_STATUS 9

// Status codes
#define STATUS_OK 200
#define STATUS_ERR_AUTH 401
#define STATUS_ERR_NOT_FOUND 404
#define STATUS_ERR_FORBIDDEN 403
#define STATUS_ERR_CONFLICT 409
#define STATUS_ERR_FULL 507
#define STATUS_ERR_INTERNAL 500


// Structs

typdef struct 
{
    int32_t user_id;
    char username[MAX_USERNAME_LEN];
    char role[MAX_ROLE_LEN];
    char password_hash[65];
    int32_t is_active;
} user_record;

typedef struct 
{
    int32_t room_id;
    char room_name[MAX_ROOM_NAME_LEN];
    char password[MAX_PASSWORD_LEN];
    int32_t is_active;
    int32_t user_count;
} room_record;


typdef struct
{
    int room_id;
    int seq_num;
    int timestamp;
    int payload_size;
    int payload[AUDIO_PAYLOAD_SIZE];
}audio_packet;

typedef struct
{
    int opcode;
    char username[MAX_USERNAME_LEN];
    char password[MAX_PASSWORD_LEN];
    char room_name[MAX_ROOM_NAME_LEN];
    char room_password[MAX_PASSWORD_LEN];
    char target_username[MAX_USERNAME_LEN];
    char data[MAX_PACKET_SIZE];
}command_packet;

typedef struct
{
    int opcode;
    int status;
    char message[256];
    char data[MAX_PACKET_SIZE];
}response_packet;

typedef struct room room; // forward declaration for client struct
{
    int fd;
    int user_id;
    char username[MAX_USERNAME_LEN];
    int role;
    struct room *current_room;
    int is_mute;
    int is_active;
}client;

typedef struct room
{
    int room_id;
    char room_name[MAX_ROOM_NAME_LEN];
    char password[MAX_PASSWORD_LEN];
    client *participants[MAX_CLIENTS];
    int participant_count;
    int active_speakers;
    pthread_mutex_t lock;
    struct room *prev; 
    struct room *next; // for linked list (dynamic deletion or addition)
}room;

 

typedef struct
{
    int seq_num;
    int timestamp;
    int payload_size;
    int payload[AUDIO_PAYLOAD_SIZE];
    int valid; // flag to indicate if the packet is valid (for jitter buffer)
}jitter_entry;

typedef struct
{
    jitter_entry buffer[JITTER_BUFFER_SIZE];
    int head;
    int tail;
    int count;
    int next_expected_seq;
}jitter_buffer;

typedef struct
{
    audio_packet packets[RING_BUFFER_CAPACITY];
    int head;
    int tail;
    int count;
}ring_buffer;

#endif

