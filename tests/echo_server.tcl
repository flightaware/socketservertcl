package require Tclx
package require socketserver

# Creat the socket
# Start listening and accepting connections in a background thread
::socketserver::socket server 8888

set done 0

proc handle_accept {fd} {
	puts "received fd=$fd"
	fconfigure $fd -encoding utf-8 -buffering line -blocking 1 -translation lf
	while {1} {
	    set line [gets $fd]
	    if {[string first "quit" $line] != -1} {
		    break
	    }
	    puts $fd "[pid] $line"
	}
	puts "client closing socket"
	close $fd
	# Now that we have closed, we are ready for another socket
	::socketserver::socket client handle_accept
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
