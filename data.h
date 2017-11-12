/*
 * data.h: Declarations of objects holding the Unicode data.
 *
 * Copyright (C) 2013-2017 Brian Raiter <breadbox@muppetlabs.com>
 *
 * Permission is hereby granted, free of charge, to any person
 * obtaining a copy of this software and associated documentation
 * files (the "Software"), to deal in the Software without
 * restriction, including without limitation the rights to use, copy,
 * modify, merge, publish, distribute, sublicense, and/or sell copies
 * of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#ifndef _data_h_
#define _data_h_

/* Data stored for each character.
 */
typedef struct charinfo {
    unsigned int nameoffset:24;	/* offset of official glyph name */
    unsigned int namesize:8;	/* length of the glpyh name */
    unsigned int uchar:21;	/* the codepoint value */
    unsigned int combining:1;	/* true if this is a combining character */
} charinfo;

/* Data stored for each block.
 */
typedef struct blockinfo {
    unsigned int from;		/* first codepoint in the block */
    unsigned int to;		/* last codepoint in the block */
    char const *name;		/* official block name */
} blockinfo;

/* The complete array of Unicode characters.
 */
extern charinfo const charlist[];
extern int const charlistsize;

/* The complete list of blocks of Unicode characters.
 */
extern blockinfo const blocklist[];
extern int const blocklistsize;

/* The heap of codepoint names.
 */
extern char const *charnamebuffer;

/* The Unicode version string.
 */
extern char const *unicodeversion;

#endif
