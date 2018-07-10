/*
 * ubrowse.c: Display the Unicode character set in a table.
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

#define _XOPEN_SOURCE
#define _XOPEN_SOURCE_EXTENDED
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdarg.h>
#include <limits.h>
#include <errno.h>
#include <wchar.h>
#include <locale.h>
#include <getopt.h>
#include <ncurses.h>
#include "data.h"

/* The value of the highest possible Unicode codepoint.
 */
static unsigned long const lastucharval = 0x10FFFF;

/* Online help for program invocation.
 */
static char const *yowzitch[] = {
    "Usage: ubrowse [OPTIONS] [CHAR | CODEPOINT | STRING]",
    "Display Unicode characters in a scrolling table.",
    "",
    "  -a, --accent=C    Specify codepoint C to use when rendering combining",
    "                    accent characters (default is U+00B7).",
    "  -A, --noaccent    Suppress display of combining accent characters.",
    "      --help        Display this online help.",
    "      --version     Display version information.",
    "",
    "CHAR is a literal character with which to initialize the list position.",
    "CODEPOINT is specified as a hex value, optionally prefixed with \"U+\".",
    "STRING is a substring to search for in the codepoint names.",
    "",
    "Use \"?\" while the program is running to see a list of key commands.",
};

/* Version information.
 */
static char const *vourzhon[] = {
    "ubrowse: Unicode character set browser, version 1.3",
    "Copyright (C) 2013-2017 by Brian Raiter <breadbox@muppetlabs.com>",
    "This is free software; you are free to change and redistribute it.",
    "There is NO WARRANTY, to the extent permitted by law."
};

/* Array marking the blocks in the block list that are devoid of
 * printable characters. Created by emptyblocksinit().
 */
static unsigned char *emptyblocks;

/* The default number of columns in the table.
 */
static int columncount = 2;

/* The smallest width columns are permitted to shrink to.
 */
static int const mincolumnwidth = 8;

/* The size of the terminal (and thus the size of the table).
 */
static int xtermsize, ytermsize, lastrow;

/* Combining characters are displayed by combining them with this
 * character.
 */
static wchar_t accentchar = 0x00B7;

/* If false, combining characters are not displayed.
 */
static int showcombining = TRUE;

/* Find the codepoint with the value uchar in the charlist array and
 * return its index. If uchar doesn't map to a defined codepoint,
 * return the nearest one.
 */
static int lookupchar(int uchar)
{
    int top, bottom, n;

    top = 0;
    bottom = charlistsize - 1;
    while (bottom - top > 1) {
	n = (top + bottom) / 2;
	if (charlist[n].uchar < uchar)
	    top = n;
	else if (charlist[n].uchar > uchar)
	    bottom = n;
	else
	    return n;
    }
    return uchar - charlist[top].uchar < charlist[bottom].uchar - uchar ?
		top : bottom;
}

/* Return the index of the (nearest) codepoint that is charoffset away
 * from the current codepoint, as indicated by pos.
 */
static int offsetchar(int pos, int charoffset)
{
    return lookupchar(charlist[pos].uchar + charoffset);
}

/* Return the index of the next codepoint that contains the given
 * substring in its official name. The return value is negative if the
 * substring appears nowhere in any name. If substring is NULL, the
 * previous search string is used.
 */
static int findcharbyname(char const *substring, int startpos, int direction)
{
    static char lastsubstring[265];
    char buf[256];
    char const *p;
    int size, pos;

    if (substring) {
	if (strlen(substring) >= sizeof buf - 1)
	    return -1;
    } else if (*lastsubstring) {
	substring = lastsubstring;
    } else {
	return -1;
    }

    pos = startpos;
    for (;;) {
	pos += direction;
	if (pos >= charlistsize)
	    pos = 0;
	else if (pos < 0)
	    pos = charlistsize - 1;
	p = charnamebuffer + charlist[pos].nameoffset;
	size = charlist[pos].namesize;
	if (memchr(p, *substring, size)) {
	    memcpy(buf, p, size);
	    buf[size] = '\0';
	    if (strstr(buf, substring)) {
		if (substring != lastsubstring)
		    strcpy(lastsubstring, substring);
		return pos;
	    }
	}
	if (pos == startpos)
	    return -1;
    }
}

/* Go through each block in the block list and mark the ones that
 * don't contain any valid or displayable codepoints.
 */
static void emptyblocksinit(void)
{
    int i, n;

    if (emptyblocks)
	return;
    emptyblocks = malloc(blocklistsize);
    for (i = 0 ; i < blocklistsize ; ++i) {
	n = lookupchar(blocklist[i].from);
	if (charlist[n].uchar >= blocklist[i].from &&
			charlist[n].uchar <= blocklist[i].to)
	    emptyblocks[i] = FALSE;
	else if (n + 1 < charlistsize &&
			charlist[n + 1].uchar >= blocklist[i].from &&
			charlist[n + 1].uchar <= blocklist[i].to)
	    emptyblocks[i] = FALSE;
	else
	    emptyblocks[i] = TRUE;
    }
}

/* Parse a string containing a hex value representing a Unicode
 * codepoint and return the parsed value. -1 is returned if the
 * string's contents are not valid.
 */
static int readuchar(char const *input)
{
    unsigned long value;
    char *p;

    if (input[0] == 'U' && input[1] == '+')
	input += 2;
    value = strtoul(input, &p, 16);
    if (*p || p == input || (value == ULONG_MAX && errno == ERANGE))
	return -1;
    if (value > lastucharval)
	return -1;
    return lookupchar(value);
}

/* If str points to a string containing a single codepoint (according
 * to the current locale), read and return the codepoint as an
 * integer. Otherwise the string contains either zero or at least two
 * codepoints, in which case -1 is returned.
 */
static int readsinglecharstring(char const *str)
{
    int ch;

    if (sscanf(str, "%lc", &ch) == 1 && sscanf(str, "%*lc%*c") == EOF)
	return lookupchar(ch);
    return -1;
}

/*
 * Curses-specific functions
 */

/* This callback ensures that ncurses shuts down cleanly at exit.
 */
static void shutdown(void)
{
    if (!isendwin())
	endwin();
}

/* Display an error message and exit the program.
 */
static void die(char const *fmt, ...)
{
    va_list	args;

    shutdown();
    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    fputc('\n', stderr);
    va_end(args);
    exit(EXIT_FAILURE);
}

/* Get the dimensions of the terminal. The bottommost row is not used
 * by the table.
 */
static void measurescreen(void)
{
    getmaxyx(stdscr, ytermsize, xtermsize);
    lastrow = ytermsize - 1;
}

/* Initialize ncurses.
 */
static int ioinit(void)
{
    atexit(shutdown);
    if (!initscr())
	return FALSE;
    measurescreen();
    nonl();
    noecho();
    keypad(stdscr, TRUE);

    return TRUE;
}

/* Translate a key event from ncurses. Special keys that represent
 * controls are translated to appropriate ASCII equivalents. If the
 * terminal is resized, the function automatically updates the
 * variables defining the display area.
 */
static int translatekey(int key)
{
    if (key == KEY_RESIZE) {
	measurescreen();
	return '\f';
    }
    switch (key) {
      case KEY_RIGHT:		return '>';
      case KEY_LEFT:		return '<';
      case KEY_DOWN:		return '+';
      case KEY_UP:		return '-';
      case ' ':			return 'F';
      case KEY_NPAGE:		return 'F';
      case KEY_PPAGE:		return 'B';
      case KEY_BACKSPACE:	return 'B';
      case '\010':		return 'B';
      case '\177':		return 'B';
      case KEY_ENTER:		return '\n';
      case '\r':		return '\n';
      case 'h':			return '?';
      case 'H':			return '?';
    }
    return tolower(key);
}

/* Allow the user to input a string. The first parameter supplies the
 * input buffer that will receive the string. The second parameter
 * gives the buffer length. prompt provides a string that will appear
 * in front of the input. The last parameter provides a callback that
 * returns a true value to permit a character to be added to the
 * input string.
 */
static int doinputui(char *input, int inputsize, char const *prompt,
		     int (*validchar)(int))
{
    int promptlen;
    int inputlen, inputmax;
    int done = FALSE;
    int ch;

    promptlen = strlen(prompt);
    mvaddstr(lastrow, 0, prompt);
    clrtoeol();
    inputlen = 0;
    inputmax = xtermsize - promptlen;
    if (inputmax > inputsize - 1)
	inputmax = inputsize - 1;
    while (!done) {
	ch = getch();
	if (ch == ERR)
	    return -1;
	if (validchar(ch)) {
	    if (inputlen < inputmax) {
		input[inputlen++] = ch;
		echochar(ch);
	    } else {
		beep();
	    }
	} else {
	    if (ch == killchar())
		ch = '\025';
	    switch (ch) {
	      case KEY_ENTER:
	      case '\r':
	      case '\n':
		done = TRUE;
		break;
	      case KEY_BACKSPACE:
	      case '\010':
	      case '\177':
		if (inputlen) {
		    --inputlen;
		    move(lastrow, promptlen + inputlen);
		    clrtoeol();
		} else {
		    beep();
		}
		break;
	      case '\025':
		inputlen = 0;
		move(lastrow, promptlen);
		clrtoeol();
		break;
	      case '\007':
		move(lastrow, 0);
		clrtoeol();
		return -1;
	      case KEY_RESIZE:
		measurescreen();
		inputmax = xtermsize - promptlen;
		if (inputmax > inputsize - 1)
		    inputmax = inputsize - 1;
		if (inputlen >= inputmax)
		    inputlen = inputmax;
		mvaddstr(lastrow, 0, prompt);
		addnstr(input, inputlen);
		clrtoeol();
		break;
	    }
	}
    }
    input[inputlen] = '\0';
    return inputlen;
}

/* Allow the user to input a string and search for it in the codepoint
 * names. If repeat is nonzero, then no prompt is shown and instead
 * the previous search is repeated.
 */
static int searchui(int index, int repeat)
{
    char searchstring[256];
    int n;

    if (repeat) {
	n = findcharbyname(NULL, index, repeat);
    } else {
	n = doinputui(searchstring, sizeof searchstring, "/", isprint);
	if (n < 0)
	    return index;
	if (n == 0)
	    n = findcharbyname(NULL, index, +1);
	while (n--)
	    searchstring[n] = tolower(searchstring[n]);
	n = findcharbyname(searchstring, index, +1);
    }

    if (n < 0) {
	beep();
	return index;
    }
    return n;
}

/* Get a string from the user containing a hexadecimal number and
 * return the index for that codepoint. The passed-in index is
 * returned if the user doesn't enter a valid codepoint number.
 */
static int jumpui(int index)
{
    char buf[7];
    unsigned long value;
    int n;

    n = doinputui(buf, sizeof buf, "U+", isxdigit);
    if (n < 0)
	return index;
    value = strtoul(buf, NULL, 16);
    if (value > lastucharval) {
	beep();
	return index;
    }
    return lookupchar(value);
}

/* Display the Unicode version the program was built with. Alert if no
 * version string is available.
 */
static void showversion(void)
{
    if (unicodeversion && *unicodeversion) {
	mvaddstr(lastrow, 0, "Unicode version ");
	addstr(unicodeversion);
	(void)getch();
    } else {
	beep();
    }
}

/* Display a brief description of the key commands.
 */
static void showblockhelptext(void)
{
    static char const *helptext[] = {
	"Spc    Move forward one screenful   Bkspc  Move back one screenful",
	"Down   Move forward one row         Up     Move back one row",
	"}      Move to end of list          {      Move to top of list",
	"Enter  View the characters at the selected block",
	"V      Display Unicode version      ?      Display this help text",
	"^L     Redraw the screen            Q      Cancel and return"
    };

    int i;

    for (i = 0 ; i < (int)(sizeof helptext / sizeof *helptext) ; ++i) {
	move(i, 0);
	clrtoeol();
	addstr("   ");
	addstr(helptext[i]);
    }
    move(i, 0);
    clrtoeol();
    mvaddstr(lastrow, 0, "[Press any key to continue]");
    (void)getch();
}

/* Display a full screen's worth of the block table, centered as
 * closely as possible on the selected entry.
 */
static void drawblocklist(int selected)
{
    char frombuf[8], tobuf[8];
    int namesize, top, i;

    top = selected - ytermsize / 2;
    if (top < 0)
	top = 0;
    if (top + lastrow > blocklistsize)
	top = blocklistsize - lastrow;
    namesize = xtermsize - 32;

    erase();
    for (i = top ; i < blocklistsize && i < top + lastrow ; ++i) {
	if (i == selected)
	    attron(A_STANDOUT);
	if (emptyblocks[i])
	    attron(A_DIM);
	sprintf(frombuf, "%04X", blocklist[i].from);
	sprintf(tobuf, "%04X", blocklist[i].to);
	mvprintw(i - top, 4, "%6s ..%6s  %-*s",
		 frombuf, tobuf, namesize, blocklist[i].name);
	attrset(A_NORMAL);
	if (emptyblocks[i])
	    addstr(" [empty]");
    }
    mvaddstr(lastrow, 0, "Character Blocks");
    refresh();
}

/* Render the display of the block table and alter it in response to
 * keystrokes from the user. If the user presses enter the function
 * returns and moves the character display to that block. The user can
 * also quit this function, in which case the existing position in the
 * character table is returned.
 */
static int blockselectui(int index)
{
    int selected, done, i;

    emptyblocksinit();
    for (i = 0 ; i < blocklistsize ; ++i)
	if (blocklist[i].to >= charlist[index].uchar)
	    break;
    selected = i;
    done = FALSE;
    while (!done) {
	if (selected < 0)
	    selected = 0;
	if (selected >= blocklistsize)
	    selected = blocklistsize - 1;
	drawblocklist(selected);
	switch (translatekey(getch())) {
	  case '+':	++selected;				break;
	  case '-':	--selected;				break;
	  case 'F':	selected += ytermsize - 1;		break;
	  case 'B':	selected -= ytermsize - 1;		break;
	  case '{':	selected = 0;				break;
	  case '}':	selected = blocklistsize;		break;
	  case '?':	showblockhelptext();			break;
	  case 'v':	showversion();				break;
	  case '\f':	clearok(stdscr, TRUE);			break;
	  case 'q':	return index;
	  case '\007':	return index;
	  case '\003':	exit(EXIT_SUCCESS);
	  case '\n':
	    if (emptyblocks[selected])
		beep();
	    else
		done = TRUE;
	    break;
	}
    }
    return lookupchar(blocklist[selected].from);
}

/* Display the index-th character at location (y, x) using colwidth
 * cells. The official name is rendered first, with the actual glyph
 * displayed at the rightmost position. (Note that wcwidth(3) is used
 * to determine how many cells the glyph occupies. Some terminals
 * and/or terminal fonts do not 100% adhere to what this function
 * reports. It is used because there is currently no alternative.)
 */
static int drawentry(int y, int x, int colwidth, int index)
{
    static wchar_t const ellipsis[] = { (wchar_t)0x2026, L'\0' };
    char buf[8];
    wchar_t wch[3];
    cchar_t cch;
    char const *name;
    int width, size, n;

    if (colwidth < mincolumnwidth)
	return FALSE;
    n = sprintf(buf, " %04X", charlist[index].uchar);
    mvaddstr(y, x, buf + n - 5);
    width = wcwidth(charlist[index].uchar);
    if (width < 0)
	width = 0;
    if (charlist[index].combining && showcombining && width == 0)
	width = 1;
    if (n + 3 < colwidth) {
	addch(' ');
	name = charnamebuffer + charlist[index].nameoffset;
	size = charlist[index].namesize;
	n = colwidth - 7 - width;
	if (n >= size) {
	    addnstr(name, size);
	} else if (n > 6) {
	    addnstr(name, n / 2);
	    setcchar(&cch, ellipsis, 0, 0, NULL);
	    add_wch(&cch);
	    n -= n / 2 + 1;
	    addnstr(name + size - n, n);
	} else {
	    setcchar(&cch, ellipsis, 0, 0, NULL);
	    add_wch(&cch);
	    if (n > 1)
		addnstr(name + size - n + 1, n - 1);
	}
    }
    if (width == 0)
	return TRUE;
    if (charlist[index].combining && showcombining) {
	wch[0] = accentchar;
	wch[1] = charlist[index].uchar;
	wch[2] = L'\0';
    } else {
	wch[0] = charlist[index].uchar;
	wch[1] = L'\0';
    }
    setcchar(&cch, wch, 0, 0, NULL);
    mvadd_wch(y, x + colwidth - width, &cch);
    return TRUE;
}

/* Display a full screen's worth of the character table, starting with
 * the character given by index. The range of displayed codepoints is
 * shown on the bottommost line of the terminal.
 */
static int drawtable(int index)
{
    int colwidth = xtermsize / columncount;
    int i = index;
    int y, x;

    erase();
    for (x = 0 ; x <= xtermsize - colwidth ; x += colwidth) {
	for (y = 0 ; y < lastrow ; ++y)
	    drawentry(y, x, colwidth - 1, i++);
	refresh();
    }
    mvprintw(lastrow, 0, "[%04X - %04X]",
	     charlist[index].uchar, charlist[i - 1].uchar);
    refresh();
    return i;
}

/* Display a brief description of the key commands.
 */
static void showmainhelptext(void)
{
    static char const *helptext[] = {
	"Spc    Move forward one screenful   Bkspc  Move back one screenful",
	"Right  Move forward one column      Left   Move back one column",
	"Down   Move forward one row         Up     Move back one row",
	"}      Move forward by U+1000       {      Move back by U+1000",
	"[      Add another column           ]      Reduce number of columns",
	"U or S Go to a specific codepoint   J or B Jump to a selected block",
	"/      Search forward for a codepoint name containing a substring",
	"N      Repeat the last search       P      To previous search result",
	"V      Display Unicode version      ?      Display this help text",
	"^L     Redraw the screen            Q      Exit the program"
    };

    int i;

    for (i = 0 ; i < (int)(sizeof helptext / sizeof *helptext) ; ++i) {
	move(i, 0);
	clrtoeol();
	addstr("   ");
	addstr(helptext[i]);
    }
    move(i, 0);
    clrtoeol();
    mvaddstr(lastrow, 0, "[Press any key to continue]");
    (void)getch();
}

/* Render a view of the character table as per the user's keyboard
 * input. Other inputs can temporarily move into other UIs. Return
 * when the user requests to leave the program.
 */
static void mainui(int index)
{
    int tablesize;

    for (;;) {
	if (index < 0)
	    index = 0;
	if (columncount < 1)
	    columncount = 1;
	else if (columncount > (xtermsize - 1) / (mincolumnwidth + 1))
	    columncount = (xtermsize - 1) / (mincolumnwidth + 1);
	tablesize = (ytermsize - 1) * columncount;
	if (index > charlistsize - tablesize)
	    index = charlistsize - tablesize;
	drawtable(index);
	clearok(stdscr, TRUE);
	switch (translatekey(getch())) {
	  case '+':	++index;				break;
	  case '-':	--index;				break;
	  case '>':	index += ytermsize - 1;			break;
	  case '<':	index -= ytermsize - 1;			break;
	  case 'F':	index += tablesize;			break;
	  case 'B':	index -= tablesize;			break;
	  case '}':	index = offsetchar(index, +0x1000);	break;
	  case '{':	index = offsetchar(index, -0x1000);	break;
	  case '/':	index = searchui(index, 0);		break;
	  case 'n':	index = searchui(index, +1);		break;
	  case 'p':	index = searchui(index, -1);		break;
	  case 'u':	index = jumpui(index);			break;
	  case 's':	index = jumpui(index);			break;
	  case 'j':	index = blockselectui(index);		break;
	  case 'b':	index = blockselectui(index);		break;
	  case '[':	++columncount;				break;
	  case ']':	--columncount;				break;
	  case '?':	showmainhelptext();			break;
	  case 'v':	showversion();				break;
	  case '\f':	clearok(stdscr, TRUE);			break;
	  case 'q':	return;
	  case '\003':	exit(EXIT_SUCCESS);
	}
    }
}

/* Parse the command-line arguments. The return value is the initial
 * codepoint specified on the command-line, or 0 if no initial
 * codepoint was present. If the command-line was invalid, the
 * function quits the program.
 */
static int readcmdline(int argc, char *argv[])
{
    static char const *optstring = "a:A";
    static struct option options[] = {
	{ "accent", required_argument, NULL, 'a' },
	{ "noaccent", no_argument, NULL, 'A' },
	{ "help", no_argument, NULL, 'h' },
	{ "version", no_argument, NULL, 'v' },
	{ 0, 0, 0, 0 }
    };
    char const *str;
    int ch, i;

    while ((ch = getopt_long(argc, argv, optstring, options, NULL)) != EOF) {
	switch (ch) {
	  case 'a':
	    if (optarg[1] == '\0') {
		accentchar = optarg[0];
	    } else {
		accentchar = readuchar(optarg);
		if (accentchar < 0)
		    die("invalid accent character value: \"%s\"", optarg);
		accentchar = charlist[accentchar].uchar;
	    }
	    break;
	  case 'A':
	    showcombining = FALSE;
	    break;
	  case 'h':
	    for (i = 0 ; i < (int)(sizeof yowzitch / sizeof *yowzitch) ; ++i)
		puts(yowzitch[i]);
	    exit(EXIT_SUCCESS);
	  case 'v':
	    for (i = 0 ; i < (int)(sizeof vourzhon / sizeof *vourzhon) ; ++i)
		puts(vourzhon[i]);
	    exit(EXIT_SUCCESS);
	  default:
	    die("Try --help for more information.");
	}
    }
    ch = 0;
    if (optind < argc) {
	str = argv[optind];
	ch = readsinglecharstring(str);
	if (ch < 0) {
	    ch = readuchar(str);
	    if (ch < 0) {
		ch = findcharbyname(str, 0, +1);
		if (ch < 0)
		    die("Invalid start value: \"%s\".", argv[optind]);
	    }
	}
	++optind;
    }
    if (optind < argc)
	die("Bad command-line argument.\nTry --help for more information.");
    return ch;
}

/* Run the program.
 */
int main(int argc, char *argv[])
{
    int startpos;

    setlocale(LC_ALL, "");
    startpos = readcmdline(argc, argv);
    ioinit();
    mainui(startpos);
    return 0;
}
