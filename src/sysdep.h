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
#ifndef SYSDEP_H__
#define SYSDEP_H__

/* Modern OSes have these (originally defined by gnuplot/config.h(autoconf)) */
#define HAVE_SNPRINTF 1
#define HAVE_STRERROR 1

#include <sys/types.h>
#include <netinet/in.h>
#include <netdb.h>

#ifdef __OSF1__
#include <sys/sysinfo.h> /* for setsysinfo() */
#endif

/* MAXHOSTNAMELEN is undefined on some systems */
#ifndef MAXHOSTNAMELEN
#define MAXHOSTNAMELEN 64
#endif

/* SunOS doesn't define NULL */
#ifndef NULL
#define NULL 0
#endif

/* encapsulation of minor UNIX/WIN NT differences */

#define strnicmp(s1,s2,n)	strncasecmp(s1,s2,n)

#define _NETREAD(sock, buf, len)	read(sock, buf, len)
#define _NETWRITE(sock, buf, len)	write(sock, buf, len)
#define	_NETCLOSE(sock)			close(sock)
#define OUTPUT_WRITE(sock, buf, len)	write(sock, buf, len)
#define _BADSOCKET(sock)		((sock) < 0)
#define _BADSOCKET_ERRNO(sock)		(BADSOCKET(sock) || errno)
#define	_BADSOCKET_VALUE		(-1)
#define S_ADDR				s_addr
#define	GET_ERROR			errno
#define	SET_ERROR(err)			(errno = (err))

#define	GETTIMEOFDAY(timeval,tz)	gettimeofday(timeval, NULL)

typedef unsigned short			NETPORT;

#if defined (USE_LRAND48_R)
extern void osf_srand48_r(unsigned int seed);
extern long osf_lrand48_r(void);
#define	SRANDOM				osf_srand48_r
#define	RANDOM				osf_lrand48_r

#elif defined (USE_LRAND48)

#define	SRANDOM				srand48
#define	RANDOM				lrand48

#else /* !USE_LRAND48 */

#define	SRANDOM				srandom
#define	RANDOM				random

#endif /* USE_LRAND48_R */

#define PROGPATH			"/mailstone/mailclient"
#define	FILENAME_SIZE			1024
#define HAVE_VPRINTF			1

typedef int				_SOCKET;

#if 0
typedef _SOCKET		SOCKET;
#define NETREAD 	_NETREAD
#define NETWRITE	_NETWRITE
#define NETCLOSE	_NETCLOSE
#define BADSOCKET	_BADSOCKET
#define BADSOCKET_ERRNO	_BADSOCKET_ERRNO
#define BADSOCKET_VALUE _BADSOCKET_VALUE
#define INIT_SOCKET(sock, host) (sock)
#define SOCK_FD(s)	(s)
#endif /* 0 (was !LINESPEED) */

#define min(a,b)			(((a) < (b)) ? a : b)
#define max(a,b)			(((a) > (b)) ? a : b)

#define THREAD_RET void *
#ifdef USE_PTHREADS
#include <pthread.h>
#define THREAD_ID	pthread_t
#else
#define THREAD_ID	int
#endif


typedef THREAD_RET (*thread_fn_t)(void *);

/* function prototypes */

extern void crank_limits(void);
extern int sysdep_thread_create(THREAD_ID *id, thread_fn_t tfn, void *arg);
extern int sysdep_thread_join(THREAD_ID id, int *pstatus);
extern void setup_signal_handlers (void);

#ifdef NO_REXEC
extern int	rexec(char **, int, char *, char *, char *, int *);
#endif


#ifndef HAVE_STRERROR
/* strerror() is not available on SunOS 4.x and others */
char *strerror(int errnum);

#endif
/* strerror() */


#ifndef INADDR_NONE
#define INADDR_NONE -1
#endif

#endif /* !SYSDEP_H__ */
