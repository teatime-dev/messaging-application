#include <stdio.h>
#include <stdbool.h>
#include <ctype.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <netdb.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/signal.h>
#include <unistd.h>
#include "message.h"
#include <time.h>
#include <pthread.h>
#include <semaphore.h>
#define INPUTLENGTH 1200
#define MAXCOMMANDLENGTH 100
#define MESSAGELENGTH 1024
#define MAXTHREADS 20
#define MAXTRIES 100
/* --------------------------------------Function headers-------------------------------------- */
void setupSIGINT();
int receive_response(int socket_identifier, struct serverresponse *response);
int send_request(int socket_id, struct clientrequest *request);
int doHandshake();
void cleanup();
void freeAll();
void handleSIGINT(int num);
int getCommandNum(char *command);
void *livefeed_next_handle();

/* --------------------------------------Global Variables-------------------------------------- */
bool isConnected = false;
struct clientrequest currentSend;
struct serverresponse currentRecieve;
int sockfd;
// If false, thread will stop asap
//bool threadactive = false;
pthread_mutex_t sendrecieve_mutex;
// If true, thread will pause once message is finished
//bool livefeedparamsactive = false;
//int livefeedChannel = 0;

struct livefeed_next_data
{
	pthread_t p_thread;
	bool isActive;
	bool isLivefeed;
	bool isParams;
	bool finished;
	bool joined;
	int channelID;
};

// Delay for livefeed so it doesn't fully clog the application.
struct timespec time1, time2;

struct livefeed_next_data data_to_use[MAXTHREADS];
/* --------------------------------------Function Bodies-------------------------------------- */
// Handles a SIGINT
void handleSIGINT(int num)
{
	// If SIGINT is recieved while doing livefeed.
	printf("\nStopping livefeed/next...\n");
	for (int i = 0; i < MAXTHREADS; i++)
	{
		if (data_to_use[i].isActive)
		{
			data_to_use[i].isActive = false;
			pthread_join(data_to_use[i].p_thread, NULL);
		}
	}
	if (isConnected == true)
	{
		printf("\nRecieved SIGINT, attempting to disconnect...\n");
		isConnected = false;
		currentSend.type = BYE;
		//send_request(sockfd,&currentSend);
		//receive_response(sockfd,&currentRecieve);
		//printf("%s\n",currentRecieve.arg2);
		freeAll();
		close(sockfd);
	}
	else
	{
		printf("\nReceived SIGINT, exiting...\n");
		freeAll();
		close(sockfd);
		exit(0);
	}
}

int main(int argc, char *argv[])
{
	/*-------------Variable Setup-------------*/
	// User Input Variables
	char userinput[INPUTLENGTH];
	char command[MAXCOMMANDLENGTH];
	int optionalParameter;
	char usermessage[1024];
	int currentcommand;
	bool reqParam1 = false;
	bool reqParam2 = false;
	bool param1Found = false;
	int tempArgument = 0;

	//threading setup
	pthread_mutex_init(&sendrecieve_mutex, NULL);
	for (int i = 0; i < MAXTHREADS; i++)
	{
		data_to_use[i].finished = true;
		data_to_use[i].isActive = false;
		data_to_use[i].joined = true;
	}
	// Network Variables
	struct hostent *he;
	struct sockaddr_in their_addr; /* connector's address information */

	time1.tv_nsec = 50000000L; //50ms
	time1.tv_sec = 0;
	// Socket timeout variables
	struct timeval tv;
	tv.tv_sec = 3;
	tv.tv_usec = 0;

	setupSIGINT();

	/*-------------Network setup-------------*/

	// Get IP and Port
	if (argc != 3)
	{
		fprintf(stderr, "usage: server_IP_address port#\n");
		cleanup();
		exit(1);
	}
	if ((he = gethostbyname(argv[1])) == NULL)
	{ /* get the host info */
		herror("gethostbyname");
		cleanup();
		exit(1);
	}
	if ((sockfd = socket(AF_INET, SOCK_STREAM, 0)) == -1)
	{
		perror("socket");
		cleanup();
		exit(1);
	}
	their_addr.sin_family = AF_INET;			/* host byte order */
	their_addr.sin_port = htons(atoi(argv[2])); /* short, network byte order */
	their_addr.sin_addr = *((struct in_addr *)he->h_addr);
	bzero(&(their_addr.sin_zero), 8); /* zero the rest of the struct */
	printf("Connecting...\n");

	// Open Socket and connect to server socket
	if (connect(sockfd, (struct sockaddr *)&their_addr,
				sizeof(struct sockaddr)) == -1)
	{
		printf("Failed to connect.\n");
		perror("connect");
		cleanup();
		exit(1);
	}

	// Setup socket timeout
	setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, (const char *)&tv, sizeof tv);

	// Handshake with server
	printf("Connected. Waiting for server...\n");
	isConnected = true;
	if (doHandshake() == -1)
	{
		printf("Lost connection to server.\n");
		isConnected = false;
		cleanup();
		exit(1);
	}

	// Main Loop
	while (isConnected)
	{
		// if (livefeedactive == true)
		// {
		// 	if (nanosleep(&time1, &time2) < 0)
		// 	{
		// 		//printf("Failed to sleep");
		// 	}
		// }
		// RESET VARIABLES
		bzero((void *)userinput, INPUTLENGTH);
		bzero((void *)usermessage, MESSAGELENGTH);
		bzero((void *)command, MAXCOMMANDLENGTH);
		param1Found = false;
		reqParam1 = false;
		reqParam2 = false;
		currentcommand = UNKNOWN;
		tempArgument = 0;

		currentRecieve.type = 0;
		currentRecieve.arg1 = 0;
		currentRecieve.arg2Length = 0;
		bzero((void *)currentRecieve.arg2, MAX_RESPONSE_LENGTH);

		currentSend.type = UNKNOWN;
		currentSend.arg1 = 0;
		currentSend.arg2Length = 0;
		bzero((void *)currentSend.arg2, MESSAGELENGTH);

		//GET USER INPUT
		// if (livefeedactive == false)
		// {

		// }
		//printf("Enter a command:");
		fgets(userinput, INPUTLENGTH, stdin);
		// else
		// {
		// 	if (livefeedparamsactive == true)
		// 	{
		// 		sprintf(userinput, "NEXT %i\n", livefeedChannel);
		// 	}
		// 	else
		// 	{
		// 		sprintf(userinput, "NEXT\n");
		// 	}
		// }
		if (strlen(userinput) > 0)
		{
			if (userinput[strlen(userinput) - 1] == '\n')
			{
				userinput[strlen(userinput) - 1] = '\0';
			}
		}
		//DECODE USER INPUT
		if (sscanf(userinput, "%s", command) <= 0)
		{
			//empty
		};
		if (sscanf(userinput, "%*s %i", &tempArgument) == 1)
		{
			param1Found = true;
		}
		for (int i = 0; i < strlen(command); i++)
		{
			command[i] = toupper((unsigned char)command[i]);
		}
		currentcommand = getCommandNum(command);

		// Set flags based on command
		switch (currentcommand)
		{
		case SUB:
			//printf("---Subscribe---\n");
			currentSend.type = SUB;
			reqParam1 = true;
			break;
		case CHANNELS:
			//printf("---Channels---\n");
			currentSend.type = CHANNELS;
			break;
		case UNSUB:
			//printf("---Unsubscribe---\n");
			currentSend.type = UNSUB;
			reqParam1 = true;
			break;
		case NEXT:
			if (param1Found)
			{
				//printf("---Next Params---\n");
				currentSend.type = NEXTPARAMS;
				reqParam1 = true;
			}
			else
			{
				//printf("---Next---\n");
				currentSend.type = NEXT;
			}

			break;
		case LIVEFEED:
			//livefeedactive = true;
			if (param1Found)
			{
				//livefeedparamsactive = true;
				//printf("---Livefeed Params---\n");
				currentSend.type = LIVEFEEDPARAMS;
				reqParam1 = true;
				break;
			}
			else
			{
				//printf("---Livefeed---\n");
				currentSend.type = LIVEFEED;
				break;
			}

		case SEND:
			//printf("---Send---\n");
			currentSend.type = SEND;
			reqParam1 = true;
			reqParam2 = true;
			break;
		case BYE:
			//printf("---Bye---\n");
			currentSend.type = BYE;
			break;
		case STOP:
			currentSend.type = STOP;
			break;
		default:
			currentcommand = UNKNOWN;
			printf("---Unknown command---\n");
			break;
		}

		// ACT ON COMMAND
		if (currentcommand != UNKNOWN)
		{
			if (reqParam1 == true)
			{
				char *userstring;
				int startofstring = 0;
				if (sscanf(userinput, "%*s %i %n", &tempArgument, &startofstring) != 1)
				{
					printf("Incorrect command\n");
					continue;
				}
				if (tempArgument < 0 || tempArgument > 255)
				{
					printf("Incorrect command. Value should be between 0 and 255\n");
					continue;
				}
				currentSend.arg1 = tempArgument;
				// if (currentcommand == LIVEFEEDPARAMS)
				// {
				// 	livefeedChannel = tempArgument;
				// }
				//printf("First parameter is %i\n",currentSend.arg1);
				if (reqParam2 == true)
				{
					userstring = userinput + startofstring;
					if (strlen(userstring) > 1024)
					{
						printf("Sorry, your message is too long. Please split it up into smaller messages.\n");
						continue;
					}
					strcpy(currentSend.arg2, userstring);
					currentSend.arg2Length = strlen(currentSend.arg2);
					//printf("MESSAGE TO SEND |%s|\n",currentSend.arg2);
				}
			}
			if (currentSend.type == LIVEFEED ||
				currentSend.type == LIVEFEEDPARAMS ||
				currentSend.type == NEXT ||
				currentSend.type == NEXTPARAMS)
			{
				bool foundThread = false;
				int tries = 0;
				int firstAvailableThread = -1;
				//printf("Starting find thread");
				while(foundThread == false) {
					nanosleep(&time1, &time2);
					//printf("Ran find thread loop once.tries = %i\n",tries);
					
					for (int i = 0; i < MAXTHREADS; i++)
					{
						if (data_to_use[i].finished == true)
						{
							firstAvailableThread = i;
							foundThread = true;
							break;
							
						}
					}
					tries++;
					if(tries >= MAXTRIES) {
						foundThread = true;
						//printf("failed to find thread\n");
					}
				}
				//printf("found thread or failed");
				if (firstAvailableThread < 0)
				{
					printf("Not enough threads to handle your request. Please use the STOP command to free up some threads.\n");
				}
				else
				{
					// if the thread was hasn't been joined do so
					if(data_to_use[firstAvailableThread].joined == false) {
						pthread_join(data_to_use[firstAvailableThread].p_thread,NULL);
					}

					data_to_use[firstAvailableThread].finished = false;
					data_to_use[firstAvailableThread].isActive = true;
					data_to_use[firstAvailableThread].joined = false;
					if (currentSend.type == LIVEFEED || currentSend.type == LIVEFEEDPARAMS)
					{
						data_to_use[firstAvailableThread].isLivefeed = true;
					}
					else
					{
						data_to_use[firstAvailableThread].isLivefeed = false;
					}
					if (currentSend.type == LIVEFEEDPARAMS || currentSend.type == NEXTPARAMS)
					{
						data_to_use[firstAvailableThread].isParams = true;
						data_to_use[firstAvailableThread].channelID = currentSend.arg1;
					}
					else
					{
						data_to_use[firstAvailableThread].isParams = false;
					}
					pthread_create(&data_to_use[firstAvailableThread].p_thread, NULL, livefeed_next_handle, (void*)&data_to_use[firstAvailableThread]);
				}
			}
			// Is not livefeed or next
			else
			{
				if (currentSend.type == STOP)
				{
					printf("\nStopping livefeed/next...\n");
					for (int i = 0; i < MAXTHREADS; i++)
					{
						if (data_to_use[i].isActive)
						{
							data_to_use[i].isActive = false;
							pthread_join(data_to_use[i].p_thread, NULL);
							data_to_use[i].joined = true;
						}
					}
				}
				else
				{
					//printf("Main locking mutex\n");
					pthread_mutex_lock(&sendrecieve_mutex);
					if (send_request(sockfd, &currentSend) == -1)
					{
						printf("Failed to contact server. Disconnecting...\n");
						isConnected = false;
						//printf("Main unlocking mutex\n");
						pthread_mutex_unlock(&sendrecieve_mutex);
						continue;
					}
					if (receive_response(sockfd, &currentRecieve) == -1)
					{
						printf("Failed to recieve response from server. Disconnecting...\n");
						isConnected = false;
						//printf("Main unlocking mutex\n");
						pthread_mutex_unlock(&sendrecieve_mutex);
						continue;
					}
					//printf("Main unlocking mutex\n");
					pthread_mutex_unlock(&sendrecieve_mutex);
					if (currentRecieve.type == EMPTY)
					{
						//do nothing
					}
					else
					{
						printf("%s\n", currentRecieve.arg2);
					}
					if (currentRecieve.type == ERROR)
					{
						// livefeedactive = false;
						// livefeedparamsactive = false;
					}

					if (currentRecieve.type == FAREWELL)
					{
						isConnected = false;
					}
				}
			}
			// Send and recieve message
		}
		//receive_response(sockfd,&currentRecieve);
		//printf("looped once\n");
		//isConnected = false;
	}

	/* Receive message back from server */
	cleanup();
	close(sockfd);

	return 0;
}

// Binds the handleSIGINT function to the SIGINT signal
void setupSIGINT()
{
	struct sigaction sigintAction;
	sigintAction.sa_handler = handleSIGINT;
	sigemptyset(&sigintAction.sa_mask);
	sigintAction.sa_flags = 0;
	sigaction(SIGINT, &sigintAction, NULL);
}

// Joins all threads before program exits
void cleanup()
{
	freeAll();
	for (int i = 0; i < MAXTHREADS; i++)
	{
		if (data_to_use[i].joined == false)
		{
			data_to_use[i].isActive = false;
			pthread_join(data_to_use[i].p_thread, NULL);
		}
	}
}

// Frees all pointers. in this version of the program there are no pointers to be freed, however.
void freeAll()
{
	// free(currentRecieve.arg2);
	// free(currentSend.arg2);
	// currentRecieve.arg2 = NULL;
	// currentSend.arg2 = NULL;
}

// Sends and recieves a handshake from the server
int doHandshake()
{
	int status = 0;
	currentSend.type = HELLO;
	currentSend.arg1 = 0;
	bzero((void *)currentRecieve.arg2, 1024);
	currentSend.arg2Length = 0;
	status += send_request(sockfd, &currentSend);
	status += receive_response(sockfd, &currentRecieve);
	if (status < 0)
	{
		return -1;
	}
	if (currentRecieve.type != WELCOME)
	{
		if (currentRecieve.type == FAREWELL)
		{
			printf("%s\n", currentRecieve.arg2);
			isConnected = false;
			return 0;
		}
		perror("Recieved incorrect response");
	}
	printf("%s", currentRecieve.arg2);
	printf("\n");
	return 0;
}
/*
struct clientrequest {
    enum requesttype type;
    uint8_t arg1;
    uint32_t arg2Length;
    char* arg2;
};
struct serverresponse {
    enum responsetype type;
    uint8_t status;
    uint32_t messageLength;
    char* message;
};
*/

// Sends a client request to the server
int send_request(int socket_id, struct clientrequest *request)
{
	int status = 0;
	uint8_t network_responsetype = request->type;
	uint8_t network_arg1 = request->arg1;
	uint32_t network_arg2Length = htonl(request->arg2Length);
	status += send_data(socket_id, &network_responsetype, sizeof(uint8_t));
	status += send_data(socket_id, &network_arg1, sizeof(uint8_t));
	status += send_data(socket_id, &network_arg2Length, sizeof(uint32_t));
	status += send_data(socket_id, (void *)request->arg2, request->arg2Length);
	if (status < 0)
	{
		return -1;
	}
	//printf("Sent request: Type%i arg1:%i arg2length:%i arg2:%s|MESSAGE ENDS\n",request->type,request->arg1,request->arg2Length,request->arg2);
	fflush(stdout);
	return 0;
}

// Recieves a server response from the server
int receive_response(int socket_identifier, struct serverresponse *response)
{
	//	struct clientrequest recievedRequest;
	int status = 0;
	uint32_t network_arg2Length;
	// recieve type
	status += recieve_data(socket_identifier, (void *)&(response->type), sizeof(uint8_t));
	// recieve arg1
	status += recieve_data(socket_identifier, (void *)&(response->arg1), sizeof(uint8_t));
	// recieve arg 2 length
	status += recieve_data(socket_identifier, (void *)&network_arg2Length, sizeof(uint32_t));
	response->arg2Length = ntohl(network_arg2Length);
	//recieve arg 2
	// free(response->arg2);
	// response->arg2 = calloc(response->arg2Length,sizeof(char));
	bzero((void *)response->arg2, 1024);
	//strcpy(response->arg2,"EMPTY");
	status += recieve_data(socket_identifier, (void *)(response->arg2), response->arg2Length);
	if (status < 0)
	{
		return -1;
	}
	//printf("Recieved: Type%i arg1:%i arg2length:%i arg2:%s|MESSAGE ENDS\n",response->type,response->arg1,response->arg2Length,response->arg2);
	return 0;
	// TO DO
}

// Gets the corresponding number from a command string
int getCommandNum(char *command)
{
	if (strcmp(command, "SUB") == 0)
	{
		return SUB;
	}
	if (strcmp(command, "CHANNELS") == 0)
	{
		return CHANNELS;
	}
	if (strcmp(command, "UNSUB") == 0)
	{
		return UNSUB;
	}
	if (strcmp(command, "NEXT") == 0)
	{
		return NEXT;
	}
	if (strcmp(command, "LIVEFEED") == 0)
	{
		return LIVEFEED;
	}
	if (strcmp(command, "SEND") == 0)
	{
		return SEND;
	}
	if (strcmp(command, "BYE") == 0)
	{
		return BYE;
	}
	if (strcmp(command, "STOP") == 0)
	{
		return STOP;
	}
	return UNKNOWN;
}

// Handles the next and livefeed command/thread. Only run from separate threads
void *livefeed_next_handle(void *input)
{
	struct livefeed_next_data *data = (struct livefeed_next_data *)input;
	struct clientrequest req;
	struct serverresponse rec;
	rec.type = UNKNOWN;
	rec.arg1 = 0;
	rec.arg2Length = 0;
	bzero(rec.arg2, MAX_RESPONSE_LENGTH);
	rec.arg2Length = 0;
	
	req.type = UNKNOWN;
	req.arg1 = 0;
	req.arg2Length = 0;
	bzero(req.arg2, MAX_RESPONSE_LENGTH);

	if (data->isParams == true)
	{
		req.arg1 = data->channelID;
		req.type = NEXTPARAMS;
	}
	else
	{
		req.type = NEXT;
	}

	while (data->isActive == true)
	{
		//printf("Thread sleeps\n");
		if (nanosleep(&time1, &time2) < 0)
		{
			printf("thread failed to sleep during normal process\n");
		}
		//printf("Thread wakes up\n");
		//printf("Thread locking mutex\n");
		pthread_mutex_lock(&sendrecieve_mutex);

		if (data->isActive == false)
		{
			//printf("Thread unlocking mutex\n");
			pthread_mutex_unlock(&sendrecieve_mutex);
			data->finished = true;
			return NULL;
		}
		if (send_request(sockfd, &req) == -1)
		{
			printf("Failed to contact server. Disconnecting...\n");
			isConnected = false;
			data->isActive = false;
			//printf("Thread unlocking mutex\n");
			pthread_mutex_unlock(&sendrecieve_mutex);
			data->finished = true;
			return NULL;
		}
		if (receive_response(sockfd, &rec) == -1)
		{
			printf("Failed to recieve response from server. Disconnecting...\n");
			isConnected = false;
			data->isActive = false;
			//printf("Thread unlocking mutex\n");
			pthread_mutex_unlock(&sendrecieve_mutex);
			data->finished = true;
			return NULL;
		}
		//printf("Thread unlocking mutex\n");
		pthread_mutex_unlock(&sendrecieve_mutex);
		if (rec.type == ERROR)
		{
			data->isActive = false;
		}
		if (data->isLivefeed == false)
		{
			data->isActive = false;
		}
		if (rec.type != EMPTY)
		{
			printf("\033[A\n%s", rec.arg2);
			printf("\n");
		}
	}
	data->finished = true;
	return NULL;
}