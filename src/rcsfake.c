/* -*- Mode: C; c-file-style: "stroustrup"; comment-column: 40 -*- */
/* rcsfake.c: generate fake revision control system data (for testing) */
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
#include "socket.h"
#include "xalloc.h"
#include "checksum.h"
#include <errno.h>

#include <unistd.h>

/* Protocol specific state for a single session. */
typedef struct _doFrcs_state {
    char             work_dir[PATH_LEN]; /* top dir of our checkouts/auth */
    char             password[USER_LEN];
    char             user[USER_LEN];
    words_t          files;          /* list of all checked out files */
    words_t          dirs;           /* list of all checked out dirs */
} doFrcs_state_t;

words_t  wordDict;      /* word dictionary for random generation */
char *   syntaxChars;               /* syntax string */
char *  dictPath;                   /* path to word dictionary */

#define CYCLE_PERIOD 60.0

/* Generate a delay sawtooth in absolute time */
static int
SawtoothTime(
    double offset,                      // sawtooth minimum
    double span,                        // sawtooth height (above offset)
    double period)                      // sawtooth period in seconds
{
    struct timeval now;
    long long n;
    double t1, t2;
    assert(period >= 0);

    if (gettimeofday(&now, NULL) < 0) {
        return -1;
    }
    t1 = now.tv_sec + now.tv_usec*1E-6;
    n = t1 / period;
    t2 = t1 - n*period;
    /*D_PRINTF(stderr, "SawtoothTime: %.6f %% %.6f = %.6f (%ld)\n",
      t1, period, t2, n);*/
    assert(t2 >= 0);
    assert(t2 <= period);
    t1 = span*(t2/period) + offset;
    assert(t1 >= 0);
    D_PRINTF(stderr, "SawtoothTime: o=%.3f s=%.3f p=%.3f @ %ld.%06ld = %.6f\n",
             offset, span, period, now.tv_sec, now.tv_usec, t1);
    MS_usleep((unsigned int)(1E6 * t1));
    return 0;
}

/* Initialize subversion libraries.
 Returns: 1 on success, else <= 0
*/
int
FrcsInit (void)
{
    D_PRINTF(stderr, "Rcsfake: global init finished.\n");

    return 1;
}

/*
  Main entry:  Called to initialize a command block with defaults
  Set defaults in command structure
*/
int
FrcsParseStart (pmail_command_t cmd,
                char *line,
                param_list_t *defparm)
{
    static int initialized;

    if (!initialized) {
        initialized++;
        if (FrcsInit() <= 0) {
            return 0;
        }
    }

    return RcsParseStart(cmd, line, defparm);
}

/*
  Main entry:  Called to fill in a command block from user text
  Fill in command structure from a list of lines
 */
int
FrcsParseEnd (pmail_command_t cmd,
                string_list_t *section,
                param_list_t *defparm)
{
    int ret;

    D_PRINTF(stderr, "Rcsfake: Assign section lines\n");
    ret = RcsParseEnd(cmd, section, defparm);
    if (ret < 0)
        return ret;

    return 1;
}


// Internal exit function.  Free all memory.
static void
doFrcsExit (ptcx_t ptcx, doFrcs_state_t *me)
{
  WordsDestroy(&me->files);
  WordsDestroy(&me->dirs);
  xfree (me);
}

// Main entry point at the start of a command block
// Creates and returns our session state
void *
doFrcsStart(ptcx_t ptcx, mail_command_t *cmd, cmd_stats_t *ptimer)
{
    doFrcs_state_t * me = XCALLOC (doFrcs_state_t);
    rcs_stats_t   * stats = (rcs_stats_t *)ptimer->data;
    rcs_command_t * conf = (rcs_command_t *)cmd->data;
    unsigned long   loginId, subDirId;
    int n, ret;                         /* temp return status */

    WordsInit(&me->files);
    WordsInit(&me->dirs);
    loginId = rangeNext (&stats->loginRange, stats->loginLast);
    stats->loginLast = loginId;
    snprintf(me->user, USER_LEN, conf->loginFormat, loginId);
    snprintf(me->password, USER_LEN, conf->passwdFormat, loginId);
    // get a unique woring dir
    snprintf(me->work_dir, PATH_LEN,
             "%s/work-%d-%d",
             conf->localDir, ptcx->processnum, ptcx->threadnum);

    D_PRINTF(stderr, "RCSFAKE: working dir '%s'\n", me->work_dir);
    {
        char buff[PATH_LEN];
        subDirId = rangeNext (&stats->subDirRange, stats->subDirLast);
        stats->subDirLast = subDirId;
        D_PRINTF(
            stderr,
            "rcsfake: repoUrl='%s' topDir='%s' subDir='%s' subDirId=%d rev=%d\n",
            conf->repoUrl, conf->topDir, conf->subDir, subDirId,
            conf->revision);
        snprintf(buff, sizeof(buff), conf->subDir, subDirId);
    }

    /* Check the current head revision to force authentication. */
    if (gf_timeexpired) goto _exit_fail;
    event_start(ptcx, &stats->connect);
    ret = SawtoothTime(0.1, 0.9, 3*CYCLE_PERIOD);
    event_stop(ptcx, &stats->connect);
    stats->connect.bytesread += 1024;
    if (ret < 0) {
        if (gf_timeexpired < EXIT_FAST)
            stats->connect.errs++;
        goto _exit_fail;
    }

    // checkout code
    if (gf_timeexpired) goto _exit_fail;
    event_start(ptcx, &stats->checkout);
    ret = SawtoothTime(1.5, 4.5, 3*CYCLE_PERIOD);
    event_stop(ptcx, &stats->checkout);
    stats->checkout.bytesread += 1024;
    if (ret < 0) {
        if (gf_timeexpired < EXIT_FAST)
            stats->checkout.errs++;
        goto _exit_fail;
    }

    for (n = 10; n > 0; --n) {         // create some dummy directories/files
      char chunk[80];
      char dir[PATH_LEN];
      int  i;
      ret = WordSequence(chunk, sizeof(chunk), &wordDict, "_", 2);
      if (ret < 0) {
        D_PRINTF(stderr, "rcsfake: Error generating words(add)\n");
        break;
      }
      snprintf(dir, sizeof(dir), "/nowhere/%s.d", chunk);
      WordAdd(&me->dirs, dir);
      for (i = 10; i > 0; --i) {
        char filename[PATH_LEN];
        ret = WordSequence(chunk, sizeof(chunk), &wordDict, "_", 2);
        if (ret < 0) {
          D_PRINTF(stderr, "rcsfake: Error generating words(add)\n");
          break;
        }
        snprintf(filename, sizeof(filename),
                 "%s/%s.m", dir, chunk);
        WordAdd(&me->files, filename);
      }
    }
    return me;

 _exit_fail:
    doFrcsExit (ptcx, me);
    return NULL;
}

// The body of a command block
int
doFrcsLoop(ptcx_t ptcx, mail_command_t *cmd,
             cmd_stats_t *ptimer, void *mystate)
{
    doFrcs_state_t       *me = mystate;
    rcs_stats_t *stats = (rcs_stats_t *)ptimer->data;
    rcs_command_t       *conf = (rcs_command_t *)cmd->data;
    int ret;
    double todo;
    int  tot_errors = 0, max_errors = 10;

    for (todo = conf->listCnt; todo > 0.0; todo = 0.0) { /* list files */
        if ((todo < 1.0) && ((RANDOM() & 0xffff) >= (0xffff * todo))) {
            break;
        }
        event_start(ptcx, &stats->list);
        ret = SawtoothTime(0.01, 0.09, CYCLE_PERIOD);
        event_stop(ptcx, &stats->list);
        stats->list.bytesread += 1024;
        if ((ret < 0) && (gf_timeexpired < EXIT_FAST)) {
            stats->list.errs++;
            if (++tot_errors >= max_errors) /* soft error */
                goto _exit_fail;
        }
    }

    for (todo = sample(conf->addCnt);
         todo > 0.0;
         todo -= 1.0) {                 /* Create/Add files */
        char chunk[80];
        char filename[PATH_LEN];
        int  ret;

        if ((todo < 1.0) && ((RANDOM() & 0xffff) >= (0xffff * todo))) {
            break;
        }
        // Create a random new filename under a random directory.
        ret = WordSequence(chunk, sizeof(chunk), &wordDict, "_", 2);
        if (ret < 0) {
            D_PRINTF(stderr, "rcsfake: Error generating words(add)\n");
            break;
        }
        snprintf(filename, sizeof(filename),
                 "%s/%s.m", WordRandom(&me->dirs), chunk);

        event_start(ptcx, &stats->add);
        ret = SawtoothTime(0.01, 0.09, CYCLE_PERIOD);
        event_stop(ptcx, &stats->add);
        stats->add.bytesread += 1024;
        if ((ret < 0) && (gf_timeexpired < EXIT_FAST)) {
            stats->add.errs++;
            if (++tot_errors >= max_errors) /* soft error */
                goto _exit_fail;
        }
        D_PRINTF(stderr, "rcsfake: ADD '%s'\n", filename);
    }

    for (todo = sample(conf->mkdirCnt);
         todo > 0.0;
         todo -= 1.0) {                 /* Add directories*/
        char chunk[80];
        char dirname[PATH_LEN];

        if ((todo < 1.0) && ((RANDOM() & 0xffff) >= (0xffff * todo))) {
            break;
        }
        // Create a random new directory under a random directory.
        ret = WordSequence(chunk, sizeof(chunk), &wordDict, "_", 2);
        if (ret < 0) {
            D_PRINTF(stderr, "rcsfake: Error generating directory name\n");
            break;
        }
        snprintf(dirname, sizeof(dirname),
                 "%s/%s.d", WordRandom(&me->dirs), chunk);

        // Add directory to frcs.
        event_start(ptcx, &stats->mkdir);
        ret = SawtoothTime(0.01, 0.09, CYCLE_PERIOD);
        event_stop(ptcx, &stats->mkdir);
        stats->mkdir.bytesread += 1024;
        if ((ret < 0) && (gf_timeexpired < EXIT_FAST)) {
            stats->mkdir.errs++;
            if (++tot_errors >= max_errors) /* soft error */
                goto _exit_fail;
        }
        D_PRINTF(stderr, "rcsfake: MKDIR '%s'\n", dirname);
    }

    for (todo = sample(conf->diffCnt);
         todo > 0.0;
         todo -= 1.0) {                 /* diff files */
        const char *filepath;
        if ((todo < 1.0) && ((RANDOM() & 0xffff) >= (0xffff * todo))) {
            break;
        }
        filepath = WordRandom(&me->files);
        if (!filepath) break;
        event_start(ptcx, &stats->diff);
        ret = SawtoothTime(0.01, 0.09, CYCLE_PERIOD);
        event_stop(ptcx, &stats->diff);
        stats->diff.bytesread += 1024;
        if ((ret < 0) && (gf_timeexpired < EXIT_FAST)) {
            stats->diff.errs++;
            if (++tot_errors >= max_errors) /* soft error */
                goto _exit_fail;
        }
    }

    for (todo = sample(conf->logCnt);
         todo > 0.0;
         todo -= 1.0) {                 /* get file logs */
        const char *filepath;
        if ((todo < 1.0) && ((RANDOM() & 0xffff) >= (0xffff * todo))) {
            break;
        }
        filepath = WordRandom(&me->files);
        if (!filepath) break;
        event_start(ptcx, &stats->log);
        ret = SawtoothTime(0.01, 0.09, CYCLE_PERIOD);
        event_stop(ptcx, &stats->log);
        stats->log.bytesread += 1024;
        if ((ret < 0) && (gf_timeexpired < EXIT_FAST)) {
            stats->log.errs++;
            if (++tot_errors >= max_errors) /* soft error */
                goto _exit_fail;
        }
    }

    for (todo = sample(conf->blameCnt);
         todo > 0.0;
         todo -= 1.0) {                 /* do file blame */
        const char *filepath;
        if ((todo < 1.0) && ((RANDOM() & 0xffff) >= (0xffff * todo))) {
            break;
        }
        filepath = WordRandom(&me->files);
        if (!filepath) break;
        event_start(ptcx, &stats->blame);
        ret = SawtoothTime(0.01, 0.09, CYCLE_PERIOD);
        event_stop(ptcx, &stats->blame);
        stats->blame.bytesread += 1024;
        if ((ret < 0) && (gf_timeexpired < EXIT_FAST)) {
            stats->blame.errs++;
            if (++tot_errors >= max_errors) /* soft error */
                goto _exit_fail;
        }
    }

    for (todo = sample(conf->delCnt);
         todo > 0.0;
         todo -= 1.0) {                 /* Delete files */
        if ((todo < 1.0) && ((RANDOM() & 0xffff) >= (0xffff * todo))) {
            break;
        }

        // Schedule file for removal from frcs.
        event_start(ptcx, &stats->del);
        ret = SawtoothTime(0.01, 0.09, CYCLE_PERIOD);
        event_stop(ptcx, &stats->del);
        stats->del.bytesread += 1024;
        if ((ret < 0) && (gf_timeexpired < EXIT_FAST)) {
            stats->del.errs++;
            if (++tot_errors >= max_errors) /* soft error */
                goto _exit_fail;
        }
    }

    for (todo = sample(conf->modCnt);
         todo > 0.0;
         todo -= 1.0) {                 /* Modify files */
        if ((todo < 1.0) && ((RANDOM() & 0xffff) >= (0xffff * todo))) {
            break;
        }
        // Track modifies to count changed files and indicate client load.
        event_start(ptcx, &stats->mod);
        ret = SawtoothTime(0.01, 0.09, CYCLE_PERIOD);
        event_stop(ptcx, &stats->mod);
        if ((ret < 0) && (gf_timeexpired < EXIT_FAST)) {
            stats->stat.errs++;
            if (++tot_errors >= max_errors) /* soft error */
                goto _exit_fail;
        }
    }

    for (todo = sample(conf->statCnt);
         todo > 0.0;
         todo -= 1.0) {                 /* stat files */
        if ((todo < 1.0) && ((RANDOM() & 0xffff) >= (0xffff * todo))) {
            break;
        }
        event_start(ptcx, &stats->stat);
        ret = SawtoothTime(0.01, 0.09, CYCLE_PERIOD);
        event_stop(ptcx, &stats->stat);
        stats->stat.bytesread += 1024;
        if ((ret < 0) && (gf_timeexpired < EXIT_FAST)) {
            stats->stat.errs++;
            if (++tot_errors >= max_errors) /* soft error */
                goto _exit_fail;
        }
    }

    event_start(ptcx, &stats->update);
    ret = SawtoothTime(0.01, 0.09, CYCLE_PERIOD);
    event_stop(ptcx, &stats->update);
    stats->update.bytesread += 1024;

    for (todo = conf->commitCnt; todo > 0.0; todo = 0.0)  { /* Commit (once) */
        if ((todo < 1.0) && ((RANDOM() & 0xffff) >= (0xffff * todo))) {
            break;
        }
        event_start(ptcx, &stats->commit);
        ret = SawtoothTime(0.01, 0.09, CYCLE_PERIOD);
        event_stop(ptcx, &stats->commit);
        stats->commit.byteswritten += 1024;
        if ((ret < 0) && (gf_timeexpired < EXIT_FAST)) {
            stats->stat.errs++;
            if (++tot_errors >= max_errors) /* soft error */
                goto _exit_fail;
        }
    }

    return 0;

 _exit_fail:
    return -1;

}

// Exit a command block
void
doFrcsEnd(ptcx_t ptcx, mail_command_t *cmd,
            cmd_stats_t *ptimer, void *mystate)
{
    doFrcs_state_t *me = (doFrcs_state_t *)mystate;
    rcs_stats_t   *stats = (rcs_stats_t *)ptimer->data;

    /* Logout allows us to track concurrent clients */
    event_start(ptcx, &stats->logout);
    SawtoothTime(0.1, 0.9, 3*CYCLE_PERIOD);
    event_stop(ptcx, &stats->logout);

    doFrcsExit (ptcx, me);
}

void
FrcsParseFree(pmail_command_t cmd)
{
    RcsParseFree(cmd);
}

void
FrcsFree(void)
{
    RcsFree();
}

/* Register this protocol with the system. */
void
FrcsRegister(void)
{
    ProtocolRegister(
        "RCSFAKE", FrcsParseStart, FrcsParseEnd,
        doFrcsStart, doFrcsLoop, doFrcsEnd,
        RcsStatsFormat, RcsStatsInit, RcsStatsUpdate, RcsStatsOutput,
        FrcsParseFree, FrcsFree);
}

#ifdef MODULE
/* This is the entry point if compiled as an independent module. */
void
ModuleRegister(void)
{
    FrcsRegister();
}
#endif
