package require Tclx
#package require socketserver

load "./libsocketserver1.0.1.so"

# Creat the socket
# Start listening and accepting connections in a background thread
::socketserver::socket server 8888

set done 0

proc handle_readable {fd ipaddr port} {
    set line [gets $fd]
    if {[string first "quit" $line] != -1} {
	puts "client closing socket"
    	puts $fd "[pid] $ipaddr $port $line"
	fileevent $fd readable {}
	close $fd
	# Now that we have closed, we are ready for another socket
	::socketserver::socket client -port 8888 handle_accept
    } else {
    	puts $fd "[pid] $ipaddr $port $line"
    }
}

proc handle_accept {fd ipaddr port} {
	puts "received fd=$fd"
	fconfigure $fd -encoding utf-8 -buffering line -blocking 1 -translation lf
	fileevent $fd readable [list handle_readable $fd $ipaddr $port]
}

proc do_client {} {
	puts "child process started"
    # forked child process
	# block until the parent process receives connection
	# and passes the fd
	::socketserver::socket client handle_accept
	vwait done
}

proc make_worker {} {
  set pid [fork]
  if {$pid == 0} {
	do_client
  } elseif {$pid == -1} {
	  puts "fork failed"
  } else {
	  puts "child pid = $pid"
  }
}

signal trap SIGUSR1 [list make_worker]

make_worker

vwait done
