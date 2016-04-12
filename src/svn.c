/* -*- Mode: C; c-file-style: "stroustrup"; comment-column: 40 -*- */
/* svn.c: test subversion client operations */
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
#include "svn_client.h"
#include "svn_cmdline.h"
#include "svn_pools.h"
#include "svn_config.h"
#include "svn_fs.h"
#include "svn_io.h"
#include "svn_path.h"

/* Protocol specific state for a single session. */
typedef struct _doSvn_state {
    apr_pool_t *     jobpool;           /* current svn memory sub-pool */
    svn_client_ctx_t *ctx;              /* svn context */
    apr_array_header_t *resolve_paths;  /* array of files to be resolved */
    apr_array_header_t *added_paths;    /* array of new files */
    apr_array_header_t *removed_paths;  /* array of removed files */
    int              changed;           /* count of changed files */
    apr_pool_t *     pool;              /* svn memory pool */
    unsigned long    total_bytes;    /* total bytes from last operation */
    long	     entry_count;    /* count of log or blame callbacks */
    char *           config_dir;     /* private config directory (in pool) */
    char *           checkout_path;  /* full checkout path (in pool) */
    char             work_dir[PATH_LEN]; /* top dir of our checkouts/auth */
    char             password[USER_LEN];
    char             user[USER_LEN];
    words_t          files;          /* list of all checked out files */
    words_t          dirs;           /* list of all checked out dirs */
    long             head_rev;       /* the head revision */
    long             our_rev;        /* the checked out revision */
    int             flags;            /* special flags */
} doSvn_state_t;

#define SVN_SAVE_WC 0x01                /* don't nuke WC at end. */


/* Initialize subversion libraries.
 Returns: 1 on success, else <= 0
*/
static int
SvnInit (void)
{
    if (svn_cmdline_init ("mstone_svn", stderr) != EXIT_SUCCESS)
        return 0;
    D_PRINTF(stderr, "Svn global init finished.\n");

    return 1;
}

/*
  Main entry:  Called to initialize a command block with defaults
  Set defaults in command structure
*/
int
SvnParseStart (pmail_command_t cmd,
                char *line,
                param_list_t *defparm)
{
    static int initialized;

    if (!initialized) {
        initialized++;
        if (SvnInit() <= 0) {
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
SvnParseEnd (pmail_command_t cmd,
                string_list_t *section,
                param_list_t *defparm)
{
    rcs_command_t       *conf = (rcs_command_t *)cmd->data;
    int ret;

    D_PRINTF(stderr, "Svn Assign section lines\n");
    ret = RcsParseEnd(cmd, section, defparm);
    if (ret < 0)
        return ret;

    if (!svn_path_is_url(conf->repoUrl)) {
        return returnerr(stderr, "repoUrl is not a valid URL '%s'\n",
                         conf->repoUrl);
    }
    return 1;
}


// Internal exit function.  Free all memory.
static void
doSvnExit (ptcx_t ptcx, doSvn_state_t *me)
{
  svn_pool_destroy(me->pool);
  WordsDestroy(&me->files);
  WordsDestroy(&me->dirs);
  xfree (me);
}

// Tie svn cancel to our exit flag.
static svn_error_t *
_svn_cancel (void *cancel_baton)
{
    if (gf_timeexpired > EXIT_SOON) {
        D_PRINTF(stderr, "SVN CANCEL triggered.");
        return svn_error_create(SVN_ERR_CANCELLED, NULL, "Exiting");
    } else {
        return SVN_NO_ERROR;
    }
}

// Use progress notify callback to track bytes transfered
static void
_progress_notify(apr_off_t progress, apr_off_t total,
                  void *baton, apr_pool_t *pool)
{
    doSvn_state_t *me = baton;
    // total seems to always be 0, so store the max progress value.
    if (progress > me->total_bytes) {
        me->total_bytes = progress;
    }
}

/* A variant of returnerr() that understands svn_err messages. */
static int
ret_svn_err(const char *msg, svn_error_t *err)
{
    time_t      t=time(0L) - gt_startedtime;
    if (err->child && err->child->message) {
        svn_error_t *ec;
        fprintf(stderr, "%s[%d]\tt=%lu: %s: %s[%d] (%s[%d])\n",
                gs_shorthostname, gn_myPID, t,
                msg, err->message, err->apr_err,
                err->child->message, err->child->apr_err);
        if (gn_debug > 0) {             // DEBUG show all info
            for (ec = err->child->child; ec; ec = ec->child) {
                if (ec->message) {
                    fprintf(stderr, " ...\t\t%s[%d]\n",
                            ec->message, ec->apr_err);
                }
            }
        }
    } else {
        fprintf(stderr, "%s[%d]\tt=%lu: %s: %s[%d]\n",
                gs_shorthostname, gn_myPID, t,
                msg, err->message, err->apr_err);
    }
    return -1;
}

/* Track which files are in conflict and record for later clean up. */
static void
_file_notify(
    void *baton,
    const svn_wc_notify_t *notify,
    apr_pool_t *pool)
{
    doSvn_state_t *me = baton;


    // note that the copied path lives in a subpool and most be resolved
    // before that pool is destroyed.
    if (notify->action == svn_wc_notify_update_add) {
        if (!me->jobpool) {             /* happens during checkout */
            //returnerr(stderr, "svn: NO JOB POOL in _file_notify:add\n");
            return;
        }
        //D_PRINTF(stderr, "svn: added '%s'\n", notify->path);
        APR_ARRAY_PUSH(me->added_paths, const char *)
            = apr_pstrdup(me->jobpool, notify->path);
    }
    else if (notify->action == svn_wc_notify_update_delete) {
        if (!me->jobpool) {
            returnerr(stderr, "svn: NO JOB POOL in _file_notify:delete\n");
            return;
        }
        //D_PRINTF(stderr, "svn: removed '%s'\n", notify->path);
        APR_ARRAY_PUSH(me->removed_paths, const char *)
            = apr_pstrdup(me->jobpool, notify->path);
    }
    else if ((notify->action == svn_wc_notify_blame_revision)
             || (notify->action == svn_wc_notify_update_completed)) {
        ;                               /* do nothing */
    }
    else if (notify->action == svn_wc_notify_update_update) {
        if (notify->content_state == svn_wc_notify_state_conflicted) {
            if (!me->jobpool) {
                returnerr(stderr, "svn: NO JOB POOL in _file_notify:conflict\n");
                return;
            }
            //D_PRINTF(stderr, "svn: conflict at '%s'\n", notify->path);
            APR_ARRAY_PUSH(me->resolve_paths, const char *)
                = apr_pstrdup(me->jobpool, notify->path);
        }
        else if ((notify->content_state == svn_wc_notify_state_changed)
                 || (notify->content_state == svn_wc_notify_state_merged)) {
            me->changed++;
        }
    }
    else {                            /* unknown type */
        D_PRINTF(stderr, "svn: notify '%s' action=%d content_state=%d\n",
                 notify->path, notify->action, notify->content_state);
    }
}

/* Massively hacked from svn_cmdline_auth_username_prompt.
   Permanently accept all server certificates without prompting. */
static svn_error_t *
_auth_ssl_server_trust_prompt
  (svn_auth_cred_ssl_server_trust_t **cred_p,
   void *baton,
   const char *realm,
   apr_uint32_t failures,
   const svn_auth_ssl_server_cert_info_t *cert_info,
   svn_boolean_t may_save,
   apr_pool_t *pool)
{
    *cred_p = apr_pcalloc(pool, sizeof(**cred_p));
    (*cred_p)->may_save = TRUE;
    (*cred_p)->accepted_failures = failures;

  return SVN_NO_ERROR;
}

/* hacked version of svn_cmdline_setup_auth_baton */
static svn_error_t *
_setup_auth_baton(svn_auth_baton_t **ab,
                  const char *auth_username,
                  const char *auth_password,
                  const char *config_dir,
                  svn_boolean_t no_auth_cache,
                  svn_config_t *cfg,
                  svn_cancel_func_t cancel_func,
                  void *cancel_baton,
                  apr_pool_t *pool)
{
    svn_auth_provider_object_t *provider;

    /* The whole list of registered providers */
    apr_array_header_t *providers
        = apr_array_make(pool, 12, sizeof(svn_auth_provider_object_t *));

    /* Auth providers, for both 'username/password' creds and
       'username' creds.  */
    svn_auth_get_simple_provider(&provider, pool);
    APR_ARRAY_PUSH(providers, svn_auth_provider_object_t *) = provider;
    svn_auth_get_username_provider(&provider, pool);
    APR_ARRAY_PUSH(providers, svn_auth_provider_object_t *) = provider;

    /* The server-cert, client-cert, and client-cert-password providers. */
    svn_auth_get_ssl_server_trust_file_provider(&provider, pool);
    APR_ARRAY_PUSH(providers, svn_auth_provider_object_t *) = provider;
    svn_auth_get_ssl_client_cert_file_provider(&provider, pool);
    APR_ARRAY_PUSH(providers, svn_auth_provider_object_t *) = provider;
    svn_auth_get_ssl_client_cert_pw_file_provider(&provider, pool);
    APR_ARRAY_PUSH(providers, svn_auth_provider_object_t *) = provider;

    /* Permanently accept all certificates without prompting */
    svn_auth_get_ssl_server_trust_prompt_provider
        (&provider, _auth_ssl_server_trust_prompt, NULL, pool);
    APR_ARRAY_PUSH(providers, svn_auth_provider_object_t *) = provider;

    /* Build an authentication baton to give to libsvn_client. */
    svn_auth_open(ab, providers, pool);

    /* Place --username or --password credentials into the
       auth_baton's run-time parameter hash. */
    svn_auth_set_parameter(*ab, SVN_AUTH_PARAM_DEFAULT_USERNAME,
                           auth_username);
    svn_auth_set_parameter(*ab, SVN_AUTH_PARAM_DEFAULT_PASSWORD,
                           auth_password);

    svn_auth_set_parameter(*ab, SVN_AUTH_PARAM_NON_INTERACTIVE,
                           ""); /* enable non-interactive operation */

    svn_auth_set_parameter(*ab, SVN_AUTH_PARAM_CONFIG_DIR, config_dir);

    return SVN_NO_ERROR;
}

/* Setup svn context and auth */
static int
_context_setup(
    rcs_command_t *conf,
    doSvn_state_t *me)
{
    svn_error_t *err;                   /* temp svn error status */
    svn_config_t *cfg;                  /* config options table */

    // setup context
    err = svn_client_create_context(&me->ctx, me->pool);
    if (err) goto _failed_with_err;
    // Load the run-time config file into a hash
    err = svn_config_get_config (&(me->ctx->config), me->config_dir, me->pool);
    if (err) goto _failed_with_err;
    cfg = apr_hash_get(me->ctx->config, SVN_CONFIG_CATEGORY_CONFIG,
                       APR_HASH_KEY_STRING);

    // setup auth_baton
    err = _setup_auth_baton(&me->ctx->auth_baton,
                            me->user, me->password,
                            me->config_dir,
                            FALSE, /* no_auth_cache */
                            cfg,
                            _svn_cancel,
                            NULL,       /* cancel baton */
                            me->pool);
    if (err) goto _failed_with_err;
    me->ctx->progress_func = _progress_notify;
    me->ctx->progress_baton = me;
    me->ctx->notify_func2 = _file_notify;
    me->ctx->notify_baton2 = me;

    if (conf->interface) {
        svn_config_t *servers;              /* server options table */
        D_PRINTF(stderr, "SVN: Setting http_library to '%s'\n",
                 conf->interface);
        servers = apr_hash_get(me->ctx->config, SVN_CONFIG_CATEGORY_SERVERS,
                               APR_HASH_KEY_STRING);
        if (servers) {
            svn_config_set(servers, SVN_CONFIG_SECTION_GLOBAL,
                           SVN_CONFIG_OPTION_HTTP_LIBRARY, conf->interface);
        } else {
            D_PRINTF(stderr, "SVN: Could not get server config\n");
        }
    }
    err = svn_config_ensure (me->config_dir, me->pool);
    if (err) goto _failed_with_err;

    return 0;

 _failed_with_err:
    return ret_svn_err("SVN: context setup failed", err);
}

/* Helper to read the head revision count.
   Modified from tools/examples/headrev.c

   The real goal is to see how long it takes to connect to the server.
   The head revision is the smallest data product we can ask for.

   An alternate approach would be to use svn_client_info().  This
   retrieves more info, but the session would be recycled for
   checkout.
 */
static int
_get_head_rev(doSvn_state_t *me, const char *URL, apr_pool_t *pool)
{
    svn_error_t * err;
    svn_ra_session_t  *session;
    svn_ra_callbacks2_t *cbtable;
    svn_revnum_t rev;

    /* Create a table of callbacks for the RA session */
    err = svn_ra_create_callbacks(&cbtable, pool);
    if (err) goto hit_error;
    cbtable->auth_baton = me->ctx->auth_baton;

    // initialize the RA library
    err = svn_ra_open2(&session, URL, cbtable, me, me->ctx->config, pool);
    if (err) goto hit_error;

    err = svn_ra_get_latest_revnum(session, &rev, pool);
    if (err) goto hit_error;

    me->head_rev = rev;
    D_PRINTF(stderr, "Svn The head revision is %ld.\n", rev);

    return 0;

 hit_error:
    return ret_svn_err("SVN: get_head_rev failed", err);
}

/* Helper to list a URL */
static int
_list_url(doSvn_state_t *me, const char *URL, apr_pool_t *pool)
{
    svn_error_t *err;
    svn_opt_revision_t revision;
    apr_hash_t *dirents;

    if (me->our_rev > 0) {
        revision.kind = svn_opt_revision_number;
        revision.value.number = me->our_rev;
    } else {
        revision.kind = svn_opt_revision_head;
        revision.value.number = svn_opt_revision_unspecified;
    }
    err = svn_client_ls (&dirents,
                         URL, &revision,
                         FALSE, /* no recursion */
                         me->ctx, pool);
    // results are ignored
    if (err)
        return ret_svn_err("SVN: list_url failed", err);
    return 0;
}

/* Helper to checkout a URL to work_dir */
static int
_checkout(rcs_command_t * conf, doSvn_state_t *me, apr_pool_t *pool)
{
    svn_error_t *err;
    svn_opt_revision_t peg_rev;
    svn_opt_revision_t revision;
    svn_revnum_t result_rev;
    const char *true_url = svn_path_canonicalize(me->checkout_path, pool);

    revision.value.number = conf->revision;
    peg_rev.value.number = conf->revision;
    if (conf->revision > 0) {
        revision.kind = svn_opt_revision_number;
        peg_rev.kind = svn_opt_revision_number;
    } else {
        revision.kind = svn_opt_revision_head;
        peg_rev.kind = svn_opt_revision_unspecified;
    }

    err = svn_client_checkout2(&result_rev, true_url, me->work_dir,
                               &peg_rev,
                               &revision,
                               TRUE,    /* recursive */
                               TRUE,    /* ignore_externals */
                               me->ctx, pool);
    if (err) {
        char buff[256];
        snprintf(buff, sizeof(buff), "SVN: FAILED checkout on '%s'", true_url);
        return ret_svn_err(buff, err);
    }
    me->our_rev = result_rev;
    if (me->head_rev < me->our_rev) {
        me->head_rev = me->our_rev;
        D_PRINTF(stderr, "Svn Checked out at head=%ld.\n", me->head_rev);
    } else {
        D_PRINTF(stderr, "Svn Checked out at %ld.\n", me->our_rev);
    }
    if (me->our_rev <= 0) {
        return returnerr(stderr, "SVN: checkout FAULT rev=%ld on '%s'\n",
                         me->our_rev, true_url);
    }
    return 0;
}

/* Helper to add a file to version control */
static int
_add(doSvn_state_t *me, const char *path, apr_pool_t *pool)
{
    svn_error_t *err;

    err = svn_client_add3(path, FALSE, FALSE, FALSE, me->ctx, pool);
    if (err)
        return ret_svn_err("SVN: add failed", err);
    return 0;
}

/* Helper to add a file to version control */
static int
_mkdir(doSvn_state_t *me, const char *path, apr_pool_t *pool)
{
    svn_commit_info_t *commit_info = NULL;
    svn_error_t *err;
    apr_array_header_t *paths;
    paths = apr_array_make(pool, 1, sizeof(const char *));
    APR_ARRAY_PUSH(paths, const char *) = path;

    err = svn_client_mkdir2(&commit_info, paths,
                            me->ctx, pool);
    if (err)
        return ret_svn_err("SVN: mkdir failed", err);
    return 0;
}

/* Helper to delete a file from version control */
static int
_delete(doSvn_state_t *me, const char *path, apr_pool_t *pool)
{
    svn_commit_info_t *commit_info = NULL;
    svn_error_t *err;
    apr_array_header_t *paths;
    paths = apr_array_make(pool, 1, sizeof(const char *));
    APR_ARRAY_PUSH(paths, const char *) = path;

    err = svn_client_delete2(&commit_info, paths,
                             FALSE,     /* force */
                             me->ctx, pool);
    if (err)
        return ret_svn_err("SVN: delete failed", err);
    return 0;
}

/* Helper to diff the head of a file against a previous revision. */
static int
_diff_file(doSvn_state_t *me, const char *path, svn_revnum_t revision,
           apr_pool_t *pool)
{
    svn_error_t *err;
    svn_opt_revision_t rev1, rev2;
    apr_array_header_t * diff_options;
    apr_file_t *out_file;
    char *out_path = "/dev/null";
    int ret;
    struct stat stat_buf;

    diff_options = apr_array_make(pool, 1, sizeof(const char *));
    APR_ARRAY_PUSH(diff_options, const char *) = "-u";

    if (revision > 0) {
        rev1.kind = svn_opt_revision_number;
        rev1.value.number = revision;       /* a previous version */
    } else {
        rev1.kind = svn_opt_revision_base; /* checked out version */
    }
    rev2.kind = svn_opt_revision_working; /* to WC */
    // results are ignored
    ret = apr_file_open(&out_file, out_path,
                        APR_FOPEN_WRITE | APR_FOPEN_CREATE | APR_FOPEN_APPEND,
                        APR_FPROT_OS_DEFAULT, pool);
    if (ret < 0) {
        return returnerr(stderr,
                         "SVN: failed to open %s for writing\n", out_path);
    }

    err = svn_client_diff3 (diff_options,
                            path, &rev1,
                            path, &rev2,
                            FALSE,      /* no recursion */
                            TRUE,       /* ignore ancestry */
                            FALSE,      /* no_diff_delete */
                            TRUE,       /* ignore content type */
                            APR_LOCALE_CHARSET, /* output encoding */
                            out_file, out_file,
                            me->ctx, pool);
    apr_file_close(out_file);
    if (err) {
        me->flags |= SVN_SAVE_WC;
        return ret_svn_err("SVN: diff_file failed", err);
    }
    if (stat(me->work_dir, &stat_buf) >= 0) {
        /* use size as a difference indicator */
        me->entry_count += stat_buf.st_size;
    }
    return 0;
}

/* do nothing handler for the log_receiver */
static svn_error_t *
_log_cb_noop(void *baton, apr_hash_t *changed_paths, svn_revnum_t revision,
    const char *author, const char *date, const char *message,
    apr_pool_t *pool)
{
    doSvn_state_t *me = baton;
    me->entry_count++;
    /*if (author && date) {
        D_PRINTF(stderr, "log: author=%s date=%s: \t'%s'\n",
                 author, date, message);
    } else {
        D_PRINTF(stderr, "log: \t\t\t'%s'\n", message);
        }*/
    return NULL;
}

/* Helper to get the log of a file from a previous revision to current. */
static int
_log_file(doSvn_state_t *me, const char *path,
          svn_revnum_t revision, int limit,
          apr_pool_t *pool)
{
    svn_error_t *err;
    svn_opt_revision_t rev1, rev2, peg_rev;
    apr_array_header_t *pthlist;
    pthlist = apr_array_make(pool, 1, sizeof(const char *));
    APR_ARRAY_PUSH(pthlist, const char *) = path;

    peg_rev.kind = svn_opt_revision_number;
    assert(me->our_rev > 0);
    peg_rev.value.number = me->our_rev;
    rev1.kind = svn_opt_revision_number;
    rev1.value.number = revision;
    rev2.kind = svn_opt_revision_number;
    rev2.value.number = me->our_rev;

    if (limit >= 0) {                   /* forward log of limit entries */
        if (0 == limit) limit = 1;
        err = svn_client_log3 (pthlist, &peg_rev, &rev1, &rev2, limit,
                               TRUE,       /* discover_changed_paths */
                               FALSE,      /* strict_node_history */
                               _log_cb_noop, me,
                               me->ctx, pool);
    } else {                            /* reverse direction log */
        err = svn_client_log3 (pthlist, &peg_rev, &rev2, &rev1, -limit,
                               TRUE,       /* discover_changed_paths */
                               FALSE,      /* strict_node_history */
                               _log_cb_noop, me,
                               me->ctx, pool);
    }
    if (err) {
        me->flags |= SVN_SAVE_WC;
        return ret_svn_err("SVN: log_file failed", err);
    }
    return 0;
}

static svn_error_t*
_blame_cb_noop(void *baton, apr_int64_t line_no, svn_revnum_t revision, const char *author, const char *date, const char *line, apr_pool_t *pool)
{
    doSvn_state_t *me = baton;
    me->entry_count++;
    /*if (author && date) {
        D_PRINTF(stderr, "blame: author=%s date=%s: \t'%s'\n",
                 author, date, line);
    } else {
        D_PRINTF(stderr, "blame: \t\t\t'%s'\n", line);
        }*/
    return NULL;
}

/* Helper to find blame on a file. */
static int
_blame_file(doSvn_state_t *me, const char *path, apr_pool_t *pool)
{
    svn_error_t *err;
    svn_opt_revision_t rev1, rev2, peg_rev;
    svn_diff_file_options_t * diff_opt;
    apr_array_header_t * diff_options_list;

    diff_options_list = apr_array_make(pool, 1, sizeof(const char *));
    APR_ARRAY_PUSH(diff_options_list, const char *) = "-u";

    diff_opt = svn_diff_file_options_create(pool);
    err = svn_diff_file_options_parse(diff_opt, diff_options_list, pool);

    peg_rev.kind = svn_opt_revision_number;
    assert(me->our_rev > 0);
    peg_rev.value.number = me->our_rev;
    rev1.kind = svn_opt_revision_number;
    rev1.value.number = 1;
    rev2.kind = svn_opt_revision_number;
    rev2.value.number = me->our_rev;

    err = svn_client_blame3 (path, &peg_rev, &rev1, &rev2,
                             diff_opt,
                             TRUE,      /* ignore_mime_type */
                             _blame_cb_noop, me,
                            me->ctx, pool);
    if (err) {
        me->flags |= SVN_SAVE_WC;
        return ret_svn_err("SVN: blame_file failed", err);
    }
    return 0;
}

/* Helper to update from version control */
static int
_update(rcs_command_t * conf, doSvn_state_t *me,
        const char *path, apr_pool_t *pool)
{
    apr_array_header_t *pthlist;
    svn_opt_revision_t revision;
    apr_array_header_t *result_revs;
    svn_revnum_t get_rev=0, *revp;
    svn_error_t *err;

    pthlist = apr_array_make(pool, 1, sizeof(const char *));
    APR_ARRAY_PUSH(pthlist, const char *) = path;
    result_revs = apr_array_make(pool, 1, sizeof(svn_revnum_t));
    if (conf->updateStep > 0) {
        assert(me->our_rev > 0);
        get_rev = me->our_rev + conf->updateStep;
        if (get_rev > me->head_rev) {  /* just get head */
            get_rev = 0;
        }
    }
    if (get_rev > 0) {
        revision.kind = svn_opt_revision_number;
        revision.value.number = get_rev;
    } else {
        revision.kind = svn_opt_revision_head;
    }
    err = svn_client_update2(&result_revs,
                             pthlist,
                             &revision,
                             TRUE,      /* recursive */
                             TRUE,      /* ignore externals */
                             me->ctx, pool);
    if (err)
        return ret_svn_err("SVN: update failed", err);
    if ((revp = (svn_revnum_t *)apr_array_pop(result_revs))) {
        assert (me->our_rev <= *revp);
        me->our_rev = *revp;
        if (me->head_rev < me->our_rev) {
            me->head_rev = me->our_rev;
        }
        if (me->our_rev <= 0) {
            return returnerr(stderr, "SVN: update FAULT rev=%ld\n",
                             me->our_rev);
        }
    }
    return 0;
}

/* Helper to commit changes */
static svn_error_t *
_commit(doSvn_state_t *me, const char *path, apr_pool_t *pool)
{
    svn_commit_info_t *info=NULL;
    apr_array_header_t *targets;
    svn_error_t *err;

    targets = apr_array_make(pool, 1, sizeof(const char *));
    APR_ARRAY_PUSH(targets, const char *) = path;

#if 0
    {                                   /* DEBUG */
        int i;
        const char *target;
        for (i = 0; i < targets->nelts; i++) {
            target = APR_ARRAY_IDX(targets, i, const char *);
            D_PRINTF(stderr, "commit target '%s'\n", target);
        }
    }
#endif
    err = svn_client_commit3(&info, targets,
                             TRUE,      /* recursive */
                             FALSE,     /* keep_locks */
                             me->ctx, pool);
    if (err)
        return err;
    if (info && (info->revision > 0)) { /* rev < 0 if nothing changed */
        me->head_rev = info->revision;
        me->our_rev = me->head_rev;     /* Is our_rev implicitly head??? */
    }
    return NULL;
}

/* callback for svn_client_status2. Do Nothing. */
static void
_status_cb_noop(void *baton, const char *path, svn_wc_status2_t *status)
{
    /*D_PRINTF(stderr, "svn: status of '%s': %d\n",
      path, status->text_status);*/
    ;
}

/* Check the status of files */
static int
_stat(doSvn_state_t *me, const char *dir, apr_pool_t *pool)
{
    svn_error_t *err;
    svn_opt_revision_t revision;
    revision.kind = svn_opt_revision_number; /* ignored if update is false */
    revision.value.number = me->our_rev;

    err = svn_client_status2(NULL, dir, &revision,
                             _status_cb_noop, me,
                             TRUE,      /* recurse */
                             FALSE,     /* get_all */
                             FALSE,     /* update against server*/
                             FALSE,     /* no_ignore */
                             TRUE,      /* ignore externals */
                             me->ctx, pool);
    if (err) {
        return ret_svn_err("SVN stat", err);
    }
    return 0;
}

/* Build file/dir list.  Callback for svn_io_dir_walk. */
svn_error_t *
file_walk_func(void *baton, const char *path,
                   const apr_finfo_t *finfo, apr_pool_t *pool)
{
    doSvn_state_t *me = baton;
    char *cp = NULL;

    if ((finfo->filetype != APR_REG) && (finfo->filetype != APR_DIR)) {
        D_PRINTF(stderr, "Skipping special file '%s'\n", path); /* DEBUG */
        return SVN_NO_ERROR;
    }
    for (cp = strchr(path, '/'); cp != NULL; cp = strchr(cp+1, '/')) {
        if (!cp) break;
        if ('.' == cp[1]) { /* (crudely) avoid .svn and .subversion */
            //D_PRINTF(stderr, "Skipping dot file '%s'\n", cp); /* DEBUG */
            return SVN_NO_ERROR;
        }
    }

    if (finfo->filetype == APR_REG) {
        //D_PRINTF(stderr, "Found FILE '%s'\n", path); /* DEBUG */
        WordAdd(&me->files, path);
    } else {
        //D_PRINTF(stderr, "Found DIR '%s'\n", path); /* DEBUG */
        WordAdd(&me->dirs, path);
    }
    return SVN_NO_ERROR;
}

// Main entry point at the start of a command block
// Creates and returns our session state
void *
doSvnStart(ptcx_t ptcx, mail_command_t *cmd, cmd_stats_t *ptimer)
{
    doSvn_state_t * me = XCALLOC (doSvn_state_t);
    rcs_stats_t   * stats = (rcs_stats_t *)ptimer->data;
    rcs_command_t * conf = (rcs_command_t *)cmd->data;
    unsigned long   loginId, subDirId;
    int ret;                            /* temp return status */
    apr_pool_t *subpool;
    svn_error_t *err;
    struct stat stat_buf;

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

    me->pool = svn_pool_create (NULL);
    me->resolve_paths = apr_array_make(me->pool, 1, sizeof(const char *));
    me->added_paths = apr_array_make(me->pool, 1, sizeof(const char *));
    me->removed_paths = apr_array_make(me->pool, 1, sizeof(const char *));
    subpool = svn_pool_create(me->pool);

    if (stat(me->work_dir, &stat_buf) >= 0) {
        /* recursive remove */
        err = svn_io_remove_dir(me->work_dir, subpool);
        if (err) {
            ret_svn_err("SVN: Error purging work_dir (setup)", err);
            goto _exit_fail;
        }
    }
    if (0 != mkdir(me->work_dir, 0777)) { /* umask sets real perms */
        returnerr(stderr, "SVN: Failed to mkdir '%s': %s\n",
                  me->work_dir, strerror(errno));
        goto _exit_fail;
    }
    D_PRINTF(stderr, "SVN working dir '%s'\n", me->work_dir);
    {
        char buff[PATH_LEN];
        subDirId = rangeNext (&stats->subDirRange, stats->subDirLast);
        stats->subDirLast = subDirId;
        D_PRINTF(
            stderr,
            "svn: repoUrl='%s' topDir='%s' subDir='%s' subDirId=%d rev=%d\n",
            conf->repoUrl, conf->topDir, conf->subDir, subDirId,
            conf->revision);
        snprintf(buff, sizeof(buff), conf->subDir, subDirId);
        me->checkout_path = svn_path_join_many(
            me->pool, conf->repoUrl, conf->topDir, buff, NULL);
    }
    // Form config path under work_dir
    me->config_dir = svn_path_join(me->work_dir, ".subversion", me->pool);
    (void)mkdir(me->config_dir, 0777);
    D_PRINTF(stderr,
             "svn: INIT user='%s' url='%s'\n",
             me->user, me->checkout_path);

    ret = _context_setup(conf, me);  /* setup dirs, svn context, and auth */
    if (ret < 0) {
        goto _exit_fail;
    }

    /* Check the current head revision to force authentication. */
    svn_pool_clear(subpool);
    if (gf_timeexpired) goto _exit_fail;
    event_start(ptcx, &stats->connect);
    ret = _get_head_rev(me, me->checkout_path, subpool);
    event_stop(ptcx, &stats->connect);
    if (ret < 0) {
        if (gf_timeexpired < EXIT_FAST)
            stats->connect.errs++;
        goto _exit_fail;
    }

    // checkout code
    me->total_bytes = 0;
    svn_pool_clear(subpool);
    if (gf_timeexpired) goto _exit_fail;
    event_start(ptcx, &stats->checkout);
    ret = _checkout(conf, me, subpool);
    event_stop(ptcx, &stats->checkout);
    stats->checkout.bytesread += me->total_bytes;
    if (ret < 0) {
        if (gf_timeexpired < EXIT_FAST)
            stats->checkout.errs++;
        goto _exit_fail;
    }

    // find all the files and dirs to aid add/modify/del
    svn_io_dir_walk(me->work_dir, APR_FINFO_TYPE,
                    file_walk_func, me, me->pool);
    // Always log this (with timestamp)
    returnerr(stderr, "SVN: %d-%d Got %s @%ld (%d files in %d dirs)\n",
              ptcx->processnum, ptcx->threadnum,
              me->checkout_path, me->our_rev,
              me->files.indexLen, me->dirs.indexLen);
    svn_pool_destroy(subpool);
    return me;

 _exit_fail:
    svn_pool_destroy(subpool);
    doSvnExit (ptcx, me);
    return NULL;
}

static int
_resolve_file(const char *path, doSvn_state_t *me, apr_pool_t *pool)
{
    int ret;
    svn_error_t *err;
    char mine[PATH_LEN];
    snprintf(mine, sizeof(mine), "%s.mine", path);
    ret = rename(mine, path);
    if (ret < 0) {
        me->flags |= SVN_SAVE_WC;       /* save this WC for inspection */
        return returnerr(stderr, "svn: FAILED to update '%s' from '%s'\n",
                         path, mine);
    }

    err = svn_client_resolved(path,
                              FALSE,    /* recursive */
                              me->ctx,
                              pool);
    if (err) {
        return ret_svn_err("SVN resolved", err);
    }
    D_PRINTF(stderr, "svn: resolved conflict on '%s'\n", path);
    return 0;
}

static int
_do_update(ptcx_t ptcx, mail_command_t *cmd,
           rcs_stats_t  * stats, doSvn_state_t *me, apr_pool_t *pool)
{                                   /* update */
    rcs_command_t     *conf = (rcs_command_t *)cmd->data;
    int ret;

    me->changed = 0;
    me->total_bytes = 0;
    event_start(ptcx, &stats->update);
    ret = _update(conf, me, me->work_dir, pool);
    event_stop(ptcx, &stats->update);
    stats->update.bytesread += me->total_bytes;
    if ((ret < 0) && (gf_timeexpired < EXIT_FAST)) {
        stats->update.errs++;
        return returnerr(stderr, "svn: Update failure %d-%d\n",
                         ptcx->processnum, ptcx->threadnum);
    }
    returnerr(stderr,
              "SVN: updated %d-%d WC to %s%ld, add/del/changed/conflict: %d/%d/%d/%d\n",
              ptcx->processnum, ptcx->threadnum,
              (me->head_rev == me->our_rev) ? "head=" : "", me->our_rev,
              me->added_paths->nelts,
              me->removed_paths->nelts,
              me->changed,
              me->resolve_paths->nelts);
    if (me->resolve_paths->nelts) {
        const char **path;
        while ((path = (const char **) apr_array_pop(me->resolve_paths))) {
            _resolve_file(*path, me, pool);
        }
    }
    if (me->removed_paths->nelts) {
        const char **path;
        while ((path = (const char **) apr_array_pop(me->removed_paths))) {
            if (WordDelete(&me->files, *path) < 0) {
                if (WordDelete(&me->dirs, *path) < 0) {
                    D_PRINTF(stderr,
                             "Failed to find %s in file or dir lists\n",
                             *path);
                } else {
                    D_PRINTF(stderr, "Removed %s from dir list\n", *path);
                }
            } else {
                D_PRINTF(stderr, "Removed %s from file list\n", *path);
            }
        }
    }
    if (me->added_paths->nelts) {
        const char **path;
        while ((path = (const char **) apr_array_pop(me->added_paths))) {
            struct stat stat_buf;
            if (stat(*path, &stat_buf) < 0) {
                continue;
            }
            if (S_ISDIR(stat_buf.st_mode)) {
                WordAdd(&me->dirs, *path); /* add to directory list */
                D_PRINTF(stderr, "Added %s to dir list\n", *path);
            } else {
                WordAdd(&me->files, *path); /* add to file list */
                D_PRINTF(stderr, "Added %s to file list\n", *path);
            }
        }
    }

    return 0;
}

/* Helper that returns the commit log message (passed in as the baton). */
static svn_error_t *
_get_commit_log (const char **log_msg,
                    const char **tmp_file,
                    const apr_array_header_t *commit_items,
                    void *baton,        /* msg string allocated in pool */
                    apr_pool_t *pool)
{
    const char *msg = baton;
    *log_msg = msg;
    *tmp_file = NULL;
    //D_PRINTF(stderr, "get_commit_log '%s'\n", *log_msg);
    return SVN_NO_ERROR;
}

static int
_do_commit(ptcx_t ptcx, mail_command_t *cmd,
           rcs_stats_t * stats, doSvn_state_t *me, apr_pool_t *pool)
{                                   /* commit */
    rcs_command_t       *conf = (rcs_command_t *)cmd->data;
    char *message = apr_palloc(pool, MSG_LEN);
    int plen = strlen(me->work_dir) + 5;
    char *path = apr_palloc(pool, plen);
    int tries;
    svn_error_t *err;
    apr_pool_t *iterpool;

    {                                   // random change message
        char chunk[60];                 /* TODO: configurable size */
        int ret;
        ret = WordSequence(chunk, sizeof(chunk),
                           &wordDict, syntaxChars, 0);
        if (ret < 0) {
            D_PRINTF(stderr, "svn: Error generating words(commit)\n");
            strcpy(chunk, "mstone test");
        }
        snprintf(message, MSG_LEN,
                 "Block: %d Process: %d Thread: %d Msg: %s",
                 cmd->blockID, ptcx->processnum, ptcx->threadnum,
                 chunk);
    }
    assert(path);
    snprintf(path, plen, "%s/", me->work_dir); /* must add trailing / */
    D_PRINTF(stderr, "svn: Committing %s '%s'\n", path, message);
    me->ctx->log_msg_func2 = _get_commit_log;
    me->ctx->log_msg_baton2 = message;

    iterpool = svn_pool_create(me->pool);
    for (tries = conf->commitRetryCnt ;
         (tries > 0) && (gf_timeexpired < EXIT_SOON);
         --tries) { /* commit can fail due to external factors */
        int ret;
        me->total_bytes = 0;
        svn_pool_clear(iterpool);
        event_start(ptcx, &stats->commit);
        err = _commit(me, path, iterpool);
        event_stop(ptcx, &stats->commit);

        stats->commit.byteswritten += me->total_bytes;
        if (!err) {
            D_PRINTF(stderr, "svn: Committed %s at %ld\n", path, me->head_rev);
            break;
        }

        // Commit failed.  Figure out why and recover.
        stats->commit.errs++;

        // differentiate between error types and take appropriate action.
        // All the interesting information is in the child error message.
        if (!err->child || !err->child->message) {
            ret_svn_err("SVN: commit error with no info", err);
            svn_pool_destroy(iterpool);
            return -1;
        }
        ret = err->apr_err;
        if ((SVN_ERR_BASE == ret) && err->child) {
            ret = err->child->apr_err;
        }
        switch (ret) {

            /* User initiated. */
        case SVN_ERR_CANCELLED:
            ret_svn_err("SVN: commit terminated by user", err);
            svn_pool_destroy(iterpool);
            return -1;

            /* Transient errors. */
        case SVN_ERR_FS_TRANSACTION_DEAD:
        case SVN_ERR_FS_LOCK_EXPIRED:
        case SVN_ERR_REPOS_LOCKED:
        case SVN_ERR_REPOS_HOOK_FAILURE: /* hooks might not be transient... */
        case SVN_ERR_REPOS_POST_COMMIT_HOOK_FAILED:
        case SVN_ERR_REPOS_POST_LOCK_HOOK_FAILED:
        case SVN_ERR_REPOS_POST_UNLOCK_HOOK_FAILED:
        case SVN_ERR_APMOD_ACTIVITY_NOT_FOUND:
        case SVN_ERR_APMOD_CONNECTION_ABORTED:
        case SVN_ERR_RA_SVN_CONNECTION_CLOSED:
        case SVN_ERR_RA_SVN_IO_ERROR:
        case SVN_ERR_RA_SVN_MALFORMED_DATA:
            // Show user because it may indicate system corruption
            ret_svn_err("SVN: transient error", err);
            continue;            // Just re-try.

            /* errors that can be fixed by update+retry */
        case SVN_ERR_FS_TXN_OUT_OF_DATE:
        case SVN_ERR_FS_CONFLICT:
        case SVN_ERR_FS_REP_CHANGED:
        case SVN_ERR_FS_OUT_OF_DATE:
        case SVN_ERR_FS_NOT_FOUND:
        case SVN_ERR_RA_OUT_OF_DATE:
        case SVN_ERR_RA_DAV_PATH_NOT_FOUND:
        case SVN_ERR_WC_FOUND_CONFLICT: /* obstruction.  update may not help */
            // Show error if multiple re-tries have already been tried
            if ((tries <= 3) || (gn_debug > 0))
                ret_svn_err("SVN: Handling commit error", err);
            _do_update(ptcx, cmd, stats, me, iterpool);
            continue;

            /* All other errors can not be handled automatically. */
        default:
            me->flags |= SVN_SAVE_WC;       /* save this WC for inspection */
            ret_svn_err("SVN: Unknown commit error", err);
            svn_pool_destroy(iterpool);
            return -1;
        }
    }
    svn_pool_destroy(iterpool);

    if (!tries) {
        // continue since an update could fix the problems
        me->flags |= SVN_SAVE_WC;       /* save this WC for inspection */
        return returnerr(stderr, "SVN: WARNING: exhausted commit retries\n");
    }
    return 0;
}

// The body of a command block
int
doSvnLoop(ptcx_t ptcx, mail_command_t *cmd,
             cmd_stats_t *ptimer, void *mystate)
{
    doSvn_state_t       *me = mystate;
    rcs_stats_t *stats = (rcs_stats_t *)ptimer->data;
    rcs_command_t       *conf = (rcs_command_t *)cmd->data;
    int ret;
    float todo;
    long rev_span;
    int  tot_errors = 0, max_errors = 10;
    apr_pool_t *jobpool = svn_pool_create(me->pool);
    me->jobpool = jobpool;

    if (0 == conf->revSpan) {           /* use full range */
        rev_span = me->our_rev;
    } else {                    /* use indicated range (ignore sign) */
        rev_span = MIN(me->our_rev, ABS(conf->revSpan));
    }

    for (todo = conf->listCnt; todo > 0.0; todo = 0.0) { /* list files */
        if ((todo < 1.0) && ((RANDOM() & 0xffff) >= (0xffff * todo))) {
            break;
        }
        svn_pool_clear(jobpool);
        me->total_bytes = 0;
        event_start(ptcx, &stats->list);
        ret = _list_url(me, me->checkout_path, jobpool);
        event_stop(ptcx, &stats->list);
        stats->list.bytesread += me->total_bytes;
        if ((ret < 0) && (gf_timeexpired < EXIT_FAST)) {
            stats->list.errs++;
            if (++tot_errors >= max_errors) /* soft error */
                goto _exit_fail;
        }
    }

    for (todo = sample(conf->addCnt);
         todo > 0.0;
         todo -= 1.0) {                 /* Create/Add files */
        FILE *fp;
        int  file_size = sample(conf->addFileSize); /* total size goal */
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
            D_PRINTF(stderr, "svn: Error generating words(add)\n");
            break;
        }
        snprintf(filename, sizeof(filename),
                 "%s/%s.m", WordRandom(&me->dirs), chunk);
        svn_pool_clear(jobpool);

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

        // Add file to svn.
        me->total_bytes = 0;
        event_start(ptcx, &stats->add);
        ret = _add(me, filename, jobpool);
        event_stop(ptcx, &stats->add);
        stats->add.bytesread += me->total_bytes;
        if ((ret < 0) && (gf_timeexpired < EXIT_FAST)) {
            stats->add.errs++;
            if (++tot_errors >= max_errors) /* soft error */
                goto _exit_fail;
        }
        D_PRINTF(stderr, "svn: ADD '%s'\n", filename);
        //TODO add to me->files
    }

    for (todo = sample(conf->mkdirCnt);
         todo > 0.0;
         todo -= 1.0) {                 /* Add directories*/
        struct stat stat_buf;
        char chunk[80];
        char dirname[PATH_LEN];

        if ((todo < 1.0) && ((RANDOM() & 0xffff) >= (0xffff * todo))) {
            break;
        }
        // Create a random new directory under a random directory.
        ret = WordSequence(chunk, sizeof(chunk), &wordDict, "_", 2);
        if (ret < 0) {
            D_PRINTF(stderr, "svn: Error generating directory name\n");
            break;
        }
        snprintf(dirname, sizeof(dirname),
                 "%s/%s.d", WordRandom(&me->dirs), chunk);
        svn_pool_clear(jobpool);

        // see if the directory already exits
        if (stat(dirname, &stat_buf) >= 0) {
            //++todo;  // retry limit???
            continue;
        }

        // Add directory to svn.
        me->total_bytes = 0;
        event_start(ptcx, &stats->mkdir);
        ret = _mkdir(me, dirname, jobpool);
        event_stop(ptcx, &stats->mkdir);
        stats->mkdir.bytesread += me->total_bytes;
        if ((ret < 0) && (gf_timeexpired < EXIT_FAST)) {
            stats->mkdir.errs++;
            if (++tot_errors >= max_errors) /* soft error */
                goto _exit_fail;
        }
        D_PRINTF(stderr, "svn: MKDIR '%s'\n", dirname);
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
        svn_pool_clear(jobpool);
        me->total_bytes = 0;
        me->entry_count = 0;
        if (conf->revSpan >= 0) {
            rev = me->our_rev - (RANDOM() % rev_span);
            rev = MAX(rev, 1);
        } else {                        /* use checked out value */
            rev = 0;
        }
        event_start(ptcx, &stats->diff);
        ret = _diff_file(me, filepath, rev, jobpool);
        event_stop(ptcx, &stats->diff);
        stats->diff.bytesread += me->total_bytes;
        D_PRINTF(stderr, "svn: diff from %ld to WC=%ld -> %ld bytes\n",
                 rev, me->our_rev, me->entry_count);
        if ((ret < 0) && (gf_timeexpired < EXIT_FAST)) {
            stats->diff.errs++;
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
        svn_pool_clear(jobpool);
        rev = me->our_rev - (RANDOM() % rev_span);
        rev = MAX(rev, 1);
        me->total_bytes = 0;
        me->entry_count = 0;
        logLimit = sample(conf->logLimit);
        event_start(ptcx, &stats->log);
        ret = _log_file(me, filepath, rev, logLimit, jobpool);
        event_stop(ptcx, &stats->log);
        stats->log.bytesread += me->total_bytes;
        D_PRINTF(stderr, "svn: log from %ld of %d entries -> %ld\n",
                 rev, logLimit, me->entry_count);
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
        svn_pool_clear(jobpool);
        me->total_bytes = 0;
        me->entry_count = 0;
        event_start(ptcx, &stats->blame);
        ret = _blame_file(me, filepath, jobpool);
        event_stop(ptcx, &stats->blame);
        stats->blame.bytesread += me->total_bytes;
        D_PRINTF(stderr, "svn: blame @%ld -> %ld\n",
                 me->our_rev, me->entry_count);
        if ((ret < 0) && (gf_timeexpired < EXIT_FAST)) {
            stats->blame.errs++;
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
            D_PRINTF(stderr, "svn: No files safe to delete\n");
            break;
        }

        svn_pool_clear(jobpool);
        // Schedule file for removal from svn.
        me->total_bytes = 0;
        event_start(ptcx, &stats->del);
        ret = _delete(me, filename, jobpool);
        event_stop(ptcx, &stats->del);
        stats->del.bytesread += me->total_bytes;
        if ((ret < 0) && (gf_timeexpired < EXIT_FAST)) {
            stats->del.errs++;
            if (++tot_errors >= max_errors) /* soft error */
                goto _exit_fail;
        }
        D_PRINTF(stderr, "svn: DELETE '%s'\n", filename);
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
            D_PRINTF(stderr, "svn: No files safe to modify\n");
            break;
        }
        fd = open(filename, O_RDWR, 0);
        if (fd < 0) {
            D_PRINTF(stderr, "svn: Warning '%s' missing.\n", filename);
            WordPointerDelete(&me->files, filename);
            break;
        }

        chunkCnt = sample(conf->modChunkCnt);
        chunkSize = sample(conf->modChunkSize);
        // Track modifies to count changed files and indicate client load.
        event_start(ptcx, &stats->mod);
        /* write words at N randomly selected locations in the file */
        for (mods = 0; mods < chunkCnt; ++ mods) {
            char chunk[chunkSize];
            ret = WordSequence(chunk, sizeof(chunk),
                               &wordDict, syntaxChars, 0);
            if (ret < 0) {
                D_PRINTF(stderr, "svn: Error generating words(mod)\n");
                close(fd);
                break;
            }
            fstat(fd, &info);
            offset =  (info.st_size) ? (RANDOM() % info.st_size) : 0;
            // BUG: this doesnt understand UTF8 sequences
            lseek(fd, offset, SEEK_SET);
            write(fd, chunk, strlen(chunk));
        }
        event_stop(ptcx, &stats->mod);
        D_PRINTF(stderr, "svn: Modified '%s'\n", filename);
        close(fd);
    }

    for (todo = sample(conf->statCnt);
         todo > 0.0;
         todo -= 1.0) {                 /* stat files */
        if ((todo < 1.0) && ((RANDOM() & 0xffff) >= (0xffff * todo))) {
            break;
        }
        svn_pool_clear(jobpool);
        me->total_bytes = 0;
        event_start(ptcx, &stats->stat);
        ret = _stat(me, me->work_dir, jobpool);
        event_stop(ptcx, &stats->stat);
        stats->stat.bytesread += me->total_bytes;
        if ((ret < 0) && (gf_timeexpired < EXIT_FAST)) {
            stats->stat.errs++;
            if (++tot_errors >= max_errors) /* soft error */
                goto _exit_fail;
        }
    }

    svn_pool_clear(jobpool);
    (void)_do_update(ptcx, cmd, stats, me, jobpool);

    for (todo = conf->commitCnt; todo > 0.0; todo = 0.0)  { /* Commit (once) */
        if ((todo < 1.0) && ((RANDOM() & 0xffff) >= (0xffff * todo))) {
            break;
        }
        svn_pool_clear(jobpool);
        if (_do_commit(ptcx, cmd, stats, me, jobpool) < 0) {
            goto _exit_fail;          /* commits are failing, abort */
        }
    }

    me->jobpool = NULL;
    svn_pool_destroy(jobpool);
    return 0;

 _exit_fail:
    me->jobpool = NULL;
    svn_pool_destroy(jobpool);
    return -1;

}

// Exit a command block
void
doSvnEnd(ptcx_t ptcx, mail_command_t *cmd,
            cmd_stats_t *ptimer, void *mystate)
{
    doSvn_state_t *me = (doSvn_state_t *)mystate;
    rcs_stats_t   *stats = (rcs_stats_t *)ptimer->data;
    svn_error_t   *err = NULL;

    /* Logout allows us to track concurrent clients */
    event_start(ptcx, &stats->logout);
    /* Note: Svn doesn't involve the server when removing the working copy. */
    if (me->flags & SVN_SAVE_WC) {
        static int saveCnt = 0;
        rcs_command_t * conf = (rcs_command_t *)cmd->data;
        char name[PATH_LEN];
        while (saveCnt < 100) {
            snprintf(name, sizeof(name), "%s/save-%d",
                     conf->localDir, saveCnt++);
            if (rename(me->work_dir, name) >= 0) {
                returnerr(stderr, "SVN: saved %s as %s for postmortem\n",
                          me->work_dir, name);
                break;
            }
        }
        if (saveCnt >= 100) {
            returnerr(stderr,
                      "SVN: could not save %s: too many save attempts %d\n",
                      me->work_dir, saveCnt);
        }
    } else {
        err = svn_io_remove_dir(me->work_dir, me->pool); /* recursive remove */
    }
    event_stop(ptcx, &stats->logout);
    if (err) {
        ret_svn_err("SVN: Error purging work_dir (logout)", err);
    }

    doSvnExit (ptcx, me);
}

void
SvnParseFree(pmail_command_t cmd)
{
    RcsParseFree(cmd);
}

void
SvnFree(void)
{
    RcsFree();
    apr_terminate();
}

/* Register this protocol with the system. */
void
SvnRegister(void)
{
    ProtocolRegister(
        "SVN", SvnParseStart, SvnParseEnd,
        doSvnStart, doSvnLoop, doSvnEnd,
        RcsStatsFormat, RcsStatsInit, RcsStatsUpdate, RcsStatsOutput,
        SvnParseFree, SvnFree);
}

#ifdef MODULE
/* This is the entry point if compiled as an independent module. */
void
ModuleRegister(void)
{
    SvnRegister();
}
#endif
