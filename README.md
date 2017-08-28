socketserver, a Tcl extension for socket serving by socket passing and SCM_RIGHTS
===

Socketserver provides a Tcl command for creating a socketserver.  A socketserver is a process
which passes accepted TCP connections to a child process over a socket pair.  The socket FD can be passed to
a child process using sendmsg and SCM_RIGHTS.  This is internally implemented using libancillary for the 
file descriptor passing.

```
+--------+     Listening and Accepting
| parent | <-- TCP:8888
+-----+--+
      |
      V  socketpair()
    +----+-----+
    | in | out +--------+
    +----+--+--+        |
            |           |
            V           V
         +-----+     +-----+
         |child|     |child| ...
         +-----+     +-----+

```

A ::socketserver::socket server <port number> opens the listening and accepting TCP socket.  You will do this once before forking children.  The socket accept() call is performed in a background daemon thread. 
Clients will be able to send data immediately on accept. Clients will not receive data until child processes
call ::sockerserver::socket client ?-port <port number>? <handleProc>, Tcl dispatches the events and invokes the handlerProc and the handleProc reads the socket.

A ::socketserver::socket client <handlerProc> must be called to receive a connected TCP socket from the parent process.
This allows the forked process to process single connects serially.
All of the child processes share a single queue implemented as the socketpair() between the parent and child processes.
Multiple forked processes can then handle many connections in "parallel" after they serially recvmsg() the file descriptor.
The child processes should only receive one connection and close the connection before requesting a new connection.

The queue
---------
All of the child processes inherit the file descriptor for the read side of the socket pair.
Therefore, the accepted() file descriptors are processed on a first-come, first-serve by the child processes.
It is not predicatable which child process will receive the accepted file descriptor.

In Tcl program tests/echo_server.tcl implements an example server.  Telnet to port 8888 and your input will be echoed with the
process id of the child process servicing the accepted socket.
If you signal the parent server process with SIGUSR1, more child processes are forked.

Outline of Tcl Code use
----
```
# Create the listening socket and a background C thread to accept connections
::socketserver::socket server 8888

# Fork some children, they will inherit the socketpair created in ::socketserver::socket
# ... in the forked child
proc handle_socket {sock} {
    # ... communicate over the socket
    set line [gets $sock]
    puts $sock $line
    close $sock
    # now we are ready to handle another
    ::socketserver::socket client -port 8888 handle_socket
}

# ... fork child process
if {[fork] == 0} {
  ::socketserver::socket client -port 8888 handle_socket
}

vwait done
```

To build do a standard Tcl extension build.
```
autoreconf
./configure
make
make install
```

Thanks to libancillary which made this code possible by clearly implementing file descriptor passing.
Thanks fo flingfd which provided more cleanups on how to call SCM_RIGHTS.
https://github.com/sharvil/flingfd
