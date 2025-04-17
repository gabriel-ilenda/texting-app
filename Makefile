CC = gcc
CFLAGS = -Wall

all: server client

server:
	$(CC) $(CFLAGS) -g -pthread -o server \
		src/server/server.c src/server/database.c \
		-lmysqlclient -lssl -lcrypto

client:
	$(CC) $(CFLAGS) -g -o client src/client/client.c

clean:
	rm -f server client
