/* scroller.c:
 * -----------
 *
 * A scrolling buffer utility.  Able to add and subtract to the buffer.
 * All routines that would require a screen update will automatically refresh
 * the scroller.
 */

/* Local Includes */
#if HAVE_CONFIG_H
#include "config.h"
#endif /* HAVE_CONFIG_H */

/* System Includes */
#if HAVE_CTYPE_H
#include <ctype.h>
#endif

#if HAVE_STDLIB_H
#include <stdlib.h>
#endif /* HAVE_STDLIB_H */

#if HAVE_STRING_H
#include <string.h>
#endif /* HAVE_STRING_H */

#define PCRE2_CODE_UNIT_WIDTH 8
#include <pcre2.h>
/* #include <regex.h> */
/* #include <sys/types.h> */

/* Local Includes */
#include "cgdb.h"
#include "scroller.h"
#include "tokenizer.h"
#include "logger.h"

/* --------------- */
/* Local Functions */
/* --------------- */

/* count: Count the occurrences of a character c in a string s.
 * ------
 *
 *   s:  String to search
 *   c:  Character to search for
 *
 * Return Value:  Number of occurrences of c in s.
 */
static int count(const char *s, char c)
{
    int rv = 0;
    char *x = strchr(s, c);

    while (x) {
        rv++;
        x = strchr(x + 1, c);
    }

    return rv;
}

/* parse: Translates special characters in a string.  (i.e. backspace, tab...)
 * ------
 *
 *   buf:  The string to parse
 *
 * Return Value:  A newly allocated copy of buf, with modifications made.
 */
static char *parse(struct scroller *scr, const char *orig, const char *buf)
{
    const int tab_size = 8;
    int length = strlen(orig) + strlen(buf) + (tab_size - 1) * count(buf, '\t');
    char *rv = (char *) malloc(length + 1);
    int i, j;

    /* Zero out the string */
    memset(rv, 0, length + 1);
    strcpy(rv, orig);
    i = scr->current.pos;

    /* Expand special characters */
    for (j = 0; j < strlen(buf); j++) {
        switch (buf[j]) {
                /* Backspace/Delete -> Erase last character */
            case 8:
            case 127:
                if (i > 0)
                    i--;
                break;
                /* Tab -> Translating to spaces */
            case '\t':
                do
                    rv[i++] = ' ';
                while (i % tab_size != 0);
                break;
                /* Carriage return -> Move back to the beginning of the line */
            case '\r':
                i = 0;
                break;
                /* Default case -> Only keep printable characters */
            default:
                if (isprint((int) buf[j])) {
                    rv[i] = buf[j];
                    i++;
                }
                break;
        }
    }

    scr->current.pos = i;
    /* Remove trailing space from the line */
    for (j = strlen(rv) - 1; j > i && isspace((int) rv[j]); j--);
    rv[j + 1] = 0;

    return realloc(rv, strlen(rv) + 1);
}

/* ----------------- */
/* Exposed Functions */
/* ----------------- */

/* See scroller.h for function descriptions. */

struct scroller *scr_new(int pos_r, int pos_c, int height, int width)
{
    struct scroller *rv;

    if ((rv = malloc(sizeof (struct scroller))) == NULL)
        return NULL;

    rv->current.r = 0;
    rv->current.c = 0;
    rv->current.pos = 0;
    rv->win = newwin(height, width, pos_r, pos_c);

    /* Start with a single (blank) line */
    rv->buffer = malloc(sizeof (char *));
    rv->buffer[0] = strdup("");
    rv->length = 1;

    return rv;
}

void scr_free(struct scroller *scr)
{
    int i;

    /* Release the buffer */
    if (scr->length) {
        for (i = 0; i < scr->length; i++)
            free(scr->buffer[i]);
        free(scr->buffer);
    }
    delwin(scr->win);

    /* Release the scroller object */
    free(scr);
}

void scr_up(struct scroller *scr, int nlines)
{
    int height, width;
    int length;
    int i;

    /* Sanity check */
    getmaxyx(scr->win, height, width);
    if (scr->current.c > 0) {
        if (scr->current.c % width != 0)
            scr->current.c = (scr->current.c / width) * width;
    }

    for (i = 0; i < nlines; i++) {
        /* If current column is positive, drop it by 'width' */
        if (scr->current.c > 0)
            scr->current.c -= width;

        /* Else, decrease the current row number, and set column accordingly */
        else {
            if (scr->current.r > 0) {
                scr->current.r--;
                if ((length = strlen(scr->buffer[scr->current.r])) > width)
                    scr->current.c = ((length - 1) / width) * width;
            } else {
                /* At top */
                break;
            }
        }
    }
}

void scr_down(struct scroller *scr, int nlines)
{
    int height, width;
    int length;
    int i;

    /* Sanity check */
    getmaxyx(scr->win, height, width);
    if (scr->current.c > 0) {
        if (scr->current.c % width != 0)
            scr->current.c = (scr->current.c / width) * width;
    }

    for (i = 0; i < nlines; i++) {
        /* If the current line wraps to the next, then advance column number */
        length = strlen(scr->buffer[scr->current.r]);
        if (scr->current.c < length - width)
            scr->current.c += width;

        /* Otherwise, advance row number, and set column number to 0. */
        else {
            if (scr->current.r < scr->length - 1) {
                scr->current.r++;
                scr->current.c = 0;
            } else {
                /* At bottom */
                break;
            }
        }
    }
}

void scr_home(struct scroller *scr)
{
    scr->current.r = 0;
    scr->current.c = 0;
}

void scr_end(struct scroller *scr)
{
    int height, width;

    getmaxyx(scr->win, height, width);

    scr->current.r = scr->length - 1;
    scr->current.c = (strlen(scr->buffer[scr->current.r]) / width) * width;
}

void scr_add(struct scroller *scr, const char *buf)
{
    int distance;               /* Distance to next new line character */
    int length;                 /* Length of the current line */
    char *x;                    /* Pointer to next new line character */

    char colorStop[11] = { '\\', '[', '\\', '0', '3', '3', '[', '0', 'm', '\\', ']' };
    char cyan[14] = { '\\', '[', '\\', '0', '3', '3', '[', '0', ';','3','6','m', '\\', ']' };

    /* Find next newline in the string */
    x = strchr(buf, '\n');
    length = strlen(scr->buffer[scr->length - 1]);
    distance = x ? x - buf : strlen(buf);

    /* Append to the last line in the buffer */
    if (distance > 0) {
        char *temp = scr->buffer[scr->length - 1];
        /* char *buf2 = malloc(14 + distance + 11 + 1); */
        char *buf2 = malloc(distance + 1);

        /* strncpy(buf2, cyan, 14); */
        strncpy(buf2, buf, distance);
        /* strncpy(buf2+14, buf, distance); */
        /* strncpy(buf2+distance, colorStop, 11); */
        buf2[distance] = 0;
        scr->buffer[scr->length - 1] = parse(scr, temp, buf2);
        free(temp);
        free(buf2);
    }

    /* Create additional lines if buf contains newlines */
    while (x != NULL) {
        char *newbuf;

        buf = x + 1;
        x = strchr(buf, '\n');
        distance = x ? x - buf : strlen(buf);

        /* Create a new buffer that stops at the next newline */
        newbuf = malloc(distance + 1);
        memset(newbuf, 0, distance + 1);
        strncpy(newbuf, buf, distance);

        /* Expand the buffer */
        scr->length++;
        scr->buffer = realloc(scr->buffer, sizeof (char *) * scr->length);
        scr->current.pos = 0;

        /* Add the new line */
        scr->buffer[scr->length - 1] = parse(scr, "", newbuf);
        free(newbuf);
    }

    scr_end(scr);
}

void scr_move(struct scroller *scr, int pos_r, int pos_c, int height, int width)
{
    delwin(scr->win);
    scr->win = newwin(height, width, pos_r, pos_c);
    wclear(scr->win);
}

int consume_path(int* lineIdx, char* buffer, int nChars)
{
    /*
       f/f/
       /f/f

       look for a slash, then backtrack until we find a space

       look for escaped spaces (or not, who cares)
       after the path, if it exists, look for ":123"
    */

    return 0;
}

int consume_hex(int* lineIdx, char* buffer, int nChars)
{
    char cur = buffer[*lineIdx];
    if(cur != '0') return 0;
    
    ++(*lineIdx);
    if(*lineIdx < nChars){
        cur = buffer[*lineIdx];
        if(cur == 'x' || cur == 'X'){
            ++(*lineIdx);
            int numSize = consume_num(lineIdx, buffer, nChars);
            if(numSize > 0){
                return numSize+2;
            } 
        } 
    } 

    return 0;
}

int consume_num(int* lineIdx, char* buffer, int nChars)
{
    int numSize = 0;
    char cur;

    while(*lineIdx < nChars){
        cur = buffer[*lineIdx];
        // ascii 0-9
        bool isDec = cur >= 48 && cur <= 57;
        bool isHexLower = cur >= 65 && cur <= 70;
        bool isHexUpper = cur >= 97 && cur <= 102;
        bool isHex = isHexLower || isHexUpper;

        if(isDec || isHex){
            ++numSize;
            ++(*lineIdx);
        } else {
            // TODO the loop this is called in should be ok with going 1 past
            --(*lineIdx);
            break;
        }
    }

    
    return numSize;
}

// TODO: move me! this should be a callback within scr_refresh or something
// TODO: should this be line-by-line or buffer at once?
//       some stuff needs previous line to work.
//       like if we have some command we just ran and want to highlight it
void highlight_gdb(char* buffer, int nChars, struct scroller *scr, int y)
{
    if(nChars == 0){
        return;
    }
    
    /* buffer = "imnotapath f/f.c imalsonotapath f/f.c:500"; */
    /* nChars = strlen(buffer);  */

    write_log("buffer (len=%d) %.*s", nChars, (int)nChars, buffer);

	int errNum,errOffset;
    // const char* pathRegex ="([^ /]*/[^ /]*\.?[^ /]*:?[\d]*)";
    const char* pathRegex ="([^ /]*/[^ /]*[\.]?\w*[:]?[\d]*)";
    // const char* pathRegex ="([^ /]*/[^ /]+\.[^ /]+:?[\d]*)";
    // const char* pathRegex ="([^ /]*/[^ /]*)";
    init_pair(1, COLOR_RED, COLOR_BLACK);

    int rc;
    int result;
    pcre2_code *re = pcre2_compile(
      pathRegex,  
      PCRE2_ZERO_TERMINATED, 
      0,                    
      &errNum,          
      &errOffset,      
      NULL);          

    int startOffset = 0;
    uint32_t options = 0;

    if (re == NULL) {
        return;
    }
    
    pcre2_match_data *match_data = pcre2_match_data_create_from_pattern(re, NULL);

    int offset = 0;
    while(1){
        rc = pcre2_match(
            re,                   /* the compiled pattern */
            buffer,              /* the subject string */
            nChars,       /* the length of the subject */
            offset,         /* starting offset in the subject */
            0,              /* options */
            match_data,           /* block for storing the result */
            NULL);                /* use default match context */
     
        if (rc > 0){
            write_log("got %d matches", rc);
            int i = 0;
            /* for(int i = 0; i < rc; i++){ */
                PCRE2_SIZE* ovector = pcre2_get_ovector_pointer(match_data);
                uint32_t matchStart = ovector[2*i];
                uint32_t matchLen = ovector[2*i+1] - matchStart;
                uint32_t matchEnd = ovector[2*i+1];
                write_log("match start: %d, matchLen: %d\n", matchStart, matchLen);
                mvwchgat(scr->win, y, matchStart, matchLen, NULL, 1, NULL);

            /* }    */
        } else {
            break;
        }	
       
        break;
    }

   
    if(match_data != NULL) pcre2_match_data_free(match_data);
    if(re != NULL) pcre2_code_free(re);


    /* init_pair(1, COLOR_RED, COLOR_BLACK); */
    /* char cur,prev;                                                         */
    /* for(int lineIdx = 0; lineIdx < nChars; lineIdx++) {                    */
    /*     cur = buffer[lineIdx];                                             */
        
        // backtrace
        // the start of each stack frame line is #n EG: #1
    /*     if(cur == '#'){                                                    */
    /*         int startIdx = lineIdx;                                        */
    /*         ++lineIdx;                                                     */
    /*         int numSize = consume_num(&lineIdx, buffer, nChars);           */
    /*         if(numSize > 0){                                               */
                // the +1 includes the "#"
    /*             mvwchgat(scr->win, y, startIdx, numSize+1, NULL, 1, NULL); */
    /*         }                                                              */
    /*     }                                                                  */

        // hex
    /*     if(cur == '0'){                                                    */
    /*         int startIdx = lineIdx;                                        */
    /*         int numSize = consume_hex(&lineIdx, buffer, nChars);           */
    /*         if(numSize > 0){                                               */
    /*             mvwchgat(scr->win, y, startIdx, numSize, NULL, 1, NULL);   */
    /*         }                                                              */
    /*     }                                                                  */

        /* if(cur == '*' || cur == '/' || cur == '+' || cur == '-'){ */
        /*     mvwchgat(scr->win, y, lineIdx, 1, NULL, 1, NULL);     */
        /* }                                                         */
        
    /* }                                                                      */
    
}

void scr_refresh(struct scroller *scr, int focus)
{
    int length;                 /* Length of current line */
    int nlines;                 /* Number of lines written so far */
    int r;                      /* Current row in scroller */
    int c;                      /* Current column in row */
    int width, height;          /* Width and height of window */
    char *buffer;               /* Current line segment to print */

    /* Sanity check */
    getmaxyx(scr->win, height, width);

    if (scr->current.c > 0) {
        if (scr->current.c % width != 0)
            scr->current.c = (scr->current.c / width) * width;
    }
    r = scr->current.r;
    c = scr->current.c;
    buffer = malloc(width + 1);
    buffer[width] = 0;

    init_pair(1, COLOR_RED, COLOR_BLACK);
    /* wattron(scr->win, COLOR_PAIR(1));     */

    /* Start drawing at the bottom of the viewable space, and work our way up */
    for (nlines = 1; nlines <= height; nlines++) {

        /*
        eat char:
            if some kind of special char, color it
            else try to read the whole word
        */

        /* Print the current line [segment] */
        memset(buffer, ' ', width);
        if (r >= 0) {
            length = strlen(scr->buffer[r] + c);
            memcpy(buffer, scr->buffer[r] + c, length < width ? length : width);
        }

        mvwprintw(scr->win, height - nlines, 0, "%s", buffer);

        /* highlight_gdb(buffer, length < width ? length : width, scr, height - nlines); */
        highlight_gdb(buffer, width, scr, height - nlines);
        

        /* Update our position */
        if (c >= width)
            c -= width;
        else {
            r--;
            if (r >= 0) {
                length = strlen(scr->buffer[r]);
                if (length > width)
                    c = ((length - 1) / width) * width;
            }
        }
    }

    length = strlen(scr->buffer[scr->current.r] + scr->current.c);
    if (focus && scr->current.r == scr->length - 1 && length <= width) {
        /* We're on the last line, draw the cursor */
        curs_set(1);
        wmove(scr->win, height - 1, scr->current.pos % width);
    } else {
        /* Hide the cursor */
        curs_set(0);
    }

    free(buffer);
    wrefresh(scr->win);
    /* wattroff(scr->win, COLOR_PAIR(1)); */
}
