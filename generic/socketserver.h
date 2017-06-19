/* -*- mode: c; tab-width: 4; indent-tabs-mode: t -*- */

/*
 * Include file for sockerserver package
 *
 * Copyright (C) 2017 by FlightAware, All Rights Reserved
 *
 * Freely redistributable under the Berkeley copyright, see license.terms
 * for details.
 */

#ifndef SOCKERSERVER_H
#define SOCKERSERVER_H

#include <tcl.h>
#include <string.h>

extern int
socketserverObjCmd(ClientData clientData, Tcl_Interp *interp, int objc, Tcl_Obj *CONST objvp[]);

#define SOCKETSERVER_OBJECT_MAGIC 71820352

typedef struct socketserver_thread_args {
	int port;
	int in;
} socketserver_thread_args;

typedef struct socketserver_port {
	socketserver_thread_args targs;
	int out; /* Output for socketpair to write FD */
	const char *callback; /* tcl handler proc */
	size_t scriptLen; /* length of callback plus overhead */
	Tcl_Interp *interp;
	Tcl_ThreadId threadId;
	int active; /* process event from socketpair */
	int have_channel; 
	Tcl_Channel channel;
	struct socketserver_port * nextPtr;
} socketserver_port;

typedef struct socketserver_objectClientData
{
	int object_magic;
	// Allocate a structure per port, NULL terminated array
	socketserver_port* ports;
} socketserver_objectClientData;

typedef struct socketserver_ThreadEvent {
	Tcl_Event event;
	socketserver_port* data;
} socketserver_ThreadEvent;

#endif

/* vim: set ts=4 sw=4 sts=4 noet : */

