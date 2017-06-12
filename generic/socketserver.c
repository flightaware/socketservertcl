/* -*- mode: c; tab-width: 4; indent-tabs-mode: t -*- */

/*
 * socketserver - Tcl interface to libancillary to create a socketserver
 *
 * Copyright (C) 2017 FlightAware LLC
 *
 * freely redistributable under the Berkeley license
 */

#include <assert.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
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
#endif

#define SPARE_SEND_FDS 1
#define SPARE_RECV_FDS 1

#include "../libancillary/ancillary.h"
#include "../libancillary/fd_recv.c"
#include "../libancillary/fd_send.c"

#include "socketserver.h"

TCL_DECLARE_MUTEX(threadMutex);

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
#endif
}

static void * socketserver_thread(void *args)
{
	int *int_args = (int *)args;
	int sock = int_args[0];
	int socket_desc , client_sock;
	struct sockaddr_in server , client;
	int c = sizeof(struct sockaddr_in);

	// create tcp socket
	socket_desc = socket(AF_INET , SOCK_STREAM , 0);
	if (socket_desc == -1)
	{
		debug("Could not create socket");
	}
	debug("Socket created");

	server.sin_family = AF_INET;
	server.sin_addr.s_addr = INADDR_ANY;
	server.sin_port = htons( int_args[1] );
	if( bind(socket_desc,(struct sockaddr *)&server , sizeof(server)) < 0)
	{
		debug("bind failed");
		return (void *)1;
	}
	debug("bind done");

	listen(socket_desc , SOMAXCONN);

	debug("Waiting for incoming connections...");

	while (1) {
		client_sock = accept(socket_desc, (struct sockaddr *)&client, (socklen_t*)&c);
		if (client_sock < 0 && errno != EINTR)
		{
			// EINTR are ok in accept calls, retry
			debug("accept failed");
		}
		else if (client_sock != -1)
		{
			debug("Connection accepted");
			if (ancil_send_fd(sock, client_sock)) {
				debug("ancil_send_fd failed");
			} else {
				debug("Sent fd.\n");
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
	socketserver_ThreadEvent *evPtr = (socketserver_ThreadEvent *)tcl_event;
	socketserver_objectClientData * data = (socketserver_objectClientData *)evPtr->data;

	/* Check the active flag to see if we ignore this callback */
	Tcl_MutexLock(&threadMutex);
	if (!data->active) {
		Tcl_MutexUnlock(&threadMutex);
		return TCL_OK;
	}
	data->active = 0;
	/* attempt to read and FD from the socketpair. */
	int fd = -1;
	if (ancil_recv_fd(data->out, &fd) || fd == -1) {
		/* receive errors are ok. The socketpair is non-blocking and
		 * interrupts can happen. */
		data->active = 1;
		Tcl_MutexUnlock(&threadMutex);
		return TCL_OK;
	}
	Tcl_MutexUnlock(&threadMutex);

	/* Create a channel from the unix fd. */
	Tcl_Channel channel = Tcl_MakeFileChannel((void *)((long)fd), TCL_READABLE|TCL_WRITABLE);
	Tcl_RegisterChannel(data->interp, channel);

	/* Invoke the callback handler. */
	const char *channel_name = Tcl_GetChannelName(channel);
	if (channel_name == NULL || *channel_name == 0) {
		Tcl_AddErrorInfo(data->interp, "Failed to get channel name for ancil_recv_fd file descriptor.");
		return TCL_ERROR;
	}
	char * script = (char *)ckalloc(data->scriptLen);
	strcpy(script, data->callback);
	strcat(script, " ");
	strcat(script, channel_name);
	int rc = Tcl_Eval(data->interp, script);
	ckfree(script);

	return rc;
}

/*
 * When socket is readable create a Tcl event.
 */
static void socketserver_readable(ClientData client_data, int mask)
{
	socketserver_objectClientData * data = (socketserver_objectClientData *)client_data;

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

int socketserverObjCmd(ClientData clientData, Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[])
{
	socketserver_objectClientData *data = (socketserver_objectClientData *)clientData;
	int optIndex;

	enum options {
		OPT_CLIENT,
		OPT_SERVER
	};
	static CONST char *options[] = { "client", "server" };

	// basic command line processing
	if (objc != 3) {
		Tcl_WrongNumArgs (interp, 1, objv, "server port | client handlerProc");
		return TCL_ERROR;
	}

	// argument must be one of the subOptions defined above
	if (Tcl_GetIndexFromObj (interp, objv[1], options, "option",
							 TCL_EXACT, &optIndex) != TCL_OK) {
		return TCL_ERROR;
	}

	if (data->object_magic != SOCKETSERVER_OBJECT_MAGIC) {
		Tcl_AddErrorInfo(interp, "Incorrect magic value on internal state");
		return TCL_ERROR;
	}

	switch ((enum options) optIndex) {
		case OPT_SERVER:

			/* parse the port number argument */
			if (Tcl_GetIntFromObj(interp, objv[2], &data->threadArgs.port)) {
				Tcl_AddErrorInfo(interp, "problem getting port number as integer");
				return TCL_ERROR;
			}

			/* If we do not have a socket pair create it */
			Tcl_MutexLock(&threadMutex);
			if (data->threadArgs.in == -1) {
				int sock[2];
				pthread_t tid;

				if (socketpair(PF_UNIX, SOCK_STREAM, 0, sock)) {
					Tcl_AddErrorInfo(interp, "Failed to create thread to read socketpipe");
					Tcl_MutexUnlock(&threadMutex);
					return TCL_ERROR;
				}
				data->threadArgs.in = sock[0];
				data->out = sock[1];

				/* Create a background thread to call accept and send the fd to the socketpair. */
				if (pthread_create(&tid, NULL, socketserver_thread, &data->threadArgs) != 0) {
					Tcl_AddErrorInfo(interp, "Failed to create thread to read socketpipe");
					Tcl_MutexUnlock(&threadMutex);
					return TCL_ERROR;
				}
				pthread_detach(tid);
			}
			Tcl_MutexUnlock(&threadMutex);
			break;

		case OPT_CLIENT:
			{
				Tcl_MutexLock(&threadMutex);
				data->interp = interp;
				data->threadId = Tcl_GetCurrentThread();
				data->callback = Tcl_GetString(objv[2]);
				/* Bytes needed for callback script. Command plus sockXXXXXXXX */
				data->scriptLen = strlen(data->callback) + 80;
				/* make the socket non-blocking */
				fcntl(data->out, F_SETFL, fcntl(data->out, F_GETFL, 0) | O_NONBLOCK);
				/* When the client end of the socketpair is readable, then
				 * create an event to consume the fd.
				 */
				if (data->need_channel) {
					data->channel = Tcl_MakeFileChannel((void *)((long)data->out), TCL_READABLE);
					data->need_channel = 0;
					Tcl_CreateChannelHandler(data->channel, TCL_READABLE, socketserver_readable, (void *)data);
				}
				/* Allow a readable event to process a message */
				data->active = 1;
				Tcl_MutexUnlock(&threadMutex);
				/* Because the socket is no blocking, we can attempt to queue an event right away. */
				socketserver_readable(data, 0);
				return TCL_OK;
			}
			break;

		default:
			Tcl_AddErrorInfo(interp, "Unexpected command option");
			return TCL_ERROR;    
	}

	return TCL_OK;
}

/* vim: set ts=4 sw=4 sts=4 noet : */
