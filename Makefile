all: server client

server: raid_server.o vector.o
	gcc -w raid_server.o vector.o `pkg-config fuse --cflags --libs` -o server.out -lpthread -lcrypto

client: raid_client.o vector.o
	gcc -w raid_client.o vector.o `pkg-config fuse --cflags --libs` -o client.out -lcrypto

raid_server.o: raid_server.c raid.h
	gcc -w `pkg-config fuse --cflags --libs` -c raid_server.c

raid_client.o: raid_client.c raid.h
	gcc -w `pkg-config fuse --cflags --libs` -c raid_client.c

vector.o: vector.c vector.h
	gcc -w -c vector.c

clean:
	rm *.o server.out client.out

check_server:
	./server.out 127.0.0.1 5000 sfs/

check_client:
	./client.out mount/ -f