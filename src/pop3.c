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
/* pop3.c: POP3 protocol test */

#include "bench.h"
#include "pish.h"
#include "socket.h"
#include "xalloc.h"
#include "checksum.h"

typedef struct _doPOP3_state {
    int		numMsgs;			/* messages in folder */
    int		totalMsgLength;		/* total msg length */
    int		msgCounter;		/* count in download */
} doPOP3_state_t;

/* flags definitions */
#define leaveMailOnServer 0x01

static void doPop3Exit (ptcx_t ptcx, doPOP3_state_t	*me);


static int
PopParseNameValue (pmail_command_t cmd,
		    char *name,
		    char *tok)
{
    pish_command_t	*pish = (pish_command_t *)cmd->data;

    /* find a home for the attr/value */
    if (pishParseNameValue(cmd, name, tok) == 0)
	;				/* done */
    else if (strcmp(name, "leavemailonserver") == 0) {
	if (atoi(tok) > 0) {
	    pish->flags |= leaveMailOnServer;
	    pish->leaveMailOnServerDist =
	        parse_distrib(tok, (value_parser_t)&atof);
	} else {
	    pish->flags &= ~leaveMailOnServer;
	}
    }
    else {
	return -1;
    }
    return 0;
}


/*
  Set defaults in command structure
*/
int
Pop3ParseStart (pmail_command_t cmd,
		char *line,
		param_list_t *defparm)
{
    param_node_t	*pp;
    pish_command_t	*pish = XCALLOC(pish_command_t);
    cmd->data = pish;

    cmd->numLoops = 9999;		/* default 9999 downloads */
    pish->hostInfo.portNum = POP3_PORT;	/* default port */

    D_PRINTF(stderr, "Pop3 Assign defaults\n");
    /* Fill in defaults first, ignore defaults we dont use */
    for (pp = defparm->head; pp; pp = pp->next) {
	(void)PopParseNameValue (cmd, pp->name, pp->value);
    }

    return 1;
}

/*
  Fill in command structure from a list of lines
 */
int
Pop3ParseEnd (pmail_command_t cmd,
		string_list_t *section,
		param_list_t *defparm)
{
    list_node_t	*sp;
    pish_command_t	*pish = (pish_command_t *)cmd->data;

    /* Now parse section lines */
    D_PRINTF(stderr, "Pop3 Assign section lines\n");
    sp = section->head;
    if (sp->next) sp = sp->next;        /* skip first */
    for (; sp->next; sp = sp->next) {
	char *name = sp->value;
	char *tok = name + strcspn(name, " \t=");
	*tok++ = 0;			/* split name off */
	tok += strspn(tok, " \t=");

	string_tolower(name);
	tok = string_unquote(tok);

	if (PopParseNameValue (cmd, name, tok) < 0) {
	    /* not a known attr */
	    D_PRINTF(stderr,"unknown attribute '%s' '%s'\n", name, tok);
	    returnerr(stderr,"unknown attribute '%s' '%s'\n", name, tok);
	}
    }

    /* check for some of the required command attrs */
    if (!pish->hostInfo.hostName) {
	D_PRINTF(stderr,"missing server for command");
	return returnerr(stderr,"missing server for command\n");
    }

    if (!pish->loginFormat) {
	D_PRINTF(stderr,"missing loginFormat for command");
	return returnerr(stderr,"missing loginFormat for command\n");
    }

    if (!pish->passwdFormat) {
	D_PRINTF(stderr,"missing passwdFormat for command");
	return returnerr(stderr,"missing passwdFormat for command\n");
    }

    /* see if we can resolve the mailserver addr */
    if (resolve_addrs(pish->hostInfo.hostName, "tcp",
		      &(pish->hostInfo.host_phe),
		      &(pish->hostInfo.host_ppe),
		      &(pish->hostInfo.host_addr),
		      &(pish->hostInfo.host_type))) {
	return returnerr (stderr, "Error resolving hostname '%s'\n",
			  pish->hostInfo.hostName);
    } else {
	pish->hostInfo.resolved = 1;	/* mark the hostInfo resolved */
    }

    rangeSetFirstCount (&pish->loginRange, pish->loginRange.first,
			   pish->loginRange.span, pish->loginRange.sequential);
    rangeSetFirstCount (&pish->domainRange, pish->domainRange.first,
			   pish->domainRange.span, pish->domainRange.sequential);

    return 1;
}

int
doPopCommandResponse(ptcx_t ptcx, SOCKET sock, char *command, char *response, int resplen)
{
    int rc;

    T_PRINTF(ptcx->logfile, command, strlen (command), "POP3 SendCommand");
    rc = doCommandResponse(ptcx, sock, command, response, resplen);
    if (rc == -1)
    {
	trimEndWhite (command);
	trimEndWhite (response);
	returnerr(debugfile,"POP3 error command=[%s], response=[%s]\n",
		  command, response);
    }
    T_PRINTF(ptcx->logfile, response, strlen(response),
	     "POP3 ReadResponse");	/* telemetry log. should be lower level */
    /* D_PRINTF(stderr, "POP command=[%s] response=[%s]\n", command, response); */
    if (strncmp(response, "+OK", 3) != 0) {
	if (gf_timeexpired < EXIT_FAST) {
	    trimEndWhite (command);
	    trimEndWhite (response);
	    returnerr(debugfile,"POP3 error command=[%s] response=[%s]\n",
		      command, response);
	}
	return -1;
    }
    return 0;
}

int
popLogin(ptcx_t ptcx, mail_command_t *cmd, cmd_stats_t *ptimer, SOCKET sock)
{
    char	command[MAX_COMMAND_LEN];
    char	respBuffer[MAX_RESPONSE_LEN];
    char	mailUser[MAX_MAILADDR_LEN];
    char	userPasswd[MAX_MAILADDR_LEN];
    unsigned long loginNum;
    unsigned long domainNum;
    int	rc;
    pish_stats_t	*stats = (pish_stats_t *)ptimer->data;
    pish_command_t	*pish = (pish_command_t *)cmd->data;

    /* generate a random username and domainname(with a mailbox on the server) */
    domainNum = rangeNext (&stats->domainRange, stats->lastDomain);
    stats->lastDomain = domainNum;
    loginNum = rangeNext (&stats->loginRange, stats->lastLogin);
    stats->lastLogin = loginNum;

    sprintf(mailUser, pish->loginFormat, loginNum, domainNum);
    D_PRINTF(debugfile,"mailUser=%s\n", mailUser);
    sprintf(command, "USER %s%s", mailUser, CRLF);

    event_start(ptcx, &stats->cmd);
    rc = doPopCommandResponse(ptcx, sock, command, respBuffer, sizeof(respBuffer));
    event_stop(ptcx, &stats->cmd);
    if (rc == -1) {
	if (gf_timeexpired < EXIT_FAST) {
	    stats->login.errs++;
					/* error already displayed */
	}
	return -1;
    }

    /* send Password */
    sprintf(userPasswd, pish->passwdFormat, loginNum);
    sprintf(command, "PASS %s%s", userPasswd, CRLF);

    event_start(ptcx, &stats->login);
    rc = doPopCommandResponse(ptcx, sock, command, respBuffer, sizeof(respBuffer));
    event_stop(ptcx, &stats->login);
    if (rc == -1) {
	if (gf_timeexpired < EXIT_FAST) {
	    stats->login.errs++;
	    returnerr(debugfile,"POP3 cannot login user=%s pass=%s\n",
		      mailUser, userPasswd);
	}
	return -1;
    }
    return 0;
}

void *
doPop3Start(ptcx_t ptcx, mail_command_t *cmd, cmd_stats_t *ptimer)
{
    doPOP3_state_t	*me = XCALLOC (doPOP3_state_t);
    char	respBuffer[MAX_RESPONSE_LEN];
    int		rc;
    int		numBytes;
    char	command[MAX_COMMAND_LEN];
    pish_stats_t	*stats = (pish_stats_t *)ptimer->data;
    pish_command_t	*pish = (pish_command_t *)cmd->data;

    if (!me) return NULL;

    me->numMsgs = 0;
    me->totalMsgLength = 0;
    me->msgCounter = 0;

    ptcx->net_timeout = pish->net_timeout;

    event_start(ptcx, &stats->connect);
    ptcx->sock = connectSocket(ptcx, &pish->hostInfo, "tcp");
    event_stop(ptcx, &stats->connect);
    if (BADSOCKET(ptcx->sock)) {
	if (gf_timeexpired < EXIT_FAST) {
	    stats->connect.errs++;
	    returnerr(debugfile, "POP3 Couldn't connect to %s: %s\n",
		      pish->hostInfo.hostName, neterrstr());
	}
	xfree (me);
	return NULL;
    }

    if (gf_abortive_close) {
	if (set_abortive_close(ptcx->sock) != 0) {
	    returnerr (debugfile, "POP3: WARNING: Could not set abortive close\n");
	}
    }

#ifdef SOCK_SSL
    if (pish->sslTunnel)
	SSL_INIT(ptcx->sock, pish);
#endif /* SOCK_SSL */

    LS_INIT(ptcx->sock, pish);

    /* READ connect response from server */
    event_start(ptcx, &stats->banner);
    numBytes = readResponse(ptcx, ptcx->sock, respBuffer, sizeof(respBuffer));
    event_stop(ptcx, &stats->banner);
    if (numBytes <= 0) {
	if (gf_timeexpired < EXIT_FAST) {
	    stats->banner.errs++;
	    returnerr(debugfile,"POP3 Error reading banner: %s\n",
		      neterrstr());
	}
	doPop3Exit (ptcx, me);
	return NULL;
    }

#ifdef SOCK_SSL

    if (!pish->useTLS)
	goto end_tls;
    /*
    **  Try TLS
    */

    event_start(ptcx, &stats->cmd);
    rc = doPopCommandResponse(ptcx, ptcx->sock, "CAPA" CRLF, respBuffer,
			     sizeof(respBuffer));
    event_stop(ptcx, &stats->cmd);
    if (rc < 0) {
	stats->cmd.errs++;
	goto end_tls;
    }
    if (strstr(respBuffer, "STLS") != NULL) {
	SOCKET s = BADSOCKET_VALUE;
	D_PRINTF(debugfile, "trying STLS\n");
	event_start(ptcx, &stats->cmd);
	if (doPopCommandResponse(ptcx, ptcx->sock, "STLS" CRLF, respBuffer,
				 sizeof(respBuffer) >= 0)) {
	    s = ptcx->sock;
	    SSL_INIT(s, pish);
	}
	event_stop(ptcx, &stats->cmd);
	if (BADSOCKET(s)) {
	    D_PRINTF(stderr, "STLS failed\n");
	    stats->cmd.errs++;
	} else {
	    ptcx->sock = s;
	}
    }
 end_tls:

#endif /* SOCK_SSL */

    /*
   * LOGIN
   */

    rc = popLogin(ptcx, cmd, ptimer, ptcx->sock);
    if (rc != 0) {
	doPop3Exit (ptcx, me);
	return NULL;
    }
#ifdef POP_UIDL
    /* send an UIDL */
    sprintf(command, "UIDL" CRLF);
    event_start(ptcx, &stats->cmd);
    rc = doPopCommandResponse(ptcx, ptcx->sock, command, respBuffer, sizeof(respBuffer));
    event_stop(ptcx, &stats->cmd);
    if (rc == -1) {
	if (gf_timeexpired < EXIT_FAST) {
	    stats->cmd.errs++;
	}
	doPop3Exit (ptcx, me);
	return NULL;
    }
#endif /* POP_UIDL */
    /* send a STAT */
    sprintf(command, "STAT%s", CRLF);
    event_start(ptcx, &stats->cmd);
    rc = doPopCommandResponse(ptcx, ptcx->sock, command, respBuffer, sizeof(respBuffer));
    event_stop(ptcx, &stats->cmd);
    if (rc == -1) {
	if (gf_timeexpired < EXIT_FAST) {
	    stats->cmd.errs++;
	}
	doPop3Exit (ptcx, me);
	return NULL;
    }

    /* parse number of msgs out of buffer */
    if (!sscanf(respBuffer, "+OK %d %d", &me->numMsgs, &me->totalMsgLength)) {
	if (gf_timeexpired < EXIT_FAST) {
	    stats->cmd.errs++;
	    returnerr(debugfile,"POP3 Error parsing STAT response, %s: %s\n",
		      respBuffer, neterrstr());
	}
	doPop3Exit (ptcx, me);
	return NULL;
    }

    D_PRINTF(debugfile,"STAT shows %d msgs of %d total bytes\n",
	     me->numMsgs, me->totalMsgLength);
    return me;
}

int
doPop3Loop(ptcx_t ptcx, mail_command_t *cmd, cmd_stats_t *ptimer, void *mystate)
{
    doPOP3_state_t	*me = (doPOP3_state_t *)mystate;
    char	command[MAX_COMMAND_LEN];
    char	respBuffer[MAX_RESPONSE_LEN];
    int		rc, numBytes;
    pish_stats_t	*stats = (pish_stats_t *)ptimer->data;
    pish_command_t	*pish = (pish_command_t *)cmd->data;
    int leave_on_server;

    if (me->msgCounter >= me->numMsgs) return -1; /* done, close */

    /* retr the msgs */
    /* send the RETR command */
    sprintf(command, "RETR %d%s", ++me->msgCounter, CRLF);
    event_start(ptcx, &stats->msgread);
    rc = sendCommand(ptcx, ptcx->sock, command);
    if (rc == -1) {
	event_stop(ptcx, &stats->msgread);
	if (gf_timeexpired < EXIT_FAST) {
	    stats->msgread.errs++;
	    returnerr(debugfile,"POP3 Error sending RETR %d command: %s\n",
		      me->msgCounter, neterrstr());
	}
	doPop3Exit (ptcx, me);
	return -1;
    }

    /* read msg */
    if (pish->genChecksum == CS_NONE)
	numBytes = retrMsg(ptcx, ptcx->sock);
    else
	numBytes = cs_retrieve(ptcx, ptcx->sock, CRLF CRLF, CRLF "." CRLF);

    event_stop(ptcx, &stats->msgread);

    if (numBytes < 0) {
	if (gf_timeexpired < EXIT_FAST) {
	    stats->msgread.errs++;
	    returnerr(debugfile,"POP3 Error retrieving msg %d: %s\n",
		      me->msgCounter, neterrstr());
	}
	doPop3Exit (ptcx, me);
	return -1;
    }

    /* if we're not told to leave mail on server, delete the message */
     leave_on_server = (int)sample(pish->leaveMailOnServerDist);
    if (!(pish->flags & leaveMailOnServer) ||
	 (leave_on_server == 0)) {
	/* send the DELE command */
	sprintf(command, "DELE %d%s", me->msgCounter, CRLF);
	event_start(ptcx, &stats->cmd);
	rc = doPopCommandResponse(ptcx, ptcx->sock, command, respBuffer, sizeof(respBuffer));
	event_stop(ptcx, &stats->cmd);
	if (rc == -1) {
	    if (gf_timeexpired < EXIT_FAST) {
		stats->cmd.errs++;
	    }
	    doPop3Exit (ptcx, me);
	    return -1;
	}
    }

    return 0;
}

void
doPop3End(ptcx_t ptcx, mail_command_t *cmd, cmd_stats_t *ptimer, void *mystate)
{
    doPOP3_state_t	*me = (doPOP3_state_t *)mystate;
    char	command[MAX_COMMAND_LEN];
    char	respBuffer[MAX_RESPONSE_LEN];
    int	rc;
    pish_stats_t	*stats = (pish_stats_t *)ptimer->data;

    if (BADSOCKET(ptcx->sock)) return;	/* closed by previous error */

    /* send QUIT */
    sprintf(command, "QUIT%s", CRLF);
    event_start(ptcx, &stats->logout);
    rc = doPopCommandResponse(ptcx, ptcx->sock, command, respBuffer, sizeof(respBuffer));
    event_stop(ptcx, &stats->logout);
    if (rc == -1) {
	if (gf_timeexpired < EXIT_FAST) {
	    stats->logout.errs++;	/* counted twice? */
	}
    }
    doPop3Exit (ptcx, me);
}

void
doPop3Exit (ptcx_t ptcx, doPOP3_state_t	*me)
{
  if (!BADSOCKET(ptcx->sock))
    NETCLOSE(ptcx->sock);
  ptcx->sock = BADSOCKET_VALUE;

  xfree (me);
}

/* Register this protocol with the system. */
void
Pop3Register(void)
{
    ProtocolRegister(
        "POP3", Pop3ParseStart, Pop3ParseEnd,
        doPop3Start, doPop3Loop, doPop3End,
        pishStatsFormat, pishStatsInit, pishStatsUpdate, pishStatsOutput,
        NULL, NULL);
}

#ifdef MODULE
/* This is the entry point if compiled as an independent module. */
void
ModuleRegister(void)
{
    Pop3Register();
}
#endif
