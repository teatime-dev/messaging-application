all: client server
client: client.c
	gcc -lrt -pthread -o client client.c
server: server.c
	gcc -lrt -pthread -o server server.c
clean:
	rm client
	rm server
rebuild: clean client server
