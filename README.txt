
                    MSTONE 4.9.4

Mstone is a revision control system and mail server performance
testing tool.  It is designed to simulate the different types and
volume of load a server would experience.  Mstone was formerly known
as Mailstone.

A quick installation guide is available in INSTALL.txt.  If you are
building from source, see Building.txt.

The full mstone user manual is available in doc/mstone.odt

See src/README-svn.txt for notes about the subversion module.

Testing strategy
----------------

Mstone is capable of opening SMTP, POP3, IMAP4, and other protocol
connections to mail servers.  The number and type of connections made
to the mail server is based on a weighted command list which provides
the ability to test mail server implementation requirements.

A series of perl script allow you to setup client machines, run tests,
and then cleanup client machine files.  Each client machine has a copy
of the mailclient program and SMTP message files.  When the test is
run, the mailclient is started with the proper command line and work load.

After experimenting with mstone loads, you will notice that there
are a few factors that can distort server the byte and message
throughput.  You will find that the server byte throughput is related
to the average SMTP message (file) size.  Also, server throughput, in
bytes and messages, is affected by larger than normal POP3/IMAP4
mailboxes.  So it is important to approach the mstone command
configuration with data collected from existing mail server
implementations, for example, a customer might say "during our peak
activity in the morning, we handle up to two thousand employees
sending an average of 5 messages of 20K average size and receiving 25
messages of same size". With input like this, you can begin tuning
mstone to generate relevant data.

There are two important things to consider when reviewing the results of
mstone performance analysis:  Was the test run on target for
simulating the type and volume of mail traffic; and did the server, both
software and machine, handle the load within an acceptable margin?

With this information, it can be determined: whether enough SMTP
connections were made to the server during the run, and how many
messages were downloaded over how many POP3/IMAP4 connections.  If the
number of SMTP connections is not in the acceptable range, then
consider adding more client processes/machines or checking the server
performance during the run.  The message/connection ratio for
POP3/IMAP4 should be checked for soundness, and adjustments should be
made to the mailboxes before running the next test.

Monitoring the server performance during test runs is crucial in
understanding the results.  If the number of client connections is not
being achieved and the server cpu usage and process run queue is not
settling down after the initial spike, then modifications to the server
architecture could be in order.

The analysis of mstone results is an iterative process of client
(mstone client) and server tuning.  The bottom line is to determine
whether the messaging solution can handle the type of load expected in
an acceptable manner.


Server Log Tuning
-----------------
The Messaging and Directory server ship with access logging enabled by
default.  This gives the most information about what is going on in
the system, but can reduce performance.  You should test the way the
system will be run.  

Noticeable performance increases are often obtained by disabling access
logging on the directory server and by reducing the logging level of
the messaging servers from "Notice" to "Warning".
