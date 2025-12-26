# Makefile za File Server projekt

CC = gcc
CFLAGS = -Wall -Wextra -g
INCLUDES = -I./include

# ============================================
# SERVER
# ============================================
SERVER_SRC_DIR = src/server
SERVER_SRCS = $(SERVER_SRC_DIR)/serverMain.c \
              $(SERVER_SRC_DIR)/networkServer.c \
              $(SERVER_SRC_DIR)/protocol.c \
              $(SERVER_SRC_DIR)/session.c \
              $(SERVER_SRC_DIR)/fsOps.c \
              $(SERVER_SRC_DIR)/utils.c \
              $(SERVER_SRC_DIR)/serverCommands.c

SERVER_OBJS = $(SERVER_SRCS:.c=.o)

# ============================================
# CLIENT
# ============================================
CLIENT_SRC_DIR = src/client
CLIENT_SRCS = $(CLIENT_SRC_DIR)/clientMain.c \
              $(CLIENT_SRC_DIR)/clientCommands.c \
              $(CLIENT_SRC_DIR)/networkClient.c \
              $(CLIENT_SRC_DIR)/protocol.c

CLIENT_OBJS = $(CLIENT_SRCS:.c=.o)

# ============================================
# SHARED UTILS (koristi server/utils.c)
# ============================================
UTILS_OBJ = src/server/utils.o

# ============================================
# TARGETS
# ============================================
all: server client

server: $(SERVER_OBJS)
	$(CC) $(CFLAGS) -o $@ $(SERVER_OBJS)

client: $(CLIENT_OBJS) $(UTILS_OBJ)
	$(CC) $(CFLAGS) -o $@ $(CLIENT_OBJS) $(UTILS_OBJ)

# ============================================
# PATTERN RULES
# ============================================
%.o: %.c
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

# ============================================
# CLEAN
# ============================================
clean:
	rm -f server client \
	      $(SERVER_OBJS) $(CLIENT_OBJS) $(UTILS_OBJ)

.PHONY: all clean