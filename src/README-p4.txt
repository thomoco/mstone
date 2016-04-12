The Perforce version control system benchmarking module (P4).

See ../conf/p4*.wld for related workloads.

The cmake configuration is clunky (to say the least).  First, enable
P4_MODULE and hit 'c'.  It will (mostly likely) give you an error
about 3 P4_ variables being undefined.  Just hit 'e', then edit
P4_CLIENT_LIB to the right path, and hit 'c'.  This will fix the other
paths automatically.

Perforce doesn't mention what compiler was used to build p4api, but
gcc-3.4 or latter seems to work.

This code has only been tested with p4api 2007.2.  Versions 2005.2
through 2007.1 may work, but haven't been tested.  Later versions
should be fine.  Anything earlier than 2005.2 is unlikely to work.

For compatibility with SVN, the logLimit parameter may be positive or
negative.  However, the order is always backward in time (sign is
ignored).


Known bugs:

There is a bug in the p4api headers that causes gcc to generate this
warning: "p4api-2007.2.122958/include/p4/keepalive.h:24: warning:
'class KeepAlive' has virtual functions but non-virtual destructor".
This can be ignored.
