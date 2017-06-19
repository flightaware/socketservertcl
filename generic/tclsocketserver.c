/*
 * socketserver_Init and socketserver_SafeInit
 *
 * Copyright (C) 2016 - 2017 FlightAware
 *
 * Freely redistributable under the Berkeley copyright.  See license.terms
 * for details.
 */

#include <tcl.h>
#include "socketserver.h"

#undef TCL_STORAGE_CLASS
#define TCL_STORAGE_CLASS DLLEXPORT

static void socketserver_CmdDeleteProc(ClientData clientData)
{
	if (clientData != NULL) {
		socketserver_objectClientData *cdPtr = (socketserver_objectClientData *)clientData;
		if (cdPtr->ports != NULL) {
			socketserver_port *p = cdPtr->ports;
			while (p != NULL) {
				socketserver_port *prev = p;
				p = p->nextPtr;
				ckfree(prev);
			}
		}
		ckfree(clientData);
	}
}


/*
 *----------------------------------------------------------------------
 *
 * socketserver_Init --
 *
 *	Initialize the socketserver extension.  The string "socketserver"
 *      in the function name must match the PACKAGE declaration at the top of
 *	configure.in.
 *
 * Results:
 *	A standard Tcl result
 *
 * Side effects:
 *	One new command "::socketserver::socket" is added to the Tcl interpreter.
 *
 *----------------------------------------------------------------------
 */

EXTERN int
Socketserver_Init(Tcl_Interp *interp)
{
	Tcl_Namespace *namespace;
	/*
	 * This may work with 8.0, but we are using strictly stubs here,
	 * which requires 8.1.
	 */
	if (Tcl_InitStubs(interp, "8.1", 0) == NULL) {
		return TCL_ERROR;
	}

	if (Tcl_PkgRequire(interp, "Tcl", "8.1", 0) == NULL) {
		return TCL_ERROR;
	}

	if (Tcl_PkgProvide(interp, PACKAGE_NAME, PACKAGE_VERSION) != TCL_OK) {
		return TCL_ERROR;
	}

	namespace = Tcl_CreateNamespace (interp, "::socketserver", NULL, NULL);
	socketserver_objectClientData *data = (socketserver_objectClientData *)ckalloc(sizeof(socketserver_objectClientData));

	if (data == NULL) {
		return TCL_ERROR;
	}

	data->object_magic = SOCKETSERVER_OBJECT_MAGIC;
	data->ports = NULL;

	/* Create the create command  */
	Tcl_CreateObjCommand(interp, "::socketserver::socket", (Tcl_ObjCmdProc *) socketserverObjCmd, 
						 (ClientData)data, (Tcl_CmdDeleteProc *)socketserver_CmdDeleteProc);

	Tcl_Export (interp, namespace, "*", 0);

	return TCL_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * socketserver_SafeInit --
 *
 *	Initialize socketservertcl in a safe interpreter.
 *
 * Results:
 *	A standard Tcl result
 *
 *----------------------------------------------------------------------
 */

EXTERN int
Socketserver_SafeInit(Tcl_Interp *interp)
{
	/*
	 * can this work safely?  I don't know...
	 */
	return TCL_ERROR;
}

/* vim: set ts=4 sw=4 sts=4 noet : */
