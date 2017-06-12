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

typedef struct socketserver_threadArgs {
	// Input and output fd's for pipe
	int in;
	// TCP port number
	int port;
} socketserver_threadArgs;

typedef struct socketserver_objectClientData
{
	int object_magic;
	socketserver_threadArgs threadArgs;
	// Client end of socketpair
	int out; 
	// Handler proc
	const char *callback;
	size_t scriptLen;
	Tcl_Interp *interp;
	Tcl_ThreadId threadId;
	int active;
	int need_channel;
	Tcl_Channel channel;
} socketserver_objectClientData;

typedef struct socketserver_ThreadEvent {
	Tcl_Event event;
	socketserver_objectClientData* data;
} socketserver_ThreadEvent;

#endif

/* vim: set ts=4 sw=4 sts=4 noet : */

