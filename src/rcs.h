/* -*- Mode: C; c-file-style: "stroustrup"; comment-column: 40 -*- */
/* Public definitions for Revision Control System protocols */
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

#ifndef _rcs_h
#define _rcs_h

/* these are protocol dependent timers for file system testing */
typedef struct rcs_stats {    // per thread data
    event_timer_t  connect;   // track hostname errors and connections
    event_timer_t  checkout;  // checkout data from repository
    event_timer_t  add;       // add new file to repository
    event_timer_t  del;       // delete file from repository
    event_timer_t  mod;       // a file was modified
    event_timer_t  list;      // list files in repository
    event_timer_t  diff;      // diff against an older version
    event_timer_t  log;       // log from an older version
    event_timer_t  blame;     // find "blame" from an older version
    event_timer_t  stat;      // status of changed files
    event_timer_t  mkdir;     // add new directory to repository
    event_timer_t  modify;    // modify files
    event_timer_t  update;    // update file from repository
    event_timer_t  commit;    // commit changed data to repository
    event_timer_t  logout;    // track current connection
    //FUTURE: metadata, permissions

    range_t        loginRange;  // login range for this thread
    unsigned long  loginLast;

    range_t        subDirRange; // subdir range for this thread
    unsigned long  subDirLast;

} rcs_stats_t;

/* read only command info */
typedef struct rcs_command {
    char *   loginFormat;   /* format string (integer argument) */
    range_t  loginRange;    /* login range for all threads */
    char *   passwdFormat;  /* password */

    char *  repoUrl;        /* repo URL */
    char *  topDir;         /* path under repoUrl */
    char *  subDir;         /* sub directory under topDir */
    range_t subDirRange;    /* sub directory range for all threads */
    char *  localDir;       /* local directory that holds the working copies */
    char *  interface;      /* which interface library to use (neon or serf) */

    dinst_t *addCnt;         /* number of files to add per loop */
    dinst_t *delCnt;         /* number of files to delete per loop */
    float   listCnt;         /* probability of a recursive listing per loop */
    dinst_t *diffCnt;        /* number of files to diff per loop */
    dinst_t *logCnt;         /* number of files to check logs per loop */
    dinst_t *blameCnt;       /* number of files to find blame per loop */
    dinst_t *statCnt;        /* number of times to stat per loop */
    dinst_t *mkdirCnt;       /* number of directories to add per loop */
    dinst_t *modCnt;         /* number of files to modify per loop */
    float   commitCnt;       /* probability of committing per loop */

    dinst_t *addFileSize;    /* bytes in each new file */
    dinst_t *modChunkSize;   /* size of each modification chunk */
    dinst_t *modChunkCnt;    /* number of modification chunks */
    int     commitRetryCnt;  /* number of time to re-try commit */
    dinst_t *logLimit;       /* log entry count (negative for reverse) */
    long    revSpan;         /* maximum revision span for diff/log/blame */
    long    revision;        /* revision to checkout (0=head) */
    long    updateStep;      /* Max rev delta for update (0=head) */
} rcs_command_t;

void StripFinalSlash(char *str);
int RcsParseNameValue (pmail_command_t cmd, char *name, char *tok);
int RcsParseStart (pmail_command_t cmd, char *line, param_list_t *defparm);
int RcsParseEnd (pmail_command_t cmd, string_list_t *section,
                 param_list_t *defparm);
void RcsStatsInit(mail_command_t *cmd, cmd_stats_t *p, int procNum,
                  int threadNum);
void RcsStatsUpdate(protocol_t *proto, cmd_stats_t *sum, cmd_stats_t *incr);
void RcsStatsOutput(protocol_t *proto, cmd_stats_t *ptimer, char *buf);
void RcsStatsFormat (protocol_t *pp, const char *extra, char *buf);
void RcsParseFree(pmail_command_t cmd);
void RcsFree(void);


// Define max length+1 of the user/password field
#ifndef USER_LEN
#define USER_LEN 80
#endif

// Define max length+1 of the full file path
#ifndef PATH_LEN
#define PATH_LEN 1024
#endif // PATH_LEN

// Define max length+1 of the commit message
#ifndef MSG_LEN
#define MSG_LEN 512
#endif // MSG_LEN

#endif /* _rcs_h */
