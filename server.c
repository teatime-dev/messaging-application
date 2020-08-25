#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/shm.h>
#include <sys/wait.h>
#include <unistd.h>
#include <errno.h>
#include <pthread.h>
#include <sys/signal.h>
#include <semaphore.h>
#include <sys/ipc.h>
#include <time.h>
#include "message.h"
#include <stdbool.h>
#define MAXCLIENTS 10
#define MAXMESSAGES 1001
#define MAX_MESSAGE_LENGTH 1024
#define DEFAULT_PORT 12345
//#include <signal.h>

/*****Variables*****/

int shmsz;
int shmid;
key_t key;
struct message *sharedmessages;
//sem_t *rw_mutex;
//sem_t *mutex;
//int *read_count;

struct shmstructure
{
	struct client clients[MAXCLIENTS];
	struct message messages[MAXMESSAGES];
	sem_t rw_mutex;
	sem_t mutex;
	int read_count;
	int current_message_index;
	int activeclients;
};
struct shmstructure *shm;
//struct client clients[MAXCLIENTS];
//struct client *shared_clients;

struct clientrequest currentRequest;
//struct message messages[MAXMESSAGES];
//struct message *shared_messages;
int forks[MAXCLIENTS];
int client_id;
//int currentMessageIndex = 0;
bool isFork = false;
bool usingMutex = false;
bool usingRWMutex = false;
bool usingread_count = false;
/*****Function headers*****/

int recieve_request(int socket_identifier, struct clientrequest *receivedRequest);
void cleanup();
void freeAll();
void setup();
void handleSIGINT(int num);
int send_message(char *message, enum responsetype type, int socket_id);
int send_response(int socket_id, struct serverresponse *response);
int add_client(int socket_id, int *client_out);
int remove_client(bool sendMessage, int clientID);
int handle_request(int clientID, struct clientrequest *request);
int add_message(char *messageToAdd, int channelID);
int sendChannelMessage(int clientID);
int sendNextMessage(int clientID);
int sendNextParamsMessage(int clientID, int channelID);
int startReading();
int startWriting();
int stopReading();
int stopWriting();
/*****Function bodies*****/

// Handle SIGINT (CTRL+C)

int main(int argc, char *argv[])
{
	shmsz = sizeof(struct message) * MAXMESSAGES + sizeof(sem_t) * 2 + sizeof(int) + sizeof(struct client) * MAXCLIENTS;
	key = 0x5678;
	if ((shmid = shmget(key, sizeof(struct shmstructure), IPC_CREAT | 0666)) < 0)
	{
		perror("shmget");
		exit(1);
	}
	sleep(1);

	if ((shm = shmat(shmid, NULL, 0)) == (char *)-1)
	{
		perror("shmat");
		exit(1);
	}
	//s = shm;
	// read_count = (int*)shm;
	// rw_mutex = (sem_t*)((char*)shm + sizeof(int));
	// mutex = (sem_t*)((char*)rw_mutex + sizeof(sem_t));
	// shared_clients = (struct client*)((char*)mutex + sizeof(sem_t));
	// shared_messages = (struct message*)((char*)shared_clients + sizeof(struct client)*MAXCLIENTS);
	setup();

	int listen_fd, client_fd;	  /* Listen using listen_fd, accept a client with client_fd */
	struct sockaddr_in my_addr;	/* my address information */
	struct sockaddr_in their_addr; /* connector's address information */

	socklen_t sin_size;
	int numbytes, i = 0, addstatus = 0;
	char buf[MAXDATASIZE];
	bool connected = false;
	char response[MAX_RESPONSE_LENGTH];
	// Signal Handling
	struct sigaction sigintAction;
	sigintAction.sa_handler = handleSIGINT;
	sigemptyset(&sigintAction.sa_mask);
	sigintAction.sa_flags = 0;
	sigaction(SIGINT, &sigintAction, NULL);

	/* Set up the structure to specify the new action. */

	/* Get port number for server to listen on */
	int defaultport;
	//TO DO
	if (argc != 2)
	{
		//	fprintf(stderr, "usage: port#\n");
		//	cleanup();
		//	exit(1);
		defaultport = DEFAULT_PORT;
	}
	else
	{
		defaultport = atoi(argv[1]);
	}
	/* generate the socket */
	if ((listen_fd = socket(AF_INET, SOCK_STREAM, 0)) == -1)
	{
		perror("socket");
		cleanup();
		exit(1);
	}

	/* generate the end point */
	my_addr.sin_family = AF_INET;			  /* host byte order */
	my_addr.sin_port = htons(defaultport);	/* short, network byte order */
	my_addr.sin_addr.s_addr = INADDR_ANY;	 /* auto-fill with my IP */
	/* bzero(&(my_addr.sin_zero), 8);   ZJL*/ /* zero the rest of the struct */

	/* bind the socket to the end point */
	if (bind(listen_fd, (struct sockaddr *)&my_addr, sizeof(struct sockaddr)) == -1)
	{
		perror("bind");
		cleanup();
		exit(1);
	}
	// From https://stackoverflow.com/questions/2876024/linux-is-there-a-read-or-recv-from-socket-with-timeout
	struct timeval tv;
	tv.tv_sec = 3;
	tv.tv_usec = 0;
	setsockopt(listen_fd, SOL_SOCKET, SO_RCVTIMEO, (const char *)&tv, sizeof tv);
	/* start listnening */
	if (listen(listen_fd, BACKLOG) == -1)
	{
		perror("listen");
		cleanup();
		exit(1);
	}

	printf("server starts listening ...\n");

	/* repeat: accept, send, close the connection */
	/* for every accepted connection, use a sepetate process or thread to serve it */
	while (1)
	{ /* main accept() loop */
		sin_size = sizeof(struct sockaddr_in);
		if ((client_fd = accept(listen_fd, (struct sockaddr *)&their_addr,
								&sin_size)) == -1)
		{
			//perror("accept");
			continue;
		}
		printf("server: got connection from %s\n",
			   inet_ntoa(their_addr.sin_addr));
		if (recieve_request(client_fd, &currentRequest) == -1)
		{
			printf("Failed to recieve hello from client. Continuing.\n");
			continue;
		}
		if (currentRequest.type != HELLO)
		{
			printf("Recieved type %d from unwelcome client\n", currentRequest.type);
			perror("unwelcome client request");
			continue;
		}
		addstatus = add_client(client_fd, &client_id);
		if (addstatus > 0)
		{
			printf("Successfully added client\n");
			connected = true;
			forks[client_id] = fork();
		}
		else if (addstatus == -1)
		{
			printf("Failed to add client due to error in array.\n");
		}
		else if (addstatus == -2)
		{
			printf("Failed to add client due to no space in array.\n");
		}
		if (connected == true)
		{
			if (forks[client_id] == 0)
			{
				isFork = true;
				// This is the forked process.
			}
			else
			{
				connected = false;
			}
		}
		while (connected)
		{
			bzero((void *)response, sizeof(char) * MAX_RESPONSE_LENGTH);
			if (recieve_request(client_fd, &currentRequest) == -1)
			{
				printf("Failed to recieve response from client. Removing client.\n");
				remove_client(false, client_id);
				connected = false;
				continue;
			}
			sprintf(response, "server got type:%i arg1:%i arg2Length:%i arg2:%s|END\n", currentRequest.type, currentRequest.arg1, currentRequest.arg2Length, currentRequest.arg2);
			//printf("%s",response);
			handle_request(client_id, &currentRequest);
			sprintf(response, "message to client. I got type:%i arg1:%i arg2Length:%i arg2:%s", currentRequest.type, currentRequest.arg1, currentRequest.arg2Length, currentRequest.arg2);
			if (currentRequest.type == BYE)
			{
				remove_client(true, client_id);
				connected = false;
			}
			else
			{
				//send_message(testMessage,MESSAGE,client_fd);
			}
		}
		if (isFork == true)
		{
			break;
		}
	}
	if (isFork == true)
	{
		close(listen_fd);
	}
	cleanup();

	exit(0);
}
// Cleans up the shared memory before exiting
void cleanup()
{
	if (shmdt(shm) == -1)
	{
		perror("shmdt");
	}
	if (isFork == false)
	{

		wait(NULL);
		if (shmctl(shmid, IPC_RMID, NULL) == -1)
		{
			perror("shmctl");
			exit(1);
		}
	}
	freeAll();
}

// Originally used to free any pointers, but none used in final project
void freeAll()
{
	// free(currentRecieve.arg2);
	// free(currentSend.arg2);
	// currentRecieve.arg2 = NULL;
	// currentSend.arg2 = NULL;
}

// Adds a message to the message structure
int add_message(char *messageToAdd, int channelID)
{
	startWriting();
	shm->messages[shm->current_message_index].channelID = channelID;
	strcpy(shm->messages[shm->current_message_index].message, messageToAdd);
	shm->messages[shm->current_message_index].messageLength = strlen(shm->messages[shm->current_message_index].message);
	shm->messages[shm->current_message_index].time = shm->current_message_index;
	shm->current_message_index++;
	if (shm->current_message_index >= MAXMESSAGES)
	{
		for(int i = 0; i < 10;i++) {
			printf("\n______________________________");
			for(int i = 0; i < 5; i++) {
				printf("!!!!!!!Max message count reached!!!!!!!");
			}
			printf("____________________________\n");
		}
		
		perror("max count limit reached");
		stopWriting();
		cleanup();
		exit(-1);
	}
	stopWriting();
	return 0;
};
// Handles a client request i.e. next would send the next message, send would store the sent message.
int handle_request(int clientID, struct clientrequest *request)
{
	startReading();
	int socket_id = shm->clients[clientID].socket_id;
	stopReading();
	char currentMessage[MAX_RESPONSE_LENGTH];
	bzero((void *)currentMessage, MAX_RESPONSE_LENGTH);
	switch (request->type)
	{
	case SUB:
		startReading();
		if (shm->clients[clientID].subscribed[request->arg1] == true)
		{
			sprintf(currentMessage, "Already subscribed to channel %i.", request->arg1);
		}
		else
		{
			shm->clients[clientID].subscribed[request->arg1] = true;
			sprintf(currentMessage, "Subscribed to channel %i.", request->arg1);
		}
		stopReading();
		send_message(currentMessage, MESSAGE, socket_id);
		return 0;
		break;
	case UNSUB:
		startReading();
		if (shm->clients[clientID].subscribed[request->arg1] == false)
		{
			sprintf(currentMessage, "Not subscribed to channel %i.", request->arg1);
		}
		else
		{
			shm->clients[clientID].subscribed[request->arg1] = false;
			sprintf(currentMessage, "Unsubscribed from channel %i.", request->arg1);
		}
		stopReading();
		send_message(currentMessage, MESSAGE, socket_id);
		return 0;
		break;
	case SEND:
		add_message(request->arg2, request->arg1);
		send_message(" ", EMPTY, socket_id);
		break;
	case CHANNELS:
		sendChannelMessage(clientID);
		break;
	case NEXT:
		sendNextMessage(clientID);
		break;
	case NEXTPARAMS:
		sendNextParamsMessage(clientID, request->arg1);
	case UNKNOWN:
		return -2;
		break;
	}
	return -1;
};
// Sends an unread message from a specific channel to a specific client
int sendNextParamsMessage(int clientID, int channelID)
{
	//TODO
	startReading();
	int socket_id = shm->clients[clientID].socket_id;
	char messageToSend[MAX_RESPONSE_LENGTH];
	if (shm->clients[clientID].subscribed[channelID] == false)
	{
		sprintf(messageToSend, "Not subscribed to channel %i", channelID);
		stopReading();
		send_message(messageToSend, ERROR, socket_id);
		return 2;
	}
	for (int currentMessage = 0; currentMessage < shm->current_message_index; currentMessage++)
	{
		if (shm->messages[currentMessage].channelID == channelID && shm->clients[clientID].currenttime[shm->messages[currentMessage].channelID] < shm->messages[currentMessage].time)
		{
			// This is the next message to send
			stopReading();
			startWriting();
			shm->clients[clientID].currenttime[shm->messages[currentMessage].channelID] = shm->messages[currentMessage].time;
			sprintf(messageToSend, "%s", shm->messages[currentMessage].message);
			stopWriting();
			send_message(messageToSend, MESSAGE, socket_id);
			return 0;
		}
	}
	stopReading();
	send_message(" ", EMPTY, socket_id);
	return 1;
};

// Sends the first unread message from any channel to a particular client
int sendNextMessage(int clientID)
{
	char messageToSend[MAX_RESPONSE_LENGTH];
	int subscribedChannels = 0;
	startReading();
	for (int currentChannel = 0; currentChannel < 256; currentChannel++)
	{
		if (shm->clients[clientID].subscribed[currentChannel] == true)
		{
			subscribedChannels++;
		}
	}
	if (subscribedChannels == 0)
	{
		send_message("Not subscribed to any channels.", ERROR, shm->clients[clientID].socket_id);
		stopReading();
		return 2;
	}
	for (int currentMessage = 0; currentMessage < shm->current_message_index; currentMessage++)
	{
		if (shm->clients[clientID].subscribed[shm->messages[currentMessage].channelID] == true && shm->clients[clientID].currenttime[shm->messages[currentMessage].channelID] < shm->messages[currentMessage].time)
		{
			// This is the next message to send
			stopReading();
			startWriting();
			shm->clients[clientID].currenttime[shm->messages[currentMessage].channelID] = shm->messages[currentMessage].time;
			sprintf(messageToSend, "%i:%s", shm->messages[currentMessage].channelID, shm->messages[currentMessage].message);
			int socket_id = shm->clients[clientID].socket_id;
			stopWriting();
			send_message(messageToSend, MESSAGE, socket_id);
			return 0;
		}
	}
	send_message(" ", EMPTY, shm->clients[clientID].socket_id);
	stopReading();
	return 1;
};
// Sends the message showing subscribed channels, unread read and total messages, etc. to a specific client
int sendChannelMessage(int clientID)
{
	char response[MAX_RESPONSE_LENGTH];
	bzero(response, sizeof(char) * MAX_RESPONSE_LENGTH);
	char tempMessage[100];
	startReading();
	for (int channelIndex = 0; channelIndex < 256; channelIndex++)
	{
		if (shm->clients[clientID].subscribed[channelIndex] == true)
		{
			int countTotal = 0;
			int countRead = 0;
			int countUnread = 0;
			for (int messageIndex = 0; messageIndex < shm->current_message_index; messageIndex++)
			{
				if (shm->messages[messageIndex].channelID == channelIndex)
				{
					countTotal++;
					if (shm->messages[messageIndex].time > shm->clients[clientID].currenttime[channelIndex])
					{
						countUnread++;
					}
					else
					{
						countRead++;
					}
				}
			}
			sprintf(tempMessage, "Channel:%i\tTotal:%i\tUnread:%i\tRead:%i\n", channelIndex, countTotal, countUnread, countRead);
			strcat(response, tempMessage);
		}
	}
	int socket_id = shm->clients[clientID].socket_id;
	stopReading();
	if (strlen(response) <= 0)
	{
		send_message(" ", EMPTY, socket_id);
	}
	else
	{
		response[strlen(response)] = '\0';
		send_message(response, MESSAGE, socket_id);
	}
}
//returns 0 successful
//returns -1 if lost connection
// Recieves a request from a specific socket and stores it in a struct
int recieve_request(int socket_identifier, struct clientrequest *receivedRequest)
{
	//	struct clientrequest recievedRequest;
	int result = 0;
	uint32_t network_arg2length;
	// recieve arg1
	result += recieve_data(socket_identifier, (void *)&(receivedRequest->type), sizeof(uint8_t));
	//	printf("Message type recieved is:%i\n",receivedRequest->type);
	result += recieve_data(socket_identifier, (void *)&(receivedRequest->arg1), sizeof(uint8_t));
	//	printf("Message arg1 recieved is:%i\n",receivedRequest->arg1);
	result += recieve_data(socket_identifier, (void *)&network_arg2length, sizeof(uint32_t));
	receivedRequest->arg2Length = ntohl(network_arg2length);
	//	printf("Message arg2 length recieved is:%i\n",receivedRequest->arg2Length);
	bzero((void *)receivedRequest->arg2, MAX_MESSAGE_LENGTH);
	//free(receivedRequest->arg2);
	//receivedRequest->arg2 = calloc(receivedRequest->arg2Length,sizeof(char));
	result += recieve_data(socket_identifier, (void *)(receivedRequest->arg2), receivedRequest->arg2Length);
	//	printf("Arg2 recieved is:%s\n",receivedRequest->arg2);
	//printf("fork %i Recieved: Type%i arg1:%i arg2length:%i arg2:%s|MESSAGE ENDS\n",forks[client_id],receivedRequest->type,receivedRequest->arg1,receivedRequest->arg2Length,receivedRequest->arg2);
	if (result < 0)
	{
		return -1;
	}
	return 0;
}

// Sends a response to a specific socket based on the provided struct
int send_response(int socket_id, struct serverresponse *response)
{
	int result = 0;
	//int i=0;
	//uint32_t network_byte_order_long = 0;
	uint8_t network_responsetype = response->type;
	uint8_t network_arg1 = response->arg1;
	uint32_t network_arg2Length = htonl(response->arg2Length);
	result += send_data(socket_id, &network_responsetype, sizeof(uint8_t));
	result += send_data(socket_id, &network_arg1, sizeof(uint8_t));
	result += send_data(socket_id, &network_arg2Length, sizeof(uint32_t));
	result += send_data(socket_id, (void *)response->arg2, response->arg2Length);
	if (result < 0)
	{
		return -1;
	}
	//printf("fork %i sent: Type%i arg1:%i arg2length:%i arg2:%s|MESSAGE ENDS\n",forks[client_id],response->type,response->arg1,response->arg2Length,response->arg2);
	fflush(stdout);
	return 0;
}

// Handles when a SIGINT is recieved
void handleSIGINT(int num)
{
	if (isFork == true)
	{
		//printf("Fork %i Recieved SIGINT, exiting...\n",forks[client_id]);
	}
	else
	{
		printf("Received SIGINT, exiting...\n");
	}
	if (usingRWMutex == true)
	{
		printf("still using rwmutex?\n");
		sem_post(&shm->rw_mutex);
	}
	if (usingMutex == true)
	{
		printf("still using mutex?\n");
		sem_post(&shm->mutex);
	}
	if (usingread_count == true)
	{
		printf("still using read_count?\n");
		shm->read_count--;
	}
	cleanup();
	exit(0);
}
// Sets up and clear the variables
void setup()
{
	sem_init(&shm->rw_mutex, 1, 1);
	sem_init(&shm->mutex, 1, 1);
	startWriting();
	shm->current_message_index = 1;
	shm->activeclients = 0;
	for (int i = 0; i < MAXCLIENTS; i++)
	{
		shm->clients[i].clientID = 0;
		shm->clients[i].socket_id = 0;
		shm->clients[i].active = false;
		for (int ii = 0; ii < 256; ii++)
		{
			shm->clients[i].subscribed[ii] = false;
			shm->clients[i].currenttime[ii] = shm->current_message_index - 1;
		}
	}
	stopWriting();
}

// Adds a client to the list and sets the client out pointer to the client's ID
int add_client(int socket_id, int *client_out)
{
	startWriting();
	if (shm->activeclients == MAXCLIENTS)
	{
		perror("Max client number hit.");
		char nospace_msg[100];
		sprintf(nospace_msg, "Sorry, the server has no space to accept your request. Goodbye.");
		send_message(nospace_msg, FAREWELL, socket_id);
		stopWriting();
		return -2;
	}
	for (int i = 0; i < MAXCLIENTS; i++)
	{
		if (shm->clients[i].active == false)
		{
			shm->clients[i].socket_id = socket_id;
			shm->clients[i].clientID = i;
			for (int ii = 0; ii < 256; ii++)
			{
				shm->clients[i].subscribed[ii] = false;
				shm->clients[i].currenttime[ii] = shm->current_message_index - 1;
			}
			shm->clients[i].active = true;
			char welcome_msg[100];
			sprintf(welcome_msg, "Welcome! Your client ID is %i", shm->clients[i].clientID);
			send_message(welcome_msg, WELCOME, socket_id);
			shm->activeclients++;
			*client_out = i;
			stopWriting();
			return 1;
		}
	}
	stopWriting();
	printf("not max clients yet couldn't find a space in the client array?\n");
	perror("not max clients yet couldn't find a space in the client array?");
	return -1;
}

// Removes a client from the list, sending them a farewell message to get them to disconnect
int remove_client(bool sendMessage, int clientID)
{
	startWriting();
	if (shm->clients[clientID].active == false)
	{
		perror("tried to remove inactive client?");
		return -1;
	}
	shm->clients[clientID].active = false;
	shm->activeclients--;
	if (sendMessage == true)
	{
		char goodbye_msg[100];
		sprintf(goodbye_msg, "Goodbye.");
		send_message(goodbye_msg, FAREWELL, shm->clients[clientID].socket_id);
	}
	close(shm->clients[clientID].socket_id);
	stopWriting();
}

// Sends a message struct as defined in the parameters to a specified socket
int send_message(char *message, enum responsetype type, int socket_id)
{
	struct serverresponse response;
	response.arg1 = 0;
	strcpy(response.arg2, message);
	//response.arg2 = message;
	response.arg2Length = strlen(message);
	response.type = type;
	if (send_response(socket_id, &response) < 0)
	{
		perror("sent client response error");
		return -1;
	}
	return 0;
}
// rw_mutex;
// 	mutex;
// 	read_count;
// Mutex functions below, based on reader/writer problem
int startReading()
{
	sem_wait(&shm->mutex);
	usingMutex = true;
	shm->read_count++;
	if (shm->read_count == 1)
	{
		sem_wait(&shm->rw_mutex);
	}
	sem_post(&shm->mutex);
	usingMutex = false;
}
int startWriting()
{
	sem_wait(&shm->rw_mutex);
	usingRWMutex = true;
}
int stopReading()
{
	sem_wait(&shm->mutex);
	usingMutex = true;
	shm->read_count--;
	if (shm->read_count == 0)
	{
		sem_post(&shm->rw_mutex);
	}
	sem_post(&shm->mutex);
	usingMutex = false;
}
int stopWriting()
{
	sem_post(&shm->rw_mutex);
	usingRWMutex = false;
}