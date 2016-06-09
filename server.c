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
    void* game_ptr; // void* because game_t is defined later...
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

    // mutex to lock the struct, and condition variable in case I need to wait.
    pthread_t gameLock;
    pthread_cond_t lockCond;

    // Checks for the active player. 'A' for client A, 'B' for client B
    char activePlayer;
} game_t;

// function to handle the clients using threads.
void* Handle_Client(void* param);

// function to send a message to all clients.
void Broadcast_Message(char * msg);
void Send_Message(char * msg, client_t* client);

game_t* AddInvitation(char * name, client_t* client);
int CancelInvitation(client_t* client);
game_t* AcceptInvitation(char * name, client_t* client);
game_t* Init_Game();

void Display_Game(game_t* game, client_t* client);

void MainGameLoop(game_t* game);
int Try_Place_Boat(char map[][MAP_WIDTH],char col, int row, char dir, int spaces);
void Place_Ships(game_t* game, client_t* client);
char CheckGameOver(game_t* game);
int Shoot_Boat(game_t* game, int col, int row, client_t* client);

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
    char * host = NULL;

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
    game_t* game = NULL;
    char game_side = '\0';

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
    strcat(sendBuf,"\n");
    Broadcast_Message(sendBuf);

    // listen for input    
    while ( isWorking && !clientDone && (len = recv(client->socket, buf, BUFFER_SIZE, 0)) > 0 )
    {
        if(game != NULL)
        {
            if(game->started)
            {
                Place_Ships(game,client);
                // not sure if this will ever get the lock...
                //pthread_mutex_lock(&(game->gameLock));
                while(game->started)
                {
                  //  pthread_cond_wait(&(game->lockCond),&(game->gameLock));
                }
                //pthread_mutex_unlock(&(game->gameLock));
                game = NULL;
            }
        }

        // handle the input of the client, just append their name.
        strcpy(sendBuf,client->name);
        
        // remove newline and replace with null
        strcpy(buf + strlen(buf)-1,"\0");
        if(buf[0] == '/')
        {
            if(strstr(buf,"/quit") == buf)
            {
                strcat(sendBuf,"has left!");
                strcat(sendBuf,"\n");
                clientDone = 1; // break the loop after the send.
                // send message to every client
                Broadcast_Message(sendBuf);
            }
            else if(strstr(buf,"/cancel") == buf) // invite someone to a game
            {
                int isCanceled = CancelInvitation(client);
                if(isCanceled)
                {
                    Send_Message("Invitation canceled\n",client);
                    game = NULL;
                }
                else
                {
                    Send_Message("No invitation pending\n",client);
                }
            }
            else if(strstr(buf,"/invite") == buf) // invite someone to a game
            {
                strcpy(buf, buf + 8); // get the name of the invitee

                // let everyone know of the invite
                strcpy(sendBuf,client->name);
                strcat(sendBuf, " has invited ");
                strcat(sendBuf, buf);
                strcat(sendBuf, " to a game");
                strcat(sendBuf,"\n");

                // add invite
                game = AddInvitation(buf, client);
                // send message to every client
                Broadcast_Message(sendBuf);
            }
            else if(strstr(buf,"/who") == buf)
            {
                int index = 0;
                strcpy(buf,"Current users:\n");
                list_item_t cur = List_First(clientList);
                cur = List_Next(cur);
                while(cur != NULL)
                {
                    client_t* cClient = (client_t*)List_Get_At(cur);
                    sprintf(buf +strlen(buf), "%3i: %s\n",index, cClient->name);
                    cur = List_Next(cur);
                    ++index;
                }
                List_Done_Iterating(clientList);
                Send_Message(buf,client);
            }
            else if(strstr(buf,"/accept") == buf) // accept an invite
            {
                strcpy(buf, buf + 8); // get the name
            
                //attempt to accept
                game_t* game = AcceptInvitation(buf, client);
                if(game != NULL)
                {
                    // let everyone know of the acceptance
                    strcpy(sendBuf,client->name); // remove ": "
                    strcat(sendBuf, " has accepted the invite from ");
                    strcat(sendBuf, buf);
                    strcat(sendBuf, " to a game");
                    strcat(sendBuf,"\n");

                    // send message to every client
                    Broadcast_Message(sendBuf);
                    Place_Ships(game,client);
                    MainGameLoop(game);
                    game = NULL;    
                }
                else
                {
                    Send_Message("No pending invite from user\n",client);
                }
            }
            else    
            {
                // command not recognized   
                Send_Message("Command not recognized\n",client);
            }
        }
        else    
        {
            strcat(sendBuf,": ");
            strcat(sendBuf,buf);
            strcat(sendBuf,"\n");
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

    List_Done_Iterating(gameList);

    return foundGame;
}

// Adds an invite for the pair of people to the list.
// Only one invite can be pending from a user at a time. This prevents
// a user from inviting multiple people and the server creating multiple
// games for the user and messing things up.
//
// returns the game if there is not already a game invite.
game_t* AddInvitation(char * name, client_t* client)
{
    // try to find a game. If none found, add it
    int notFoundGame = 1;
    list_item_t current = List_First(gameList);
    list_item_t prev = current;
    current = List_Next(current);
    game_t* game = NULL;
    while(notFoundGame && current != NULL)
    {
        game = (game_t*)List_Get_At(current);
        if(strcmp(game->clientA->name,client->name) == 0)
        {
            game = NULL;
            notFoundGame = 0; // found one, don't add another
        }
        prev = current;
        current = List_Next(current);
    }

    if(notFoundGame)
    {
        // find other client
        client_t* otherClient = NULL;
        int foundClient = 0;

        list_item_t curClient = List_First(clientList);
        curClient = List_Next(curClient);
        while(!foundClient && curClient != NULL)
        {
            otherClient = (client_t*)List_Get_At(curClient);
            int dif = strcmp(name,otherClient->name);
            if(dif == 0)
            {
                foundClient = 1;
            }
            curClient = List_Next(curClient);
        }

        List_Done_Iterating(clientList);

        if(foundClient)
        {
            game = Init_Game();
    
            game->clientA = client;
            game->clientB = otherClient;
        
            List_Insert_At(prev,game);

            notFoundGame = 1;
        }
    }
    List_Done_Iterating(gameList);

    return game;
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

    newGame->activePlayer = 'B';

    newGame->started = 0;

    return newGame;
}

// Main game logic loop for battleship
void MainGameLoop(game_t* game)
{
    char buf[BUFFER_SIZE];
    int len = 0;
    char result = ' ';

    client_t* client = NULL;

    // wait for other player to place ships.
    while(game->started != 3);
    
    while(game->started)
    {
        if(game->activePlayer == 'A')
        {
            client = game->clientA;
        }
        else
        {
            client = game->clientB;
        }

        Display_Game(game, game->clientA);
        Display_Game(game, game->clientB);

        sprintf(buf,"### Player %s's turn ###\n",client->name);
        Send_Message(buf,game->clientA);
        Send_Message(buf,game->clientB);

        int valid = 0;

        do
        {
            // Get player input loop
            len = recv(client->socket, buf,BUFFER_SIZE,0);

            // interpret input -- change map
            int row = buf[1] - '0';
            int col = toupper(buf[0]) - 'A';

            int hit = Shoot_Boat(game,col,row,client);

            valid = 1;

            if(hit == 1)
            {
                sprintf(buf,"%c%i HIT!\n",buf[0],row);
                Send_Message(buf,game->clientA);
                Send_Message(buf,game->clientB);
            }
            else if(hit == 0)
            {
                sprintf(buf,"%c%i MISS!\n",buf[0],row);
                Send_Message(buf,game->clientA);
                Send_Message(buf,game->clientB);
            }
            else
            {
                strcpy(buf,"Invalid location. A-j, 0-9 are accepted\n");
                Send_Message(buf,client);
                valid = 0;
            }
        }while(!valid);


        // change turns.
        game->activePlayer = game->activePlayer == 'A' ? 'B' : 'A';

        result = CheckGameOver(game);
        if(result != 'N')
            game->started = 0;
    }

    if(result == 'A')
        sprintf(buf,"Player %s has won!\n",game->clientA->name);
    else
        sprintf(buf,"Player %s has won!\n",game->clientB->name);

    Send_Message(buf,game->clientA);
    Send_Message(buf,game->clientB);
    
    // wake up the listener for the other client
    //pthread_cond_signal(&(game->lockCond));

    // push the clients back in the list.
    list_item_t cur = List_First(clientList);
    List_Insert_At(cur,game->clientA);
    List_Insert_At(cur,game->clientB);
    List_Done_Iterating(clientList);
}

void Place_Ships(game_t* game, client_t* client)
{
    // list of ship spaces and names
    int shipSizes[] = { 5, 4, 3, 3, 2 };
    const char * shipNames[] ={ "Aircraft carrier(5)", "Battleship(4)", "Submarine(3)", "Cruiser(3)", "Destroyer(2)"};
    char buf[BUFFER_SIZE];
    int len = 0;

    // remove the client from the list
    list_item_t cur = List_First(clientList);
    cur = List_Next(cur);
    while(cur != NULL)
    {
        client_t* cClient = (client_t*)List_Get_At(cur);
        if(cClient == client)
        {
            List_Remove_At(cur);
            break;
        }
        cur = List_Next(cur);
    }
    List_Done_Iterating(clientList);

    //
    // loop through each ship
    int i;
    for(i = 0; i < 5; ++i)
    {
        Display_Game(game,client);

        int valid = 0;
        do
        {
            len = sprintf(buf, "Please place your %s: [Column][Row][Direction] ex: F5D\n",shipNames[i]);
            sleep(0);
            send(client->socket,buf,len,0);

            len = recv(client->socket, buf,BUFFER_SIZE,0);

            if(len > 2)
            {
                char dir = toupper(buf[2]);
                int row = buf[1] - '0';
            
                if((buf[0] >= 'a' && buf[0] <= 'z'
                    || buf[0] >= 'A' && buf[0] <= 'Z') 
                    && (row >= 0 && row < MAP_WIDTH)
                    && (dir == 'U' || dir == 'D' || dir == 'L' || dir == 'R'))
                {
                    int placed;
                    if(client == game->clientA)
                        placed = Try_Place_Boat(game->shipsA, buf[0],row,dir,shipSizes[i]);
                    else
                        placed = Try_Place_Boat(game->shipsB, buf[0],row,dir,shipSizes[i]);
                    if(placed)
                        valid = 1;
                    else
                    {
                    len = sprintf(buf, "That ship either cannot fit, or overlaps another ship\n");
                    send(client->socket,buf,len,0);
                    }
                }
                else
                {
                    len = sprintf(buf, "Bad input. Columns are A-J, Rows are 0-9, Directions are (U)p (D)own (R)ight and (L)eft\n");
                    send(client->socket,buf,len,0);
                }
            }
            
        } while(!valid);
    }

    ++game->started;
}

// returns 'N' for not over, or 'A' for clientA win, and 'B' for clientB win.
char CheckGameOver(game_t* game)
{
    return 'A'; // testing...
    // check for clientA win
    int r = 0;
    int c = 0;
    int winA = 1;
    int winB = 1;

    for(r = 0; (winA || winB) && r < MAP_HEIGHT; ++r)
    {
        for(c = 0; (winA || winB) && c < MAP_WIDTH; ++c)
        {
            // if one ship space has not been hit, set win to 0;
            if(winA && game->shipsB[r][c] == 'B')
                winA = 0;
            if(winB && game->shipsA[r][c] == 'B')
                winB = 0;
        }
    }
    if(winA == 1)
        return 'A';
    else if(winB == 1)
        return 'B';

    return 'N';
}

// 1 if hit, 0 if miss. -1 on invalid
int Shoot_Boat(game_t* game, int col, int row, client_t* client)
{
    if(col < 0 || col >= MAP_WIDTH || row < 0 || row >= MAP_HEIGHT) return -1;

    int hit = 0;

    if(client == game->clientA)
    {
        if(game->visableA[row][col] != '-')
        {
            hit = -1;
        }
        else if(game->shipsB[row][col] == 'B')
        {
            hit = 1;
            game->visableA[row][col] = 'X';
            game->shipsB[row][col] = 'X';
        }
        else
        {
            hit = 0;
            game->visableA[row][col] = 'O';
            game->shipsB[row][col] = 'O';
        }
    }
    else
    {
        if(game->visableB[row][col] != '-')
        {
            hit = -1;
        }
        else if(game->shipsA[row][col] == 'B')
        {
            hit = 1;
            game->visableB[row][col] = 'X';
            game->shipsA[row][col] = 'X';
        }
        else
        {
            hit = 0;
            game->visableB[row][col] = 'O';
            game->shipsA[row][col] = 'O';
        }
    }

    return hit;
}

// 1 if placed, 0 if not
int Try_Place_Boat(char map[][MAP_WIDTH],char col, int row, char dir, int spaces)
{
    int canPlace = 1;
    char tempMap[MAP_WIDTH][MAP_HEIGHT];
    char direction = toupper(dir);
    int curCol = toupper(col) - 'A';
    int curRow = row;
    int done = 0;

    memcpy(tempMap,map,MAP_HEIGHT*MAP_WIDTH);

    while(!done && spaces > 0)
    {
        // check if in bounds

        if(curRow >= MAP_HEIGHT || curRow < 0 || curCol >= MAP_WIDTH || curCol < 0
                || tempMap[curRow][curCol] == 'B')
        {
            canPlace = 0;
            done = 1;
        }

        if(!done)
        {
            tempMap[curRow][curCol] = 'B';
            --spaces;
            
            switch(direction)
            {
                case 'U': 
                    --curRow;
                    break;
                case 'D':
                    ++curRow;
                    break;
                case 'L':
                    --curCol;
                    break;
                case 'R':
                    ++curCol;
                    break;
            }
        }
    }
    if(canPlace) // placement was valid
    {
        memcpy(map,tempMap,MAP_HEIGHT*MAP_WIDTH);
    }

    return canPlace;
}

// Displays the map.
void Display_Game(game_t* game, client_t* client)
{
    char buf[2048];
    int size = 0;
    int row = 0;
    //Our map on the left, enemy on the right.
    //ex:    A B C D E F
    //     1 - - - - - -
    //     2 - - - - - -
    //     3 - - - - - -
    //
    //     pieces:
    //     Carrier (occupies 5 spaces), Battleship (4), Cruiser (3), Submarine (3), and Destroyer (2).  
    //

    // display the top bar
    size = sprintf(buf,        "______________________________________________________\n");
    size += sprintf(buf + size,"|    --- Home Side ---    |    --- Enemy Side ---   |\n");
    size += sprintf(buf + size,"|    A B C D E F G H I J  |    A B C D E F G H I J  |\n");
    for(row = 0; row < 10; ++ row)
    {
        if(game->clientA == client)
        {

        size += sprintf(buf +size,"| %2i %c %c %c %c %c %c %c %c %c %c  | %2i %c %c %c %c %c %c %c %c %c %c  |\n", 
            row,
             game->shipsA[row][0], game->shipsA[row][1], game->shipsA[row][2], game->shipsA[row][3], game->shipsA[row][4], game->shipsA[row][5], game->shipsA[row][6], game->shipsA[row][7], game->shipsA[row][8], game->shipsA[row][9],
            row,
            game->visableA[row][0],game->visableA[row][1],game->visableA[row][2],game->visableA[row][3],
            game->visableA[row][4],game->visableA[row][5],game->visableA[row][6],game->visableA[row][7],
            game->visableA[row][8],game->visableA[row][9]);
        }
        else
        {
        size += sprintf(buf + size,"| %2i %c %c %c %c %c %c %c %c %c %c  | %2i %c %c %c %c %c %c %c %c %c %c  |\n", 
            row,
            game->shipsB[row][0], game->shipsB[row][1], game->shipsB[row][2], game->shipsB[row][3], 
            game->shipsB[row][4], game->shipsB[row][5], game->shipsB[row][6], game->shipsB[row][7],
            game->shipsB[row][8], game->shipsB[row][9],
            row,
            game->visableB[row][0],game->visableB[row][1],game->visableB[row][2],game->visableB[row][3],
            game->visableB[row][4],game->visableB[row][5],game->visableB[row][6],game->visableB[row][7],
            game->visableB[row][8],game->visableB[row][9]);
        }
    }

    size += sprintf(buf + size,"_____________________________________________________\n");

    send(client->socket,buf,size,0);
}

