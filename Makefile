CC = gcc
CFLAGS = -Wall -Wextra -g

SERVER_SOURCES = \
    src/server/serverMain.c \
    src/server/networkServer.c \
    src/server/protocol.c \
    src/server/session.c \
    src/server/fsOps.c \
    src/server/utils.c \
    src/server/concurrency.c \
    src/server/serverCommands.c

CLIENT_SOURCES = \
    src/client/clientMain.c \
    src/client/clientCommands.c \
    src/client/networkClient.c \
    src/client/protocol.c \
    src/server/utils.c

SERVER_OBJECTS = $(SERVER_SOURCES:.c=.o)
CLIENT_OBJECTS = $(CLIENT_SOURCES:.c=.o)

all: server client

server: $(SERVER_OBJECTS)
	$(CC) $(CFLAGS) -o server $(SERVER_OBJECTS)

client: $(CLIENT_OBJECTS)
	$(CC) $(CFLAGS) -o client $(CLIENT_OBJECTS)

clean:
	rm -f server client $(SERVER_OBJECTS) $(CLIENT_OBJECTS)