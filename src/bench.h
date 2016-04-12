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
 * Copyright (C) 1997-2000 Netscape Communications Corporation. All
 * Rights Reserved.
 *
 * Contributor(s):	Dan Christian <robodan@netscape.com>
 *			Marcel DePaolis <marcel@netcape.com>
 *			Mike Blakely
 *			David Shak
 *			Sean O'Rourke <sean@sendmail.com>
 *			Thom O'Connor <thom@sendmail.com>
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
#ifndef __BENCH_H__
#define __BENCH_H__

#ifdef USE_PTHREADS
#include <signal.h>
#include <pthread.h>
#endif
#include <stdio.h>
#include <stdarg.h>
#include <limits.h>
#include <float.h>
#include <sys/stat.h>
#include <assert.h>

#include <errno.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <math.h>
#include <sys/types.h>
#include <ctype.h>
#include <fcntl.h>

#include <sys/poll.h>

/*
 * thom@sendmail.com, 2004/06/07
 * This FREEBSD define prevents building mstone on post-FreeBSD 4.8
 * so removing it
 * #ifdef __FREEBSD__
 * #define poll _thread_sys_poll
 * #endif
*/

#include <sys/param.h>
#include <sys/ipc.h>
#include <sys/errno.h>
#include <sys/socket.h>
#include <sys/time.h>
#ifndef timersub
/* aix doesn't seem to define this */
# define timersub(a, b, result)                                                \
  do {                                                                        \
    (result)->tv_sec = (a)->tv_sec - (b)->tv_sec;                             \
    (result)->tv_usec = (a)->tv_usec - (b)->tv_usec;                          \
    if ((result)->tv_usec < 0) {                                              \
      --(result)->tv_sec;                                                     \
      (result)->tv_usec += 1000000;                                           \
    }                                                                         \
  } while (0)
#endif /* timersub */
#ifndef timeradd
# define timeradd(a, b, result)                                                \
  do {                                                                        \
    (result)->tv_sec = (a)->tv_sec + (b)->tv_sec;                             \
    (result)->tv_usec = (a)->tv_usec + (b)->tv_usec;                          \
    if ((result)->tv_usec >= 1000000)                                         \
      {                                                                       \
        ++(result)->tv_sec;                                                   \
        (result)->tv_usec -= 1000000;                                         \
      }                                                                       \
  } while (0)
#endif /* timeradd */
#ifndef timercmp
# define timercmp(a, b, CMP)                                                   \
  (((a)->tv_sec == (b)->tv_sec) ?                                             \
   ((a)->tv_usec CMP (b)->tv_usec) :                                          \
   ((a)->tv_sec CMP (b)->tv_sec))
#endif /* timercmp */
#include <sys/wait.h>
#include <stdlib.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>
#include <sys/resource.h> /* for struct rlimit, RLIMIT_NOFILE */
#ifdef HAVE_SELECT_H
#include <sys/select.h>
#endif
#ifdef HAVE_WAIT_H
#include <wait.h>
#endif

#include "sysdep.h"

#include "socket.h"
#include "idle.h"
#include "constants.h"

#define USECINSEC	    1000000
#define MSECINSEC	    1000
#define MAX_ACCEPT_SECS	    180		/* maximum time master will wait for listen() */

#define SECS_2_USECS(x)	    ((x) * USECINSEC)
#define USECS_2_SECS(x)	    ((x) / USECINSEC)

#define LINE_BUFSIZE	    4096
#define MAXPROCSPERNODE	    4096		/* max # of procs/node */

#define SMTP_PORT 25			/* standard port numbers */
#define POP3_PORT 110
#define IMAP4_PORT 143
#define HTTP_PORT 80
#define WMAP_PORT 80

#define CRLF "\r\n"

#define MAX_USERNAME_LEN 32
#define MAX_MAILADDR_LEN 64
#define MAX_ERRORMSG_LEN 256
#define DATESTAMP_LEN 40

#define MAX_IMAP_FOLDERNAME_LEN 256
#define MAX_SEARCH_PATTERN_LEN 256

#define MAX_HTTP_COMMAND_LEN 1024

/* TODO make these dynamic.  For now just use big buffers */
#define SIZEOF_NTOABUF  ((3+1)*4)
#define SIZEOF_EVENTTEXT    150		/* report text from a single timer */
#define SIZEOF_RQSTSTATSTEXT 2048	/* report text from all timers */
#define SIZEOF_SUMMARYTEXT 8192		/* report text from all protocols */

/* levels for timeexpired */
#define EXIT_SOON	1		/* do a clean shutdown ASAP */
#define EXIT_FAST	20		/* shutdown now, dont block */
#define EXIT_FASTEST	50		/* close sockets unconnditionally */

/* Debug macros */
#define D_PRINTF  if (gn_debug > 0) d_printf
#define T_PRINTF if (gn_record_telemetry) t_printf

/* make sure we don't pass NULL's to printf() and friends */
#define PSTR(X) ((X)?(X):"(null)")

/*
  Simple keyword indexed string storage
*/
typedef struct param_node {
    struct param_node	*next;
    char *	name;
    char *	value;
} param_node_t;

typedef struct param_list {
    param_node_t	*head;
    param_node_t	*tail;
} param_list_t;

/*
  Simple string list
*/
typedef struct list_node {
    struct list_node	*next;
    char *	value;
} list_node_t;

typedef struct string_list {
    list_node_t	*head;
    list_node_t	*tail;
} string_list_t;

/* Numeric range (Shared).  Previous value must be stored seperately */
typedef struct range {
    unsigned long	first;			/* first valid value */
    unsigned long	last;			/* last valid value */
    unsigned long	span;			/* last-first */
    int		sequential;		/* 0=random, +=sequential up, -=down */
} range_t;

/* basic timing structure */
typedef struct event_timer {
    unsigned long	trys;
    unsigned long	errs;
    double		bytesread;
    double		byteswritten;
    double		elapsedtime;
    double		elapsedtimesq;
    double		maxtime;
    double		mintime;
} event_timer_t;

/* command stats kept for every block and for every thread */
typedef struct cmd_stats {
					/* This is protocol independent */
    unsigned long  	totalerrs;
    unsigned long  	totalcommands;
    event_timer_t	combined;	/* AKA total */
    event_timer_t	idle;

    void *		data;		/* protocol dependent data */
} cmd_stats_t;

typedef struct stats {			/* used for throttling ??? */
    struct timeval      starttime;
    struct timeval      endtime;
    unsigned int	total_num_of_commands;
} stats_t;

typedef struct resolved_addr {
    char  *	hostName;		/* name of server */
    NETPORT  	portNum;		/* port ot use */
    int resolved;
    struct hostent host_phe;
    struct protoent host_ppe;
    unsigned long host_addr;
    short host_type;
} resolved_addr_t;

typedef struct thread_context *ptcx_t;
typedef struct mail_command *pmail_command_t;
typedef struct protocol *p_protocol_t;
typedef int (*parseStartPtr_t)(pmail_command_t, char *, param_list_t *);
typedef int (*parseEndPtr_t)(pmail_command_t, string_list_t *, param_list_t *);
typedef void *(*commStartPtr_t)(ptcx_t , pmail_command_t , cmd_stats_t *);
typedef int   (*commLoopPtr_t)(ptcx_t , pmail_command_t , cmd_stats_t *, void *);
typedef void  (*commEndPtr_t)(ptcx_t , pmail_command_t , cmd_stats_t *, void *);
typedef void (*statsFormatPtr_t)(p_protocol_t, const char *extra, char *buf);
typedef void (*statsInitPtr_t)(pmail_command_t, cmd_stats_t *, int, int);
typedef void (*statsUpdatePtr_t)(p_protocol_t, cmd_stats_t *, cmd_stats_t *);
typedef void (*statsOutputPtr_t)(p_protocol_t, cmd_stats_t *, char *);
typedef void (*parseFreePtr_t)(pmail_command_t);
typedef void (*protocolFreePtr_t)(void);

typedef struct protocol {
    const char *	name;		/* text name */
    parseStartPtr_t	parseStart;	/* section start parse routine */
    parseEndPtr_t	parseEnd;	/* section end parse routine */

    commStartPtr_t	cmdStart;	/* command start routine */
    commLoopPtr_t	cmdLoop;	/* command loop routine */
    commEndPtr_t	cmdEnd;		/* command end routine */

    statsFormatPtr_t	statsFormat;	/* output format information */
    statsInitPtr_t	statsInit;	/* init and zero stats structure */
    statsUpdatePtr_t	statsUpdate;	/* sum commands */
    statsOutputPtr_t	statsOutput;	/* output stats */

    parseFreePtr_t	parseFree;	/* free command memory */
    protocolFreePtr_t	protoFree;	/* free memory */

    int			cmdCount;	/* commands using this protocol */
    cmd_stats_t		stats;		/* total stats for this protocol */
} protocol_t;

/* This structure defines a mail command */
typedef struct mail_command {
    /* Required fields */
    protocol_t *	proto;
    int 	weight;

    /* These are protocol independent (client.c) */
    int 	blockID;		/* ID number for each block */
    int 	numLoops;		/* typically messages per connection */

    /* Sean O'Rourke: added startDelay to simulate arrival rates */
    dinst_t	*startDelay;
    dinst_t 	*idleTime;
    dinst_t 	*loopDelay;
    dinst_t	*blockTime;

    /* XXX: not threadsafe! */
    double 	throttle;		/* throttling factor */
    int		loopThrottle;		/* loopDelay over/underrun for throttling */
    double	throttleFactor;		/* multiple by which to change throttle rate */

    void	*data;			/* protocol specific data */
} mail_command_t;

typedef struct child_context {		/* forked sub processes */
    int		pid;
    _SOCKET	socket;
} ccx_t, *pccx_t;

typedef struct thread_context {
    /* initialized by parent thread */
    THREAD_ID	tid;			/* thread id */
    int		processnum;		/* ordinal process number */
    int		threadnum;		/* ordinal thread number */
    int		random_seed;		/* seed for srandom */

    /* local thread context, also read by parent */
    int		blockCount;		/* how many command blocks */
    int		connectCount;		/* how many connections */
    cmd_stats_t *cmd_stats;		/* stats for each command */

    /* temporary storage (event_start, event_stop) */
    struct timeval starttime;		/* a starting timestamp */
    int		bytesread;		/* num bytes read in per event */
    int		byteswritten;		/* num bytes written per event */

    int		ofd;			/* connection to master */
    FILE	*dfile;			/* debug file */
    int		logfile;		/* telemetry log file */

#ifdef USE_EVENTS
    int		ev_loop;		/* loops left for current event */
    struct timeval ev_next;		/* time to schedule next init */
    void *	ev_state;		/* client state */
    mail_command_t * ev_comm;
    cmd_stats_t * ev_stats;

#endif /* USE_EVENTS */

    SOCKET	sock;			/* network connection */
    char	errMsg[MAX_ERRORMSG_LEN]; /* low level error string */

    int		net_timeout;		/* timeout for network ops (msec) */
} tcx_t;

/* About errMsg:
   This should store what was being attempted when a IO error occurs.
   From errMsg and errno (or its NT equivalent) you should be able
   to understand what went wrong.

   No message is printed by the common functions (since some errors are
   recoverable).

   The protocol handlers combine errMsg with neterrstr() to generate
   the message that the user sees (if not handled by the protocol).

   Note that this is a small buffer (since it is replicated with every
   thread).  Don't try to stuff the read/written data into it.

   The routine getting the system error sets errMsg (strcpy, or sprintf).
   Calling routines append a "call trace" with additional info (strcat).
   The "call trace" starts with '<' as a seperator.
*/
#define debugfile (ptcx->dfile)

/* routines in bench.c */
extern void event_start(ptcx_t ptcx, event_timer_t *pevent);
extern void event_stop(ptcx_t ptcx, event_timer_t *pevent);
extern void event_reset(event_timer_t *pevent);
extern void event_sum(event_timer_t *psum, event_timer_t *pincr);
extern char *event_to_text(event_timer_t *pevent, char *ebuf);
extern void stats_init(stats_t *);

extern char * double_to_text(const double the_double, char *textbuf);
#if 0
extern cmd_stats_t *text_to_cmd_stats(ptcx_t ptcx, char *cmd_stats_as_text, cmd_stats_t *the_cmd_stats);
extern cmd_stats_t * text_to_cmd_stats(ptcx_t ptcx, char *, cmd_stats_t *the_cmd_stats);
extern stats_t * text_to_stats(ptcx_t ptcx, char *, stats_t *the_stats);
extern char * stats_to_text(ptcx_t ptcx, const stats_t *, char *statstextbuf);
#endif

/* shared variables */
extern int	gn_debug;
extern int	gn_record_telemetry;
extern int	gn_total_weight;
extern int	gn_client_throttle;
extern int	gn_maxerrorcnt;
extern int	gn_maxBlockCnt;
extern int	gn_numprocs;
extern int	gn_numthreads;
extern int	gn_feedback_secs;
extern time_t	gt_testtime;
extern time_t	gt_startedtime;
extern volatile time_t	gt_shutdowntime;
extern volatile int gf_timeexpired;
extern time_t	gt_stopinterval;	/* MAX (ramptime/5, 10) */
extern time_t	gt_aborttime;		/* startedtime + testtime + ramptime*/
extern int	gn_number_of_commands;
extern int	gf_abortive_close;
extern int	gf_imapForceUniqueness;
extern char	gs_dateStamp[DATESTAMP_LEN];
extern char	gs_thishostname[];
extern char	gs_shorthostname[];
extern char	*gs_parsename;
extern pid_t 	gn_myPID;
extern mail_command_t *g_loaded_comm_list; /* actually a dynamic array */
extern protocol_t     *g_protocols;
extern int             g_protocol_size;
extern const char *gs_eventToTextFormat;


/* more routines in bench.c */

extern int timeval_clear(struct timeval *tv);
extern int timeval_stamp(struct timeval *tv);

/*  extern int waitReadWrite(int fd, int flags); */
extern int waitReadWrite(SOCKET s, int timeout, int flags);
extern int retryRead(ptcx_t ptcx, SOCKET sock, char *buf, int count);
extern int retryWrite(ptcx_t ptcx, SOCKET sock, char *buf, int count);

extern int recvdata(ptcx_t ptcx, SOCKET sock, char *ptr, int nbytes);
extern int senddata(ptcx_t ptcx, SOCKET sock, char *ptr, int nbytes);
extern void rqstat_times(cmd_stats_t *rs, cmd_stats_t *rt);
extern void rqstat_to_buffer(char *buf, char *comm, cmd_stats_t *stats);
extern int readResponse(ptcx_t ptcx, SOCKET sock, char *buffer, int buflen);
extern int sendCommand(ptcx_t ptcx, SOCKET sock, char *command);
extern int doCommandResponse(ptcx_t ptcx, SOCKET sock, char *command, char *response, int resplen);
extern int sendOutput(int fd, char *command);
extern int retrMsg(ptcx_t ptcx, SOCKET sock);
extern void trimEndWhite (char *buff);
unsigned long rangeNext (range_t *, unsigned long );
void rangeSetFirstLast (range_t *, unsigned long , unsigned long , int );
void rangeSetFirstCount (range_t *, unsigned long , unsigned long , int );
void rangeSplit (range_t *whole, range_t *sub, int pnum, int tnum);
extern void cmdStatsInit (cmd_stats_t *p);

/* routines in sysdep.c */
extern void MS_usleep(unsigned int microsecs);
extern void MS_sleep(unsigned int secs);

/* routines in errexit.c */
extern void errexit(FILE *dfile, const char *, ...);
extern int returnerr(FILE *dfile, const char *, ...);
extern int d_printf(FILE *dfile, const char *, ...);
extern int t_printf(int fd, const char *buffer, size_t count, const char *format, ...);
extern char *neterrstr(void);


/* routines in parse.c */
extern char *string_tolower(char *string);
extern char *string_unquote(char *string);
extern int cmdParseNameValue (pmail_command_t cmd, char *name, char *tok);
extern int time_atoi(const char *pstr);
extern int millitime_atoi(const char *pstr);
extern double size_atof(const char *);
extern int load_commands(char *commands);
extern void free_commands(void);
extern param_list_t *paramListAlloc (void);
/* paramListAdd returns: 1 update existing value, 0 new, -1 out of memory */
extern int paramListAdd (param_list_t *list, const char *name, const char *value);
/* paramListGet returns value or NULL */
extern char *paramListGet (param_list_t *list, const char *name);
void paramListDump(param_list_t *list);
extern param_list_t	*g_default_params; /* default section params */
extern string_list_t *stringListAlloc (void);
extern void stringListAdd (string_list_t *list, const char *value);
extern void stringListFree (string_list_t *list);
extern void stringListDump(string_list_t *list);

/* routines in timefunc.c */
extern double	timevaldouble(struct timeval *);
extern void	doubletimeval(const double, struct timeval *);
extern double	compdifftime_double(struct timeval *End, struct timeval *Strt);

/* routines in main.c */
extern protocol_t *protocol_get (char *name);
extern void ProtocolRegister(
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
    parseFreePtr_t	parseFree,	/* section end parse routine */
    protocolFreePtr_t	protoFree);	/* free memory */

extern char *safe_inet_ntoa(struct in_addr ina, char *psz, int len);

extern SOCKET connectSocket(ptcx_t ptcx, resolved_addr_t *, char *protocol);
extern int set_abortive_close(SOCKET sock);

extern void throttle(ptcx_t ptcx, mail_command_t *comm, cmd_stats_t *timer);

/* routines in client.c */
extern void beginShutdown (void);
extern int clientInit(ptcx_t ptcx);
extern int clientLoop(ptcx_t ptcx);
extern THREAD_RET clientThread(void *);
extern void clientSummary(ptcx_t ptcxs, int ntcxs, int ii, int outfd);
extern THREAD_RET summaryThread(void *);
/*  extern int clientProc(int pnum, SOCKET outfd, unsigned int timeleft, unsigned int thread_stagger_usec); */
extern int clientProc(int pnum, int outfd, unsigned int timeleft, unsigned int thread_stagger_usec);

extern double now(void);
extern void MS_idle(ptcx_t ptcx, int idleSecs);
extern int resolve_addrs(char *host, char *protocol, struct hostent *host_phe,
			 struct protoent *proto_ppe, unsigned long *addr,
			 short *type);
#if 0
extern void dumpevent(ptcx_t ptcx, event_timer_t *pevent);
extern void dumptimer(ptcx_t ptcx, cmd_stats_t *rqsttimer);
extern int clientStats(ptcx_t ptcx);
unsigned long get_next_login(ptcx_t ptcx, mail_command_t *comm, cmd_stats_t *ptimer);
unsigned long get_next_address(ptcx_t ptcx, mail_command_t *comm, cmd_stats_t *ptimer);
#endif

/* routines in files.c */
extern int unlink_dir_file (const char *dirpath, const char *file);
extern int nuke_dir_subdir (const char *dirpath, const char *subdir,
                            const char *save_dir);
extern int nuke_dir (const char *dir, const char *save_dir);
extern int copy_file(const char *from_path, const char *to_path);


#undef VERSION
#define VERSION "4.9.4"
#ifdef _DEBUG
#define MAILCLIENT_VERSION "mailclient (" VERSION " DEBUG built " __DATE__ " " __TIME__ ")"
#else
#define MAILCLIENT_VERSION "mailclient (" VERSION " built " __DATE__ " " __TIME__ ")"
#endif

FILE *fdopen(int fildes, const char *mode);
extern int	getopt(int, char *const *, const char *);
#ifndef __OSF1__
#ifndef __LINUX__
extern long random(void);
#endif
#endif

/* miscellaneous */

#ifndef MIN
#define MIN(x,y)	(((x) < (y)) ? (x) : (y))
#endif
#ifndef MAX
#define MAX(x,y)	(((x) >= (y)) ? (x) : (y))
#endif
#ifndef ABS
#define ABS(x)	        (((x) >= 0) ? (x) : -(x))
#endif

#ifdef HAVE_SSL
extern void mstoneSslInit(void);
extern void mstoneSslExit(void);
#endif

#endif /* !__BENCH_H__ */
