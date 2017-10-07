/* -*- mode: c; tab-width: 4; indent-tabs-mode: t -*- */

/*
 * socketserver - Tcl interface to libancillary to create a socketserver
 *
 * Copyright (C) 2017 FlightAware LLC
 *
 * freely redistributable under the Berkeley license
 * educated by code snippets from flingfd under Apache V2.0 license.
 *
 */

#include <assert.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <errno.h>
#include <sys/wait.h>
#include <sys/socket.h>
#include <string.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <pthread.h>
#ifdef __FreeBSD__
#include <netinet/in.h>
#include <signal.h>
#endif

#include "socketserver.h"

#define HOST_AND_PORT_LEN (INET_ADDRSTRLEN + INET6_ADDRSTRLEN + 80)

TCL_DECLARE_MUTEX(threadMutex);

/*
 * Send and fd over sock with SCM_RIGHTS.
 *
 * Returns: 0 for success and 1 for error.
 */
static int send_fd(int sock, int fd, const char *host_and_port) {
	struct msghdr msg;
	struct iovec iov;
	char buf[CMSG_SPACE(sizeof(int))];
    char null_byte = 0;
    
    if (host_and_port) {
        // Send the host and port as the data in the message
        iov.iov_base = (void *)host_and_port;
        iov.iov_len = strlen(host_and_port) + 1;
    } else {
        // We have to send at least a byte
        iov.iov_base = &null_byte;
        iov.iov_len = 1;
    }
    
	msg.msg_iov = &iov;
	msg.msg_iovlen = 1;
	msg.msg_control = buf;
	msg.msg_controllen = sizeof(buf);
	msg.msg_name = NULL;
	msg.msg_namelen = 0;

	struct cmsghdr *header = CMSG_FIRSTHDR(&msg);
	header->cmsg_level = SOL_SOCKET;
	header->cmsg_type = SCM_RIGHTS;
	header->cmsg_len = CMSG_LEN(sizeof(int));
	memcpy(CMSG_DATA(header), &fd, sizeof(fd));

	return sendmsg(sock, &msg, 0) > 0 ? 0 : 1;
}

/*
 * Receive a fd from socket.
 *
 * Returns: -1 for error or the fd one success.
 */
static int recv_fd(int sock, char *host_and_port, int host_and_port_max) {
	struct msghdr msg;
	struct iovec iov;
    char buf[CMSG_SPACE(sizeof(int))];
    char data[HOST_AND_PORT_LEN];

    // Set address to null string
    if (host_and_port) {
        *host_and_port = 0;
    }
    
	iov.iov_base = data;
	iov.iov_len = sizeof(data);
	msg.msg_iov = &iov;
	msg.msg_iovlen = 1;
	msg.msg_control = buf;
	msg.msg_controllen = sizeof(buf);
	msg.msg_name = NULL;
	msg.msg_namelen = 0;

	if (recvmsg(sock, &msg, MSG_DONTWAIT) == -1)
		return -1;
    
	struct cmsghdr *header;
	for (header = CMSG_FIRSTHDR(&msg); header != NULL; header = CMSG_NXTHDR(&msg, header)) {
		if (header->cmsg_level == SOL_SOCKET && header->cmsg_type == SCM_RIGHTS) {
			int count = header->cmsg_len - (CMSG_DATA(header) - (unsigned char *)header);
			if (count == sizeof(int)) {
				int fd = -1;
				memcpy(&fd, CMSG_DATA(header), sizeof(int));
                strncpy(host_and_port, data, host_and_port_max);
				return fd;
			}
		}
	}

	return -1;
}

/*
 *----------------------------------------------------------------------
 *
 * sockerserverObjCmd --
 *
 *      socketserver command
 *
 * Results:
 *      A standard Tcl result.
 *
 *----------------------------------------------------------------------
 */

#ifdef SOCKETSERVER_DEBUG
static char debug_msgbuf[512];
#endif

static void debug(const char * msg) {
#ifdef SOCKETSERVER_DEBUG
	strcpy(debug_msgbuf, msg);
	fprintf(stderr, "%s\n", msg);
#endif
}

/*
 * Thread entry point.
 * The thread creates the IP socket and accepts connections.
 * When a connection is accepted it is written to socketpair to pass
 * the FD to the worker using SCM_RIGHTS.
 *
 * Arguments - socketpair write side and TCP port number.
 */
static void * socketserver_thread(void *args)
{
	socketserver_thread_args* targs = (socketserver_thread_args *)args;
	int sock = targs->in;
	int socket_desc , client_sock;
	struct sockaddr_in server, *sin;
    struct sockaddr_in6 *sin6;
    struct sockaddr client;
	int client_max = sizeof(client);
	int on = 1;
    char clientaddr[INET_ADDRSTRLEN + INET6_ADDRSTRLEN];
    int clientaddr_max = sizeof(clientaddr);
    int client_port = -1;
    char host_and_port[sizeof(clientaddr) + 80];

	// create tcp socket
	socket_desc = socket(AF_INET , SOCK_STREAM , 0);
	if (socket_desc == -1)
	{
		debug("Could not create socket");
	}
	debug("Socket created");

	if (setsockopt(socket_desc, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(int)) < 0) {
		debug("SO_REUSEADDR failed");    
	}

	server.sin_family = AF_INET;
	server.sin_addr.s_addr = INADDR_ANY;
	server.sin_port = htons( targs->port );
	if( bind(socket_desc,(struct sockaddr *)&server , sizeof(server)) < 0)
	{
		debug("bind failed");
		// Send a TERM signal to self, to exit the master process
		kill(getpid(), 15);
		return (void *)1;
	}
	debug("bind done");

	listen(socket_desc , SOMAXCONN);

	debug("Waiting for incoming connections...");

	while (1) {
		client_sock = accept(socket_desc, &client, (socklen_t*)&client_max);
		if (client_sock < 0 && errno != EINTR)
		{
			// EINTR are ok in accept calls, retry
			debug("accept failed");
		}
		else if (client_sock != -1)
		{
			debug("Connection accepted");
            // Try to get the address as a string
            switch(client.sa_family) {
                case AF_INET:
                    sin = (struct sockaddr_in *)&client;
                    inet_ntop(AF_INET, &(sin->sin_addr),
                              clientaddr, clientaddr_max);
                    client_port = ntohs(sin->sin_port);
                    break;
                    
                case AF_INET6:
                    sin6 = (struct sockaddr_in6 *)&client;
                    inet_ntop(AF_INET6, &(sin6->sin6_addr),
                          clientaddr, clientaddr_max);
                    client_port = ntohs(sin6->sin6_port);
                    break;
                    
                default:
                    strncpy(clientaddr, "\"Unknown AF\"", clientaddr_max);
                    client_port = -1;
                    return NULL;
            }
            snprintf(host_and_port, sizeof(host_and_port) - 1, "%s %d", clientaddr, client_port);
			if (send_fd(sock, client_sock, host_and_port)) {
				debug("Send fd failed");
			} else {
				debug("Sent fd.");
			}
			close(client_sock);
		}
	}
	return (void *)0;
}

/*
 * Read the fd from the socketpair and call the callback handler with the name
 * of the socket.
 */
static int socketserver_EventProc(Tcl_Event *tcl_event, int flags)
{
    char host_and_port[HOST_AND_PORT_LEN];
	socketserver_ThreadEvent *evPtr = (socketserver_ThreadEvent *)tcl_event;
	socketserver_port * data = (socketserver_port *)evPtr->data;

	/* Check the active flag to see if we ignore this callback */
	Tcl_MutexLock(&threadMutex);
	if (!data->active) {
		Tcl_MutexUnlock(&threadMutex);
		return 1;
	}
	data->active = 0;
	/* attempt to read and FD from the socketpair. */
	int fd = recv_fd(data->out, host_and_port, sizeof(host_and_port));
	if (fd == -1) {
		/* receive errors are ok. The socketpair is non-blocking and
		 * interrupts can happen. */
		data->active = 1;
		Tcl_MutexUnlock(&threadMutex);
		return 1;
	}
	Tcl_MutexUnlock(&threadMutex);

	/* Create a channel from the unix fd. */
	void *fdPtr = (void *)((long)fd);
	Tcl_Channel channel = Tcl_MakeFileChannel(fdPtr, TCL_READABLE|TCL_WRITABLE);
	Tcl_RegisterChannel(data->interp, channel);

	/* Invoke the callback handler. */
	const char *channel_name = Tcl_GetChannelName(channel);
	if (channel_name == NULL || *channel_name == 0) {
		Tcl_AddErrorInfo(data->interp, "Failed to get channel name for ancil_recv_fd file descriptor.");
		return 1;
	}
	char * script = (char *)ckalloc(data->scriptLen);
	strcpy(script, data->callback);
	strcat(script, " ");
	strcat(script, channel_name);
    strcat(script, " ");
    strcat(script, host_and_port);
	Tcl_Eval(data->interp, script);
	ckfree(script);

	return 1;
}

/*
 * When socket is readable create a Tcl event.
 */
static void socketserver_readable(ClientData client_data, int mask)
{
	socketserver_port * data = (socketserver_port *)client_data;

	Tcl_MutexLock(&threadMutex);

	/* Create a Tcl event. */
	socketserver_ThreadEvent * event = (socketserver_ThreadEvent *)ckalloc(sizeof(socketserver_ThreadEvent));
	event->event.proc = socketserver_EventProc;
	event->event.nextPtr = NULL;
	event->data = data;
	Tcl_ThreadQueueEvent(data->threadId, (Tcl_Event *)event, TCL_QUEUE_TAIL);
	Tcl_ThreadAlert(data->threadId);

	Tcl_MutexUnlock(&threadMutex);
}

static socketserver_port * socketserver_getPort(socketserver_objectClientData *clientData, int port, int allocate)
{
	/* Default port is the first allocated port */
	if (port == 0) {
		if (clientData->ports) {
			port = clientData->ports->targs.port;
		}
	}

	/* search for the port */
	socketserver_port * p = clientData->ports;
	while (p != NULL) {
		if (p->targs.port == port) {
			return p;
		}
		p = p->nextPtr;
	}

	/* If we cannot find an existing entry, return an error. */
	if (!allocate) {
		return NULL;
	}

	/* If the list is non-empty allocate a new link. */
	if (clientData->ports != NULL) {
		p = clientData->ports;
		while (p->nextPtr != NULL) {
			p = p->nextPtr;
		}
		p->nextPtr = (socketserver_port *)ckalloc(sizeof(socketserver_port));
		p = p->nextPtr;
	} else {
		/* Allocate the head */
		clientData->ports = (socketserver_port *)ckalloc(sizeof(socketserver_port));
		p = clientData->ports;
	}
	/* Make a new entry. */
	memset(p, 0, sizeof(socketserver_port));
	p->targs.port = port;
	p->targs.in = -1;

	return p;
}

int socketserverObjCmd(ClientData clientData, Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[])
{
	socketserver_objectClientData *cdPtr = (socketserver_objectClientData *)clientData;
	int optIndex;
	int port = 0;
	const char *callback = NULL;
	socketserver_port *data = NULL;

	enum options {
		OPT_CLIENT,
		OPT_SERVER
	};
	static CONST char *options[] = { "client", "server" };

	// basic command line processing

	// argument must be one of the subOptions defined above
	if (Tcl_GetIndexFromObj (interp, objv[1], options, "option",
				TCL_EXACT, &optIndex) != TCL_OK) {
		return TCL_ERROR;
	}

	if (cdPtr->object_magic != SOCKETSERVER_OBJECT_MAGIC) {
		Tcl_AddErrorInfo(interp, "Incorrect magic value on internal state");
		return TCL_ERROR;
	}

	switch ((enum options) optIndex) {
		case OPT_SERVER:

			if (objc != 3) {
				Tcl_WrongNumArgs (interp, 1, objv, "server port | client [-port N] handlerProc");
				return TCL_ERROR;
			}

			/* parse the port number argument */
			if (Tcl_GetIntFromObj(interp, objv[2], &port)) {
				Tcl_AddErrorInfo(interp, "problem getting port number as integer");
				return TCL_ERROR;
			}

			Tcl_MutexLock(&threadMutex);
			data = socketserver_getPort(cdPtr, port, 1);

			/* If we do not have a socket pair create it */
			if (data->targs.in == -1) {
				int sock[2];
				pthread_t tid;

				if (socketpair(PF_UNIX, SOCK_STREAM, 0, sock)) {
					Tcl_AddErrorInfo(interp, "Failed to create thread to read socketpipe");
					Tcl_MutexUnlock(&threadMutex);
					return TCL_ERROR;
				}
				data->targs.in = sock[0];
				data->out = sock[1];

				/* Create a background thread to call accept and send the fd to the socketpair. */
				if (pthread_create(&tid, NULL, socketserver_thread, &data->targs) != 0) {
					Tcl_AddErrorInfo(interp, "Failed to create thread to read socketpipe");
					Tcl_MutexUnlock(&threadMutex);
					return TCL_ERROR;
				}
				pthread_detach(tid);
			}
			Tcl_MutexUnlock(&threadMutex);
			break;

		case OPT_CLIENT:
			if (objc == 5) {
				/* parse the port number argument */
				if (Tcl_GetIntFromObj(interp, objv[3], &port)) {
					Tcl_AddErrorInfo(interp, "problem getting port number as integer");
					return TCL_ERROR;
				}
				callback = Tcl_GetString(objv[4]);
			} else if (objc == 3) {
				port = 0;
				callback = Tcl_GetString(objv[2]);
			} else {
				Tcl_WrongNumArgs (interp, 1, objv, "server port | client [-port N] handlerProc");
				return TCL_ERROR;
			}

			if (callback == NULL) {
				Tcl_AddErrorInfo(interp, "problem getting callback proc name");
				return TCL_ERROR;
			}

			Tcl_MutexLock(&threadMutex);
			data = socketserver_getPort(cdPtr, port, 0);
			if (!data) {
				Tcl_AddErrorInfo(interp, "Could not find socketserver structure for port");
				Tcl_MutexUnlock(&threadMutex);
				return TCL_ERROR;
			}
			data->interp = interp;
			data->threadId = Tcl_GetCurrentThread();
			data->callback = callback;
			/* Bytes needed for callback script. Command plus sockXXXXXXXX */
			data->scriptLen = strlen(data->callback) + HOST_AND_PORT_LEN * 2;
			/* When the client end of the socketpair is readable, then
			 * create an event to consume the fd.
			 */
			if (!data->have_channel) {
				data->channel = Tcl_MakeFileChannel((void *)((long)data->out), TCL_READABLE);
				data->have_channel = 0;
				Tcl_CreateChannelHandler(data->channel, TCL_READABLE, socketserver_readable, (void *)data);
			}
			/* Allow a readable event to process a message */
			data->active = 1;
			Tcl_MutexUnlock(&threadMutex);
			/* Because the socket is no blocking, we can attempt to queue an event right away. */
			socketserver_readable(data, 0);
			break;

		default:
			Tcl_AddErrorInfo(interp, "Unexpected command option");
			return TCL_ERROR;    
	}

	return TCL_OK;
}

/* vim: set ts=4 sw=4 sts=4 noet : */
