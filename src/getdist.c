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

#include <stdlib.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>
#define __USE_ISOC9X	1	/* for NAN on Linux */
#include <math.h>
#include <limits.h>

#include "idle.h"
#include "bench.h"
#include "xalloc.h"

int
main(int argc, char **argv)
{
    char buf[80];
    dinst_t *distrib;
    extern char *optarg;
    char 	c;
    int 	nsamples = 2000;
    pid_t 	myPID = getpid();
    long 	randomSeed = time(0) ^ myPID;

    while((c = getopt(argc, argv, "hn:S:")) != EOF)
    {
        switch(c)
        {
        case 'n':
            nsamples = atoi(optarg);
            break;

        case 'S':
            randomSeed = atoi(optarg);
            break;

        case '?':
            printf("Unrecognized option '%c'\n", c);
            // fall through into help
        case 'h':
            printf("Usage: %s [-n NumSamples(2000)] [-S seed]\n", argv[0]);
            exit(0);
            break;
        }
    }
    printf( "Random seed %ld\n", randomSeed);
    srand48(randomSeed);

    printf("Enter 'quit' to exit.\n");
    while(1) {
        double var, mean;

        *buf = 0;
        printf("Distribution: ");
        if (NULL == fgets(buf, sizeof(buf), stdin)) {
            exit(0);
        }
        if (strlen(buf) <= 2) {
            continue;
        }
        if (0 == strncmp("quit", buf, 4)) {
          break;
        }
        if ((distrib = parse_distrib(buf, &atof))) {
            int i;
            double total = 0.0;
            double sq = 0.0;
            printf("Samples:\n");
            for (i=0; i < 10; ++i) {    /* show first 10 samples */
                double samp = sample(distrib);
                total += samp;
                sq += samp * samp;
                printf("%f\n", samp);
            }
            for ( ; i < nsamples; ++i) { /* just gather statistics on rest */
                double samp = sample(distrib);
                total += samp;
                sq += samp * samp;
            }
            mean = total / (double)nsamples;
            var = (sq / (double)nsamples - mean * mean);
            printf("mean:   %f\nvar:    %f\nstddev: %f\n",
                   mean, var, sqrt(var));
        } else {
            printf("Unable to parse distribution from: %s", buf);
        }
    }
    return 0;
}
