/* -*- Mode: C; c-file-style: "stroustrup"; comment-column: 40 -*- */
/*
 * The contents of this file are subject to the Netscape Public
 * License Version 1.1 (the "License"); you may not use this file
 * except in compliance with the License. You may obtain a copy of
 * the License at http://www.mozilla.org/NPL/
 *
 * Software distributed under the License is distributed on an "AS
 * IS" basis, WITHOUT WARRANTY OF ANY KIND, either express or
 * implied. See the License for the specific language governing
 * rights and limitations under the License.
 *
 * The Original Code is the Netscape Mailstone utility,
 * released March 17, 2000.
 *
 * The Initial Developer of the Original Code is Netscape
 * Communications Corporation. Portions created by Netscape are
 * Copyright (C) 1999-2000 Netscape Communications Corporation. All
 * Rights Reserved.
 *
 * Contributor(s):	Dan Christian <robodan@netscape.com>
 *			Marcel DePaolis <marcel@netcape.com>
 *			Sean O'Rourke <sean@sendmail.com>
 *
 * Alternatively, the contents of this file may be used under the
 * terms of the GNU Public License Version 2 or later (the "GPL"), in
 * which case the provisions of the GPL are applicable instead of
 * those above.  If you wish to allow use of your version of this file
 * only under the terms of the GPL and not to allow others to use your
 * version of this file under the NPL, indicate your decision by
 * deleting the provisions above and replace them with the notice and
 * other provisions required by the GPL.  If you do not delete the
 * provisions above, a recipient may use your version of this file
 * under either the NPL or the GPL.
 */
/*
  main.c handles all the initialization and then forks off sub processes.
*/

#include "bench.h"
#include "xalloc.h"

/* really globalize variables */
volatile int gf_timeexpired = 0;
time_t	gt_testtime = 0;		/* time of test, in seconds */
time_t	gt_startedtime = 0;		/* when we started */
volatile time_t	gt_shutdowntime = 0;	/* startedtime + testtime */
time_t	gt_stopinterval = 0;		/* MAX (ramptime/5, 10) */
time_t	gt_aborttime = 0;		/* startedtime + testtime + ramptime */

#ifdef USE_EVENTS
int	gf_use_events = 0;
#endif
int	gn_record_telemetry = 0;
int     gn_total_weight = 0;
int	gn_maxerrorcnt = 0;
int	gn_maxBlockCnt = 0;
int	gn_numprocs = 0;
int	gn_numthreads = 0;
int     gn_debug = 0;
int	gn_feedback_secs = 5;
int	gf_abortive_close = 0;
int	gn_number_of_commands = 0;
int	gf_imapForceUniqueness = 0;
long	gn_randomSeed = 0;
char	gs_dateStamp[DATESTAMP_LEN];
char	gs_thishostname[MAXHOSTNAMELEN+10] = "";
char	gs_shorthostname[MAXHOSTNAMELEN+10] = "";
char	*gs_parsename = gs_thishostname; /* name used during parsing */
pid_t 	gn_myPID = 0;
mail_command_t *g_loaded_comm_list;	/* actually a dynamic array */
protocol_t	*g_protocols;      /* array of protocol information */
int g_protocol_size = 0;                /* size of g_protocols */

/* End of globals */

static int      help_and_exit = 0;     /* print help and exit */
static time_t	ramptime = 0;
static float    wait_start = 0; /* initial wait in seconds (<0 -> randomize) */

static char	mailmaster[MAXHOSTNAMELEN];
static NETPORT	listenport = 0;
static char	*commandsfilename = NULL;
static int	f_usestdin = 0;
#if 0
static struct hostent 	mailserv_phe, mailmast_phe;
static struct protoent	mailserv_ppe, mailmast_ppe;
static unsigned long	mailserv_addr, mailmast_addr;
static short 		mailserv_type, mailmast_type; /* socket type */
#endif

static void
usage(const char *progname)
{
    fprintf(stderr, "Usage: %s [options] -n <clients> -t <time> <-s | -u <commandFile>>\n\n",
			progname);
    fprintf(stderr, "  required parameters:\n");
    fprintf(stderr, "    -n numProcs      number of clients processes to run\n");
    fprintf(stderr, "    -t testTime      test duration (seconds or #[hmsd])\n");
    fprintf(stderr, "    -s               use stdin for commands\n");
    fprintf(stderr, "    -u commandFile   file for commands\n\n");

    fprintf(stderr, "  options:\n");
    fprintf(stderr, "    -N numThreads    number of clients threads per process\n");
    fprintf(stderr, "    -R rampTime      test rampup time (seconds or #[hmsd])\n");
    fprintf(stderr, "    -w waitTime      start wait time (float secs, randomize if negative)\n");
    fprintf(stderr, "    -f sumPerSec     time between summary results (seconds)\n");
    fprintf(stderr, "    -m maxErrorCnt   threshold to force test abort\n");
    fprintf(stderr, "    -M maxBlockCnt   number of blocks to run\n");
    fprintf(stderr, "    -H name          hostname for parsing HOSTS=\n");
    fprintf(stderr, "    -S num           Seed for random number generator\n");
    fprintf(stderr, "    -A               use abortive close\n");
#ifdef USE_EVENTS
    fprintf(stderr, "    -e               use event-queue model\n");
#endif
    fprintf(stderr, "    -D dateStamp     assign a datestamp\n");
    fprintf(stderr, "    -d               debug\n");
    fprintf(stderr, "    -r               record all transactions\n");
    fprintf(stderr, "    -h               help - this message\n\n");
    fprintf(stderr, "    -v               print version\n");

    fprintf(stderr, "\n");
    fprintf(stderr, "  The following protocols are available:\n");
    {
        protocol_t	*pp;
        for (pp=g_protocols; pp < (g_protocols + g_protocol_size); ++pp) {
            fprintf(stderr, "    %s\n", pp->name);
        }
    }
    fprintf(stderr, "\n");

    exit(1);
} /* END usage() */

void
parseArgs(int argc, char **argv)
{
    int		getoptch;
    extern char	*optarg;
    extern int	optind;

    /*
     * PARSE THE COMMAND LINE OPTIONS
     */

    while((getoptch =
	getopt(argc,argv,"hf:H:t:u:c:m:M:n:N:R:D:S:w:Adrsve")) != EOF)
    {
        switch(getoptch)
        {
	case 'e':
#ifdef USE_EVENTS
	    gf_use_events = 1;
#else
            fprintf(stderr, "Warning: events disabled.  Ignoring -e\n");
#endif
	    break;
	case 'h':
            help_and_exit++;
	    break;
	case 'H':			/* name for parsing */
	    gs_parsename = xstrdup(optarg);
	    break;
	case 'A':
	    gf_abortive_close = 1;
	    break;
	case 'f':
	    /* feedback frequency in seconds */
	    gn_feedback_secs = atoi(optarg);
	    break;
	case 'd':
            gn_debug = 1;
            break;
	case 'D':
	    strcpy(gs_dateStamp, optarg);
	    break;
	case 'm':
            gn_maxerrorcnt = atoi(optarg);
            break;
	case 'M':
            gn_maxBlockCnt = atoi(optarg);
            break;
	case 'n':
            gn_numprocs = atoi(optarg);
            break;
	case 'N':
	    gn_numthreads = atoi(optarg);
	    break;
	case 'u':
	    commandsfilename = xstrdup(optarg);
	    break;
	case 's':
	    f_usestdin = 1;
	    break;
	case 'S':
	    /* Randome seed */
	    gn_randomSeed = atol(optarg);
	    break;
	case 't':
	    gt_testtime = time_atoi(optarg);
	    break;
	case 'R':
	    ramptime = time_atoi(optarg);
	    break;
	case 'w':
	    wait_start = atof(optarg);
	    break;
	case 'v':
	    fprintf(stderr, "Netscape " MAILCLIENT_VERSION "\n");
	    fprintf(stderr,
                    "Copyright (c) Netscape Communications Corporation 1997-2000\n");
	    exit(1);
	    break;
	case 'r':
	    gn_record_telemetry = 1;
	    break;
        default:
            fprintf(stderr, "Error: Unknown argument '%c'\n", getoptch);
            usage(argv[0]);
        }
    }
}

/* Find a protocol in the global protocol list */
protocol_t *
protocol_get (char *name)
{
    protocol_t	*pp;

    for (pp=g_protocols; pp < (g_protocols + g_protocol_size); ++pp) {
	/* we only check the length of the registered protocol name */
	/* that way NAME can have additonal stuff after it */
	/* This is much better than the old single letter checking */
	if (0 == strnicmp (name, pp->name, strlen (pp->name)))
	    return pp;
    }
    return NULL;
}

void
ProtocolRegister(
    const char *	name,		/* text name */
    parseStartPtr_t	parseStart,	/* section start parse routine */
    parseEndPtr_t	parseEnd,	/* section end parse routine */

    commStartPtr_t	cmdStart,	/* command start routine */
    commLoopPtr_t	cmdLoop,	/* command loop routine */
    commEndPtr_t	cmdEnd,		/* command end routine */

    statsFormatPtr_t	statsFormat,	/* output format information */
    statsInitPtr_t	statsInit,	/* init and zero stats structure */
    statsUpdatePtr_t	statsUpdate,	/* sum commands */
    statsOutputPtr_t	statsOutput,	/* output stats */
    parseFreePtr_t	parseFree,	/* free command memory */
    protocolFreePtr_t	protoFree)	/* free memory */
{
    protocol_t *newproto;
    if (!g_protocol_size) {
        g_protocols = xcalloc(sizeof(protocol_t));
    } else {
        g_protocols = xrealloc(g_protocols,
                               (1+g_protocol_size) * sizeof(protocol_t));
    }
    newproto = g_protocols + g_protocol_size++;

    newproto->name = name;
    newproto->parseStart = parseStart;
    newproto->parseEnd = parseEnd;

    newproto->cmdStart = cmdStart;
    newproto->cmdLoop = cmdLoop;
    newproto->cmdEnd = cmdEnd;

    newproto->statsFormat = statsFormat;
    newproto->statsInit = statsInit;
    newproto->statsUpdate = statsUpdate;
    newproto->statsOutput = statsOutput;

    newproto->parseFree = parseFree;
    newproto->protoFree = protoFree;

    newproto->cmdCount = 0;
    memset(&newproto->stats, 0, sizeof(newproto->stats));
}

static void
ProtocolFree(void)
{
    protocol_t	*pp;

    if (!g_protocols)
        return;
    //call clean up for each module.
    for (pp=g_protocols; pp < (g_protocols + g_protocol_size); ++pp) {
        if (pp->protoFree)
            (pp->protoFree)();
    }
    g_protocol_size = 0;
    xfree(g_protocols);
    g_protocols = NULL;
}

/* Register compiled in modules */
static void
RegisterKnownProtocols(void)
{
    extern void HttpRegister(void);
    HttpRegister();
    extern void Imap4Register(void);
    Imap4Register();
    extern void MPopRegister(void);
    MPopRegister();
    extern void Pop3Register(void);
    Pop3Register();
    extern void SmtpRegister(void);
    SmtpRegister();
    extern void WebmailRegister(void);
    WebmailRegister();
    extern void WmapRegister(void);
    WmapRegister();
    extern void FrcsRegister(void);
    FrcsRegister();
#ifdef SVN_MODULE
    extern void SvnRegister(void);
    SvnRegister();
#endif /* SVN_MODULE */
#ifdef P4_MODULE
    extern void P4Register(void);
    P4Register();
#endif /* P4_MODULE */
}

#define SIZEOF_NTOABUF  ((3+1)*4)

char *
safe_inet_ntoa(struct in_addr ina, char *psz, int len)
{
  snprintf(psz, len, "%d.%d.%d.%d",
	  ((unsigned char *)&ina.s_addr)[0],
	  ((unsigned char *)&ina.s_addr)[1],
	  ((unsigned char *)&ina.s_addr)[2],
	  ((unsigned char *)&ina.s_addr)[3]);
  return psz;
}

/* look up the host name and protocol
 * called from each protocol (via connectSocket)
 */

int
resolve_addrs(char *host,
	      char *protocol,
	      struct hostent *host_phe,
	      struct protoent *proto_ppe,
	      unsigned long *addr,
	      short *type)
{
    struct hostent *phe;
    struct protoent *ppe;
#ifdef USE_PTHREADS
#ifdef USE_GETHOSTBYNAME_R
    struct hostent he;
    struct protoent pe;
    char buf[512];
    int h_err;
#endif
#endif

    /* if IP address given, convert to internal form */
    if (host[0] >= '0' && host[0] <= '9') {
	*addr = inet_addr(host);
	if (*addr == INADDR_NONE)
	    return(returnerr(stderr,"Invalid IP address %s\n", host));

    } else {
	/* look up by name */
#ifdef USE_GETHOSTBYNAME_R
	phe = gethostbyname_r(host, &he, buf, sizeof(buf), &h_err);
	errno = h_err;
#else
	phe = gethostbyname(host);
#endif

	if (phe == NULL) {
	    D_PRINTF(stderr, "Gethostbyname failed: %s", neterrstr() );
	    return(returnerr(stderr, "Can't get %s host entry\n", host));
	}
	memcpy(host_phe, phe, sizeof(struct hostent));
	memcpy((char *)addr, phe->h_addr, sizeof(*addr));
    }

    /* Map protocol name to protocol number */
#ifdef USE_GETPROTOBYNAME_R
    ppe = getprotobyname_r(protocol, &pe, buf, sizeof(buf));
#else
    ppe = getprotobyname(protocol);
#endif

    if (ppe == 0) {
	D_PRINTF(stderr, "protobyname returned %d\n",	ppe );
	return(returnerr(stderr, "Can't get %s protocol entry\n", protocol));
    }
    memcpy(proto_ppe, ppe, sizeof(struct protoent));

    /* Use protocol to choose a socket type */
    if (strcmp(protocol,"udp") == 0)
	*type = SOCK_DGRAM;
    else
	*type = SOCK_STREAM;

    return 0;
}

/* connect to a socket given the hostname and protocol */

/*
**   XXX Sean O'Rourke: Note that this is used both for client connections to
**   the mailhost and for the communication channels between the
**   parent mailstone and its children.  But it calls sock_open() at
**   the end, and returns a SOCKET (e.g. with possible linespeed, SSL,
**   etc.).  Just extract the fd (with SOCK_FD()) for non-simulation use.
*/

SOCKET
connectSocket(ptcx_t ptcx,
	    resolved_addr_t *hostInfo,
	    char *protocol)
{
    struct sockaddr_in sin;  	/* an Internet endpoint address */
    _SOCKET 	s;              /* socket descriptor */
    int 		type;           /* socket type */
    short 	proto;
    int 		returnval;	/* temporary return value */
    char		ntoa_buf[SIZEOF_NTOABUF];

    D_PRINTF(debugfile, "Beginning connectSocket; host=%s port=%d proto=%s\n",
	     hostInfo->hostName, hostInfo->portNum, protocol );

    sin.sin_family = AF_INET;
    memset((char *)&sin, 0, sizeof(sin));

    sin.sin_port = htons(hostInfo->portNum);

    /* check if we've resolved this already */
    if ((hostInfo) && (hostInfo->resolved)) {
	sin.sin_addr.S_ADDR = hostInfo->host_addr;
	sin.sin_family = PF_INET;
	proto = hostInfo->host_ppe.p_proto;
	type = hostInfo->host_type;
    } else {
	struct hostent 	host_phe;
	struct protoent	host_ppe;
	unsigned long	host_addr;
	short 		host_type;       /* socket type */

	if (resolve_addrs(hostInfo->hostName, "tcp",
			  &host_phe, &host_ppe, &host_addr, &host_type)) {
	    (void) returnerr(debugfile,"Can't resolve hostname %s in get()\n",
			     hostInfo->hostName);
	    return BADSOCKET_VALUE;
	}
	sin.sin_addr.S_ADDR = host_addr;
	sin.sin_family = PF_INET;
	proto = host_ppe.p_proto;
	type = host_type;
    }

    /* Allocate a socket */
    s = socket(PF_INET, type, proto);
    if (_BADSOCKET(s)) {
	int save_errno = errno;
	D_PRINTF(debugfile, "Can't create socket: %s\n",neterrstr() );
	_NETCLOSE(s);
	errno = save_errno;
	return BADSOCKET_VALUE;
    }

    /* Connect the socket */
    D_PRINTF(debugfile, "Connecting %d to %s:%d\n",
	     s, safe_inet_ntoa(sin.sin_addr, ntoa_buf, sizeof(ntoa_buf)),
             ntohs(sin.sin_port));
    returnval = connect(s, (struct sockaddr *)&sin, sizeof(sin));

    if (returnval < 0) {
	int err = GET_ERROR;  /* preserve the error code */

	D_PRINTF(debugfile, "Can't connect: %s\n", neterrstr() );
	_NETCLOSE(s);

	SET_ERROR(err);
	return BADSOCKET_VALUE;
    }

    return sock_open(s);
} /* END connectSocket() */

int
set_abortive_close(SOCKET sock)
{
    struct linger linger_opt;
    int _sock = SOCK_FD(sock);
    linger_opt.l_onoff = 1;
    linger_opt.l_linger = 0;

    if (setsockopt(_sock, SOL_SOCKET, SO_LINGER,
		   (char *) &linger_opt, sizeof(linger_opt)) < 0) {
	returnerr(stderr, "Couldn't set SO_LINGER = 0\n");
	return -1;
    }
    return 0;
}

void
initializeCommands(char *cfilename)
{
    char *cbuf = NULL;
    int cbuflen = 0;
    int cbufalloced = 0;
    int ret;
    FILE *cfile;

    D_PRINTF(stderr, "initializeCommands(%s)\n",
	     cfilename  ? cfilename : "STDIN");

    if (cfilename == NULL || strlen(cfilename) == 0) {
	cfile = stdin;
	cfilename = "stdin";
    } else {
	if ((cfile = fopen(cfilename, "r")) == NULL) {
	    D_PRINTF(stderr, "Cannot open commands file %s: errno=%d: %s\n",
		     cfilename, errno, strerror(errno));
	    errexit(stderr, "Cannot open commands file %s: errno=%d: %s\n",
		    cfilename, errno, strerror(errno));
	}
    }

#define CMDBUF_INCR 4000         /* grow buffer in chunks this size */

    while (!feof(cfile)) {		/* read file in to char array */
        int freeSpace = cbufalloced - cbuflen - 1;
	if (freeSpace < 128) {          /* grow buffer */
	    cbufalloced += CMDBUF_INCR;
	    cbuf = (char *)xrealloc(cbuf, cbufalloced);
	    freeSpace += CMDBUF_INCR;
	    cbuf[cbuflen] = '\0';
	}
	ret = fread(cbuf+cbuflen, 1, freeSpace, cfile);
	if (ret < 0) {
	    D_PRINTF(stderr, "Error reading commands file %s: errno=%d: %s\n",
		     cfilename, errno, strerror(errno));
	    errexit(stderr, "Error reading commands file %s: errno=%d: %s\n",
		    cfilename, errno, strerror(errno));
	}
	if (ret == 0)
	    break;
	cbuflen += ret;
	cbuf[cbuflen] = '\0';
    }

    if (cfile != stdin)
	fclose(cfile);

    /* Read commands into structure, make sure we have all req arguments */
    if ((gn_total_weight = load_commands(cbuf)) < 0) {
	D_PRINTF(stderr, "could not load %s\n", cfilename);
	errexit(stderr, "Could not load command file\n");
    }
    if (0 == gn_total_weight) {
	D_PRINTF(stderr, "No commands found for this host in %s\n", cfilename);
	errexit(stderr, "No command for current host in command file\n");
    }
    xfree(cbuf);
}

int
readwriteStream(int ii, int fdin, int fdout)
{
    char buf[MAX (8192, 2*SIZEOF_SUMMARYTEXT+1)], *cp;
    int res;
    int toread = sizeof(buf)-1;
    int towrite;

    D_PRINTF(stderr, "readwriteStream(%d,%d,%d)\n", ii, fdin, fdout);

    /* structured as a while() for future nonblocking style */
    while (toread) {
	errno = 0;
	res = _NETREAD(fdin, buf, sizeof(buf)-1);
	D_PRINTF(stderr, "read %d bytes from client %d\n", res, ii);
	if (res == 0) {
	    return -1; /* EOF unless O_NDELAY */
	} else if (res < 0) {
	    if (errno == EAGAIN || errno == EINTR)
		return 0; /* go back to the poll loop to service others */

	    fprintf(stderr,
                    "readwriteStream(%d,%d,%d) error reading: errno=%d: %s\n",
		    ii, fdin, fdout, errno, strerror(errno));
	    return -1;
	} else {
	    /* TODO: ...can do more data reduction here... */

	    toread -= res;

	    cp = buf;
	    towrite = res;
	    buf[towrite] = '\0';
	    D_PRINTF(stderr, "writing %d bytes to %d\n", towrite, fdout);
	    while (towrite) {
		res = write(fdout, cp, towrite);
		D_PRINTF(stderr, "wrote %d bytes to %d\n", res, fdout);
		if (res <= 0) {
		    D_PRINTF(stderr,
                             "error writing to %d: errno=%d: %s\n",
                             fdout, errno, strerror(errno));
		} else {
		    towrite -= res;
		    cp += res;
		}
	    }
	}
	toread = 0; /* just read one chunk at a time for now... */
    }
    return 0;
}


/* This is where the master process merges output and waits for
   the spawned processes to exit.
   Note that no alarm has been set.  We never wait very long in poll.
*/
int
readwriteChildren(pccx_t pccxs)
{
    struct pollfd *pfds;
    int nfds;
    int ii;

    /*
     * Wait for all children to exit.
     */
    nfds=0;
    pfds = (struct pollfd *)xcalloc(sizeof(struct pollfd)*gn_numprocs);
    for (ii=0; ii < gn_numprocs; ++ii) {
	if (pccxs[ii].socket != _BADSOCKET_VALUE) {
	    pfds[nfds].fd = pccxs[ii].socket;
	    ++nfds;
	}
    }

    fflush(stdout);

    while (nfds > 0) {
	int ret, closethisfd;
	if (time(0L) >=  gt_aborttime) {
	    D_PRINTF (stderr, "Time is up.  Signalling exit %d\n",
		      gf_timeexpired);
	    gf_timeexpired = EXIT_FASTEST; /* signal clean up and exit */
	    break;			/* just get out of here */
	}
	for (ii=0; ii < nfds; ++ii) {
	    pfds[ii].events = POLLIN;
	    pfds[ii].revents = 0;
	}

	ret = poll(pfds, nfds, 5*1000);
	D_PRINTF(stderr, "back from poll, nfds = %d, ret=%d\n",
		 nfds, ret);

	if (ret == 0)
	    continue;

	if (ret < 0) {
	    if (errno == EAGAIN || errno == EINTR)
		continue;

	    fprintf(stderr,
                    "poll error: errno=%d: %s\n", errno, strerror(errno));
	    break;
	}

	for (ii = 0; ii < nfds; ++ii) {
	    closethisfd = 0;

	    if (pfds[ii].revents) {
		D_PRINTF(stderr,
                         "poll says stdout fd=%d for client=%d is 0x%02x\n",
			 pfds[ii].fd, ii, pfds[ii].revents);
	    }

	    if (pfds[ii].revents & POLLIN) {
		if (readwriteStream(ii, pfds[ii].fd, 1) == -1)
		    closethisfd = 1;
	    } else if (pfds[ii].revents & (POLLHUP | POLLERR | POLLNVAL)) {
		if (pfds[ii].revents & POLLHUP)
		    D_PRINTF(stderr,
                             "POLLHUP for stdout fd=%d for client=%d!\n",
			    pfds[ii].fd, ii);
		closethisfd = 1;
	    }

	    if (closethisfd) {
		D_PRINTF(stderr, "closing for slot=%d fd=%d nfds=%d\n",
			 ii, pfds[ii].fd, nfds);
		_NETCLOSE(pfds[ii].fd);

		--nfds;	/* shrink poll array */
		/* NOTE: this re-orders the array */
		if (ii != nfds) {	/* move last one into old spot */
		    pfds[ii].fd = pfds[nfds].fd;
		    pfds[ii].events = pfds[nfds].events;
		    pfds[ii].revents = pfds[nfds].revents;
		    --ii;	/* check moved entry on the next loop */
		}
	    }
	}
    }
    if (nfds > 0) {
	d_printf (stderr,
		 "WARNING: Exiting with open clients nfds=%d time=%lu shutdowntime=%lu\n",
		  nfds, time(0L), gt_shutdowntime);
	for (ii = 0; ii < nfds; ++ii) {	/* close socket to make clients die */
	    D_PRINTF(stderr, "closing for slot=%d fd=%d\n", ii, pfds[ii].fd);
	    _NETCLOSE(pfds[ii].fd);
	}
    }
    return 0;
}


/* This is where each sub process starts */
THREAD_RET
launchChild(void *targ)
{
    _SOCKET	clientsock;
    struct sockaddr_in saddr; /* server address */
    struct linger linger_opt;
    unsigned int testtimeleft;
    int ret;
    unsigned int thread_stagger_usec = 1000; /* 1000/sec */
    int pnum = (int)targ;

    gn_myPID = getpid();
    if (ramptime) {
	/* convert to microseconds */
	if (gn_numthreads > 0) {
	    /* force intermediate result to double to avoid overflow */
	    thread_stagger_usec = (unsigned int)
                ((ramptime * (double)USECINSEC) / gn_numthreads);
	} else {
	    thread_stagger_usec = 0;
	}
    }

    if ((clientsock=socket(AF_INET, SOCK_STREAM, 0)) == -1)
	errexit(stderr, "child socket(): %s\n", neterrstr());

    memset(&saddr, 0, sizeof(saddr));
    saddr.sin_addr.S_ADDR = inet_addr("127.0.0.1");
    saddr.sin_family = AF_INET;
    saddr.sin_port = listenport;

    if (connect(clientsock, (struct sockaddr *)&saddr, sizeof(saddr)) == -1) {
	_NETCLOSE(clientsock);
	errexit(stderr, "child connect(): %s\n", neterrstr());
    }
    linger_opt.l_onoff = 1;
    linger_opt.l_linger = 60;

    if (setsockopt(clientsock, SOL_SOCKET, SO_LINGER, (char *) &linger_opt,
                   sizeof(linger_opt)) < 0) {
	_NETCLOSE(clientsock);
	errexit(stderr, "child setsockopt(): %s\n", neterrstr());
    }
    D_PRINTF(stderr, "child %d: using socket %d\n", pnum, clientsock);

    if (wait_start != 0.0) {
        float w = wait_start * USECINSEC;
        if (wait_start < 0) {
            //gn_myPID helps to distribute starts even if random seed was set
            w = ((RANDOM() * gn_myPID) & 0xffff) * -w / (256*256);
        }
        D_PRINTF(stderr, "Delaying start for %.2fs (of %.2fs)\n",
                  w/USECINSEC, wait_start);
        MS_usleep(w);
    }

    testtimeleft = gt_shutdowntime - time(0L);

    D_PRINTF(stderr, "child %d: proceeding for %d remaining seconds\n",
	     pnum, testtimeleft);

    if (testtimeleft > 0) {
	/* This is where the test gets run */
#ifndef DIRECT_OUT
	ret = clientProc(pnum, clientsock,
			 testtimeleft, thread_stagger_usec);
#else
	ret = clientProc(pnum, fileno(stdout),
			 testtimeleft, thread_stagger_usec);
#endif
    } else {
	D_PRINTF(stderr,
                 "child %d: Too late to start! shudowntime=%lu now=%lu\n",
		 pnum, testtimeleft, time(0L));
	ret = -1;
    }

    D_PRINTF(stderr, "child %d: closing logging socket\n", pnum);
    _NETCLOSE(clientsock);

    return((void *)ret);
}

/* Wait for childred to exit (after readwritechildren returns).
 */
int
waitChildren(void)
{
    int pid;
    int	status;

    for (;;) {
	errno = 0;
	alarm(gt_stopinterval);		/* dont wait forever */
	pid = wait(&status);
	if (pid == -1) {
	    if (errno == ECHILD) {
		break;			/* none left.  finished */
	    }
	    else if (errno == EINTR) {	/* alarm went off */
		if (time(0L) > (gt_aborttime+(EXIT_FAST*gt_stopinterval))) {
		    d_printf (stderr,
			      "WARNING: Aborting wait for children!\n");
		    break;
		}
	    }
	    else {
		d_printf(stderr, "WARNING: wait(): %s\n", strerror(errno));
		break;
	    }
	}
	else {
	    if (WIFSIGNALED (status)) {	/* show error exits */
		d_printf (stderr, "Client pid %d died with signal %d\n",
			  pid, WTERMSIG(status));
	    } else {
		D_PRINTF(stderr, "Client pid %d: status: %d errno=%d: %s\n",
			 pid, status, errno, strerror(errno));
	    }
	}
    }
    return 0;
}

int
main(int argc, char *argv[])
{
    int 	ii;
    struct tm	*tmptm;
    time_t	currentTime;
    int		ret;
    int		pid=0;
    pccx_t	pccxs;                  /* client process contexts */
    _SOCKET	serversock;
    struct sockaddr_in saddr;           /* server address */
    struct sockaddr_in caddr;           /* client address */
    socklen_t		addr_len;
    char ntoabuf[SIZEOF_NTOABUF];
    char *cp;

    gn_myPID = getpid();

    /* default random seed may be overridden on command line. */
    gn_randomSeed = time(0) ^ gn_myPID;

    gethostname(gs_thishostname, sizeof(gs_thishostname)-1);
    strncpy(gs_shorthostname, gs_thishostname, sizeof(gs_shorthostname)-1);
    cp = strchr(gs_shorthostname, '.');  /* shorten name */
    if (cp)
        *cp = 0;

    memset(mailmaster, 0, sizeof(mailmaster));
    memset(gs_dateStamp, 0, DATESTAMP_LEN*sizeof(char));

    parseArgs(argc, argv);
    RegisterKnownProtocols();
    if (help_and_exit)  /* show help after protocols are registered */
        usage(argv[0]);
    srand48(gn_randomSeed);

    returnerr(stderr, MAILCLIENT_VERSION "\n");
    returnerr(stderr, "procs=%d threads=%d seconds=%d ramptime=%d seed=%ld\n",
	      gn_numprocs, gn_numthreads, gt_testtime, ramptime,
              gn_randomSeed);
    if (ramptime > gt_testtime) { /* bad configuration, try to recover */
	returnerr (stderr,
                   "RampTime %d longer than TestTime %d.  Adjusting.\n",
		  ramptime, gt_testtime);
        if (wait_start != 0.0) { /* split between process and thread start */
            ramptime = gt_testtime / 2;
            if (wait_start > 0.0) {
                wait_start = ramptime;
            } else {
                wait_start = -ramptime;
            }
        } else {
            ramptime = gt_testtime;
        }
    }
    if (fabs(wait_start) > (gt_testtime - ramptime)) {
	returnerr (stderr,
                   "waitDelay %.3f longer than TestTime %d.  Adjusting.\n",
                   wait_start, gt_testtime);
        if (wait_start > 0.0) {
            wait_start = gt_testtime - ramptime;
        } else {
            wait_start = -(gt_testtime - ramptime);
        }
    }


    /* print the command line */
    if (gn_debug) {
      for (ii = 0; ii < argc; ii++)
	fprintf(stderr, "%s ", argv[ii] );
      fprintf(stderr, "\n\n" );
    }

    if (commandsfilename == NULL && f_usestdin == 0) {
        /* Must specify a message list */
	returnerr(stderr,
                  "No mail message list specified (use <-s | -u cmdFile>)\n");
        usage(argv[0]);
    }

    if (gt_testtime == 0) {         /* NO TEST TIME */
       usage(argv[0]);
    }

    if (gn_numprocs < 1) {
	returnerr(stderr, "Number of clients must be between >= 1\n");
	usage(argv[0]);
    }

#ifndef USE_PTHREADS
    if (gn_numthreads > 1) {
	returnerr(stderr, "No thread support.  Limiting to 1 thread.\n");
        gn_numthreads = 1;
    }
#endif
#ifdef HAVE_SSL
    mstoneSslInit();
#endif

    crank_limits();

    if (!gs_dateStamp[0]) {
	time(&currentTime);
	tmptm = localtime(&currentTime);
	strftime(gs_dateStamp, DATESTAMP_LEN, "%Y%m%d%H%M%S", tmptm);
    }

    initializeCommands(commandsfilename); /* read command block */

    gt_startedtime = time(0L);
    gt_shutdowntime = gt_startedtime + gt_testtime; /* start clean shutdown */
    gt_stopinterval = MAX ((ramptime/EXIT_FASTEST), 1); /* signal period */
    /* this is when the master gives up on the children */
    gt_aborttime = gt_shutdowntime + gt_stopinterval*2*EXIT_FASTEST;

    pccxs = (pccx_t)xcalloc(sizeof(ccx_t) * gn_numprocs);

    D_PRINTF(stderr, "preparing serversock\n");

    if ((serversock = socket(AF_INET, SOCK_STREAM, 0)) == -1)
	errexit(stderr, "socket() error: %s\n", neterrstr());

    memset(&saddr, 0, sizeof(saddr));

    saddr.sin_addr.S_ADDR = inet_addr("127.0.0.1");
    saddr.sin_family = AF_INET;
    saddr.sin_port = 0;

    if (bind(serversock, (struct sockaddr *)&saddr, sizeof(saddr)) == -1) {
	_NETCLOSE(serversock);
	errexit(stderr, "bind() error: %s\n", neterrstr());
    }

    if (listen(serversock, 512) == -1) {
	_NETCLOSE(serversock);
	errexit(stderr, "listen() error: %s\n", neterrstr());
    }

    addr_len = sizeof(saddr);
    if (getsockname(serversock, (struct sockaddr *)&saddr, &addr_len) == -1) {
	_NETCLOSE(serversock);
	errexit(stderr, "getsockname() error: %s\n", neterrstr());
    }

    listenport = saddr.sin_port;

    D_PRINTF(stderr, "listening on [%s:%d]\n",
	     safe_inet_ntoa(saddr.sin_addr, ntoabuf, sizeof(ntoabuf)),
             ntohs(listenport));

    setup_signal_handlers ();		/* trap signals */

    for(ii = 0; ii < gn_numprocs; ++ii) {
	D_PRINTF(stderr, "parent: forking client %d\n", ii);

#ifdef DIRECT_OUT			/* for Unix, only if debugging */
	if (1 == gn_numprocs) {
	    fprintf (stderr, "Single process, NOT forking.\n");
	    launchChild (0);
	    goto exit_single;
	}
#endif
        switch(pid=fork()) {
	case 0: /* CHILD */
	    launchChild((void *)ii);
	    fprintf(stderr, "child process exiting %d\n", ii);
	    exit(ii);
	    break;
	case -1: /* ERROR */
	    fprintf(stderr,
                    "parent: error forking child %d, errno=%d: %s\n",
                    ii, errno, strerror(errno));
	    errexit(stderr, "Error forking child processes\n");
	    exit(1);
	default: /* PARENT */
	    break;
	}

	pccxs[ii].pid = pid;

	D_PRINTF(stderr, "parent: Accepting child %d pid=%d\n", ii, pid);
	/* Maybe fork everything before accepting */
	addr_len = sizeof(caddr);
	/* If the the child dies immediately, we hang here */
	if ((ret = accept(serversock, (struct sockaddr *)&caddr, &addr_len))
            == -1) {
	    errexit(stderr, "accept() error: %s\n", neterrstr());
	}
	D_PRINTF(stderr, "parent: child %d using socket %d\n", ii, ret);
	pccxs[ii].socket = ret;
    }

    readwriteChildren(pccxs);

    D_PRINTF(stderr, "done polling, now just wait\n");

    waitChildren();

 exit_single:
    returnerr(stderr, "Threads exited.  Cleaning up.\n"); fflush(stderr);

#ifdef HAVE_SSL
    mstoneSslExit();
#endif
    free_commands();
    ProtocolFree();
    if (gs_parsename) xfree(gs_parsename);
    xfree(pccxs);

    returnerr(stderr, "Master process done.\n"); fflush(stderr);

    return 0;
} /* end main() */
