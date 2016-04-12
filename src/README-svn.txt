The Subversion version control system benchmarking module (SVN).

See ../conf/svn*.wld for related workloads.

The logLimit parameter is a bit non-obvious.  It will retrieve up to N
entries, but the direction that it gets the entries is determined by
the sign of the parameter.  Negative starts with the most recent entry
and goes backward in time (this is cheap for subversion).  Positive
goes forward in time (this is harder for subversion to compute).


Known bugs:

I had problems when building the svn module with gcc-4.0.3.  As far as
I can tell, gcc is generating bad code (at least when optimized).
What happens is that it works with a local variable in a register and
then never pushes it back onto the stack.  It then makes a call many
lines later and pushes the old value from the stack.  gcc-3.4 and
gcc-2.95 work fine.  Note: in order to switch compilers, "make clean",
"rm CMakeCache.txt src/CMakeCache.txt", then "CC=gcc-3.4 CFLAGS='-g
-O' ccmake CmakeLists.txt".

The library paths are embedded in mailclient using rpath.  This means
that the proper subversion, neon, serf, and openssl libraries must
either show up in the same place on all client machines (probably via
a NFS mount) or come from the system libraries.

Cmake doesn't set rpath for mailclient until it is installed.  This
means you can't copy mailclient directly from the build tree (only
developers will care).

If you test DAV protocols (http or https), make sure neon and/or serf
were build against the same version of APR (either 0.9* or 1.*) as
subversion.  If testing http, then neon must be configured with
"--with-ssl=openssl --enable-threadsafe-ssl=posix".  You can check
this by doing: neon-config --support ts_ssl && echo 'neon has
threadsafe ssl'

Events (-e) may trigger a bug with SVN.  It runs OK, but doesn't end
properly.  The event system isn't much of an advantage for this
protocol.

Sometimes it dies at shutdown while trying to free memory pools.  This
appears to be a APR or subversion bug, but tracking it down has proven
difficult.

If you use valgrind with DAV repository access, then you need a build
of openssl that has PURIFY set.  This removes an (intentionally) unset
data source from the random number generator.
