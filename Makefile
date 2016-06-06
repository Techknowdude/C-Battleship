CC=gcc
CFLAGS=-Wall -D_POSIX_C_SOURCE -g -O0 -Wall
CLIENT_OBJ_FILES = client.o
SERVER_OBJ_FILES = server.o list.o
LIBS = -lpthread sharedInfo.h

all: client/client server/server
client/client: $(CLIENT_OBJ_FILES)
server/server: $(SERVER_OBJ_FILES)

client/client server/server:
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $^ $(LIBS) -o $@

%.o: %.c
	$(CC) -c $< -g

.PHONY: clean
clean:
	rm  -f client/client server/server *.o *~
