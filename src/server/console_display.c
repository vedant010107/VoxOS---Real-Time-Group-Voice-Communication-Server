#include "server.h"

static pthread_t display_thread;

static void *display_thread_main(void *arg) {
    (void)arg;

    while (server_on) {
        // Clear the terminal (works on Linux/macOS)
        system("clear");

        printf("===== VOXOS SERVER STATUS =====\n");
        printf("Uptime: %lld ms | Port: %d | Clients: %d/%d\n",
               get_time_ms(), server_port, client_count, MAX_CLIENTS);

        printf("----------------------------------\n");
        printf("Rooms (%d active):\n", room_count);

        pthread_mutex_lock(&rooms_mutex);      // protect linked list traversal
        room *r = room_head;
        while (r != NULL) {
            printf("  [%d] %s - %d users, %d speakers%s\n",
                   r->room_id,
                   r->room_name,
                   r->participant_count,
                   r->active_speakers,
                   (strlen(r->password) > 0) ? " (locked)" : "");
            r = r->next;
        }
        pthread_mutex_unlock(&rooms_mutex);

        if (room_head == NULL) {
            printf("  (no active rooms)\n");
        }

        printf("----------------------------------\n");
        printf("Connected clients:\n");

        pthread_mutex_lock(&clients_mutex);    // protect clients array
        for (int i = 0; i < MAX_CLIENTS; i++) {
            client *c = clients[i];
            if (c != NULL && c->is_active) {
                printf("  [%d] %s (role:%d) room:%s\n",
                       i,
                       (c->user_id != -1) ? c->username : "(anon)",
                       c->role,
                       (c->current_room) ? c->current_room->room_name : "none");
            }
        }
        pthread_mutex_unlock(&clients_mutex);

        printf("----------------------------------\n");
        printf("Database: %s | WAL: %s | users: %s\n",
               (rooms_fd != -1) ? "open" : "closed",
               (wal_fd   != -1) ? "open" : "closed",
               (users_fd != -1) ? "open" : "closed");

        printf("Emergency FIFO: /tmp/voxos_emergency\n");
        printf("Press Ctrl+C to stop the server\n");

        fflush(stdout);
        sleep(1);
    }

    return NULL;
}

void console_display_init(void) {
    pthread_create(&display_thread, NULL, display_thread_main, NULL);
    log_message("INFO", "Console display started");
}