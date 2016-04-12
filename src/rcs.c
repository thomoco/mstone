/* -*- Mode: C; c-file-style: "stroustrup"; comment-column: 40 -*- */
/* rcs.c: Revision Control System common functions */
/* Copyright 2007 Google */
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
 * Contributor(s):      Dan Christian <dchristianATgoogle.com>
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

#include "bench.h"
#include "words.h"
#include "rcs.h"
#include "xalloc.h"

#include <errno.h>

#include <unistd.h>


/* Helper to trim the final slash from paths.
   str is modified in place.
*/
void
StripFinalSlash(char *str)
{
    char *cp, *sp = NULL;

    for (cp = str; *cp; ++cp) {
        if ('/' == *cp)
            sp = cp;
    }
    if (sp && (sp == cp-1)) {
        *sp = 0;
    }
}

int
RcsParseNameValue (pmail_command_t cmd,
                    char *name,
                    char *tok)
{
    rcs_command_t       *conf = (rcs_command_t *)cmd->data;

    //D_PRINTF(stderr, "RCS name: '%s' tok: '%s'\n", name, tok);
    /* find a home for the attr/value */
    if (cmdParseNameValue(cmd, name, tok)) { // generic parsing
        ;                               /* done */
    } else if (strcmp(name, "repourl") == 0) {
        conf->repoUrl = xstrdup (tok);
    } else if (strcmp(name, "topdir") == 0) {
        conf->topDir = xstrdup (tok);
    } else if (strcmp(name, "subdir") == 0) {
        conf->subDir = xstrdup (tok);
    } else if (strcmp(name, "firstsubdir") == 0) {
        conf->subDirRange.first = atoi(tok);
    } else if (strcmp(name, "numsubdir") == 0) {
        conf->subDirRange.span = atoi(tok);
    } else if (strcmp(name, "sequentialsubdir") == 0) {
        conf->subDirRange.sequential = atoi(tok);
    } else if (strcmp(name, "localdir") == 0) {
        conf->localDir = xstrdup(tok);
    } else if (strcmp(name, "interface") == 0) {
        conf->interface = xstrdup(tok);
    } else if (strcmp(name, "firstlogin") == 0) {
        conf->loginRange.first = atoi(tok);
    } else if (strcmp(name, "numlogins") == 0) {
        conf->loginRange.span = atoi(tok);
    } else if (strcmp(name, "sequentiallogins") == 0) {
        conf->loginRange.sequential = atoi(tok);
    } else if (strcmp(name, "loginformat") == 0) {
        conf->loginFormat = xstrdup (tok);
    } else if (strcmp(name, "passwdformat") == 0) {
        conf->passwdFormat = xstrdup (tok);
    } else if (strcmp(name, "worddict") == 0) {
        dictPath = xstrdup (tok);
    } else if (strcmp(name, "addfiles") == 0) {
        conf->addCnt = parse_distrib(tok, &atof);
    } else if (strcmp(name, "addsize") == 0) {
        conf->addFileSize = parse_distrib(tok, &size_atof);
    } else if (strcmp(name, "blame") == 0) {
        conf->blameCnt = parse_distrib(tok, &atof);
    } else if (strcmp(name, "commit") == 0) {
        conf->commitCnt = atof(tok);
    } else if (strcmp(name, "commitretry") == 0) {
        conf->commitRetryCnt = atoi(tok);
    } else if (strcmp(name, "deletefiles") == 0) {
        conf->delCnt = parse_distrib(tok, &atof);
    } else if (strcmp(name, "diff") == 0) {
        conf->diffCnt = parse_distrib(tok, &atof);
    } else if (strcmp(name, "list") == 0) {
        conf->listCnt = atof(tok);
    } else if (strcmp(name, "log") == 0) {
        conf->logCnt = parse_distrib(tok, &atof);
    } else if (strcmp(name, "loglimit") == 0) {
        conf->logLimit = parse_distrib(tok, &atof);
    } else if (strcmp(name, "mkdir") == 0) {
        conf->mkdirCnt = parse_distrib(tok, &atof);
    } else if (strcmp(name, "modfiles") == 0) {
        conf->modCnt = parse_distrib(tok, &atof);
    } else if (strcmp(name, "modchunksize") == 0) {
        conf->modChunkSize = parse_distrib(tok, (value_parser_t)&size_atof);
    } else if (strcmp(name, "modchunkcnt") == 0) {
        conf->modChunkCnt = parse_distrib(tok, &atof);
    } else if (strcmp(name, "stat") == 0) {
        conf->statCnt = parse_distrib(tok, &atof);
    } else if (strcmp(name, "revision") == 0) {
        conf->revision = atol(tok);
    } else if (strcmp(name, "revspan") == 0) {
        conf->revSpan = atol(tok);
    } else if (strcmp(name, "updatestep") == 0) {
        conf->updateStep = atol(tok);
    } else {
        return -1;
    }
    return 0;
}

/*
  Main entry:  Called to initialize a command block with defaults
  Set defaults in command structure
*/
int
RcsParseStart (pmail_command_t cmd,
                char *line,
                param_list_t *defparm)
{
    param_node_t        *pp;
    rcs_command_t       *conf = XCALLOC(rcs_command_t);
    cmd->data = conf;

    cmd->numLoops = 1;              /* default 1 change-commit loop */
    conf->commitCnt = 1.0;
    conf->commitRetryCnt = 5;

    D_PRINTF(stderr, "RCS Assign defaults\n");
    /* Fill in defaults first, ignore defaults we dont use */
    for (pp = defparm->head; pp; pp = pp->next) {
        (void)RcsParseNameValue (cmd, pp->name, pp->value);
    }

    return 1;
}

/*
  Main entry:  Called to fill in a command block from user text
  Fill in command structure from a list of lines
 */
int
RcsParseEnd (pmail_command_t cmd,
                string_list_t *section,
                param_list_t *defparm)
{
    list_node_t       *sp;
    rcs_command_t       *conf = (rcs_command_t *)cmd->data;

    /* Now parse section configuration lines */
                                        /* skip first and last */
    sp = section->head;
    if (sp->next) sp = sp->next;        /* skip first */
    for (; sp->next; sp = sp->next) {   /* skip last */
        char *name = sp->value;
        char *tok = name + strcspn(name, " \t=");
        *tok++ = 0;                     /* split name off */
        tok += strspn(tok, " \t=");

        string_tolower(name);
        tok = string_unquote(tok);

        if (RcsParseNameValue (cmd, name, tok) < 0) {
            /* not a known attr */
            D_PRINTF(stderr,"unknown attribute '%s' '%s'\n", name, tok);
            returnerr(stderr,"unknown attribute '%s' '%s'\n", name, tok);
        }
    }
    if (!conf->addCnt)
        conf->addCnt = parse_distrib("0", &atof);
    if (!conf->addFileSize)
        conf->addFileSize = parse_distrib("1024", &atof);
    if (!conf->blameCnt)
        conf->blameCnt = parse_distrib("0", &atof);
    if (!conf->delCnt)
        conf->delCnt = parse_distrib("0", &atof);
    if (!conf->diffCnt)
        conf->diffCnt = parse_distrib("0", &atof);
    /* logCnt handled below */
    if (!conf->logLimit)
        conf->logLimit = parse_distrib("1", &atof);
    if (!conf->mkdirCnt)
        conf->mkdirCnt = parse_distrib("0", &atof);
    /* modCnt handled below */
    if (!conf->modChunkCnt)
        conf->modChunkCnt = parse_distrib("1", &atof);
    if (!conf->modChunkSize)
        conf->modChunkSize = parse_distrib("20", &atof);
    if (!conf->statCnt)
        conf->statCnt = parse_distrib("0", &atof);

    if (!dictPath) {
        dictPath = "/usr/share/dict/words";
    }
    if (!syntaxChars) {
        // BUG: there is no way to pass this string on a config line.
        // Mostly spaces, but some typical program syntax characters.
        syntaxChars = xstrdup(" , . ; [ ] { } ! @ # $ % ^ & * \t \n");
    }
    if (!wordDict.indexLen) {
        if (ReadWordDict(&wordDict, dictPath) < 0) {
            return returnerr(stderr,
                             "Failed to read dictionary '%s'\n", dictPath);
        }
    }

    /* check for the required command attrs */
    if (!conf->passwdFormat) {
        conf->passwdFormat = xstrdup("");
    }

    if (!conf->loginFormat) {
        conf->loginFormat = xstrdup("");
    }

    if (conf->addCnt) {
        dinst_min_min(conf->addFileSize, 1.0);
    } else {
        conf->addCnt = parse_distrib("0", &atof);
    }
    if (conf->commitRetryCnt <= 0) {
        return returnerr(stderr, "commitRetryCnt must be > 0\n");
    }
    if (conf->modCnt) {
        dinst_min_min(conf->modChunkCnt, 1.0);
        dinst_min_min(conf->modChunkSize, 1.0);
    } else {
        conf->modCnt = parse_distrib("0", &atof);
    }

    rangeSetFirstCount (&conf->loginRange, conf->loginRange.first,
                        conf->loginRange.span, conf->loginRange.sequential);
    rangeSetFirstCount (&conf->subDirRange, conf->subDirRange.first,
                        conf->subDirRange.span, conf->subDirRange.sequential);

    /* Create the localDir */
    if (!conf->localDir) {
        conf->localDir = xstrdup("/tmp/mstonercs");
    }
    if ((mkdir(conf->localDir, 0777) < 0)  /* umask sets real perms */
        && (errno != EEXIST)) {
        return returnerr(stderr, "Unable to create '%s'\n", conf->localDir);
    }

    if (!conf->repoUrl) {
        return returnerr(stderr, "repoUrl must be set\n");
    } else {
        StripFinalSlash(conf->repoUrl);
    }
    if (!conf->topDir) {
        conf->topDir = xstrdup("");
    } else {
        StripFinalSlash(conf->repoUrl);
    }
    if (!conf->subDir) {
        conf->subDir = xstrdup("");
    } else {
        StripFinalSlash(conf->repoUrl);
    }
    return 1;
}


/* Main entry to initialize the statistics block for a thread */
void
RcsStatsInit(mail_command_t *cmd, cmd_stats_t *p, int procNum, int threadNum)
{
    assert (NULL != p);
    if (!p->data) {                     /* create it  */
        p->data = XCALLOC (rcs_stats_t);
        //LEAK: where is this freed?  client.c:700 doesnt seem to catch it.
    } else {                            /* zero it */
        memset (p->data, 0, sizeof (rcs_stats_t));
    }

    if (cmd) {
        rcs_command_t *conf = (rcs_command_t *)cmd->data;
        rcs_stats_t   *stats = (rcs_stats_t *)p->data;
        rangeSplit (&conf->loginRange, &stats->loginRange,
                    procNum, threadNum);
        stats->loginLast = (stats->loginRange.sequential < 0)
            ? stats->loginRange.last+1 : stats->loginRange.first-1;

        rangeSplit (&conf->subDirRange, &stats->subDirRange,
                    procNum, threadNum);
        stats->subDirLast = (stats->subDirRange.sequential < 0)
            ? stats->subDirRange.last+1 : stats->subDirRange.first-1;
    }
}

/* Main entry to sum statistics block for a thread into the main statistics */
void
RcsStatsUpdate(protocol_t *proto,
                 cmd_stats_t *sum,
                 cmd_stats_t *incr)
{
    rcs_stats_t *ss = (rcs_stats_t *)sum->data;
    rcs_stats_t *is = (rcs_stats_t *)incr->data;

    event_sum(&sum->idle,    &incr->idle);
    event_sum(&ss->connect,  &is->connect);
    event_sum(&ss->checkout, &is->checkout);
    event_sum(&ss->add,      &is->add);
    event_sum(&ss->del,      &is->del);
    event_sum(&ss->mkdir,    &is->mkdir);
    event_sum(&ss->mod,      &is->mod);
    event_sum(&ss->list,     &is->list);
    event_sum(&ss->diff,     &is->diff);
    event_sum(&ss->log,      &is->log);
    event_sum(&ss->blame,    &is->blame);
    event_sum(&ss->stat,     &is->stat);
    event_sum(&ss->update,   &is->update);
    event_sum(&ss->commit,   &is->commit);
    event_sum(&ss->logout,   &is->logout);

    event_reset(&incr->combined);       /* figure out total */
    event_sum(&incr->combined, &incr->idle);
    event_sum(&incr->combined, &is->connect);
    event_sum(&incr->combined, &is->checkout);
    event_sum(&incr->combined, &is->add);
    event_sum(&incr->combined, &is->del);
    event_sum(&incr->combined, &is->mkdir);
    event_sum(&incr->combined, &is->mod);
    event_sum(&incr->combined, &is->list);
    event_sum(&incr->combined, &is->diff);
    event_sum(&incr->combined, &is->log);
    event_sum(&incr->combined, &is->blame);
    event_sum(&incr->combined, &is->stat);
    event_sum(&incr->combined, &is->update);
    event_sum(&incr->combined, &is->commit);
    event_sum(&incr->combined, &is->logout);

    event_sum(&sum->combined, &incr->combined); /* add our total to sum-total*/

    sum->totalerrs += incr->totalerrs;
    sum->totalcommands += incr->totalcommands;
}

/* Main entry to output statistics block data */
void
RcsStatsOutput(protocol_t *proto,
                  cmd_stats_t *ptimer,
                  char *buf)
{
    char eventtextbuf[SIZEOF_EVENTTEXT];
    rcs_stats_t *stats = (rcs_stats_t *)ptimer->data;
    char *cp = buf;
    *cp = 0;

    /* blocks=%ld is handled for us */

    /* output proto independent total */
    event_to_text(&ptimer->combined, eventtextbuf);
    sprintf(cp, "total=%s ", eventtextbuf);
    while (*cp) ++cp;

    event_to_text(&stats->connect, eventtextbuf);
    sprintf(cp, "conn=%s ", eventtextbuf);
    while (*cp) ++cp;

    event_to_text(&stats->checkout, eventtextbuf);
    sprintf(cp, "checkout=%s ", eventtextbuf);
    while (*cp) ++cp;

    event_to_text(&stats->add, eventtextbuf);
    sprintf(cp, "add=%s ", eventtextbuf);
    while (*cp) ++cp;

    event_to_text(&stats->del, eventtextbuf);
    sprintf(cp, "del=%s ", eventtextbuf);
    while (*cp) ++cp;

    event_to_text(&stats->mkdir, eventtextbuf);
    sprintf(cp, "mkdir=%s ", eventtextbuf);
    while (*cp) ++cp;

    event_to_text(&stats->mod, eventtextbuf);
    sprintf(cp, "modify=%s ", eventtextbuf);
    while (*cp) ++cp;

    event_to_text(&stats->list, eventtextbuf);
    sprintf(cp, "list=%s ", eventtextbuf);
    while (*cp) ++cp;

    event_to_text(&stats->diff, eventtextbuf);
    sprintf(cp, "diff=%s ", eventtextbuf);
    while (*cp) ++cp;

    event_to_text(&stats->log, eventtextbuf);
    sprintf(cp, "log=%s ", eventtextbuf);
    while (*cp) ++cp;

    event_to_text(&stats->blame, eventtextbuf);
    sprintf(cp, "blame=%s ", eventtextbuf);
    while (*cp) ++cp;

    event_to_text(&stats->stat, eventtextbuf);
    sprintf(cp, "stat=%s ", eventtextbuf);
    while (*cp) ++cp;

    event_to_text(&stats->update, eventtextbuf);
    sprintf(cp, "update=%s ", eventtextbuf);
    while (*cp) ++cp;

    event_to_text(&stats->commit, eventtextbuf);
    sprintf(cp, "commit=%s ", eventtextbuf);
    while (*cp) ++cp;

    event_to_text(&stats->logout, eventtextbuf);
    sprintf(cp, "logout=%s ", eventtextbuf);
    while (*cp) ++cp;

    /* output proto independent idle */
    event_to_text(&ptimer->idle, eventtextbuf);
    sprintf(cp, "idle=%s ", eventtextbuf);
}

/* Main entry to print statistics block format information */
void
RcsStatsFormat (protocol_t *pp,
                  const char *extra,    /* extra text to insert (client=) */
                  char *buf)
{
    static char *timerList[] = {        /* must match order of StatsOutput */
        "total",
        "conn", "checkout", "add", "del", "mkdir", "modify",
        "list", "diff",  "log", "blame", "stat",
        "update", "commit", "logout",
        "idle" };

    char        **tp;
    char        *cp = buf;
    int         ii;

    /* Define the contents of each timer
      These must all the same, to that the core time functions
      can be qualified.  We specify each one for reduce.pl to work right.
    */

    for (ii=0, tp=timerList;
         ii < (sizeof (timerList)/sizeof (timerList[0]));
         ++ii, ++tp) {
        sprintf(cp, "<FORMAT %s TIMER=[%s]>%s</FORMAT>\n",
                extra, *tp,
                gs_eventToTextFormat);  /* match event_to_text*/
        for (; *cp; ++cp);      /* skip to the end of the string */
    }
                                        /* BUG blocks matches clientSummary */
    sprintf(cp, "<FORMAT %s PROTOCOL={%s}>blocks=blocks ",
            extra, pp->name);
    for (; *cp; ++cp);  /* skip to the end of the string */
    for (ii=0, tp=timerList;    /* same as above list (for now) */
         ii < (sizeof (timerList)/sizeof (timerList[0]));
         ++ii, ++tp) {
        sprintf (cp, "%s=[%s] ", *tp, *tp);
        for (; *cp; ++cp);      /* skip to the end of the string */
    }
    strcat (cp, "</FORMAT>\n");
}

void
RcsParseFree(pmail_command_t cmd)
{
    rcs_command_t       *conf = (rcs_command_t *)cmd->data;

    if (!conf) return;
    if (conf->repoUrl) xfree(conf->repoUrl);
    if (conf->topDir) xfree(conf->topDir);
    if (conf->subDir) xfree(conf->subDir);
    if (conf->localDir) xfree(conf->localDir);
    if (conf->loginFormat) xfree(conf->loginFormat);
    if (conf->passwdFormat) xfree(conf->passwdFormat);

    xfree(conf);
    cmd->data = NULL;
}

void
RcsFree(void)
{
    if (dictPath) xfree(dictPath);
    if (syntaxChars) xfree(syntaxChars);
    WordsDestroy(&wordDict);
    dictPath = NULL;
    syntaxChars = NULL;
}
