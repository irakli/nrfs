all: server client

server: raid_server.o vector.o
	gcc raid_server.o vector.o `pkg-config fuse --cflags --libs` -o server.out -lpthread

client: raid_client.o vector.o
	gcc raid_client.o vector.o `pkg-config fuse --cflags --libs` -o client.out

raid_server.o: raid_server.c raid.h
	gcc -Wall `pkg-config fuse --cflags --libs` -c raid_server.c

raid_client.o: raid_client.c raid.h
	gcc -Wall `pkg-config fuse --cflags --libs` -c raid_client.c

vector.o: vector.c vector.h
	gcc -c vector.c

clean:
	rm *.o server.out client.out

check_server:
	./server.out 127.0.0.1 5000 sfs/

check_client:
	./client.out mount/ -f