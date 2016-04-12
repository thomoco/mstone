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
#include "xalloc.h"
#include <openssl/ssl.h>

#ifdef USE_PTHREADS
// ASSUME openssl-0.9.[78]
/* Implementation of locking callbacks to make OpenSSL thread-safe. */

static pthread_mutex_t *locks;
static size_t num_locks;

#ifndef HAVE_CRYPTO_SET_IDPTR_CALLBACK
/* Named to be obvious when it shows up in a backtrace. */
static unsigned long thread_id(void)
{
  /* This will break if pthread_t is a structure; upgrading OpenSSL
   * >= 0.9.9 (which does not require this callback) is the only
   * solution.  */
  return (unsigned long) pthread_self();
}
#endif

/* Another great API design win for OpenSSL: no return value!  So if
 * the lock/unlock fails, all that can be done is to abort. */
static void thread_lock(int mode, int n, const char *file, int line)
{
  if (mode & CRYPTO_LOCK) {
    if (pthread_mutex_lock(&locks[n])) {
      abort();
    }
  }
  else {
    if (pthread_mutex_unlock(&locks[n])) {
      abort();
    }
  }
}
#endif /* USE_PTHREADS */

/* Do any extra initialization related to ssl
 */
void
mstoneSslInit(void)
{
#ifdef USE_PTHREADS
  int n;
  num_locks = CRYPTO_num_locks();
  locks = xcalloc(num_locks * sizeof(*locks));
  for (n = 0; n < num_locks; n++) {
    if (pthread_mutex_init(&locks[n], NULL)) {
      returnerr(stderr, "Failed to initialize mutex %d\n", n);
    }
  }
  CRYPTO_set_id_callback(thread_id);
  CRYPTO_set_locking_callback(thread_lock);
  D_PRINTF(stderr, "Initialized SSL for threading\n");

#endif /* USE_PTHREADS */
}

void
mstoneSslExit(void)
{
    /* TODO: un-register handlers?*/
  return;
}
