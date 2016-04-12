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

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <dirent.h>
#include <errno.h>
#include "bench.h"
#include "xalloc.h"

/* unlink file given directory and name */
int
unlink_dir_file(const char *dirpath, const char *file)
{
    int dlen, nlen;
    char *path;
    int ret;

    if (!dirpath || !*dirpath) {
        return -1;
    }
    if (!file) {
        file = "";
    }
    dlen = strlen(dirpath);
    nlen = strlen(file);
    path = alloca(dlen + nlen + 2);
    strcpy (path, dirpath);
    if (*file) {
        char *cp = path + dlen;
        *cp++ = '/';
        strcpy (cp, file);
    }
    ret = unlink(path);
    if (ret < 0) {                      /* attempt to make writable */
        chmod (path, 0666);
        ret = unlink(path);
    }
    if (ret < 0) {
        D_PRINTF(stderr, "Error Unlinking '%s'\n", path);
    }
    return ret;
}

/* unlink file given directory and name */
int
move_dir_file(const char *from_dir, const char *to_dir, const char *file)
{
    int dlen, nlen, tolen;
    char *path, *newpath;
    int ret;

    if (!from_dir || !*from_dir) {
        return -1;
    }
    if (!to_dir || !*to_dir) {
        return -1;
    }
    if (!file) {
        file = "";
    }
    dlen = strlen(from_dir);
    tolen = strlen(to_dir);
    nlen = strlen(file);
    path = alloca(dlen + nlen + 2);
    strcpy (path, from_dir);
    if (*file) {
        char *cp = path + dlen;
        *cp++ = '/';
        strcpy (cp, file);
    }
    newpath = alloca(tolen + nlen + 2);
    strcpy (newpath, to_dir);
    if (*file) {
        char *cp = newpath + tolen;
        *cp++ = '/';
        strcpy (cp, file);
    }
    ret = rename(path, newpath);
    if (ret < 0) {
        D_PRINTF(stderr, "Error moving '%s' to '%s'\n", path, newpath);
    }
    return ret;
}

/* Test if a filename (without path) is probably a (Unix) core file) */
int
is_core_file(const char *name)
{
    if (0 != strncmp("core", name, 4)) {
        return 0;
    }
    if (0 == name[4]) {                 /* core */
        return 1;
    }
    if ('.' == name[4]) {               /* core.N+ */
        const char *cp = name+5;
        while ((*cp >= '0') && (*cp <= 9)) ++cp;
        if ((0 == cp) && (cp > name+5)) {
            return 2;
        }
    }
    return 0;
}

/* Recursively remove all file/directories under dir/subdir.
   If save_dir is set, move any core files there.
*/
int
nuke_dir_subdir(const char *dirpath, const char *subdir, const char *savedir)
{
    int dlen, nlen;
    char *path;
    DIR  *dir;
    struct dirent *de;
    int ret;

    if (!dirpath || !*dirpath) {
        return -1;
    }
    if (!subdir) {
        subdir = "";
    }
    dlen = strlen(dirpath);
    nlen = strlen(subdir);
    path = alloca(dlen + nlen + 2);
    strcpy (path, dirpath);
    if (*subdir) {
        char *cp = path + dlen;
        *cp++ = '/';
        strcpy (cp, subdir);
    }

    dir = opendir(path);
    if (!dir) {
        D_PRINTF(stderr, "Error removing contents of '%s': %s\n",
                 path, strerror(errno));
        rmdir(path);                /* attempt to rmdir anyway */
        return -1;
    }
    while ((de = readdir(dir))) {
        // Note that errors are intentionally ignored.
        if (de->d_type == DT_DIR) {
            if (0 == strcmp(".", de->d_name))
                continue;
            if (0 == strcmp("..", de->d_name))
                continue;
            nuke_dir_subdir (path, de->d_name, savedir);
        } else {
            if (savedir && is_core_file(de->d_name)) {
                if (move_dir_file (path, savedir, de->d_name) >= 0)
                    continue;
            }
            unlink_dir_file (path, de->d_name);
        }
    }
    closedir(dir);
    ret = rmdir(path);             /* fails if directory not empty */
    if (ret < 0) {
        D_PRINTF(stderr, "Error removing dir '%s'\n", path);
    }
    return ret;
}

/* Recursively remove all file/directories under dir/subdir.
   If save_dir is set, move any core files there.
*/
int
nuke_dir(const char *dir, const char *save_dir)
{
    int ret;
    ret = nuke_dir_subdir(dir, NULL, save_dir);
    if (ret == 0) {
        D_PRINTF(stderr, "Removed dir '%s'\n", dir);
    }
    return ret;
}

/* Copy FROM file to new (must not exist) TO file.
   Returns -1 on error, or bytes copied.
*/
int
copy_file(const char *from_path, const char *to_path)
{
    int from_file, to_file;
    char buff[32*1024];
    int cnt, ret, total=0;

    from_file = open(from_path, O_RDONLY, 0);
    if (from_file < 0) {
        return returnerr(stderr, "Error opening '%s' for reading: %s\n",
                         from_path, strerror(errno));
    }

    to_file = open(to_path, O_WRONLY | O_CREAT | O_EXCL, 0600);
    if (to_file < 0) {
        close(from_file);
        return returnerr(stderr, "Error opening '%s' for writing: %s\n",
                         to_path, strerror(errno));
    }

    while ((cnt = read(from_file, buff, sizeof(buff))) > 0) {
        ret = write(to_file, buff, cnt);
        total += ret;
        if (ret != cnt) {
            returnerr(stderr, "Error in copy of '%s' to '%s'\n",
                      from_path, to_path, strerror(errno));
            total = -1;
            break;
        }
    }

    close(from_file);
    close(to_file);
    //TODO: copy permissions
    return total;
}
