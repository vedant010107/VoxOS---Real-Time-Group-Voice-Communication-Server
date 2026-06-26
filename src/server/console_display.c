#include "server.h"

static pthread_t display_thread;

/* Clears the terminal via ANSI escape codes.
 * Wrapping the write() call satisfies -Wunused-result without hiding errors. */
static void ansi_clear(void)
{
    const char seq[] = "\033[2J\033[H";
    if(write(STDOUT_FILENO, seq, sizeof(seq) - 1) == -1)
    {
        /* Non-fatal: display corruption at worst */
    }
}

static void *display_thread_main(void *arg) {
    (void)arg;

    while (server_on) {
        // Clear the terminal — ANSI escape, no fork/exec overhead
        ansi_clear();

        printf("===== ECHOLINK SERVER STATUS =====\n");
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

        printf("Emergency FIFO: /tmp/echolink_emergency\n");
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