#define _POSIX_C_SOURCE 200809L
#define _DEFAULT_SOURCE 1

#include "server.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <semaphore.h>
#include <sched.h>
#include <errno.h>

#include <time.h>

// Forward declarations (defined in this file but used before their definitions)
static int jitter_buffer_push(jitter_buffer *jb, audio_packet *pkt);
static int jitter_buffer_pop(jitter_buffer *jb, jitter_entry *out);

static pthread_t mixer_thread;                  // The mixer thread
static sem_t room_speaker_sem[MAX_ROOMS];      // One semaphore per room
static int mixer_running = 1;                   // Flag to stop mixer thread


static void *mixer_thread_main(void *arg);
static void mix_room(room *r);
static void send_audio_to_client(client *c, int16_t *pcm, int samples);
static int16_t clamp_add(int16_t a, int16_t b);


void audio_mixer_init(void) {
    // Initialize per-room speaker semaphores (max 5 speakers per room)
    for (int i = 0; i < MAX_ROOMS; i++) {
        sem_init(&room_speaker_sem[i], 0, MAX_SPEAKERS_PER_ROOM);
    }

    // Clear all jitter buffers
    memset(jitter_buffers, 0, MAX_CLIENTS * sizeof(jitter_buffer));

    // Create the mixer thread with real-time priority
    pthread_create(&mixer_thread, NULL, mixer_thread_main, NULL);

    log_message("INFO", "Audio mixer ready (real-time thread, %d speakers/room)",
                MAX_SPEAKERS_PER_ROOM);
}


static void *mixer_thread_main(void *arg) {
    (void)arg;

    // Set real-time scheduling priority
    struct sched_param param;
    param.sched_priority = 50;
    if (pthread_setschedparam(pthread_self(), SCHED_FIFO, &param) != 0) {
        log_message("WARN", "Could not set SCHED_FIFO: %s (run as root for best results)",
                    strerror(errno));
    }

    audio_packet pkt;

    struct timespec next_tick;
    clock_gettime(CLOCK_MONOTONIC, &next_tick);

    while (mixer_running && server_on) {
       
        while (shm_ring != NULL && ring_buffer_pop(shm_ring, &pkt)) {
            // pkt.sender_id tells us which client slot sent this audio
            int slot = (int)pkt.sender_id;
            if (slot < 0 || slot >= MAX_CLIENTS) continue;

            pthread_mutex_lock(&jitter_mutex);
            jitter_buffer_push(&jitter_buffers[slot], &pkt);
            pthread_mutex_unlock(&jitter_mutex);
        }

       
        pthread_mutex_lock(&rooms_mutex);
        room *r = room_head;
        while (r != NULL) {
            if (r->participant_count > 0) {
                mix_room(r);
            }
            r = r->next;
        }
        pthread_mutex_unlock(&rooms_mutex);

        // Calculate next 20ms tick for a perfect monotonic audio clock
        next_tick.tv_nsec += 20000000;
        if (next_tick.tv_nsec >= 1000000000) {
            next_tick.tv_nsec -= 1000000000;
            next_tick.tv_sec += 1;
        }
        
        // Sleep exactly until the next tick
        clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &next_tick, NULL);
    }

    return NULL;
}


static void mix_room(room *r) {
    // Static arrays: allocated once in BSS at process start — safe because
    // mix_room is called exclusively by the single mixer thread (non-reentrant).
    // With MAX_CLIENTS=50, this avoids a 32 KB stack frame allocation at 50 Hz.
    static int16_t speaker_pcm[MAX_CLIENTS][320];
    static int     speaker_samples[MAX_CLIENTS];
    static int     speaker_slot[MAX_CLIENTS];
    static client *participant_snapshot[MAX_CLIENTS]; // for lock-free send phase

    int speaker_count  = 0;
    int snapshot_count = 0;

    // Single lock: atomically collect speaker audio AND snapshot participants.
    // The previous double-lock pattern (unlock → immediately re-lock) caused
    // ~3,200 unnecessary mutex syscalls per minute per room at 50 Hz.
    pthread_mutex_lock(&r->lock);

    for (int i = 0; i < r->participant_count; i++) {
        client *speaker = r->participants[i];
        if (speaker == NULL || speaker->is_mute || !speaker->is_active)
            continue;
        if (speaker_count >= MAX_CLIENTS) break;

        if (sem_trywait(&room_speaker_sem[r->room_id]) != 0)
            continue;

        jitter_buffer *jb = &jitter_buffers[speaker->slot];
        jitter_entry entry;

        if (jitter_buffer_pop(jb, &entry)) {
            int samples = (int)(entry.payload_size / sizeof(int16_t));
            if (samples > 320) samples = 320;

            memcpy(speaker_pcm[speaker_count], entry.payload, (size_t)samples * sizeof(int16_t));
            speaker_samples[speaker_count] = samples;
            speaker_slot[speaker_count]    = speaker->slot;
            speaker_count++;
        }

        sem_post(&room_speaker_sem[r->room_id]);
    }

    r->active_speakers = speaker_count;

    // Snapshot the participant list before releasing — lets us send audio
    // outside the lock so sendto() syscalls don't hold the room mutex.
    for (int i = 0; i < r->participant_count && snapshot_count < MAX_CLIENTS; i++) {
        participant_snapshot[snapshot_count++] = r->participants[i];
    }

    pthread_mutex_unlock(&r->lock);

    // Mix and send OUTSIDE the lock — sendto() is a kernel syscall that
    // must not block the room mutex used by the epoll and worker threads.
    for (int i = 0; i < snapshot_count; i++) {
        client *listener = participant_snapshot[i];
        if (!listener || !listener->is_active) continue;

        int16_t mixed[320] = {0};
        int mixed_speakers = 0;

        for (int s = 0; s < speaker_count; s++) {
            // Skip this speaker's own audio (no self-echo)
            if (speaker_slot[s] == listener->slot) continue;

            for (int j = 0; j < speaker_samples[s]; j++) {
                mixed[j] = clamp_add(mixed[j], speaker_pcm[s][j]);
            }
            mixed_speakers++;
        }

        // Normalize if many speakers talking at once
        if (mixed_speakers > 1) {
            for (int j = 0; j < 320; j++) {
                mixed[j] = (int16_t)(mixed[j] / mixed_speakers);
            }
        }

        // ALWAYS send a packet to keep the client's ALSA buffer alive!
        send_audio_to_client(listener, mixed, 320);
    }
}



static void send_audio_to_client(client *c, int16_t *pcm, int samples) {
    audio_packet pkt;
    size_t header_size;
    socklen_t addr_len;
    ssize_t sent;

    memset(&pkt, 0, sizeof(pkt));

    pkt.room_id   = (c->current_room != NULL) ? (uint32_t)c->current_room->room_id : 0;
    pkt.sender_id = 0;   // mixed audio has no specific sender
    pkt.seq_num   = 0;   // could be a room-level seq counter
    pkt.timestamp = (uint32_t)get_time_ms();

    pkt.payload_size = (uint16_t)(samples * (int)sizeof(int16_t));
    if (pkt.payload_size > AUDIO_PAYLOAD_SIZE) {
        pkt.payload_size = AUDIO_PAYLOAD_SIZE;
    }
    memcpy(pkt.payload, pcm, pkt.payload_size);

    // Send to the client over UDP
    // (We need the client's UDP address; for now we store it in the client struct
    //  when the first audio packet from that client is received.)
    header_size = sizeof(pkt) - AUDIO_PAYLOAD_SIZE;
    addr_len = c->udp_addr_len ? c->udp_addr_len : (socklen_t)sizeof(c->udp_addr);
    
    sent = sendto(udp_fd, &pkt, header_size + pkt.payload_size, 0,
                  (struct sockaddr *)&c->udp_addr, addr_len);
    if (sent < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
        // Non-fatal UDP drop, but suppresses warn_unused_result
    }
}


static int jitter_buffer_push(jitter_buffer *jb, audio_packet *pkt) {
    int idx = -1;
    // Find an empty slot
    for (int i = 0; i < JITTER_BUFFER_SIZE; i++) {
        if (!jb->buffer[i].valid) {
            idx = i;
            break;
        }
    }
    if (idx == -1) {
        // Buffer is full. Find the oldest packet (smallest seq_num) and overwrite it.
        int oldest_idx = 0;
        uint32_t oldest_seq = jb->buffer[0].seq_num;
        for (int i = 1; i < JITTER_BUFFER_SIZE; i++) {
            if ((int32_t)(jb->buffer[i].seq_num - oldest_seq) < 0) {
                oldest_seq = jb->buffer[i].seq_num;
                oldest_idx = i;
            }
        }
        idx = oldest_idx;
        // count doesn't increase because we replace
    } else {
        jb->count++;
    }

    jb->buffer[idx].seq_num      = pkt->seq_num;
    jb->buffer[idx].timestamp    = pkt->timestamp;
    jb->buffer[idx].payload_size = pkt->payload_size;
    memcpy(jb->buffer[idx].payload, pkt->payload, pkt->payload_size);
    jb->buffer[idx].valid = 1;
    return 1;
}

static int jitter_buffer_pop(jitter_buffer *jb, jitter_entry *out) {
    if (jb->count == 0) {
        jb->buffering = 1;
        return 0;
    }

    if (jb->buffering) {
        if (jb->count >= 5) { // 100ms playout delay cushion for Wi-Fi stability
            jb->buffering = 0;
        } else {
            return 0;
        }
    }

    int best_idx = -1;
    uint32_t min_seq = 0;
    int found = 0;

    for (int i = 0; i < JITTER_BUFFER_SIZE; i++) {
        if (!jb->buffer[i].valid) continue;
        if (!found || (int32_t)(jb->buffer[i].seq_num - min_seq) < 0) {
            min_seq = jb->buffer[i].seq_num;
            best_idx = i;
            found = 1;
        }
    }

    if (!found) return 0;

    memcpy(out, &jb->buffer[best_idx], sizeof(jitter_entry));
    jb->buffer[best_idx].valid = 0;
    jb->count--;
    return 1;
}


static int16_t clamp_add(int16_t a, int16_t b) {
    int32_t sum = (int32_t)a + (int32_t)b;
    if (sum > 32767) return 32767;
    if (sum < -32768) return -32768;
    return (int16_t)sum;
}