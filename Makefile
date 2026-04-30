CC = gcc
CFLAGS = -Wall -Wextra -g -pthread -Isrc/common -Isrc/server
LDFLAGS = -pthread -lrt

SERVER_SRCS = src/server/main.c \
              src/server/network.c \
              src/server/client_handler.c \
              src/server/auth.c \
              src/server/room_manager.c \
              src/server/file_db.c \
              src/server/audio_mixer.c \
              src/server/ipc_manager.c \
              src/server/console_display.c \
              src/common/utils.c

CLIENT_SRCS = src/client/client.c \
              src/common/utils.c

SERVER_OBJS = $(SERVER_SRCS:.c=.o)
CLIENT_OBJS = $(CLIENT_SRCS:.c=.o)

SERVER_BIN = bin/voxos_server
CLIENT_BIN = bin/voxos_client

.PHONY: all clean dirs

all: dirs $(SERVER_BIN) $(CLIENT_BIN)

dirs:
	mkdir -p bin data obj

$(SERVER_BIN): $(SERVER_OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

$(CLIENT_BIN): $(CLIENT_OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f src/server/*.o src/client/*.o src/common/*.o
	rm -f $(SERVER_BIN) $(CLIENT_BIN)
	rm -f data/*.bin data/*.log