/* **********************************************************
 * * Author:                Brandon Westmoreland
 * * Date Created:          5/12/16
 * * Last Modification Date:    5/12/16
 * * Lab Number:            CST 340 Lab 3
 * * Filename:              server.c
 * *
 * * Overview: 
 * *
 * * Input:
 * *
 * * Output:
 * *    
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
#include "sharedInfo.h"

#define HOST_NAME_MAX 2048;

extern const char* PORT;

// Buffer sizes
extern const int BUFFER_SIZE;

int isWorking;


void* ListenerFunc(void* param);

int main (int argc, char ** argv)
{
    int orig_sock = 0;		// Original socket
    struct addrinfo	*aip;
    struct addrinfo	hint;
    char * buf;
    size_t bufLen = 0; 
    int err;

    isWorking = 1;

    // check for host name
    if(argc != 2)
    {
        fprintf(stderr,"Usage: %s <sever name>\n",argv[0]);
        exit(1);
    }

    // Setup hint structure for getaddrinfo
    memset(&hint, 0, sizeof(hint));
    hint.ai_socktype = SOCK_STREAM;
    hint.ai_canonname = NULL;
    hint.ai_addr = NULL;
    hint.ai_next = NULL;

    if ((err = getaddrinfo(argv[1], PORT, &hint, &aip)) != 0) 
    {
	perror("getaddrinfo error");
	exit(1);
    }

    // Create Socket
    if ((orig_sock = socket(aip->ai_addr->sa_family, SOCK_STREAM, 0)) < 0) 
    {
	perror("create socket error");
	exit(1);
    }

    // connect to server
    if(connect(orig_sock,aip->ai_addr,aip->ai_addrlen) < 0)
    {
        perror("connection error");
        exit(1);
    }

    // Create a listener thread
    pthread_t listener;
    pthread_create(&listener,NULL,ListenerFunc, &orig_sock);
    
    do 
    {
        if(getline(&buf,&bufLen,stdin) == -1)
        {
            perror("Error on reading input");
            exit(1);
        }

        send(orig_sock,buf,bufLen,0);

        if(strstr(buf,"/quit"))
        {
            isWorking = 0;
        }

    } while(isWorking); // keep taking input until the user quits

    pthread_join(listener, NULL);
	
    return 0;
}

/* ****************************************
 *  Purpose: This function simply listens
 *          to the socket and places anything
 *          read into stdout.
 *
 *  Enter: Called within main
 *
 *  Exit:   No impact
 *
 * ************************************** */
void* ListenerFunc(void* param)
{
    int socket = *((int*)param);
    int len;
    char buf[BUFFER_SIZE]; // read buffer
    char * printPtr = buf;
    char sendBuf[BUFFER_SIZE + NAME_SIZE];
    int clientDone = 0;
    int printed = 0;
    // listen for input    
    while ( isWorking && (len = recv(socket, buf, BUFFER_SIZE, 0)) > 0 )
    {
        printPtr = buf;
        while(len > 0)
        {
            printed = printf(printPtr);
            fflush(stdout);
            len -= printed + 1;
            printPtr += printed + 1;
        }
        // clear the buffer, maybe that will help.
        memset(buf,'\0',BUFFER_SIZE);
    }
}

