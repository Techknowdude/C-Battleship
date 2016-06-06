/* **********************************************************
 * * Author:                Brandon Westmoreland
 * * Date Created:          5/12/16
 * * Last Modification Date:    5/12/16
 * * Lab Number:            CST 340 Lab 3
 * * Filename:              server.c
 * *
 * * Overview: 
 * *        This program is a chat server using sockets.
 * * Input:
 * *        None. The port number used is set in the sharedInfo.h file
 * * Output:
 * *        This server simply echos what the users type to all
 * *        users including the sender. No output to stdout on this side.
 * *
 * ************************************************************/

#include <stdio.h>
#include <sys/ioctl.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>
#include <stdlib.h>
#include <signal.h>
#include <wait.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <netdb.h>
#include "list.h"
#include "sharedInfo.h"

#define HOST_NAME_MAX 256

extern const char* PORT;

// create list for the clients. 
static list_t clientList;
static list_t gameList;

// flag to stop all of the threads
static int isWorking;

// Buffer sizes
extern const int BUFFER_SIZE;
extern const int NAME_SIZE;

// struct to hold the client info
typedef struct client_t
{
    int socket;
    char * name;
} client_t;

typedef struct game_t
{
    int started; // use 0 for invite, 1 for in play.
    client_t* clientA;
    client_t* clientB;

    // the map client A has placed his ships on, and the map of misses/hits
    char shipsA[MAP_WIDTH][MAP_HEIGHT];
    char visableA[MAP_WIDTH][MAP_HEIGHT];
    
    // the map client B has placed his ships on, and the map of misses/hits
    char shipsB[MAP_WIDTH][MAP_HEIGHT];
    char visableB[MAP_WIDTH][MAP_HEIGHT];

    pthread_t gameLock;
    pthread_cond_t lockCond;
} game_t;

// function to handle the clients using threads.
void* Handle_Client(void* param);

// function to send a message to all clients.
void Broadcast_Message(char * msg);
void Send_Message(char * msg, client_t* client);

int AddInvitation(char * name, client_t* client);
int CancelInvitation(client_t* client);
game_t* AcceptInvitation(char * name, client_t* client);
game_t* Init_Game();

// string compare because strcmp was giving me issues
int StrCmp(char*s1,char*s2);

int main (void)
{
    int orig_sock = 0;		// Original socket in server
    int new_sock = 0;		// New socket from connect
    struct addrinfo	*aip;
    struct addrinfo	hint;
    int reuse = 1; 
    int n = 0; 
    int err = 0; 
    char buf[BUFFER_SIZE]; 
    int len = 0;			// Misc Counters, etc.
    int i = 0;
    char * host; 

    isWorking = 1;
    // Initialize the list.
    List_Init(&clientList);
    List_Init(&gameList);


    // Allocate space for the host name of server
    if ((n = sysconf(_SC_HOST_NAME_MAX) + 1) < 0)
		n = HOST_NAME_MAX + 1;	
    
    // Allocate memory for the host name
    if ((host = malloc(n)) == NULL)
    {
	perror("malloc error");
        exit(1); 
    }

    // Get the host name
    if (gethostname(host, n) < 0)
    {
	perror("gethostname error");
        exit(1); 
    }

    // Setup hint structure for getaddrinfo
    memset(&hint, 0, sizeof(hint));
    hint.ai_flags = AI_CANONNAME;
    hint.ai_socktype = SOCK_STREAM;
    hint.ai_canonname = NULL;
    hint.ai_addr = NULL;
    hint.ai_next = NULL;

    if ((err = getaddrinfo(host, PORT, &hint, &aip)) != 0) 
    {
	fprintf(stderr, "ruptimed: getaddrinfo error: %s", 
        gai_strerror(err));
	exit(1);
    }

    // Create Socket
    if ((orig_sock = socket(aip->ai_addr->sa_family, SOCK_STREAM, 0)) < 0) 
    {
	perror("generate error");
	exit(1);
    }

    // Set the socket options.
    if(setsockopt(orig_sock, SOL_SOCKET, SO_REUSEADDR, &reuse,  sizeof(int)) < 0)
    {
        perror("Setting up Socket error");
	exit(1);
    }

    // Bind to the Socket
    if (bind (orig_sock, aip->ai_addr, aip->ai_addrlen) < 0)
    {
    	perror("bind error");
	close(orig_sock);
	exit(1);
    }

    // setup the listener. Allow up to 5 clients to queue connection requests.
    if(listen(orig_sock, 5) < 0)
    {
        perror("listen error");
		close (orig_sock);
        exit(1); 
    }

    printf("Started server. Host name: %s\n",host);

    // Accept clients FOREVER.... HEHEHE....
    do 
    {
	if ((new_sock = accept(orig_sock, NULL,  NULL)) < 0)
	{
            perror("accept error");
	    close(orig_sock);
	    exit(1); 
        }

        // Create a client info for the thread
        client_t * newClient = malloc(sizeof(client_t));
        newClient->socket = new_sock;
        newClient->name = malloc(NAME_SIZE);

        list_item_t first = List_First(clientList);
        List_Insert_At(first, newClient);
        List_Done_Iterating(clientList);

        //Start the thread
        pthread_t clientThread;
        if(pthread_create(&clientThread,NULL,Handle_Client,newClient) != 0)
	{
            perror("Could not create a child thread\n");
            exit(-1);
        }
		

    } while(1); // Run server forever, or until the hardware fails.
	
    return 0;
}

/* ****************************************
 *  Purpose: This function handles the echo
 *          functionality of the server for
 *          one of the connected clients. 
 *          This function run by multiple threads.
 *
 *  Enter: Called within main() on each connection.
 *
 *  Exit: The associated client is removed 
 *      from the list of connected clients.
 *
 * ************************************** */
void* Handle_Client(void* param)
{
    client_t * client = ((client_t*)param);
    int len;
    char buf[BUFFER_SIZE]; // read buffer
    char sendBuf[BUFFER_SIZE + NAME_SIZE + 2];
    int clientDone = 0;
    int nameOkay = 0;

    // get username
    send(client->socket, "Enter your username: ", 22, 0);
    
    while(!nameOkay)
    {
        len = recv(client->socket, client->name, NAME_SIZE, 0);

        if(len > 0 && CheckNameUnique(client->name))
        {
            nameOkay = 1;
        }
        else
            send(client->socket, "Enter your username: ", 22, 0);
    }


    // remove the newline
    client->name[strlen(client->name)-1] = '\0';
    
    // For some reason, the client always sends a blank line after. So capture that and send a join message
    len = recv(client->socket, buf, BUFFER_SIZE, 0);
    strcpy(sendBuf, client->name);
    strcat(sendBuf," has joined!");
    Broadcast_Message(sendBuf);

    // listen for input    
    while ( isWorking && !clientDone && (len = recv(client->socket, buf, BUFFER_SIZE, 0)) > 0 )
    {
        // handle the input of the client, just append their name.
        strcpy(sendBuf,client->name);
        
        // remove newline and replace with null
        strcpy(buf + strlen(buf)-1,"\0");
        if(buf[0] == '/')
        {
            if(strstr(buf,"/quit") == buf)
            {
                strcat(sendBuf,"has left!");
                clientDone = 1; // break the loop after the send.
                // send message to every client
                Broadcast_Message(sendBuf);
            }
            else if(strstr(buf,"/invite") == buf) // invite someone to a game
            {
                strcpy(buf, buf + 8); // get the name of the invitee

                // let everyone know of the invite
                strcpy(sendBuf,client->name);
                strcat(sendBuf, " has invited ");
                strcat(sendBuf, buf);
                strcat(sendBuf, " to a game");

                // add invite
                AddInvitation(buf, client);
                // send message to every client
                Broadcast_Message(sendBuf);
            }
            else if(strstr(buf,"/accept") == buf) // accept an invite
            {
                strcpy(buf, buf + 8);
            
                game_t* game = AcceptInvitation(buf, client);
                if(game != NULL)
                {
                    // let everyone know of the acceptance
                    strcpy(sendBuf,client->name); // remove ": "
                    strcat(sendBuf, " has accepted the invite from ");
                    strcat(sendBuf, buf);
                    strcat(sendBuf, " to a game");

                    // send message to every client
                    Broadcast_Message(sendBuf);

                    // just wait here until the game finishes.
                    pthread_mutex_lock(&(game->gameLock)); // lock
                    while(game->started) // while game in progress
                    {
                        pthread_cond_wait(&(game->lockCond),&(game->gameLock)); // sleep until game over
                    }
                    pthread_mutex_unlock(&(game->gameLock)); // unlock
                }
                else
                {
                    Send_Message("No pending invite from user",client);
                }
            }
            else    
            {
                // command not recognized   
                Send_Message("Command not recognized",client);
            }
        }
        else    
        {
            strcat(sendBuf,": ");
            strcat(sendBuf,buf);
            // send message to every client
            Broadcast_Message(sendBuf);
        }

    }
    
    // remove the client from the list
    list_item_t current = List_First(clientList);
    current = List_Next(current);
    while(current != NULL)
    {
        client_t* curClient = (client_t*)List_Get_At(current);
        if(client->socket == curClient->socket)
        {
            List_Remove_At(current);
            free(curClient->name);
            free(curClient);
            break;
        }
        current = List_Next(current);
    }
    List_Done_Iterating(clientList);

    // close the socket
    close(client->socket);
}

// Cancels the current invitation the user sent
// returns 1 if successful, 0 if no game found
int CancelInvitation(client_t* client)
{
    // try to find a game. If none found, add it
    int foundGame = 0;
    list_item_t current = List_First(gameList);
    list_item_t foundGameNode;
    list_item_t prev = current;
    current = List_Next(current);
    while(!foundGame && current != NULL)
    {
        game_t * game = (game_t*)List_Get_At(current);
        if(strcmp(game->clientA->name,client->name) == 0)
        {
            foundGameNode = current;
            foundGame = 1; // found one, don't add another
        }
        prev = current;
        current = List_Next(current);
    }

    if(foundGame)
    {
        List_Remove_At(foundGameNode);
    }

    List_Done_Iterating(clientList);

    return foundGame;
}

// Adds an invite for the pair of people to the list.
// Only one invite can be pending from a user at a time. This prevents
// a user from inviting multiple people and the server creating multiple
// games for the user and messing things up.
//
// returns 1 if successful, 0 if not.
int AddInvitation(char * name, client_t* client)
{
    // try to find a game. If none found, add it
    int notFoundGame = 1;
    list_item_t current = List_First(gameList);
    list_item_t prev = current;
    current = List_Next(current);
    while(notFoundGame && current != NULL)
    {
        game_t * game = (game_t*)List_Get_At(current);
        if(strcmp(game->clientA->name,client->name) == 0)
        {
            notFoundGame = 0; // found one, don't add another
        }
        prev = current;
        current = List_Next(current);
    }

    if(notFoundGame)
    {
        notFoundGame = 1;
        // find other client
        client_t* otherClient = NULL;
        int foundClient = 0;

        list_item_t curClient = List_First(clientList);
        curClient = List_Next(curClient);
        while(!foundClient && curClient != NULL)
        {
            // ############### RIGHT HERE PHIL! #################
            otherClient = (client_t*)List_Get_At(curClient);
            int dif = strcmp(name,otherClient->name);
            if(dif == 0)
            {
                foundClient == 1;
            }
            curClient = List_Next(curClient);
        }

        List_Done_Iterating(clientList);

        if(foundClient)
        {
            game_t* newGame = Init_Game();
    
            newGame->clientA = client;
            newGame->clientB = otherClient;
        
            List_Insert_At(prev,newGame);

            notFoundGame = 0;
        }
    }
    List_Done_Iterating(gameList);

    return notFoundGame;
}

// Creates a game and starts play for two players
game_t* AcceptInvitation(char * name, client_t* client)
{
    int accepted = 0;
    game_t* newGame = NULL;
    // try to find a game. If none found, add it
    list_item_t current = List_First(gameList);
    list_item_t prev = current;
    current = List_Next(current);
    while(!accepted && current != NULL)
    {
        game_t * game = (game_t*)List_Get_At(current);
        if(game->clientB == client && strcmp(game->clientA->name,name) == 0)
        {
            accepted = 1; // found one
            newGame = game;
            newGame->started = 1; // mark the game as started
        }
        prev = current;
        current = List_Next(current);
    }

    List_Done_Iterating(gameList);

    return newGame;
}
// Determines if a username is unique. 0 for false, 1 for true
int CheckNameUnique(char * name)
{

    int foundName = 0;
    list_item_t current = List_First(clientList);
    current = List_Next(current);
    while( !foundName &&current != NULL)
    {
        client_t* client = (client_t*)List_Get_At(current);
        if(strcmp(client->name,name) == 0)
        {
            foundName = 1;
        }
        current = List_Next(clientList);
    }
    List_Done_Iterating(clientList);

    return foundName;
}

/* ****************************************
 *  Purpose: This function sends the message
 *          to all clients currently connected.
 *  Enter: Called within Handle_Client
 *
 *  Exit:   No impact
 *
 * ************************************** */
void Broadcast_Message(char * msg)
{
    // foreach client, send the message
    list_item_t current = List_First(clientList);
    current = List_Next(current);

    while(current != NULL)
    {
        client_t * client = (client_t *)List_Get_At(current);
        send(client->socket, msg, strlen(msg) + 1, 0);
        current = List_Next(current);
    }

    List_Done_Iterating(clientList);
}

// Sends a message to one client
void Send_Message(char * msg, client_t* client)
{
    send(client->socket,msg,strlen(msg)+1,0);
}

// contructor for the game_t struct. You must use this to create a game.
game_t* Init_Game()
{
    game_t* newGame = (game_t*)malloc(sizeof(game_t));

    memset(newGame->shipsA,'-',MAP_WIDTH*MAP_HEIGHT*sizeof(char));
    memset(newGame->shipsB,'-',MAP_WIDTH*MAP_HEIGHT*sizeof(char));
    memset(newGame->visableA,'-',MAP_WIDTH*MAP_HEIGHT*sizeof(char));
    memset(newGame->visableB,'-',MAP_WIDTH*MAP_HEIGHT*sizeof(char));

    pthread_mutex_init(&(newGame->gameLock),NULL);
    pthread_cond_init(&(newGame->lockCond),NULL);
    return newGame;
}

// Displays the map. side if A or B, which is related to the client and map names
void Display_Game(game_t* game, char side)
{
    //Our map on the left, enemy on the right.
    //ex:    A B C D E F
    //     1 - - - - - -
    //     2 - - - - - -
    //     3 - - - - - -
    //
    //     pieces:
    //     Carrier (occupies 5 spaces), Battleship (4), Cruiser (3), Submarine (3), and Destroyer (2).  
    //

    char ** homeMap;
    char ** enemyMap;

    if(side == 'A')
    {
        homeMap = game->shipsA;
        enemyMap = game->visableA;
    }
    else    
    {
        homeMap = game->shipsB;
        enemyMap = game->visableB;
    }

    int row;

    // display the top bar
    printf("____________________________________________________________");
    printf("|    --- Home Side ---        |      --- Enemy Side ---    |\n");
    printf("|    A B C D E F G H I J      |      A B C D E F G H I J   |\n");
    
    for(row = 0; row < 10; ++ row)
    {
    printf("|  %i %c %c %c %c %c %c %c %c %c %c      |    1 %c %c %c %c %c %c %c %c %c %c   |\n", 
            row+1,
             homeMap[row][0], homeMap[row][1], homeMap[row][2], homeMap[row][3], homeMap[row][4], homeMap[row][5], homeMap[row][6], homeMap[row][7],
            enemyMap[row][0],enemyMap[row][1],enemyMap[row][2],enemyMap[row][3],enemyMap[row][4],enemyMap[row][5],enemyMap[row][6],enemyMap[row][7]);
    }

    printf("____________________________________________________________");
}

int StrCmp(char * s1, char * s2)
{
    int dif = 0;
    char * ptr = s1;
    char * ptr2 = s2;

    for(ptr = s1, ptr2 = s2; *ptr != NULL && *ptr2 != NULL; ++ptr, ++ptr2)
    {
        dif += *ptr - *ptr2;
    }

    return dif;
}
