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
#include <ctype.h>                      /* isspace */
#define __USE_ISOC9X	1	/* for NAN on Linux */
#include <math.h>
#include <limits.h>

#include "idle.h"
#include "xalloc.h"

/* UNIF01 -- a random variable with a distribution ~Unif(0,1). */
#define UNIF01 (drand48())
/* STDNORMAL -- a random variable with distribution ~N(0,1) */
#define STDNORMAL (sqrt(-2*log(UNIF01))*cos(2*M_PI*UNIF01))

typedef struct {
    char          *name;
    distrib_func_t func;
    int            nparams;
} distribution_t;

/*
** CONST(X) -- constant value of X
*/
static double
_constant(double *params)
{
    return params[0];
}

static distribution_t constant = {
    "const", &_constant, 1,
};

dinst_t *
constant_value(double val)
{
    dinst_t *ret = xalloc(sizeof(dinst_t));

    ret->min = ret->max = DIST_NOLIMIT;
    ret->func = &_constant;
    ret->params[0] = val;
    return ret;
}

/*
** UNIF(a, b) -- distribution ~unif(a, b)
*/
static double
_uniform(double *params)
{
    return params[0] + UNIF01*(params[1] - params[0]);
}

static distribution_t uniform = {
    "unif", &_uniform, 2,
};

/*
** EXP(mu) -- idle times distributed as ~exp(1/mu).
*/
static double
_exponential(double *params)
{
    return -params[0]*log(UNIF01);
}

static distribution_t exponential = {
    "exp", &_exponential, 1,
};

/*
** WEIB(mu, alpha, gamma) -- Weibull distribution.
**
**	The Weibull function f(x) = g/a * ((x-m)/a)^(g-1) * e^(-(x-m)/a)^g
**
**	gamma <= 1.0
*/

static double
_weibull(double *params)
{
    return params[0] - params[1]*pow(log(1.0 - UNIF01), 1.0/params[2]);
}

static distribution_t weibull = { "weib", &_weibull, 3 };

/*
** NORMAL(mu, sigma) -- ~normal(mu, sigma).
*/
static double
_normal(double *params)
{
    return params[0] + params[1]*STDNORMAL;
}

static distribution_t normal = {
    "normal", &_normal, 2,
};

/*
** LOGNORMAL(a, b) -- ~lognormal(a, b).
*/
static double
_lognormal(double *params)
{
    return params[0]*exp(sqrt(params[1])*STDNORMAL);
}

static distribution_t lognormal = {
    "lognormal", &_lognormal, 2,
};

#if 0				/* not ready for prime-time */
/*
** POISSON(mu) -- ~poisson(mu)
*/
static double
_poisson(double *params)
{
}

static distribution_t poisson = {
    "poisson", &_poisson, 1,
};
#endif

/*
** BINOMIAL(mu) -- ~binomial(p)
*/
static double
_binomial(double *params)
{
    return UNIF01 < params[0];
}

static distribution_t binomial = {
    "binomial", &_binomial, 1,
};

dinst_t *
percentage(double val)
{
    dinst_t *ret = xalloc(sizeof(dinst_t));
    ret->min = ret->max = DIST_NOLIMIT;
    assert(val >= 0 && val <= 1.0);
    ret->func = &_binomial;
    ret->params[0] = val;
    return ret;
}

static distribution_t *distributions[] =
{
    &constant,
    &uniform,
    &exponential,
    &weibull,
    &normal,
    &lognormal,
    &binomial,
    NULL
};

static int
parse_params(
    char *tok,
    int n,
    double *params,
    value_parser_t value_parser)
{
    char *p;
    int i;
    for (p = strtok(tok, ","), i = 0;
        p != NULL && i < n;
        p = strtok(NULL, ","), i++)
    {
        params[i] = (*value_parser)(p);
        if (params[i] == 0 && *p != '0')
            fprintf(stderr,
                    "Warning: parameter '%s' parsed to 0 -- syntax error?\n", p);
    }
    if (i < n || p != NULL)
    {
        fprintf(stderr, "Parse error: %s.\n",
                (i < n)?"not enough parameters":"too many parameters");
        return -1;
    }
    return n;
}

static void
strsub(
    char *dest,
    char *begin,
    char *end)
{
    while(begin < end && *begin)
        *dest++ = *begin++;
    *dest = '\0';
}

dinst_t *
parse_distrib(
    char *tok,
    value_parser_t value_parser)
{
    char name[20], params[80];
    distribution_t** d;
    char *oparen, *cparen;
    char *p = tok;
    double cmin, cmax;

    if ((tok = strchr(tok, '~')) == NULL)
    {
        double val = value_parser(p);
        if (strchr(p, '%'))
            return percentage(val/100.0);
        return constant_value(val);
    }
    ++tok;			/* skip over tilde */
    oparen = strchr(tok, '(');
    if (oparen == NULL)
    {
        assert(!"no open parenthesis");
        return NULL;
    }
    strsub(name, tok, oparen);
    if ((cparen = strchr(oparen, ')')) == NULL)
    {
        assert(!"no closing parenthesis");
        return NULL;
    }
    strsub(params, oparen+1, cparen);

    /* now look for constraints == ':' '[' [min] ',' [max] ']' */
    cmin = cmax = DIST_NOLIMIT;
    if ((cparen = strchr(cparen, ':')) != NULL
        && (oparen = strchr(cparen, '[')) != NULL
        && (p = strchr(oparen + 1, ',')) != NULL
        && (cparen = strchr(p + 1, ']')) != NULL)
    {
        *p = '\0';
        *cparen = '\0';
        /* skip whitespace */
        while (isspace(*++oparen))
            ;
        if (oparen < p)
            cmin = (*value_parser)(oparen);

        while (isspace(*++p))
            ;
        if (p < cparen)
            cmax = (*value_parser)(p);
    }

    for (d = distributions; *d; d++)
    {
        if (strcmp(name, (*d)->name) == 0)
        {
            dinst_t *ret = xalloc(sizeof(dinst_t)
                                  + ((*d)->nparams - 1)*sizeof(double));

            if (parse_params(params, (*d)->nparams, ret->params,
                            value_parser)
               < 0)
            {
                xfree(ret);
                assert(!"parse_params failed");
                return NULL;
            }
            ret->func = (*d)->func;
            ret->min = cmin;
            ret->max = cmax;
            return ret;
        }
    }
    fprintf(stderr, "No such distribution: '%s'\n", name);
    assert(0);
    return NULL;
}

double
sample(dinst_t *inst)
{
    double ret;
    if (inst == NULL)
        return 0.0;
    ret = (inst->func)(inst->params);

    /* constrain value */
    if ((inst->min != DIST_NOLIMIT) && (ret < inst->min))
        ret = inst->min;
    if ((inst->max != DIST_NOLIMIT) && (ret > inst->max))
        ret = inst->max;
    return ret;
}
