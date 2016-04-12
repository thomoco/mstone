
Fundamental data structures:

event_timer_t
	Holds the critical results information.  This gets added up
over all the block in all the threads periodically during the test.

cmd_stats_t
	This holds all the dynamic information including event timers,
and state for sequencing the command block (like next login).

mail_command_t
	This hold all the read only information about a command block.

thread_context_t
	All the information about each thread.  There is one thread
per server connection.

range_t
	Integer range based on a starting number and a length.  This
can be sequentially or randomly returned.  This needs to be adjusted
for each command thread.

protocol_t
	This defines the callbacks and shared information for each protocol.



Adding a new protocol:
	Create all the handler functions.  Look at smtp.c for an example

	Add the entry to RegisterKnownProtocols in main.c

	Add it to CMakeLists.txt


Design issues:
	Timing accuracy

	Scalability

	Extendibility

	Memory use (limits scalability)

	Result data efficiency

