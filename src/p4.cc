/* -*- Mode: C++; c-file-style: "stroustrup"; comment-column: 40 -*- */
/* p4.cc: test perforce (p4) client operations */
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

// Tested against perforce version 2007.2.
// Server/p4api versions older than 2005.2 are highly unlikely to work.
// Version 2005.2 through 2007.1 may work, but haven't been tested.
// If you try one of these versions, please e-mail the Contributor (above).

extern "C" {
#include "bench.h"
#include "words.h"
#include "rcs.h"
#include "socket.h"
#include "xalloc.h"
#include "checksum.h"
}

#include <errno.h>
#include <unistd.h>
#include <regex.h>

#include "p4/clientapi.h"

#define NUM_LEN 16                      // length of buffer to hold CL number

/* Protocol specific state for a single session. */
typedef struct _doP4_state {
    ClientApi        *client;
    char             password[USER_LEN];
    char             user[USER_LEN];
    char             client_name[USER_LEN];

    char *           work_dir;       /* top dir of our client (malloc) */
    char *           checkout_path;  /* full checkout path (malloc) */
    words_t          resolve_paths;  /* array of files to be resolved */
    words_t          revert_paths;   /* array of files to be reverted */
    words_t          files;          /* list of all checked out files */
    words_t          dirs;           /* list of all checked out dirs */
    int              changed;        /* count of edit/add/del files */
    unsigned long    total_bytes;    /* total bytes from last operation */
    long             head_rev;       /* the head revision */
    long             our_rev;        /* the checked out revision */
    int             flags;           /* special flags */
} doP4_state_t;

const int P4_SAVE_CLIENT = 0x01;      /* don't nuke client at end. */

// TODO: override signal handling behavior

// Bind KeepAlive::IsAlive() to test shutdown
class P4StoneKeepAlive : public ::KeepAlive {
public:
    int IsAlive() { return (gf_timeexpired < EXIT_FAST); }
    virtual ~P4StoneKeepAlive() {}
};
P4StoneKeepAlive g_P4Alive;   // Global object to handle time expired


static int
_format_strdict(char *buff, int len, StrDict *varList)
{
    int n = 0;
    int remain=len-3;                   // always leave room for {}
    StrRef  key, value;
    char *cp = buff;
    *cp++ = '{';

    while (0 != varList->GetVar(n++, key, value)) {
        if (0 == strcmp("func", key.Text())) // no info, skip this field
            continue;
        snprintf(cp, remain,
                 (0 == strcmp("data", key.Text()))
                   ? "%s%s='%.40s...'" : "%s%s=%s",
                 (n > 1) ? ", " : "", key.Text(), value.Text());
        while (*cp) {          // find new end, update space remaining
            cp++;
            --remain;
        }
    }
    *cp++ = '}';
    *cp = 0;
    return strlen(buff);
}


// This class provides class hooks into the Perforce SDK.  Perforce
// calls OutputStat() and provides tagged data via a StrDict.
class P4StoneUser : public ::ClientUser {
public:
    int ignore_return;  // if set, ignore p4 error value and return OK

    P4StoneUser(doP4_state_t *me=NULL, rcs_command_t *config=NULL)
        { state = me; conf = config; ignore_return = 0;}

    virtual void     HandleError(Error *err)
        {
            StrBuf err_msg;
            err->Fmt(&err_msg);
            D_PRINTF(stderr, "P4 HandleError: %s", err_msg.Text());
        }

    virtual void     Message(Error *err)
        {
            StrBuf err_msg;
            err->Fmt(&err_msg);
            error_text.Append(&err_msg);
            D_PRINTF(stderr, "P4 Message: %s", err_msg.Text());
        }

    virtual void     OutputError(const char *errBuf)
        {
            error_text.Append(errBuf);
            D_PRINTF(stderr, "P4 OutputError: %s\n", errBuf);
        }

    virtual void     OutputInfo(char level, const char *data)
        { D_PRINTF(stderr, "P4 OutputInfo[%d]: %s\n", level, data); }

    virtual void     OutputBinary(const char *data, int length)
        { D_PRINTF(stderr, "P4 OutputBinary (len=%d)\n", length); }

    virtual void     OutputText(const char *data, int length)
        { D_PRINTF(stderr, "P4 OutputText (len=%d)\n", length); }

    virtual void     OutputStat(StrDict *varList)
        {
            char buff[5*PATH_LEN];
            _format_strdict(buff, sizeof(buff), varList);
            D_PRINTF(stderr,  "P4 OutputStat: %s\n", buff);
        }

    virtual void     InputData(StrBuf *strbuf, Error *e)
        { D_PRINTF(stderr, "P4 InputData\n"); }
    virtual void     Prompt(const StrPtr &msg, StrBuf &rsp,
				int noEcho, Error *e)
        // Note: msg and rsp seem to be the same StrBuf
        { D_PRINTF(stderr, "P4 Prompt: %s\n", msg.Text()); rsp.Set("abort"); }

    virtual void     ErrorPause(char *errBuf, Error *e)
        { D_PRINTF(stderr, "P4 ErrorPause: %s\n", errBuf); }

    virtual void     Edit(FileSys *f1, Error *e)
        {
            D_PRINTF(stderr, "P4 Edit %s\n", f1->Path()->Text());
        }

    virtual void     Diff(FileSys *f1, FileSys *f2, int doPage,
                              char *diffFlags, Error *e)
        { D_PRINTF(stderr, "P4 Diff %s '%s' '%s'\n", diffFlags,
                   f1->Path()->Text(), f2->Path()->Text()); }

    virtual void     Merge(FileSys *base, FileSys *leg1, FileSys *leg2,
                               FileSys *result, Error *e)
        { D_PRINTF(stderr, "P4 Merge '%s' '%s\n",
                   leg1->Path()->Text(), leg2->Path()->Text()); }

    virtual int	     Resolve(ClientMerge *m, Error *e)
        { D_PRINTF(stderr, "P4 Resolve\n"); return (int)m->Resolve(e ); }

    virtual void     Help(const char *const *help)
        { for (; *help; help++) D_PRINTF(stderr, "P4 Help: %s\n", *help); }

    virtual FileSys  *File(FileSysType type)
        {
            //D_PRINTF(stderr, "P4 File of type 0x%X\n", type);
            return FileSys::Create(type);
        }

    virtual void     Finished() {}
    //{ D_PRINTF(stderr, "P4 Finished\n"); }

    virtual void     SetOutputCharset(int charset)
        { D_PRINTF(stderr, "P4 SetOutputCharset: %d\n", charset); }

    doP4_state_t * state;               // run state
    rcs_command_t * conf;               // static configuration state
    StrBuf error_text;                  // store any error messages
};

/* remove final newline from string (by terminating the string early) */
static char *
chomp(char *str)
{
    char *cp;
    if (!str) return str;
    for (cp = str; *cp; ++cp);          // find end
    while (--cp >= str) {
        if (('\n' == *cp) || ('\r' == *cp)) {
            *cp = 0;
        } else {
            return str;
        }
    }
    return str;
}

/* Format a p4 error message and return -1. */
static int
ret_p4_err(int argc, char * const *argv, const char *msg,
           int ret, Error *err, StrBuf *error_text,
           int argc2, char * const *argv2)
{
    StrBuf err_msg;
    time_t      t=time(0L) - gt_startedtime;

    char buff[4*PATH_LEN], *cp=buff;
    int ii;
    *cp = 0;
    for (ii=0; ii < argc; ++ii) {
        if (cp+strlen(argv[ii])+1 >= buff+sizeof(buff)) break;
        strcpy(cp, argv[ii]);
        while (*cp) cp++;
        *cp++ = ' ';
        *cp = 0;
    }
    if ((argc2) && (cp+3 < buff+sizeof(buff))) {
        *cp++ = ';'; *cp++ = ' '; *cp = 0;
        for (ii=0; ii < argc2; ++ii) {
            if (cp+strlen(argv2[ii])+1 >= buff+sizeof(buff)) break;
            strcpy(cp, argv2[ii]);
            while (*cp) cp++;
            *cp++ = ' ';
            *cp = 0;
        }
    }

    if (err && err->Test()) {
        err->Fmt(&err_msg);
        if (error_text && error_text->Length() > 0) {
            err_msg.Append(" (");
            err_msg.Append(chomp(error_text->Text()));
            err_msg.Append(")");
        }
        fprintf(stderr, "%s[%d]\tt=%lu: P4 %s %s: %s\n",
                gs_shorthostname, gn_myPID, t,
                msg, buff, err_msg.Text());
    } else if (ret) {
        if (error_text && error_text->Length() > 0) {
            err_msg.Append(" (");
            err_msg.Append(chomp(error_text->Text()));
            err_msg.Append(")");
        }
        fprintf(stderr, "%s[%d]\tt=%lu: P4 %s %s: %d%s\n",
                gs_shorthostname, gn_myPID, t,
                msg, buff, ret, err_msg.Text());
    } else  {
        if (error_text && error_text->Length() > 0) {
            err_msg.Append(" (");
            err_msg.Append(chomp(error_text->Text()));
            err_msg.Append(")");
        }
        fprintf(stderr, "%s[%d]\tt=%lu: P4 %s %s%s\n",
                gs_shorthostname, gn_myPID, t,
                msg, buff, err_msg.Text());
    }
    return -1;
}

/* Connect, run 2 commands, disconnect, return command status (for both) */
static int
do_p4_cmd2(ptcx_t ptcx, event_timer_t *timer,
           int argc, char * const *argv,
           int argc2, char * const *argv2,
           P4StoneUser *handler)
{
    Error e;

    if (gn_debug > 0) {
        ret_p4_err(argc, argv, "Begin: p4", 0, NULL, NULL, argc2, argv2);
    }
    handler->ignore_return = 0;
    handler->error_text.Clear();
    handler->state->client->SetProtocol( "tag", ""); // machine friendly data
    handler->state->client->SetProtocol( "specstring", "");
    //handler->state->client->SetProtocol( "api", "???");
    handler->state->client->SetPort(handler->conf->repoUrl);
    handler->state->client->SetClient(handler->state->client_name);
    handler->state->client->SetUser(handler->state->user);
    handler->state->client->SetPassword(handler->state->password);

    if (timer) event_start(ptcx, timer);
    handler->state->client->Init(&e);
    if (e.Test()) {                     // connection failed
        MS_idle(ptcx, 1000);            // HACK: back off and re-try
        handler->state->client->Init(&e);
    }
    if (e.Test()) {                     // connection failed
        if (timer) {
            event_stop(ptcx, timer);
            // TODO: connect errors should be counted separately
        }
        return ret_p4_err(argc, argv, "connect Error", 0,
                          &e, &handler->error_text, argc2, argv2);
    }
    handler->state->client->SetBreak(&g_P4Alive); // set out-of-time handler
    handler->state->client->SetProg("mstone");

    if (argc > 1) {
        handler->state->client->SetArgv(argc-1, argv+1);
    }
    handler->state->client->Run(argv[0], handler);

    if (argv2) {
        if (argc2 > 1) { /* BUG: what happens if argc>1 but argc2==0 ??? */
            handler->state->client->SetArgv(argc2-1, argv2+1);
        }
        handler->state->client->Run(argv2[0], handler);
    }

    int ret;
    ret = handler->state->client->Final(&e);
    if (timer) event_stop(ptcx, timer);
    if (ret - handler->ignore_return > 0) {
        if (g_P4Alive.IsAlive()) {
            if (timer) timer->errs++;
            ret_p4_err(argc, argv, "Error",
                       ret, &e, &handler->error_text,
                       argc2, argv2);
            MS_idle(ptcx, 1000);       // HACK:  reduce load on server
        }
        return -1;
    }
    return 0;
}

/* Connect, run command, disconnect, return command status */
static int
do_p4_cmd(ptcx_t ptcx, event_timer_t *timer,
          int argc, char * const *argv, P4StoneUser *handler)
{
    return do_p4_cmd2(ptcx, timer, argc, argv, 0, NULL, handler);
}

// try to revert everything
int
_revert(ptcx_t ptcx, doP4_state_t *me, event_timer_t *timer,
            long rev, P4StoneUser *handler)
{
    const char* argv[] = {"revert", "//...", 0};
    int argc = 2;
    int ret = 0;

    if (rev > 0) {                      // and delete changelist
        const char* argv2[] = {"change", "-d", 0, 0};
        int argc2 = 2;
        char num_buff[NUM_LEN];
        snprintf(num_buff, sizeof(num_buff), "%ld", rev);
        argv2[argc2++] = num_buff;
        if (me->changed > 0) {
            ret = do_p4_cmd2(ptcx, timer,
                             argc, const_cast<char* const*>(argv),
                             argc2, const_cast<char* const*>(argv2),
                             handler);
        } else {                        // only delete the changelist
            ret = do_p4_cmd(ptcx, timer,
                             argc2, const_cast<char* const*>(argv2),
                             handler);
        }
    } else if (me->changed > 0) {
        ret = do_p4_cmd(ptcx, timer,
                        argc, const_cast<char* const*>(argv),
                        handler);
    }
    if (0 == ret)
        me->changed = 0;
    return ret;
}

/* Helper to sync with depot */
static int
_delete_client(ptcx_t ptcx, rcs_stats_t *stats,
     rcs_command_t * conf, doP4_state_t *me)
{
    const char* argv[] = {"client", "-d", 0, 0};
    // Only an admin can use the -f flag.
    int argc = 2;
    P4StoneUser handler(me, conf);

    argv[argc++] = me->client_name;
    int ret;
    if (me->changed > 0) {              // revert, then delete
        const char* argv1[] = {"revert", "//...", 0};
        int argc1 = 2;
        ret = do_p4_cmd2(ptcx, &stats->logout,
                         argc1, const_cast<char* const*>(argv1),
                         argc, const_cast<char* const*>(argv),
                         &handler);
    } else {                        // only delete the changelist
        ret = do_p4_cmd(ptcx, &stats->logout,
                        argc, const_cast<char* const*>(argv), &handler);
    }
    if (ret) {
        _revert(ptcx, me, NULL, 0, &handler); // revert and re-try
        ret = do_p4_cmd(ptcx, &stats->logout,
                        argc, const_cast<char* const*>(argv), &handler);
    }
    if ((ret) && (gf_timeexpired < EXIT_FAST)) {
        return -1;
    }
    returnerr(stderr, "P4: Deleted client '%s'\n", me->client_name);
    return 0;
}

// Internal exit function.  Free all memory.
static void
doP4Exit (ptcx_t ptcx, doP4_state_t *me)
{
    WordsDestroy(&me->resolve_paths);
    WordsDestroy(&me->revert_paths);
    WordsDestroy(&me->dirs);
    WordsDestroy(&me->files);
    if (me->checkout_path) xfree(me->checkout_path);
    if (me->work_dir) xfree(me->work_dir);
    if (me->client) delete me->client;

    xfree (me);
}

static void
_build_dirs(words_t *dirs, words_t *files)
{
    unsigned int index;
    char buff[PATH_LEN], last[PATH_LEN];
    last[0] = 0;
    for (index = 0; index < files->indexLen; ++index) {
        strncpy(buff, files->index[index], sizeof(buff));
        char *cp = strrchr(buff, '/');
        if (NULL == cp) {               // relative path, should never happen
            continue;
        }
        *cp = 0;                                  // trim final /
        if (*last && (0 == strcmp(last, buff))) { // 1 entry cache
            continue;
        }
        if (WordFind(dirs, buff) >= 0) { // already there (linear search)
            continue;
        }
        WordAdd(dirs, buff);
        strncpy(last, buff, sizeof(last));
    }
}

// sync must track files added/deleted
class P4SyncUser : public ::P4StoneUser {
public:
    P4SyncUser(doP4_state_t *me, rcs_command_t *config)
        { state = me; conf = config; ignore_return = 0;}


    virtual void Message(Error *err)
        {
            StrBuf err_msg;
            err->Fmt(&err_msg);
            error_text.Append(&err_msg);
            char *line = err_msg.Text();
            D_PRINTF(stderr, "P4Sync Message: %s", line);
            if (0 == strncmp(line, "File(s) up-to-date.", 16)) {
                ++ignore_return;
            }
            // TODO: handle "//depot/PATH#2 - is opened and not being changed"
        }

    virtual void OutputStat(StrDict *varList)
        {
            {
                char buff[5*PATH_LEN];
                _format_strdict(buff, sizeof(buff), varList);
                D_PRINTF(stderr,  "P4Sync OutputStat: %s\n", buff);
            }

            int n = 0;
            StrRef  key, value;
            StrRef  client_file(""), action("");

            while (0 != varList->GetVar(n++, key, value)) {
                if (0 == strcmp("fileSize", key.Text())) {
                    state->total_bytes += atoi(value.Text());
                } else if (0 == strcmp("clientFile", key.Text())) {
                    client_file.Set(value);
                } else if (0 == strcmp("action", key.Text())) {
                    action.Set(value);
                }
            }
            if (client_file != "") {
                assert(action != "");
                if (action == "added") {
                    WordAdd(&state->files, client_file.Text());
                } else if (action == "deleted") {
                    if (WordDelete(&state->files, client_file.Text()) < 0) {
                        D_PRINTF(stderr, "Deleted file not found '%s'\n",
                                 client_file.Text());
                    }
                }
            }
        }

};

/* Helper to sync with depot */
static int
_sync(ptcx_t ptcx, rcs_stats_t *stats,
      rcs_command_t * conf, doP4_state_t *me,
      event_timer_t *timer, long rev, int flags)
{
    const char* argv[] = {"sync", 0, 0, 0, 0};
    int argc = 1;
    P4SyncUser handler(me, conf);
    char rev_buff[NUM_LEN];

    if (flags) {                        // only FORCE flag defined for now
        argv[argc++] = "-f";
    }
    if (rev > 0) {                      // set checkout revision
        snprintf (rev_buff, sizeof(rev_buff), "#%ld", rev);
        argv[argc++] = rev_buff;
    }

    int ret;
    me->total_bytes = 0;
    ret = do_p4_cmd(ptcx, timer,
                    argc, const_cast<char* const*>(argv), &handler);
    if (timer) {
        timer->bytesread += me->total_bytes;
    }
    if ((ret) && (gf_timeexpired < EXIT_FAST)) {
        return -1;
    }
    return 0;
}

// Override certain methods to automate client setup
class P4StartUser : public ::P4StoneUser {
public:
    P4StartUser(doP4_state_t *me, rcs_command_t *config)
        { state = me; conf = config; ignore_return = 0;}

    virtual void Edit(FileSys *f1, Error *e)
        {
            char desc_msg[60];                 /* TODO: configurable size */
            int ret;
            ret = WordSequence(desc_msg, sizeof(desc_msg),
                               &wordDict, " , . ; ", 0);
            if (ret < 0) {
                D_PRINTF(stderr, "svn: Error generating words(client)\n");
                strcpy(desc_msg, "mstone client");
            }
            StrBuf tmppath;
            tmppath.Set(f1->Path()->Text());
            tmppath.Append("~");

            FILE *fp = fopen(tmppath.Text(), "w"); // write client spec
            fprintf(fp, "Client:\t%s\n\n", state->client_name);
            fprintf(fp, "Owner:\t%s\n\n", state->user);
            fprintf(fp, "Host:\t%s\n\n", gs_thishostname);
            fprintf(fp, "Description:\n\t%s\n\n", desc_msg);
            fprintf(fp, "Root:\t%s\n\n", state->work_dir);
            fprintf(fp, "Options:\t%s\n\n",
                "noallwrite noclobber nocompress unlocked nomodtime normdir");
            fprintf(fp, "SubmitOptions:\t%s\n\n", "submitunchanged");
            fprintf(fp, "LineEnd:\t%s\n\n", "local");
            fprintf(fp, "View:\n\t//depot/%s/... //%s/...\n\n",
                    state->checkout_path, state->client_name);
            fclose(fp);
            D_PRINTF(stderr, "P4Start client edit %s %s\n",
                     f1->Path()->Text(), tmppath.Text());
            rename(tmppath.Text(), f1->Path()->Text());
        }

    virtual void OutputStat(StrDict *varList)
        {
            {
                char buff[5*PATH_LEN];
                _format_strdict(buff, sizeof(buff), varList);
                D_PRINTF(stderr,  "P4Start OutputStat: %s\n", buff);
            }

            int n = 0;
            StrRef  key, value;

            while (0 != varList->GetVar(n++, key, value)) {
                if (0 == strcmp("fileSize", key.Text())) {
                    state->total_bytes += atoi(value.Text());
                } else if (0 == strcmp("value", key.Text())) {
                    // ASSUMES this is the "counter change" query
                    state->head_rev += atoi(value.Text());
                } else if (0 == strcmp("clientFile", key.Text())) {
                    WordAdd(&state->files, value.Text()); // track files
                }
            }
        }

};

// Main entry point at the start of a command block
// Creates and returns our session state
extern "C" void *
doP4Start(ptcx_t ptcx, mail_command_t *cmd, cmd_stats_t *ptimer)
{
    doP4_state_t * me = XCALLOC (doP4_state_t);
    rcs_stats_t   * stats = (rcs_stats_t *)ptimer->data;
    rcs_command_t * conf = reinterpret_cast<rcs_command_t *>(cmd->data);
    unsigned long   loginId, subDirId;
    //int ret;                            /* temp return status */
    Error e;
    struct stat stat_buf;
    P4StartUser handler(me, conf);

    me->client = new ClientApi();
    WordsInit(&me->files);
    WordsInit(&me->dirs);
    loginId = rangeNext (&stats->loginRange, stats->loginLast);
    stats->loginLast = loginId;
    snprintf(me->user, USER_LEN, conf->loginFormat, loginId);
    snprintf(me->password, USER_LEN, conf->passwdFormat, loginId);

    // get a unique client name
    snprintf(me->client_name, USER_LEN,
             "%s-%d-%d",
             gs_shorthostname, ptcx->processnum, ptcx->threadnum);
    int ret;

    {
        StrBuf path;
        path.Set(conf->localDir);
        path.Append("/client-");
        path.Append(me->client_name);
        me->work_dir = xstrdup(path.Text());

        if (stat(me->work_dir, &stat_buf) >= 0) {
            // We try to save core files from previous runs, but they
            // mix with any this run dumps.
            if (nuke_dir(me->work_dir, conf->localDir) < 0) {
                returnerr(stderr, "P4: Error purging work_dir (setup)\n");
                goto _exit_fail;
            }
        }
        if (0 != mkdir(me->work_dir, 0777)) { /* umask sets real perms */
            returnerr(stderr, "P4: Failed to mkdir '%s': %s\n",
                      me->work_dir, strerror(errno));
            goto _exit_fail;
        }
        if (chdir(me->work_dir) < 0) {
            returnerr(stderr, "P4: Failed to chdir '%s': %s\n",
                      me->work_dir, strerror(errno));
            goto _exit_fail;
        }
        D_PRINTF(stderr, "P4 working dir '%s'\n", me->work_dir);
        // write .p4config with client name, user, and password
        FILE *fp = fopen(".p4config", "w");
        if (!fp) {
            returnerr(stderr, "P4: Failed to open '%s': %s\n",
                      path.Text(), strerror(errno));
            goto _exit_fail;
        }
        fprintf(fp, "P4CLIENT=%s\n", me->client_name);
        fprintf(fp, "P4USER=%s\n", me->user);
        //fprintf(fp, "P4PORT=%s\n", conf->repoUrl);
        //fprintf(fp, "P4PASSWD=%s\n", me->password);
        fclose(fp);
    }

    {                                   // determine checkout path
        char buff[PATH_LEN];
        subDirId = rangeNext (&stats->subDirRange, stats->subDirLast);
        stats->subDirLast = subDirId;
        D_PRINTF(
            stderr,
            "p4: repo='%s' topDir='%s' subDir='%s' subDirId=%d rev=%d\n",
            conf->repoUrl, conf->topDir, conf->subDir, subDirId,
            conf->revision);
        snprintf(buff, sizeof(buff), conf->subDir, subDirId);
        StrBuf path;
        path.Set(conf->topDir);
        if (buff && *buff) {
            path.Append("/");
            path.Append(buff);
        }
        me->checkout_path = xstrdup(path.Text());
        ret = strlen(me->checkout_path) - 1;
        if ('/' == me->checkout_path[ret]) { // trim '/' from path end
            me->checkout_path[ret] = 0;
        }
    }

    D_PRINTF(stderr,
             "p4: INIT user='%s' url='%s'\n", me->user, me->checkout_path);
    {                                   // p4 client
        char *argv[] = {"client", 0, 0};
        int argc = 1;

        argv[argc++] = me->client_name;
        ret = do_p4_cmd(ptcx, &stats->connect,
                        argc, argv, &handler);
        if (ret) {
            goto _exit_fail;
        }
    }
    {                                   // get depot revision
        char *argv[] = {"counter", "change", 0};
        int argc = 2;

        ret = do_p4_cmd(ptcx, NULL,
                        argc, argv, &handler);
        if (!me->head_rev) {
            _delete_client(ptcx, stats, conf, me);
            goto _exit_fail;
        }
    }
    me->our_rev = (conf->revision > 0) ? conf->revision : me->head_rev;
    ret = _sync(ptcx, stats, conf, me, &stats->checkout, conf->revision, 1);
    if (ret) {
        _delete_client(ptcx, stats, conf, me);
        goto _exit_fail;
    }
    _build_dirs(&me->dirs, &me->files);
    if (!me->files.indexLen || !me->dirs.indexLen) {
        returnerr(stderr,
                  "P4: %d-%d Setup FAILED '%s' @%ld (%d files in %d dirs)\n",
                  ptcx->processnum, ptcx->threadnum,
                  me->checkout_path, me->our_rev,
                  me->files.indexLen, me->dirs.indexLen);
        _delete_client(ptcx, stats, conf, me);
        goto _exit_fail;
    }
    returnerr(stderr, "P4: %d-%d Got '%s' @%ld (%d files in %d dirs)\n",
              ptcx->processnum, ptcx->threadnum,
              me->checkout_path, me->our_rev,
              me->files.indexLen, me->dirs.indexLen);
    return me;

 _exit_fail:
    doP4Exit (ptcx, me);
    return NULL;
}

/* Helper to list all files */
/* Do files+dirs to match SVN's list. */
static int
_list(ptcx_t ptcx, rcs_stats_t *stats,
     rcs_command_t * conf, doP4_state_t *me)
{
    P4StoneUser handler(me, conf);
    const char* argv[] = {"files", 0, 0};
    int argc = 1;
    char buff[15+PATH_LEN];
    const char* argv2[] = {"dirs", 0, 0};
    int argc2 = 1;
    char buff2[15+PATH_LEN];

    snprintf(buff, sizeof(buff), "//depot/%s/...", me->checkout_path);
    argv[argc++] = buff;
    snprintf(buff2, sizeof(buff2), "//depot/%s/*", me->checkout_path);
    argv2[argc2++] = buff2;

    int ret;
    ret = do_p4_cmd2(ptcx, &stats->list,
                     argc, const_cast<char* const*>(argv),
                     argc2, const_cast<char* const*>(argv2), &handler);
    if ((ret) && (gf_timeexpired < EXIT_FAST)) {
        return -1;
    }
    return 0;
}

/* Helper to add a file to version control */
static int
_add(ptcx_t ptcx, rcs_stats_t *stats,
     rcs_command_t * conf, doP4_state_t *me, const char *path,
     event_timer_t *timer)
{
    const char *argv[] = {"add", 0, 0};
    int argc = 1;
    P4StoneUser handler(me, conf);

    argv[argc++] = path;

    int ret;
    ret = do_p4_cmd(ptcx, timer,
                    argc, const_cast<char* const*>(argv), &handler);
    if ((ret) && (gf_timeexpired < EXIT_FAST)) {
        return -1;
    }
    me->changed++;
    return 0;
}

/* Helper to delete a file from version control */
static int
_delete(ptcx_t ptcx, rcs_stats_t *stats,
     rcs_command_t * conf, doP4_state_t *me, const char *path)
{
    const char *argv[] = {"delete", 0, 0};
    int argc = 1;
    P4StoneUser handler(me, conf);

    argv[argc++] = path;

    int ret;
    ret = do_p4_cmd(ptcx, &stats->del,
                    argc, const_cast<char* const*>(argv), &handler);
    if ((ret) && (gf_timeexpired < EXIT_FAST)) {
        return -1;
    }
    me->changed++;
    return 0;
}

/* Helper to allow edits to a file */
static int
_edit(ptcx_t ptcx, rcs_stats_t *stats,
     rcs_command_t * conf, doP4_state_t *me, const char *path)
{
    const char* argv[] = {"edit", 0, 0};
    int argc = 1;
    P4StoneUser handler(me, conf);

    argv[argc++] = path;

    int ret;
    ret = do_p4_cmd(ptcx, &stats->mod,
                    argc, const_cast<char* const*>(argv), &handler);
    if ((ret) && (gf_timeexpired < EXIT_FAST)) {
        return -1;
    }
    me->changed++;
    return 0;
}

/* Helper to diff from client to repository */
static int
_diff_file(ptcx_t ptcx, rcs_stats_t *stats, rcs_command_t * conf,
           doP4_state_t *me, const char *path, int rev)
{
    const char *argv[] = {"diff", 0, 0};
    int argc = 1;
    P4StoneUser handler(me, conf);
    char buff[15+PATH_LEN];

    if (rev > 0) {
        snprintf(buff, sizeof(buff), "%s@%d", path, rev);
    } else {                            // compare to head
        snprintf(buff, sizeof(buff), "%s", path);
    }
    argv[argc++] = buff;

    int ret;
    ret = do_p4_cmd(ptcx, &stats->diff,
                    argc, const_cast<char* const*>(argv), &handler);
    if ((ret) && (gf_timeexpired < EXIT_FAST)) {
        return -1;
    }
    return 0;
}

/* Helper to show log entries for a file */
static int
_log_file(ptcx_t ptcx, rcs_stats_t *stats, rcs_command_t * conf,
          doP4_state_t *me, const char *path, long rev, int limit)
{
    const char *argv[] = {"filelog", "-i", "-l", 0, 0, 0, 0};
    int argc = 3;
    P4StoneUser handler(me, conf);
    char buff[15+PATH_LEN], maxbuff[32];

    if (limit < 0) { // perforce can't switch log direction (like SVN)
        limit = -limit;
    }
    if (limit > 0) {
        snprintf(maxbuff, sizeof(maxbuff), "%d", limit);
        argv[argc++] = "-m";
        argv[argc++] = maxbuff;
    }
    if (rev > 0) {
        //BUG??? path should be a depot path, not a local absolute path
        snprintf(buff, sizeof(buff), "%s@%ld", path, rev);
    } else {
        snprintf(buff, sizeof(buff), "%s", path);
    }
    argv[argc++] = buff;

    int ret;
    ret = do_p4_cmd(ptcx, &stats->log,
                    argc, const_cast<char* const*>(argv), &handler);
    if ((ret) && (gf_timeexpired < EXIT_FAST)) {
        return -1;
    }
    return 0;
}

/* Helper to find files opened for edit */
static int
_opened(ptcx_t ptcx, rcs_stats_t *stats,
        rcs_command_t * conf, doP4_state_t *me)
{
    const char *argv[] = {"opened", 0};
    int argc = 1;
    P4StoneUser handler(me, conf);

    int ret;
    ret = do_p4_cmd(ptcx, &stats->stat,
                    argc, const_cast<char* const*>(argv), &handler);
    if ((ret) && (gf_timeexpired < EXIT_FAST)) {
        return -1;
    }
    return 0;
}

/* Find all revisions that created a file */
static int
_annotate(ptcx_t ptcx, rcs_stats_t *stats,
     rcs_command_t * conf, doP4_state_t *me, const char *path)
{
    const char *argv[] = {"annotate", "-c", 0, 0};
    int argc = 2;
    P4StoneUser handler(me, conf);

    argv[argc++] = path;

    int ret;
    ret = do_p4_cmd(ptcx, &stats->blame,
                    argc, const_cast<char* const*>(argv), &handler);
    if ((ret) && (gf_timeexpired < EXIT_FAST)) {
        return -1;
    }
    return 0;
}

/* Revert each file in list. */
static int
_do_revert(ptcx_t ptcx, rcs_stats_t *stats,
           rcs_command_t * conf, doP4_state_t *me,
           words_t * revert_paths)
{
    unsigned int ii, done = 0, todo = revert_paths->indexLen;
    int ret;

    for (ii = 0; (ii < todo) && (gf_timeexpired < EXIT_SOON); ++ii) {
        char *path = revert_paths->index[ii];
        if (NULL == path)
            continue;

        const char* argv[] = {"revert", 0, 0};
        int argc = 1;
        P4StoneUser handler(me, conf);
        argv[argc++] = path;

        ret = do_p4_cmd(ptcx, NULL,
                        argc, const_cast<char* const*>(argv), &handler);
        if (0 == ret) {
            WordPointerDelete(&me->revert_paths, path);
            ++done;
            continue;
        }
    }

    returnerr(stderr, "p4: Reverted %d of %d files (of %d changed)\n",
              done, todo, me->changed);
    if (me->changed > 0) {              // should always be true
        me->changed -= me->changed-done;
        /* HACK: don't trust changed count:  still attempt revert on delete */
        me->changed = MAX(me->changed, 1);
    }
    if (todo == done) {
        return 0;
    } else {
        return -1;
    }
}

// Override certain methods to automate conflict file resolve
class P4ResolveUser : public ::P4StoneUser {
public:
    int  file_ok;
    int  prompt_step;

    P4ResolveUser(doP4_state_t *me, rcs_command_t *config)
        {
            state = me;
            conf = config;
            file_ok = 0;
            ignore_return = 0;
            prompt_step = 0;
        }

protected:
    static regex_t nofile_rex;
    static regex_t  orig_rex, theirs_rex, yours_rex, end_rex;

public:
    static void RegexInit()
        {
            /* Detect:  /absolute/path - no file(s) to resolve. */
            const char *nofile_str = "^(.*) - no file";
            int ret = regcomp(&nofile_rex, nofile_str, REG_EXTENDED);
            if (0 != ret) {
                fprintf(stderr, "Error compiling regex '%s': %d\n",
                        nofile_str, ret);
            }

            const char *orig_str = \
                "^>>>> ORIGINAL //(.*)#(.*)";
            const char *theirs_str = \
                "^==== THEIRS //(.*)#(.*)";
            const char *yours_str = \
                "^==== YOURS //(.*)";
            const char *end_str = \
                "^<<<<";
            ret = regcomp(&orig_rex, orig_str, REG_EXTENDED);
            if (0 != ret) {
                fprintf(stderr,
                        "Error compiling regex '%s': %d\n", orig_str, ret);
            }
            ret = regcomp(&theirs_rex, theirs_str, REG_EXTENDED);
            if (0 != ret) {
                fprintf(stderr,
                        "Error compiling regex '%s': %d\n", theirs_str, ret);
            }
            ret = regcomp(&yours_rex, yours_str, REG_EXTENDED);
            if (0 != ret) {
                fprintf(stderr,
                        "Error compiling regex '%s': %d\n", yours_str, ret);
            }
            ret = regcomp(&end_rex, end_str, REG_EXTENDED);
            if (0 != ret) {
                fprintf(stderr,
                        "Error compiling regex '%s': %d\n", end_str, ret);
            }
        }

    static void RegexDestroy()
        {
            regfree(&nofile_rex);
            regfree(&nofile_rex);
            regfree(&nofile_rex);
            regfree(&nofile_rex);
            regfree(&nofile_rex);
        }

    virtual void Message(Error *err)
        {
            StrBuf err_msg;
            err->Fmt(&err_msg);
            error_text.Append(&err_msg);
            char *line = err_msg.Text();
            const int num_match = 3;
            regmatch_t pmatch[num_match];
            D_PRINTF(stderr, "P4Resolve Message: %s", line);

            int ret;
            ret = regexec(&nofile_rex, line, num_match, pmatch, 0);
            if (0 == ret) {
                int ii;
                // split strings.  assumes static chars between groups
                for (ii=0; ii < num_match; ++ii) {
                    if (pmatch[ii].rm_eo > 0) {
                        line[pmatch[ii].rm_eo] = 0;
                    }
                }
                /*D_PRINTF(stderr,        // DEBUG
                         "P4Resolve no_file: '%s' '%s'\n",
                         (pmatch[0].rm_so >= 0) ? line+pmatch[0].rm_so : "?",
                         (pmatch[1].rm_so >= 0) ? line+pmatch[1].rm_so : "?");*/
                file_ok = 1;
                return;
            }
        }

    virtual void OutputStat(StrDict *varList)
        {
            char buff[5*PATH_LEN];
            _format_strdict(buff, sizeof(buff), varList);
            D_PRINTF(stderr,  "P4Resolve OutputStat: %s\n", buff);
        }

    virtual void Prompt(const StrPtr &msg, StrBuf &rsp,
                        int noEcho, Error *e)
        {
            // Note: msg and rsp seem to be the same StrBuf
            const char *text="INTERNAL ERROR";
            switch (prompt_step++) {
            case 0: text = "m"; break;       // invoke merge
            case 1: text = "a"; break;       // now accept the changed version
            case 2: text = "y"; break;       // confirm accept markers (BUG!)
            case 3: text = "?"; break;       // verify our options (BUG!)
            case 4: text = "s"; break;       // skip file (BUG!)
            case 5: assert(0); break;
            }
            D_PRINTF(stderr, "P4Resolve Prompt: %s -> %s\n", msg.Text(), text);
            rsp.Set(text);
        }

    /*
      Edit perforce conflict blocks in path.
      source_prob currently means... nothing.
      Returns the number of blocks changed or -1.
    */
    static int
    resolve_conflict_markers(StrPtr *path, float source_prob)
        {
            FILE *fp = fopen(path->Text(), "r");
            if (NULL == fp) {
                return returnerr(stderr,
                                 "Error opening '%s': %s\n",
                                 path->Text(), strerror(errno));
            }
            StrBuf tmp_path;
            tmp_path.Set(path);
            tmp_path.Append("~");
            FILE *tmp_fp = fopen(tmp_path.Text(), "w");
            if (NULL == fp) {
                fclose(fp);
                return returnerr(stderr,
                                 "Error opening '%s': %s\n",
                                 tmp_path.Text(), strerror(errno));
            }
            char line[16*1024];
            StrBuf orig_text, their_text, your_text;
            StrBuf *block = NULL;
            int block_cnt = 0;
            // BUG: possible problems if long lines contain a regex match
            while (NULL != fgets(line, sizeof(line), fp)) {
                if (0 == regexec(&orig_rex, line, 0, NULL, 0)) {
                    block = &orig_text;
                    continue;
                } else if (0 == regexec(&theirs_rex, line, 0, NULL, 0)) {
                    block = &their_text;
                    continue;
                } else if (0 == regexec(&yours_rex, line, 0, NULL, 0)) {
                    block = &your_text;
                    continue;
                } else if (0 == regexec(&end_rex, line, 0, NULL, 0)) {
                    if (block) {
                        // TODO: pick orig, their, or your text using source_prob
                        // Just write current block (which should be your_text)
                        if (fputs(block->Text(), tmp_fp) < 0) {
                            returnerr(
                                stderr,
                                "resolve_conflicts: Error writing '%s': %s\n",
                                tmp_path.Text(), strerror(errno));
                            block_cnt = -1;
                            break;
                        }
                    }

                    ++block_cnt;
                    orig_text.Clear();
                    their_text.Clear();
                    your_text.Clear();
                    block = NULL;
                    continue;
                }
                if (block) {
                    block->Append(line);
                } else {                        // write through
                    fputs(line, tmp_fp);
                }
            }
            fclose(fp);
            fclose(tmp_fp);
            D_PRINTF(stderr, "resolve_conflicts: '%s' changed %d blocks\n",
                     path->Text(), block_cnt);
            if (block_cnt > 0) {
                rename(tmp_path.Text(), path->Text());
            } else {
                unlink(tmp_path.Text());
            }
            return block_cnt;
        }

    virtual void Merge(FileSys *base, FileSys *leg1, FileSys *leg2,
                               FileSys *result, Error *e)
        {
            D_PRINTF(
                stderr,
                "P4Resolve Merge base='%s' theirs='%s' yours='%s' result='%s'\n",
                base->Path()->Text(), leg1->Path()->Text(),
                leg2->Path()->Text(), result->Path()->Text());
            // leg1 = theirs, leg2 = yours, result = temp file
            // The result file replaces leg2 when you 'a'ccept
            resolve_conflict_markers(result->Path(), 0.5);
      }

    virtual int Resolve(ClientMerge *m, Error *e)
        {
            D_PRINTF(stderr, "P4Resolve Resolve\n");
            return (int)m->Resolve(e );
        }

};
regex_t P4ResolveUser::nofile_rex;
regex_t P4ResolveUser::orig_rex, P4ResolveUser::theirs_rex;
regex_t P4ResolveUser::yours_rex, P4ResolveUser::end_rex;

/* Resolve each file in conflict */
static int
_do_resolve(ptcx_t ptcx, rcs_stats_t *stats,
     rcs_command_t * conf, doP4_state_t *me)
{
    unsigned int ii, done = 0, todo = me->resolve_paths.indexLen;
    int ret;
    int errors = 0;

    for (ii = 0; (ii < todo) && (gf_timeexpired < EXIT_SOON); ++ii) {
        char *path = me->resolve_paths.index[ii];
        if (NULL == path)
            continue;

        {                               // interact with resolve
            const char* argv[] = {"resolve", 0, 0};
            int argc = 1;
            P4ResolveUser handler(me, conf);
            argv[argc++] = path;

            ret = do_p4_cmd(ptcx, NULL,
                            argc, const_cast<char* const*>(argv), &handler);
            ret = 0;             // BUG: this always returns an error!
            if ((0 == ret) || (handler.file_ok > 0)) {
                WordPointerDelete(&me->resolve_paths, path);
                ++done;
                continue;
            }
        }

        returnerr(stderr, "p4: Error resolving '%s'\n", path);
        if (++errors > 5) {           // abort if repeated errors
            return -1;
        }
    }
    returnerr(stderr, "p4: Resolved %d of %d files\n", done, todo); // D_PRINTF
    if (todo == done) {
        return 0;
    } else {
        return -errors;
    }
}

// Override certain methods to automate changelist submit
class P4SubmitUser : public ::P4StoneUser {
public:
    long     cl;

    P4SubmitUser(doP4_state_t *me, rcs_command_t *config)
        {
            state = me;
            conf = config;
            ignore_return = 0;
            cl = -1;
        }

protected:
    static regex_t  change_rex, locked_rex, resolve_rex, revert_rex, opened_rex;

public:
    static void RegexInit()
        {
            int ret;
            const char *change_str \
                = "^Change ([0-9]+) created";
            ret = regcomp(&change_rex, change_str, REG_EXTENDED);
            if (0 != ret) {
                fprintf(stderr, "Error compiling regex '%s': %d\n",
                        change_str, ret);
            }

            const char *locked_str \
                = "^//([-A-Za-z0-9_.]+)/(.+) - already locked by (.*)";
            ret = regcomp(&locked_rex, locked_str, REG_EXTENDED);
            if (0 != ret) {
                fprintf(stderr, "Error compiling regex '%s': %d\n",
                        locked_str, ret);
            }

            const char *resolve_str = \
                "^//([-A-Za-z0-9_.]+)/(.+) - must (sync/)*resolve";
            ret = regcomp(&resolve_rex, resolve_str, REG_EXTENDED);
            if (0 != ret) {
                fprintf(stderr, "Error compiling regex '%s': %d\n",
                        resolve_str, ret);
            }

            const char *revert_str \
                = "^//([-A-Za-z0-9_.]+)/(.+) - (.*); must revert";
            ret = regcomp(&revert_rex, revert_str, REG_EXTENDED);
            if (0 != ret) {
                fprintf(stderr, "Error compiling regex '%s': %d\n",
                        revert_str, ret);
            }

            const char *opened_str \
                = "^//([-A-Za-z0-9_.]+)/(.+) - is opened for edit and can't be deleted";
            ret = regcomp(&opened_rex, opened_str, REG_EXTENDED);
            if (0 != ret) {
                fprintf(stderr, "Error compiling regex '%s': %d\n",
                        opened_str, ret);
            }
        }

    static void RegexDestroy()
        {
            regfree(&change_rex);
            regfree(&locked_rex);
            regfree(&resolve_rex);
            regfree(&revert_rex);
            regfree(&opened_rex);
        }

    virtual void Message(Error *err)
        {
            StrBuf err_msg;
            err->Fmt(&err_msg);
            error_text.Append(&err_msg);
            char *line = err_msg.Text();
            const int num_match = 6;
            regmatch_t pmatch[num_match];
            D_PRINTF(stderr, "P4submit Message: %s", line);

            int ret;
            ret = regexec(&locked_rex, line, num_match, pmatch, 0) \
                && regexec(&resolve_rex, line, num_match, pmatch, 0);
            if (0 == ret) {
                int ii;
                // split strings.  assumes static chars between groups
                for (ii=0; ii < num_match; ++ii) {
                    if (pmatch[ii].rm_eo > 0) {
                        line[pmatch[ii].rm_eo] = 0;
                    }
                }
                /*D_PRINTF(stderr,        // DEBUG
                         "P4submit resolve: '%s' '%s'\n",
                         (pmatch[1].rm_so >= 0) ? line+pmatch[1].rm_so : "?",
                         (pmatch[2].rm_so >= 0) ? line+pmatch[2].rm_so : "?");*/
                if (pmatch[2].rm_so >= 0) { // store local relative path
                    char buff[PATH_LEN];
                    snprintf(buff, sizeof(buff), "%s/%s",
                             state->work_dir, line+pmatch[2].rm_so);
                    WordAdd(&state->resolve_paths, buff);
                }
                return;
            }

            ret = regexec(&revert_rex, line, num_match, pmatch, 0);
            if (0 == ret) {
                int ii;
                // split strings.  assumes static chars between groups
                for (ii=0; ii < num_match; ++ii) {
                    if (pmatch[ii].rm_eo > 0) {
                        line[pmatch[ii].rm_eo] = 0;
                    }
                }
                D_PRINTF(stderr,        // DEBUG
                         "P4submit revert: '%s' '%s'\n",
                         (pmatch[1].rm_so >= 0) ? line+pmatch[1].rm_so : "?",
                         (pmatch[2].rm_so >= 0) ? line+pmatch[2].rm_so : "?");
                if (pmatch[2].rm_so >= 0) { // store local relative path
                    char buff[PATH_LEN];
                    snprintf(buff, sizeof(buff), "%s/%s",
                             state->work_dir, line+pmatch[2].rm_so);
                    WordAdd(&state->revert_paths, buff);
                }
                return;
            }

            ret = regexec(&change_rex, line, num_match, pmatch, 0);
            if (0 == ret) {
                int ii;
                // split strings.  assumes static chars between groups
                for (ii=0; ii < num_match; ++ii) {
                    if (pmatch[ii].rm_eo > 0) {
                        line[pmatch[ii].rm_eo] = 0;
                    }
                }
                /*D_PRINTF(stderr,        // DEBUG
                         "P4submit change: '%s' '%s'\n",
                         (pmatch[0].rm_so >= 0) ? line+pmatch[0].rm_so : "?",
                         (pmatch[1].rm_so >= 0) ? line+pmatch[1].rm_so : "?");*/
                cl = atol(line+pmatch[1].rm_so);
                /*D_PRINTF(stderr,
                         "P4submit change: '%s'=%d created\n",
                         line+pmatch[1].rm_so, cl);*/
                return;
            }

        }

    virtual void Edit(FileSys *f1, Error *e)
        {
            char desc_msg[60];                 /* TODO: configurable size */
            int ret;
            ret = WordSequence(desc_msg, sizeof(desc_msg),
                               &wordDict,  " , . ; ", 0);
            if (ret < 0) {
                D_PRINTF(stderr, "svn: Error generating words(submit)\n");
                strcpy(desc_msg, "mstone submit");
            }
            StrBuf tmppath;
            tmppath.Set(f1->Path()->Text());
            tmppath.Append("~");

            FILE *fsrc = fopen(f1->Path()->Text(), "r"); // change template
            if (NULL == fsrc) {
                D_PRINTF(stderr, "P4submit Error opening %s: %s\n",
                         f1->Path()->Text(), strerror(errno));
                return;
            }
            FILE *ftmp = fopen(tmppath.Text(), "w"); // write change info
            char buff[PATH_LEN];
            fprintf(ftmp, "Change:\t%s\n\n", "new");
            fprintf(ftmp, "Client:\t%s\n\n", state->client_name);
            fprintf(ftmp, "User:\t%s\n\n",   state->user);
            fprintf(ftmp, "Status:\t%s\n\n", "new");
            fprintf(ftmp, "Description:\n\t%s\n\n", desc_msg);
            // Read up to  Files: line
            while (NULL != fgets(buff, sizeof(buff), fsrc)) {
                if (0 == strncmp("Files:", buff, 6)) {
                    fprintf(ftmp, "%s", buff);
                    break;
                }
            }
            // copy file info (to end of file)
            while (NULL != fgets(buff, sizeof(buff), fsrc)) {
                fprintf(ftmp, "%s", buff);
            }
            fclose(ftmp);
            D_PRINTF(stderr, "P4submit Submit %s %s\n",
                     f1->Path()->Text(), tmppath.Text());
            rename(tmppath.Text(), f1->Path()->Text());
        }

    virtual void OutputStat(StrDict *varList)
        {
            {
                char buff[5*PATH_LEN];
                _format_strdict(buff, sizeof(buff), varList);
                D_PRINTF(stderr,  "P4Submit OutputStat: %s\n", buff);
            }
            int n = 0;
            StrRef  key, value;

            while (0 != varList->GetVar(n++, key, value)) {
                if (0 == strcmp("change", key.Text())) {
                    cl = atoi(value.Text()); // initial changelist number
                } else if (0 == strcmp("submittedChange", key.Text())) {
                    cl = atoi(value.Text()); // submitted changelist number
                }
            }
        }

};
regex_t P4SubmitUser::change_rex, P4SubmitUser::locked_rex;
regex_t P4SubmitUser::resolve_rex, P4SubmitUser::revert_rex;
regex_t P4SubmitUser::opened_rex;

/* Helper to do create changelist and submit (while resolving and retrying) */
static int
_do_submit(ptcx_t ptcx, rcs_stats_t *stats,
     rcs_command_t * conf, doP4_state_t *me)
{
    P4SubmitUser handler(me, conf);
    int ret;

    {                                   // create a ChangeList
        const char* argv[] = {"change", 0};
        int argc = 1;

        ret = do_p4_cmd(ptcx, NULL,     // timer for this step?
                        argc, const_cast<char* const*>(argv), &handler);
        // changelist number is stored in handler.cl
        if (ret) {
            return returnerr(stderr, "p4: Could not create changelist\n");
        }
    }
    if (handler.cl < 0) {
        return returnerr(stderr, "p4: Failed to get changelist number\n");
    }

    long our_cl = handler.cl;
    int tries;
    for (tries = conf->commitRetryCnt; tries > 0; --tries) {
        if (gf_timeexpired >= EXIT_SOON) {
            D_PRINTF(stderr,
                     "p4: %s Submit failed: test ending.\n", me->client_name);
            goto _exit_fail;            // must revert CL
        }
        _sync(ptcx, stats, conf, me, &stats->update, 0, 0);
        // now synced to at least our CL
        me->our_rev = handler.cl;
        if (me->head_rev < me->our_rev) {
            me->head_rev = me->our_rev;
        }

        const char* argv[] = {"submit", "-c", 0, 0};
        int argc = 2;
        char buff[NUM_LEN];
        snprintf(buff, sizeof(buff), "%ld", our_cl);
        argv[argc++] = buff;

        me->total_bytes = 0;
        ret = do_p4_cmd(ptcx, &stats->commit,
                        argc, const_cast<char* const*>(argv), &handler);
        stats->commit.byteswritten += me->total_bytes;
        // BUG ret is unreliable here, a changed CL indicates it made it in.
        if ((0 == ret) || (our_cl != handler.cl)) {
            D_PRINTF(stderr, "p4: Submitted %ld in %s as %ld\n",
                     our_cl, me->work_dir, handler.cl);
            break;
        }

        // wait for pending transactions to finish
        MS_idle(ptcx, 1000 * (conf->commitRetryCnt - tries));
        if (gf_timeexpired >= EXIT_SOON) {
            D_PRINTF(
                stderr,
                "p4: %s Submit %ld in %s failed (with issues): test ending.\n",
                me->client_name, our_cl, me->work_dir);
            goto _exit_fail;            // must revert CL
        }

        int grokked = 0;                // did we grok the errors
        if (me->resolve_paths.indexLen > 0) {
            D_PRINTF(stderr, "p4: Resolving %d files for %ld in %s\n",
                      me->resolve_paths.indexLen, our_cl, me->work_dir);
            /*
            const char* argv2[] = {"sync", 0};
            int argc2 = 1;
            (void)do_p4_cmd(ptcx, &stats->update, // sync with our handler
            argc2, const_cast<char* const*>(argv2), &handler);*/
            (void)_sync(ptcx, stats, conf, me, &stats->update, 0, 0);
            _do_resolve(ptcx, stats, conf, me);
            // The return status is unreliable, so accept best effort
            WordsDestroy(&me->resolve_paths);
            grokked++;
        }
        if (me->revert_paths.indexLen > 0) {
            D_PRINTF(stderr, "p4: Reverting %d files for %ld in %s\n",
                      me->revert_paths.indexLen, our_cl, me->work_dir);
            _do_revert(ptcx, stats, conf, me, &me->revert_paths);
            WordsDestroy(&me->revert_paths);
            _sync(ptcx, stats, conf, me, &stats->update, 0, 0);
            // The return status is unreliable, so accept best effort
            grokked++;
        }
        if (!grokked) {
            returnerr(stderr, "p4: Unknown Error submitting %ld in %s\n",
                      our_cl, me->work_dir);
        }

    }
    if (tries <= 0) {                   // ran out of re-tries
        returnerr(stderr,
                  "p4: %s Submit failed: out of retries (%d)\n",
                  me->client_name, conf->commitRetryCnt);
        goto _exit_fail;
    }

    me->changed = 0;                    // all changed files now resolved
    return 0;

_exit_fail:
    if (our_cl > 0) {
        ret = _revert(ptcx, me, NULL, our_cl, &handler);
        if (0 == ret) {         // BUG: reverts always return an error
            returnerr(stderr, "p4: %s Reverted %ld\n",
                      me->client_name, our_cl);
        } else {
            returnerr(stderr, "p4: %s Revert failed %ld\n",
                      me->client_name, our_cl);
        }
    } else {
        returnerr(stderr, "p4: %s Submit failed.  Did not create CL\n",
            me->client_name);
    }
    return -1;
}

// The body of a command block
extern "C" int
doP4Loop(ptcx_t ptcx, mail_command_t *cmd,
         cmd_stats_t *ptimer, void *mystate)
{
    doP4_state_t       *me = reinterpret_cast<doP4_state_t *>(mystate);
    rcs_stats_t *stats = reinterpret_cast<rcs_stats_t *>(ptimer->data);
    rcs_command_t       *conf = reinterpret_cast<rcs_command_t *>(cmd->data);
    float todo;
    int ret;
    long rev_span;
    int  tot_errors = 0, max_errors = 10;

    if (0 == conf->revSpan) {           /* use full range */
        rev_span = me->our_rev;
    } else {                    /* use indicated range (ignore sign) */
        rev_span = MIN(me->our_rev, ABS(conf->revSpan));
    }

    for (todo = conf->listCnt; todo > 0.0; todo = 0.0) { /* list files */
        if ((todo < 1.0) && ((RANDOM() & 0xffff) >= (0xffff * todo))) {
            break;
        }
        me->total_bytes = 0;
        ret = _list(ptcx, stats, conf, me);
        stats->list.bytesread += me->total_bytes;
        if ((ret < 0) && (gf_timeexpired < EXIT_FAST)) {
            if (++tot_errors >= max_errors) /* soft error */
                goto _exit_fail;
        }
    }

    for (todo = sample(conf->addCnt);
         todo > 0.0;
         todo -= 1.0) {                 /* Create/Add files */
        FILE *fp;
        int  file_size                  /* total size goal */
            = static_cast<int>(sample(conf->addFileSize));
        int  size = 0;                  /* current size */
        char chunk[80];
        char filename[PATH_LEN];
        int  ret;

        if ((todo < 1.0) && ((RANDOM() & 0xffff) >= (0xffff * todo))) {
            break;
        }
        // Create a random new filename under a random directory.
        ret = WordSequence(chunk, sizeof(chunk), &wordDict, "_", 2);
        if (ret < 0) {
            D_PRINTF(stderr, "p4: Error generating words(add)\n");
            break;
        }
        snprintf(filename, sizeof(filename),
                 "%s/%s.m", WordRandom(&me->dirs), chunk);

        // make sure the file doesn't already exist.
        ret = open(filename, O_RDONLY, 0); // See if it already exists.
        if (ret >= 0) {
            close(ret);
            //++todo;  // retry limit???
            continue;
        }
        fp = fopen(filename, "w");
        // Randomly generate chunks (lines), and write to the file.
        while (size < file_size) {
            int rnum = RANDOM();
            ret = WordSequence(chunk, sizeof(chunk),
                               &wordDict, syntaxChars, 1 + (rnum % 16));
            if (ret < 0) break;
            // Note the very crude indentation simulation (no continuity)
            ret = fprintf(fp, "%*s%s\n", (rnum>>8) % 40, "", chunk);
            if (ret > 0)
                size += ret;
        }
        fclose(fp);

        // Add file to p4.
        ret = _add(ptcx, stats, conf, me, filename, &stats->add);
        if ((ret < 0) && (gf_timeexpired < EXIT_FAST)) {
            if (++tot_errors >= max_errors) /* soft error */
                goto _exit_fail;
        }
        D_PRINTF(stderr, "p4: ADD '%s'\n", filename);

        WordAdd(&me->files, filename); /* add to files list (later???) */
    }

    // mkdir equivalent.  perforce doesn't have mkdir, just files in dirs.
    for (todo = sample(conf->mkdirCnt);
         todo > 0.0;
         todo -= 1.0) {                 /* Add directories*/
        struct stat stat_buf;
        FILE *fp;
        char dirname[PATH_LEN];
        char path[PATH_LEN];

        if ((todo < 1.0) && ((RANDOM() & 0xffff) >= (0xffff * todo))) {
            break;
        }
        // Create a random new directory under a random directory.
        ret = WordSequence(path, sizeof(path), &wordDict, "_", 2);
        if (ret < 0) {
            D_PRINTF(stderr, "p4: Error generating directory name\n");
            break;
        }
        snprintf(dirname, sizeof(dirname),
                 "%s/%s.d", WordRandom(&me->dirs), path);

        // see if the directory already exits
        if (stat(dirname, &stat_buf) >= 0) {
            //++todo;  // retry limit???
            continue;
        }
        if (mkdir(dirname, 0777) < 0) {
            returnerr(stderr, "Error creating directory '%s': %s\n",
                      dirname, strerror(errno));
            continue;
        }
        // perforce needs a file to commit
        snprintf(path, sizeof(path), "%s/.mkdir", dirname);
        fp = fopen(path, "w");
        if (NULL == fp) {
            returnerr(stderr, "Error creating file '%s': %s\n",
                      path, strerror(errno));
            continue;
        }
        // put some text in there to set file type.
        fputs("empty file to enable directory creation\n", fp);
        fclose(fp);

        // Add file to p4 to force directory creation
        me->total_bytes = 0;
        ret = _add(ptcx, stats, conf, me, path, &stats->mkdir);
        if ((ret < 0) && (gf_timeexpired < EXIT_FAST)) {
            if (++tot_errors >= max_errors) /* soft error */
                goto _exit_fail;
        }
        D_PRINTF(stderr, "p4: MKDIR '%s'\n", dirname);
        WordAdd(&me->dirs, dirname); /* add to dirs list (later???) */
    }

    for (todo = sample(conf->diffCnt);
         todo > 0.0;
         todo -= 1.0) {                 /* diff files */
        long rev;
        const char *filepath;
        if ((todo < 1.0) && ((RANDOM() & 0xffff) >= (0xffff * todo))) {
            break;
        }
        filepath = WordRandom(&me->files);
        if (!filepath) break;
        me->total_bytes = 0;
        if (conf->revSpan >= 0) {
            rev = me->our_rev - (RANDOM() % rev_span);
            rev = MAX(rev, 1);
        } else {                        /* use checked out value */
            rev = 0;
        }
        ret = _diff_file(ptcx, stats, conf, me, filepath, rev);
        stats->diff.bytesread += me->total_bytes;
        D_PRINTF(stderr, "p4: diff from %ld to client=%ld of %s\n",
                 rev, me->our_rev, filepath);
        if ((ret < 0) && (gf_timeexpired < EXIT_FAST)) {
            if (++tot_errors >= max_errors) /* soft error */
                goto _exit_fail;
        }
    }

    for (todo = sample(conf->logCnt);
         todo > 0.0;
         todo -= 1.0) {                 /* get file logs */
        long rev;
        const char *filepath;
        int logLimit;
        if ((todo < 1.0) && ((RANDOM() & 0xffff) >= (0xffff * todo))) {
            break;
        }
        filepath = WordRandom(&me->files);
        if (!filepath) break;
        rev = me->our_rev - (RANDOM() % rev_span);
        rev = MAX(rev, 1);
        me->total_bytes = 0;
        logLimit = static_cast<int>(sample(conf->logLimit));
        ret = _log_file(ptcx, stats, conf, me, filepath, rev, logLimit);
        stats->log.bytesread += me->total_bytes;
        D_PRINTF(stderr, "p4: log from %ld of %d: %s\n",
                 rev, logLimit, filepath);
        if ((ret < 0) && (gf_timeexpired < EXIT_FAST)) {
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
        me->total_bytes = 0;
        ret = _annotate(ptcx, stats, conf, me, filepath);
        stats->blame.bytesread += me->total_bytes;
        D_PRINTF(stderr, "p4: annotate %s\n", filepath);
        if ((ret < 0) && (gf_timeexpired < EXIT_FAST)) {
            if (++tot_errors >= max_errors) /* soft error */
                goto _exit_fail;
        }
    }

    for (todo = sample(conf->delCnt);
         todo > 0.0;
         todo -= 1.0) {                 /* Delete files */
        const char *filename;
        if ((todo < 1.0) && ((RANDOM() & 0xffff) >= (0xffff * todo))) {
            break;
        }

        filename = GetSafeFilename(&me->files);
        if (!filename) {                /* no files */
            D_PRINTF(stderr, "p4: No files safe to delete\n");
            break;
        }

        // Schedule file for removal from p4.
        me->total_bytes = 0;
        ret = _delete(ptcx, stats, conf, me, filename);
        stats->del.bytesread += me->total_bytes;
        if ((ret < 0) && (gf_timeexpired < EXIT_FAST)) {
            if (++tot_errors >= max_errors) /* soft error */
                goto _exit_fail;
        }
        D_PRINTF(stderr, "p4: DELETE '%s'\n", filename);
        WordPointerDelete(&me->files, filename); /* remove from file list */
    }

    for (todo = sample(conf->modCnt);
         todo > 0.0;
         todo -= 1.0) {                 /* Modify files */
        int mods;
        int fd;
        int chunkCnt, chunkSize;
        off_t offset;
        const char *filename = GetSafeFilename(&me->files);
        struct stat info;

        if ((todo < 1.0) && ((RANDOM() & 0xffff) >= (0xffff * todo))) {
            break;
        }
        if (!filename) {                /* no files */
            D_PRINTF(stderr, "p4: No files safe to modify\n");
            break;
        }
        ret = _edit(ptcx, stats, conf, me, filename);
        if ((ret < 0) && (gf_timeexpired < EXIT_FAST)) {
            if (++tot_errors >= max_errors) /* soft error */
                goto _exit_fail;
        }
        fd = open(filename, O_RDWR, 0);
        if (fd < 0) {
            D_PRINTF(stderr, "p4: Warning '%s' missing.\n", filename);
            WordPointerDelete(&me->files, filename);
            break;
        }

        chunkCnt = static_cast<int>(sample(conf->modChunkCnt));
        chunkSize = static_cast<int>(sample(conf->modChunkSize));
        /* write words at N randomly selected locations in the file */
        for (mods = 0; mods < chunkCnt; ++ mods) {
            char chunk[chunkSize];
            ret = WordSequence(chunk, sizeof(chunk),
                               &wordDict, syntaxChars, 0);
            if (ret < 0) {
                D_PRINTF(stderr, "p4: Error generating words(mod)\n");
                close(fd);
                break;
            }
            fstat(fd, &info);
            offset =  (info.st_size) ? (RANDOM() % info.st_size) : 0;
            // BUG: this doesnt understand UTF8 sequences
            lseek(fd, offset, SEEK_SET);
            write(fd, chunk, strlen(chunk));
        }
        D_PRINTF(stderr, "p4: Modified '%s'\n", filename);
        close(fd);
    }

    for (todo = sample(conf->statCnt);
         todo > 0.0;
         todo -= 1.0) {                 /* check for files to be submitted */
        if ((todo < 1.0) && ((RANDOM() & 0xffff) >= (0xffff * todo))) {
            break;
        }
        me->total_bytes = 0;
        ret = _opened(ptcx, stats, conf, me);
        stats->stat.bytesread += me->total_bytes;
        if ((ret < 0) && (gf_timeexpired < EXIT_FAST)) {
            if (++tot_errors >= max_errors) /* soft error */
                goto _exit_fail;
        }
    }

    if (me->changed
        && (conf->commitCnt > 0.0)
        && ((conf->commitCnt >= 1.0)
            || ((RANDOM() & 0xffff) >= (0xffff * conf->commitCnt)))) {
        if ((_do_submit(ptcx, stats, conf, me) < 0) // which includes sync
            && (gf_timeexpired < EXIT_FAST)) {
            //me->flags |= P4_SAVE_CLIENT;
            goto _exit_fail;          /* commits are failing, abort */
        }
    } else {                            // just sync
        if (_sync(ptcx, stats, conf, me, &stats->update, 0, 0) < 0) {
            if (++tot_errors >= max_errors) /* soft error??? */
                goto _exit_fail;
        }
    }

    return 0;

 _exit_fail:
    return -1;

}

// Exit a command block
extern "C" void
doP4End(ptcx_t ptcx, mail_command_t *cmd,
        cmd_stats_t *ptimer, void *mystate)
{
    doP4_state_t *me = (doP4_state_t *)mystate;
    rcs_stats_t   *stats = (rcs_stats_t *)ptimer->data;
    rcs_command_t *conf = reinterpret_cast<rcs_command_t *>(cmd->data);

    _delete_client(ptcx, stats, conf, me);

    if (me->flags & P4_SAVE_CLIENT) {
        static int saveCnt = 0;
        char name[PATH_LEN];
        while (saveCnt < 100) {
            snprintf(name, sizeof(name), "%s/save-%d",
                     conf->localDir, saveCnt++);
            if (rename(me->work_dir, name) >= 0) {
                returnerr(stderr, "P4: saved %s as %s for postmortem\n",
                          me->work_dir, name);
                break;
            }
        }
        if (saveCnt >= 100) {
            returnerr(stderr,
                      "P4: could not save %s: too many save attempts %d\n",
                      me->work_dir, saveCnt);
        }
    } else {
        if (nuke_dir(me->work_dir, NULL) < 0) {
            returnerr(stderr, "P4: Error purging work_dir (delete)\n");
        }
    }
    doP4Exit (ptcx, me);
}

/*
  Initial setup of P4
*/
int
P4Init(void)
{
    // Either set or clear environment variables p4 might use.
    if (unsetenv("P4CHARSET") < 0) goto _exit_fail;
    if (unsetenv("P4CLIENT") < 0) goto _exit_fail;
    if (unsetenv("P4DIFF") < 0) goto _exit_fail;
    if (unsetenv("P4COMMANDCHARSET") < 0) goto _exit_fail;
    if (  setenv("P4CONFIG", ".p4config", 1) < 0) goto _exit_fail;
    if (unsetenv("P4EDITOR") < 0) goto _exit_fail;
    if (unsetenv("P4HOST") < 0) goto _exit_fail;
    if (unsetenv("P4LANGUAGE") < 0) goto _exit_fail;
    if (unsetenv("P4MERGE") < 0) goto _exit_fail;
    if (unsetenv("P4PASSWD") < 0) goto _exit_fail;
    if (unsetenv("P4PORT") < 0) goto _exit_fail;
    if (unsetenv("P4USER") < 0) goto _exit_fail;
    P4ResolveUser::RegexInit();
    P4SubmitUser::RegexInit();
    return 0;

_exit_fail:
    return returnerr(stderr, "Error setting up P4 environment\n");
}

/*
  Main entry:  Called to initialize a command block with defaults
  Set defaults in command structure
*/
extern "C" int
P4ParseStart (pmail_command_t cmd,
                char *line,
                param_list_t *defparm)
{
    static int initialized;

    if (!initialized) {
        initialized++;
        if (P4Init() < 0) {
            return 0;
        }
    }
    D_PRINTF(stderr, "P4ParseStart\n");

    return RcsParseStart(cmd, line, defparm);
}

/*
  Main entry:  Called to fill in a command block from user text
  Fill in command structure from a list of lines
 */
extern "C" int
P4ParseEnd (pmail_command_t cmd, string_list_t *section,
            param_list_t *defparm)
{
    rcs_command_t       *conf = reinterpret_cast<rcs_command_t *>(cmd->data);
    int ret;

    D_PRINTF(stderr, "P4 Assign section lines\n");
    ret = RcsParseEnd(cmd, section, defparm);
    if (ret < 0) {
        D_PRINTF(stderr, "P4ParseEnd failed: %d", ret);
        return ret;
    }

    if (!conf->topDir) {
        return returnerr(stderr, "topDir must be set\n");
    }
    return 1;
}

extern "C" void
P4ParseFree(pmail_command_t cmd)
{
    RcsParseFree(cmd);
}

extern "C" void
P4Free(void)
{
    P4ResolveUser::RegexDestroy();
    P4SubmitUser::RegexDestroy();
    RcsFree();
}

/* Register this protocol with the system. */
extern "C" void
P4Register(void)
{
    ProtocolRegister(
        "P4", P4ParseStart, P4ParseEnd,
        doP4Start, doP4Loop, doP4End,
        RcsStatsFormat, RcsStatsInit, RcsStatsUpdate, RcsStatsOutput,
        P4ParseFree, P4Free);
}

#ifdef MODULE
/* This is the entry point if compiled as an independent module. */
extern "C" void
ModuleRegister(void)
{
    P4Register();
}
#endif
