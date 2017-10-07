package require Tclx
package require socketserver

# create two ports
::socketserver::socket server 7701
::socketserver::socket server 7702

set ::port 7701

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

proc do_client {port} {
	puts "child process started"
    # forked child process
	# block until the parent process receives connection
	# and passes the fd
	set ::port $port
	::socketserver::socket client -port $port handle_accept
	vwait done
}

proc make_worker {port} {
  set pid [fork]
  if {$pid == 0} {
	do_client $port
  } elseif {$pid == -1} {
	  puts "fork failed"
  } else {
	  puts "child pid = $pid"
  }
}

signal trap SIGUSR1 [list make_worker 7701]
signal trap SIGUSR2 [list make_worker 7702]

make_worker 7701
make_worker 7702

vwait done
