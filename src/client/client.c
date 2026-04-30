#define _POSIX_C_SOURCE 199309L
#define _DEFAULT_SOURCE 1

#include "protocol.h"
#include <stdio.h>
#include <stdlib.h>
#include <strings.h>
#include <stdarg.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <errno.h>
#include <string.h>

static int tcp_fd      = -1;   // TCP socket to the server
static int udp_fd      = -1;   // UDP socket for audio
static int client_slot = -1;   // Our own client slot (given by server)
static int current_room_id = -1;
static int running     = 1;    // Client running flag
static int audio_seq   = 0;    // Sequence number for outgoing audio

// mic_muted = 1  sender thread reads mic but discards data (silent)
// mic_muted = 0  sender thread actually transmits
static volatile int mic_muted = 1;   // Muted by default on START


static void *audio_sender_thread(void *arg);
static void *audio_receiver_thread(void *arg);
static void  send_command(const char *fmt, ...);
static void  receive_response(void);


int main(int argc, char *argv[]) {
    if (argc < 3) {
        fprintf(stderr, "Usage: %s <server_ip> <port>\n", argv[0]);
        return 1;
    }

    const char *server_ip = argv[1];
    int port = atoi(argv[2]);

    tcp_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (tcp_fd == -1) { perror("TCP socket"); return 1; }

    struct sockaddr_in tcp_addr;
    memset(&tcp_addr, 0, sizeof(tcp_addr));
    tcp_addr.sin_family = AF_INET;
    tcp_addr.sin_port   = htons(port);
    inet_pton(AF_INET, server_ip, &tcp_addr.sin_addr);

    if (connect(tcp_fd, (struct sockaddr *)&tcp_addr, sizeof(tcp_addr)) == -1) {
        perror("connect");
        close(tcp_fd);
        return 1;
    }
    printf("Connected to server %s:%d\n", server_ip, port);

    udp_fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (udp_fd == -1) { perror("UDP socket"); close(tcp_fd); return 1; }

    struct sockaddr_in udp_addr;
    memset(&udp_addr, 0, sizeof(udp_addr));
    udp_addr.sin_family = AF_INET;
    udp_addr.sin_port   = htons(port);
    inet_pton(AF_INET, server_ip, &udp_addr.sin_addr);
    connect(udp_fd, (struct sockaddr *)&udp_addr, sizeof(udp_addr));

    printf("Commands: LOGIN, CREATE, JOIN, LEAVE, LIST, STATUS, LOGOUT\n");
    printf("Audio:    START (join audio, mic OFF) | UNMUTE (speak) | MUTE (stop speaking)\n\n");

    char line[512];
    int audio_started  = 0;  // receiver thread launched
    int sender_started = 0;  // sender thread launched

    while (running && fgets(line, sizeof(line), stdin)) {
        line[strcspn(line, "\n")] = '\0';
        if (strlen(line) == 0) continue;

        // Quit
        if (strcasecmp(line, "QUIT") == 0 || strcasecmp(line, "EXIT") == 0) {
            send_command("LOGOUT");
            break;
        }

        // START  begin audio session — only starts the LISTENER (aplay), mic is fully OFF
        if (strcasecmp(line, "START") == 0) {
            if (!audio_started) {
                audio_started = 1;

                // Register our UDP address with the server (keepalive packet)
                audio_packet hello;
                memset(&hello, 0, sizeof(hello));
                hello.sender_id    = client_slot;
                hello.payload_size = 0;
                send(udp_fd, &hello, sizeof(hello), 0);

                // Only start the receiver thread — NO microphone yet
                pthread_t receiver_t;
                pthread_create(&receiver_t, NULL, audio_receiver_thread, NULL);
                printf("[START] Listening for audio. Mic is OFF.\n");
                printf("        Type UNMUTE to turn mic ON and speak.\n");
            } else {
                printf("[INFO] Audio already started.\n");
            }
            continue;
        }

        // UNMUTE  start the sender thread (open mic) for the first time, or just flag it
        if (strcasecmp(line, "UNMUTE") == 0) {
            if (!audio_started) {
                printf("[INFO] Type START first.\n");
            } else if (!sender_started) {
                // First UNMUTE: launch the sender thread
                sender_started = 1;
                mic_muted = 0;
                pthread_t sender_t;
                pthread_create(&sender_t, NULL, audio_sender_thread, NULL);
                printf("[MIC] Mic is now ON — you are speaking.\n");
            } else {
                // Subsequent UNMUTE: just clear the flag
                mic_muted = 0;
                printf("[MIC] Mic is now ON — you are speaking.\n");
            }
            continue;
        }

        // MUTE → stop mic (sender thread keeps running but discards audio)
        if (strcasecmp(line, "MUTE") == 0) {
            mic_muted = 1;
            printf("[MIC] Mic is now OFF — listening only.\n");
            continue;
        }

        // Everything else → send as a server command
        send_command("%s", line);
        receive_response();
    }

    running = 0;
    close(udp_fd);
    close(tcp_fd);
    return 0;
}


static void send_command(const char *fmt, ...) {
    char buffer[MAX_PACKET_SIZE];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);
    if (send(tcp_fd, buffer, strlen(buffer), 0) == -1) {
        perror("send");
        running = 0;
    }
}


static void receive_response(void) {
    response_packet resp;
    ssize_t n = recv(tcp_fd, &resp, sizeof(resp), 0);
    if (n <= 0) {
        printf("Disconnected from server.\n");
        running = 0;
        return;
    }

    if (resp.status == 200 && strstr(resp.message, "Login successful") != NULL) {
        if (strlen(resp.data) > 0) {
            client_slot = atoi(resp.data);
            printf("[SYS] Audio channel synced. Client ID: %d\n", client_slot);
        }
    }

    printf("[%d] %s", resp.status, resp.message);
    if (strlen(resp.data) > 0) printf("\n%s", resp.data);
    printf("\n");
}


static void *audio_sender_thread(void *arg) {
    (void)arg;
    audio_packet pkt;

    FILE *arecord = popen("arecord -q -D default -t raw -f S16_LE -r 16000 -c 1 --period-size=320", "r");
    if (!arecord) { printf("Failed to start arecord\n"); return NULL; }

    int16_t pcm[320]; // 20ms @ 16kHz mono

    while (running) {
        size_t total_read = 0;
        while (total_read < sizeof(pcm) && running) {
            size_t n = fread((char*)pcm + total_read, 1, sizeof(pcm) - total_read, arecord);
            if (n > 0) total_read += n;
            else if (n == 0) break;
        }

        if (total_read == sizeof(pcm)) {
            if (!mic_muted) {
                pkt.room_id      = current_room_id;
                pkt.sender_id    = client_slot;
                pkt.seq_num      = audio_seq++;
                pkt.timestamp    = 0;
                pkt.payload_size = (uint16_t)total_read;
                memcpy(pkt.payload, pcm, total_read);
                size_t header_size = sizeof(pkt) - AUDIO_PAYLOAD_SIZE;
                send(udp_fd, &pkt, header_size + total_read, 0);
            }
        } else {
            usleep(5000);
        }
    }

    pclose(arecord);
    return NULL;
}


static void *audio_receiver_thread(void *arg) {
    (void)arg;
    audio_packet pkt;

    // Increased buffer size (-B 100000 = 100ms) to prevent underruns/noise
    FILE *aplay = popen("aplay -q -D default -t raw -f S16_LE -r 16000 -c 1 --period-size=320 -B 100000", "w");
    if (!aplay) { printf("Failed to start aplay\n"); return NULL; }

    // Pre-fill ALSA buffer with 200ms of silence to tolerate network jitter.
    // Since the server now guarantees a continuous 50-packets-per-second stream,
    // this 200ms buffer will never run out!
    int16_t silence[320] = {0};
    for (int i = 0; i < 10; i++) {
        fwrite(silence, 1, sizeof(silence), aplay);
    }
    fflush(aplay);

    int recv_count = 0;

    while (running) {
        ssize_t n = recv(udp_fd, &pkt, sizeof(pkt), 0);
        if (n >= (ssize_t)(sizeof(pkt) - AUDIO_PAYLOAD_SIZE)) {
            if (pkt.payload_size > 0 && pkt.payload_size <= AUDIO_PAYLOAD_SIZE) {
                fwrite(pkt.payload, 1, pkt.payload_size, aplay);
                fflush(aplay);
                recv_count++;
                if (recv_count % 100 == 0)
                    printf("[DEBUG] Received %d audio packets (Size: %d).\n", recv_count, pkt.payload_size);
            }
        } else {
            usleep(1000);
        }
    }

    pclose(aplay);
    return NULL;
}