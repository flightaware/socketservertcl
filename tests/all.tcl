# all.tcl --
#
# This file contains a top-level script to run all of the Tcl
# tests.  Execute it by invoking "source all.test" when running tcltest
# in this directory.
#
# Copyright (c) 1998-1999 by Scriptics Corporation.
# Copyright (c) 2000 by Ajuba Solutions
#
# See the file "license.terms" for information on usage and redistribution
# of this file, and for a DISCLAIMER OF ALL WARRANTIES.

set tcltestVersion [package require tcltest]
namespace import -force tcltest::*

# Hook to determine if any of the tests failed. Then we can exit with
# proper exit code: 0=all passed, 1=one or more failed
proc tcltest::cleanupTestsHook {} {
        variable numTests
        set ::exitCode [expr {$numTests(Failed) > 0}]
}

proc goodtime {} {
	clock format [clock seconds] -format "%Y-%m-%d %T" -gmt 0
}

#puts stdout "\nTests started at [goodtime]"

tcltest::testsDirectory [file dir [info script]]
tcltest::runAllTests

## cleanup
#puts stdout "\nTests ended at [goodtime]"
#::tcltest::cleanupTests 1

if {$exitCode} {
        puts "====== FAIL ====="
        exit $exitCode
} else {
        puts "====== SUCCESS ====="
}
