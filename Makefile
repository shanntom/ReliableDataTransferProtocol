default: server client

server: server.c packet.h
	gcc -g server.c -o server -lrt

client: client.c packet.h
	gcc -g -std=c99 client.c -o client -lrt

clean: 
	rm client server