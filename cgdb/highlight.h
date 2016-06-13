#ifndef _HIGHLIGHT_H_
#define _HIGHLIGHT_H_

/* highlight.h:
 * ------------
 * 
 * Syntax highlighting routines.
 *
 */

#if HAVE_CONFIG_H
#include "config.h"
#endif /* HAVE_CONFIG_H */

#if HAVE_CURSES_H
#include <curses.h>
#elif HAVE_NCURSES_CURSES_H
#include <ncurses/curses.h>
#endif /* HAVE_CURSES_H */

#define PCRE2_CODE_UNIT_WIDTH 8
#include <pcre2.h>

/* Local Includes */
#include "sources.h"

struct gdb_highlighter {
    pcre2_code *re;
    int name_count;
    char* merged_regex;
    pcre2_match_data* match_data; 
    PCRE2_SPTR name_table;
    int name_entry_size;
};

struct gdb_highlighter* gdb_highlighter_init();

inline void gdb_highlighter_free(struct gdb_highlighter* hl)
{
    if(!hl) return;
    if(hl->match_data) pcre2_match_data_free(hl->match_data);
    if(hl->merged_regex) free(hl->merged_regex);
    if(hl->re) pcre2_code_free(hl->re);
    free(hl);
}

/* --------- */
/* Functions */
/* --------- */
void highlight_gdb(struct gdb_highlighter* hl, WINDOW * win, char* buffer, int nChars, int y, char** output);

/* highlight:  Inserts the highlighting tags into the buffer.  Lines in
 * ----------  this file should be displayed with hl_wprintw from now on...
 *
 *   node:  The node containing the file buffer to highlight.
 */
void highlight(struct list_node *node);

/* hl_wprintw:  Prints a given line using the embedded highlighting commands
 * -----------  to dictate how to color the given line.
 *
 *   win:     The ncurses window to which the line will be written
 *   line:    The line to print
 *   width:   The maximum width of a line
 *   offset:  Character (in line) to start at (0..length-1)
 */
void hl_wprintw(WINDOW * win, const char *line, int width, int offset);
void hl_wprintw2(WINDOW * win, const char *line, int height);

/* hl_regex: Matches a regular expression to some lines.
 * ---------
 *
 *  regex:          The regular expression to match.
 *  tlines:         The lines of text to search.
 *  length:         The number of lines.
 *  cur_line:       This line is returned with highlighting embedded into it.
 *  sel_line:       The current line the user is on.
 *  sel_rline:      The current line the regular expression is on.
 *  sel_col_rbeg:   The beggining index of the last match.
 *  sel_col_rend:   The ending index of the last match.
 *  opt:            1 -> incremental match, 2 -> perminant match
 *  direction:      1 if forward, 0 if reverse
 *  icase:          1 if case insensitive, 0 otherwise
 */
int hl_regex(const char *regex, const char **highlighted_lines, const char **tlines, const int length, char **cur_line, /* Returns the correct highlighted line */
        int *sel_line,          /* Returns new cur line if regex matches */
        int *sel_rline,         /* Used for internal purposes */
        int *sel_col_rbeg,
        int *sel_col_rend, int opt, int direction, int icase);

#endif /* _HIGHLIGHT_H_ */
