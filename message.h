#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include <errno.h>
#define RETURNED_ERROR -1
#define MESSAGELENGTH 1024
#define MAXDATASIZE 100 /* max number of bytes we can get at once */
#define ARRAY_SIZE 30   /* Size of array to receive */

#define BACKLOG 10 /* how many pending connections queue will hold */

#define PORT_NO 54321
#define MAX_RESPONSE_LENGTH 10000
#define timeoutlength 3
//The message type that the client sends to the server
enum requesttype
{
	UNKNOWN,
	HELLO,
	SUB,
	CHANNELS,
	UNSUB,
	SEND,
	BYE,
	NEXTPARAMS,
	NEXT,
	LIVEFEEDPARAMS,
	LIVEFEED,
	STOP
};
// The message type that the server sends to the client
enum responsetype
{
	WELCOME,
	MESSAGE,
	FAREWELL,
	EMPTY,
	ERROR
};
//The message that the client sends to the server
struct clientrequest
{
	enum requesttype type;
	uint8_t arg1;
	uint32_t arg2Length;
	char arg2[MESSAGELENGTH];
};
// The message that the server sends to the client
struct serverresponse
{
	enum responsetype type;
	uint8_t arg1;
	uint32_t arg2Length;
	char arg2[MAX_RESPONSE_LENGTH];
};

// Represents a client connected to the server
struct client
{
	bool active;
	uint8_t clientID;
	int socket_id;
	int currenttime[256];
	bool subscribed[256];
};

// Represents a message stored on the server
struct message
{
	int time;
	uint8_t channelID;
	uint32_t messageLength;
	char message[1024];
};
//returns 0 on success
//returns -1 on send error
int send_data(int socket_id, void *data, size_t length)
{
	char *currentpos = (char *)data;
	int nb;
	while (length > 0)
	{
		nb = send(socket_id, currentpos, length, 0);
		if (nb == RETURNED_ERROR)
		{
			if (errno == EAGAIN || errno == EWOULDBLOCK)
			{
				//just a timeout
				//printf("Got send timeout.\n");
			}
			else
			{
				perror("send error");
				return -1;
			}
		}
		else
		{
			// Recieved data
			currentpos += nb;
			length -= nb;
		}
		if (nb == 0)
		{
			// Socket closed, return error
			printf("connection is closed\n");
			return -1;
		}
	}

	return 0;
}
// returns 0 if successful
// returns -1 if error
int recieve_data(int socket_id, void *data, size_t length)
{
	char *currentpos = (char *)data;
	bool finished_sending = false;
	while (length > 0)
	{
		//Recieve data
		int nb = recv(socket_id, currentpos, length, 0);
		if (nb == RETURNED_ERROR)
		{
			//Timed out or error
			if (errno == EAGAIN || errno == EWOULDBLOCK)
			{
				//Timed out, keep going as normal.
				//printf("Got recieve timeout.\n");
			}
			else
			{
				//Socket error, return error
				perror("recieve error");
				return -1;
			}
		}
		else
		{
			// Recieved data
			currentpos += nb;
			length -= nb;
		}
		if (nb == 0)
		{
			// Socket closed, return error
			//printf("connection is closed\n");
			return -1;
		}
	}
	return 0;
}