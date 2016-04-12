/* -*- Mode: C; c-file-style: "stroustrup"; comment-column: 40 -*- */
/* Public definitions for words.h */
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

#ifndef _words_h
#define _words_h

/* A random access collection of words.
   The words do not change after being added to the collection.
   WordsDestroy will remove entries from the collection without affecting
   existing pointers to the words.
   The collection is anticipated to have roughly 500k entries.
*/
typedef struct words {
    unsigned long   indexLen;           /* entries in index */
    unsigned long   indexSize;          /* length of index */
    char **         index;              /* array of string pointers */
    string_list_t * data;  /* list of data blocks that index points into */
} words_t;


extern words_t  wordDict;     /* word dictionary for random generation */
extern char *   syntaxChars;            /* syntax string */
extern char *   dictPath;               /* path to word dictionary */

void WordsInit(words_t *dict);
void WordsDestroy(words_t *dict);
int ReadWordDict(words_t *dict, const char *path);
void WordAdd(words_t *dict, const char *word);
int WordSequence(char *buff,            /* buffer to fill in */
                 size_t size,           /* size of buffer */
                 words_t *dict,         /* dictionary */
                 const char *sep_chars, /* separator characters or NULL*/
                 int max_words); /* maximum words to use (0 -> unlimited)*/

/* Return a pointer to a random word.  Static storage - do not free. */
const char *WordRandom(words_t *dict);  /* dictionary */

int WordFind(words_t *dict, const char *word);
int WordDelete(words_t *dict, const char *word);
int WordPointerDelete(words_t *dict, const char *word);
const char *GetSafeFilename(words_t *files);

#endif /* _words_h */
