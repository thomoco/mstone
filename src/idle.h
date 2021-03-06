/* -*- Mode: C; c-file-style: "stroustrup"; comment-column: 40 -*- */
/* Probability distribution support */
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
 * Contributor(s):     Sean O'Rourke <sean@sendmail.com>
 *                     Sendmail, Inc.
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

#ifndef _idle_h
#define _idle_h

#ifdef NAN
# define DIST_NOLIMIT NAN
#elif defined(DBL_MAX)
# define DIST_NOLIMIT DBL_MAX
#elif defined(FLT_MAX)
# define DIST_NOLIMIT FLT_MAX
#elif defined(MAXFLOAT)
# define DIST_NOLIMIT MAXFLOAT
#else
/* this is what FLT_MAX looks like on linux 2.2.x i386 */
# define DIST_NOLIMIT ((float)3.40282346638528860e+38)
#endif

/* Set the minimum min value for a distribution */
#define dinst_min_min(dp,v) \
	if ((DIST_NOLIMIT == (dp->min)) || ((dp->min) < (v))) (dp->min) = (v);
/* Set the maximum max value for a distribution */
#define dinst_max_max(dp,v) \
	if ((DIST_NOLIMIT == (dp->max)) || ((dp->max) < (v))) (dp->max) = (v);


typedef double (*distrib_func_t)(double *);
typedef double (*value_parser_t)(const char *);

typedef struct
{
	distrib_func_t func;
	double min, max;
	double params[1];	/* actual length varies by distribution */
} dinst_t;

extern dinst_t *parse_distrib(char*, value_parser_t);
extern dinst_t *constant_value(double);
extern double sample(dinst_t *);

#endif /* _idle_h */
