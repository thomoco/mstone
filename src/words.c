/* -*- Mode: C; c-file-style: "stroustrup"; comment-column: 40 -*- */
/* words.c: Work with random sequences of words */
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
 * Contributor(s):	Dan Christian <dchristianATgoogle.com>
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
#include "xalloc.h"

words_t  wordDict;      /* word dictionary for random generation */
char *   syntaxChars;               /* syntax string */
char *  dictPath;                   /* path to word dictionary */

/* Helper for ReadWordDict.
   Trim newline from buff.
   Return -1 on EOF, 1 for words with apostrophe, else 0
*/
static inline int
_clean_line(char *buff)
{
    char *cp;
    if (!buff) {
        return -1;
    }
    cp = strpbrk(buff, "'\r\n");       /* find apostrophe or newline */
    if (cp) {
        if ('\'' == *cp) {   /* skip lines with apostrophe */
            return 1;
        }
        *cp = 0;                        /* trim CR-NL */
    }
    return 0;
}

/* Read in a dictionary file and index it.
   Returns -1 on error.
 */
int
ReadWordDict(words_t *dict, const char *path)
{
    char buff[8*1024];                  /* read buffer */
    FILE *fin;                          /* input file */
    int count;                          /* line count */
    char **strp;                        /* index pointer */

    fin = fopen(path, "r");
    if (!fin) {
        return returnerr(stderr, "Unable to open '%s'\n", path);
    }
    for (count=0; !feof(fin); )
    {
        int ret = _clean_line(fgets(buff, sizeof(buff), fin));
        if (ret < 0) {
            break;
        }
        if ((ret > 0) || (!*buff)) {
            continue;
        }
        ++count;
    }
    D_PRINTF(stderr, "Found %d words in %s\n", count, path);
    if (count <= 0) {
        return returnerr(stderr, "No words found in '%s'\n", path);
    }
    dict->indexLen = dict->indexSize = count;
    dict->index = xcalloc(dict->indexSize * sizeof(*dict->index));
    dict->data = stringListAlloc();
    rewind(fin);                        /* re-read file (now cached) */

    buff[0] = 0;
    for (strp = dict->index, count=0; !feof(fin); ) {
        int ret = _clean_line(fgets(buff, sizeof(buff), fin));
        if (ret < 0) {
            break;
        }
        if ((ret > 0) || (!*buff)) {
            continue;
        }
        ++count;
        stringListAdd(dict->data, buff);
        *strp++ = dict->data->tail->value;
    }
    assert(count <= dict->indexLen);
    fclose(fin);
    return 0;
}

/* Initialize a word dictionary (not needed by ReadWordDict). */
void
WordsInit(words_t *dict)
{
    dict->indexLen = 0;
    dict->indexSize = 8;
    dict->index = xcalloc(dict->indexSize * sizeof(char **));
    dict->data = stringListAlloc();
}

/* Free the contents of a word dictionary. */
void
WordsDestroy(words_t *dict)
{
    if (!dict->data)                    /* never initialized */
        return;
    dict->indexLen = 0;
    dict->indexSize = 0;
    xfree(dict->index);
    dict->index = NULL;
    stringListFree(dict->data);
    dict->data = NULL;
}

/* Add a copy of WORD into the word dictionary DICT.
   Similar to ReadWordDict, but slower.
*/
void
WordAdd(words_t *dict, const char *word)
{
    if (!dict->data) {                  /* initialize */
        WordsInit(dict);
    }
    if (dict->indexLen >= dict->indexSize) { /* grow index */
        dict->indexSize += 8;
        dict->index = xrealloc(dict->index, dict->indexSize * sizeof(char **));
    }

    stringListAdd(dict->data, word);
    dict->index[dict->indexLen++] = dict->data->tail->value;
}

/* Return a pointer to a random word.  Static storage - do not free. */
const char *
WordRandom(words_t *dict)             /* dictionary */
{
    int tries;
    int randnum = RANDOM();

    if (!dict || !dict->indexLen)
        return NULL;

    tries = MIN(10, dict->indexLen);
    while (tries-- > 0) { /* simple re-try to deal with deleted entries */
        int idx =  randnum % dict->indexLen;
        if (dict->index[idx]) {
            return dict->index[idx];
        }
        randnum = RANDOM();
    }
    return NULL;
}

/* Find a word from the index and return index.
   Word is matched by string comparison.

   Returns:
     -2 if the arguments are bad.
     -1 if the match wasn't found.
     N  index of word.
*/
int
WordFind(words_t *dict, const char *word)
{
    int index;
    if (!dict || !dict->indexLen || !word)
        return -2;
    for (index = 0; index < dict->indexLen; ++index) {
        if ((NULL == dict->index[index])
            || (0 != strcmp (word, dict->index[index]))) {
            continue;
        }
        return index;
    }
    return -1;
}

/* Find a word from the index and delete it.
   Word is matched by string comparison.

   Returns:
     -2 if the arguments are bad.
     -1 if the match wasn't found.
     1 if a word was found and deleted.
*/
int
WordDelete(words_t *dict, const char *word)
{
    int index;
    if (!dict || !dict->indexLen || !word)
        return -2;
    for (index = 0; index < dict->indexLen; ++index) {
        if ((NULL == dict->index[index])
            || (0 != strcmp (word, dict->index[index]))) {
            continue;
        }
        // Instead of holding a lock to re-write index, we just NULL it.
        dict->index[index] = NULL;
        return 1;
    }
    return -1;
}

/* Find a word from the index and delete it.
   Word is matched by direct pointer comparison.

   Returns:
     -2 if the arguments are bad.
     -1 if the match wasn't found.
     1 if a word was found and deleted.
*/
int
WordPointerDelete(words_t *dict, const char *word)
{
    int index;
    if (!dict || !dict->indexLen || !word)
        return -2;
    for (index = 0; index < dict->indexLen; ++index) {
        if (word != dict->index[index])
            continue;

        // Instead of holding a lock to re-write index, we just NULL it.
        dict->index[index] = NULL;
        return 1;
    }
    return -1;
}

/* Fill a buffer with a sequence of words.
   Returns the length of the string.
 */
int
WordSequence(char *buff,                /* buffer to fill in */
             size_t size,               /* size of buffer */
             words_t *dict,             /* dictionary */
             const char *sep_chars,  /* separator characters or NULL*/
             int max_words)             /* maximum words to use */
{
    char *cp = buff;                    /* current insert position */
    int len = 1;                        /* length in buffer (with NULL) */
    int tries = 0;
    int sep_len = (sep_chars) ? strlen(sep_chars) : 0;
    assert (sep_len < (64*1024)); /* due to our random bit wrangling */

    if (!buff || !size || !dict->indexLen)
        return -1;

    while (len < size) {
        const char *wp = WordRandom(dict);
        int sz = strlen(wp);
        if ((len + sz) < size) {
            strcpy(cp, wp);
            cp += sz;
            len += sz;
            if (--max_words == 0)
                break;
            if (sep_len && (len < size)) { /* insert a separator character */
                *cp++ = sep_chars[RANDOM() % sep_len];
                len++;
            }
        } else {  /* Up to 5 tries to find words that fit in buffer */
            if (++tries > 5) {
                break;
            }
            // else try another random selected word
        }
    }
    assert(len <= size);
    *cp = 0;
    return len;
}

/* Helper to return a safe file to modify or delete.
   Never returns .* or README*
   Returns pointer to a file string (statically allocated).
*/
const char *
GetSafeFilename(words_t *files)          /* word list to ues */
{
    const char *filename = WordRandom(files);
    int tries;

    if (!filename) {                /* no files */
        return NULL;
    }
    for (tries = 10; tries > 0; --tries) {
        const char *fp = strrchr(filename, '/');
        fp = (!fp) ? filename : fp+1; /* start of file portion */
        if (('.' == *fp) || (0 == strncmp(fp, "README", 6))) {
            /*D_PRINTF(stderr, "words: skipping '%s' (%c @ %d)\n",
              filename, *fp, fp-filename);*/ /* DEBUG */
            filename = WordRandom(files);
            if (!filename) continue;
        } else {
            return filename;
        }
    }
    if (0 == tries) {
        return NULL;
    }
    return filename;
}
