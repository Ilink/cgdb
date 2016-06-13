/* highlight.c:
 * ------------
 * 
 * Syntax highlighting routines.
 *
 */

#if HAVE_CONFIG_H
#include "config.h"
#endif /* HAVE_CONFIG_H */

/* System Includes */
#if HAVE_CTYPE_H
#include <ctype.h>
#endif /* HAVE_CTYPE_H */

#if HAVE_LIMITS_H
#include <limits.h>             /* CHAR_MAX */
#endif /* HAVE_LIMITS_H */

#if HAVE_STRING_H
#include <string.h>
#endif /* HAVE_STRING_H */

#if HAVE_STDLIB_H
#include <stdlib.h>
#endif /* HAVE_STDLIB_H */

#ifdef HAVE_SYS_TYPES_H
#include <sys/types.h>
#endif /* HAVE_SYS_TYPES_H */

#if HAVE_REGEX_H
#include <regex.h>
#endif /* HAVE_REGEX_H */

/* Local Includes */
#include "highlight.h"
#include "highlight_groups.h"
#include "sources.h"
#include "cgdb.h"
#include "tokenizer.h"
#include "interface.h"
#include "sys_util.h"
#include "logger.h"

/* ----------- */
/* Definitions */
/* ----------- */

#define HL_CHAR CHAR_MAX        /* Special marker character */

#define n_gdb_regexes 3
static char *gdb_regexes[] = {
    "?<filepath>([^ /]*/[^ /]*[\.]?\w*[:]?[\\d]*)", // paths
    "?<bt_num>#\\d+", // stacktrace numbers (appear at start of bt results)
    "?<hex>0[Xx][A-Fa-f\\d]+" // hex
};


/* --------------- */
/* Local Variables */
/* --------------- */
// unless the return is null, it must be freed
static char* merge_regexes(const char** regexes, const int len)
{
    char* out = NULL;
    int outLen = 0;
    for(int i = 0; i < len; i++){
        const char* reg = regexes[i];
        // +2 includes both parens that will be added
        outLen += strlen(reg) + 2;
        if(i < len-1){
            // include the trailing '|'
            ++outLen;
        }
    }

    // null terminator
    ++outLen;
    /* write_log("merged_regex size: %d", outLen); */
    out = malloc(sizeof(char) * outLen);

    int out_idx = 0;                          

    for(int i = 0; i < len; i++){             
        const char* reg = regexes[i];         
        out[out_idx] = '(';                   
        ++out_idx;                            

        memcpy(out+out_idx, reg, strlen(reg));
        out_idx += strlen(reg);               

        out[out_idx] = ')';                   
        ++out_idx;                            

        if(i < len-1){                        
            out[out_idx] = '|';               
            ++out_idx;                        
        }                                     
    }                                         
    out[outLen-1] = '\0';                     

    return out;
}

struct gdb_highlighter* gdb_highlighter_init()
{
    struct gdb_highlighter* hl = (struct gdb_highlighter*) cgdb_malloc(sizeof (struct gdb_highlighter));
    char* merged_regex = merge_regexes(gdb_regexes, n_gdb_regexes);
    hl->merged_regex = merged_regex;
    
    int errNum,errOffset;
    hl->re = pcre2_compile(
      merged_regex,
      PCRE2_ZERO_TERMINATED, 
      0,                    
      &errNum,          
      &errOffset,      
      NULL);          
   
    if(merged_regex){
        // free(merged_regex);
    }

    if (hl->re == NULL) {
        return NULL;
    }

    hl->match_data = pcre2_match_data_create_from_pattern(hl->re, NULL);
    
    hl->name_count = 0; 
    pcre2_pattern_info(
      hl->re,
      PCRE2_INFO_NAMECOUNT, 
      &(hl->name_count)); 
    
    (void)pcre2_pattern_info(
        hl->re,
        PCRE2_INFO_NAMETABLE,
        &(hl->name_table));

    (void)pcre2_pattern_info(
        hl->re,
        PCRE2_INFO_NAMEENTRYSIZE,
        &(hl->name_entry_size));
    
    return hl;
}


// TODO: move me! this should be a callback within scr_refresh or something
// TODO: should this be line-by-line or buffer at once?
//       some stuff needs previous line to work.
//       like if we have some command we just ran and want to highlight it
void highlight_gdb(struct gdb_highlighter* hl, WINDOW * win, char* buffer, int nChars, int y, char** output)
{
    *output = NULL;
    if(nChars == 0){
        return;
    }
   
    char* merged_regex = merge_regexes(gdb_regexes, n_gdb_regexes);
    // write_log("merged regex: %s", merged_regex);
    struct ibuf *ibuf = ibuf_init();


    int errNum,errOffset;
    const char* pathRegex = merged_regex;

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
        goto cleanup; 
    }
  
	int name_count = 0; 
	pcre2_pattern_info(
	  re,                   /* the compiled pattern */
	  PCRE2_INFO_NAMECOUNT, /* get the number of named substrings */
	  &name_count);          /* where to put the answer */


    pcre2_match_data *match_data = pcre2_match_data_create_from_pattern(re, NULL);

	PCRE2_SPTR tabptr;
    PCRE2_SPTR name_table;
    int name_entry_size;

	/* Before we can access the substrings, we must extract the table for
	translating names to numbers, and the size of each entry in the table. */

	(void)pcre2_pattern_info(
    	re,                       /* the compiled pattern */
	    PCRE2_INFO_NAMETABLE,     /* address of the table */
    	&name_table);             /* where to put the answer */

	(void)pcre2_pattern_info(
    	re,                       /* the compiled pattern */
    	PCRE2_INFO_NAMEENTRYSIZE, /* size of each entry in the table */
    	&name_entry_size);        /* where to put the answer */

    /* Now we can scan the table and, for each entry, print the number, the name,
    and the substring itself. In the 8-bit library the number is held in two
    bytes, most significant first. */

    // tabptr = name_table;                                                 
    // for (int i = 0; i < name_count; i++)                                 
    // {                                                                    
    //     int n = (tabptr[0] << 8) | tabptr[1];                            
    //     printf("(%d) %*s: %.*s\n", n, name_entry_size - 3, tabptr + 2,   
    //       (int)(ovector[2*n+1] - ovector[2*n]), subject + ovector[2*n]); 
    //     tabptr += name_entry_size;                                       
    // }                                                                    
  
    int offset = 0;
    // find every match
    while(1){
        rc = pcre2_match(
            hl->re,                   /* the compiled pattern */
            buffer,              /* the subject string */
            nChars,       /* the length of the subject */
            offset,         /* starting offset in the subject */
            0,              /* options */
            hl->match_data,           /* block for storing the result */
            NULL);                /* use default match context */
    
        if (rc > 0){
            
            // write_log("got %d matches", rc);
            int i = 0;
            PCRE2_SIZE* ovector = pcre2_get_ovector_pointer(hl->match_data);
            uint32_t matchStart = ovector[2*i];
            uint32_t matchLen = ovector[2*i+1] - matchStart;
            uint32_t matchEnd = ovector[2*i+1];

            tabptr = hl->name_table;
            char hl_group;
            for (int i = 0; i < hl->name_count; i++)                                 
            {                                                                    
                int n = (tabptr[0] << 8) | tabptr[1];                            
                const char* name = tabptr+2;
                const int name_len = hl->name_entry_size - 3;
                const char* val = buffer + ovector[2*n];
                const int val_len = (int)(ovector[2*n+1] - ovector[2*n]);

                if(val_len > 0){
                    if(i == 0){ 
                        hl_group = HLG_PATH;
                    } else if(i==1){
                        hl_group = HLG_BT_LIST;
                    } else {
                        hl_group = HLG_HEX;
                    }
                }

                // write_log("%s(%d):%s(%d)", name, strlen(name), val, strlen(val));
                // write_log("(%d) %*s: %.*s", n, name_entry_size - 3, tabptr + 2,   
                //   (int)(ovector[2*n+1] - ovector[2*n]), buffer + ovector[2*n]); 
                tabptr += name_entry_size;                                       
            }
            
            // text before the match
            ibuf_addchar(ibuf, HL_CHAR); 
            ibuf_addchar(ibuf, HLG_TEXT);
            ibuf_adds(ibuf, buffer+offset, matchStart-offset);                
                
            ibuf_addchar(ibuf, HL_CHAR);
            ibuf_addchar(ibuf, hl_group);
            ibuf_adds(ibuf, buffer+matchStart, matchLen);
            
            offset = matchEnd;
            
        } else {
            break;
        }	
        
    }

    // text after the last match until the end of the buffer
    ibuf_addchar(ibuf, HL_CHAR);
    ibuf_addchar(ibuf, HLG_TEXT);
    ibuf_adds(ibuf, buffer+offset, nChars - offset);                
    
    char* out_buf = strdup(ibuf_get(ibuf));  
    *output = out_buf; 

cleanup:
    if(merged_regex != NULL) free(merged_regex);
    if(match_data != NULL) pcre2_match_data_free(match_data);
    if(re != NULL) pcre2_code_free(re);
    ibuf_free(ibuf);
}

static int highlight_node(struct list_node *node)
{
    int ret_code = 0;
    struct tokenizer *t = tokenizer_init();
    int ret;
    struct ibuf *ibuf = ibuf_init();

    ibuf_addchar(ibuf, HL_CHAR);
    ibuf_addchar(ibuf, HLG_TEXT);

    /* Initialize */
    node->buf.length = 0;
    node->buf.tlines = NULL;
    node->buf.max_width = 0;

    if (tokenizer_set_file(t, node->path, node->language) == -1) {
        if_print_message("%s:%d tokenizer_set_file error", __FILE__, __LINE__);
        ret_code = -1;
        goto cleanup;
    }

    while ((ret = tokenizer_get_token(t)) > 0) {
        enum tokenizer_type e = tokenizer_get_packet_type(t);

        /*if_print_message  ( "TOKEN(%d:%s)\n", e, tokenizer_get_printable_enum ( e ) ); */

        switch (e) {
            case TOKENIZER_KEYWORD:
                ibuf_addchar(ibuf, HL_CHAR);
                ibuf_addchar(ibuf, HLG_KEYWORD);
                ibuf_add(ibuf, tokenizer_get_data(t));
                ibuf_addchar(ibuf, HL_CHAR);
                ibuf_addchar(ibuf, HLG_TEXT);
                break;
            case TOKENIZER_TYPE:
                ibuf_addchar(ibuf, HL_CHAR);
                ibuf_addchar(ibuf, HLG_TYPE);
                ibuf_add(ibuf, tokenizer_get_data(t));
                ibuf_addchar(ibuf, HL_CHAR);
                ibuf_addchar(ibuf, HLG_TEXT);
                break;
            case TOKENIZER_LITERAL:
                ibuf_addchar(ibuf, HL_CHAR);
                ibuf_addchar(ibuf, HLG_LITERAL);
                ibuf_add(ibuf, tokenizer_get_data(t));
                ibuf_addchar(ibuf, HL_CHAR);
                ibuf_addchar(ibuf, HLG_TEXT);
                break;
            case TOKENIZER_NUMBER:
                ibuf_add(ibuf, tokenizer_get_data(t));
                break;
            case TOKENIZER_COMMENT:
                ibuf_addchar(ibuf, HL_CHAR);
                ibuf_addchar(ibuf, HLG_COMMENT);
                ibuf_add(ibuf, tokenizer_get_data(t));
                ibuf_addchar(ibuf, HL_CHAR);
                ibuf_addchar(ibuf, HLG_TEXT);
                break;
            case TOKENIZER_DIRECTIVE:
                ibuf_addchar(ibuf, HL_CHAR);
                ibuf_addchar(ibuf, HLG_DIRECTIVE);
                ibuf_add(ibuf, tokenizer_get_data(t));
                ibuf_addchar(ibuf, HL_CHAR);
                ibuf_addchar(ibuf, HLG_TEXT);
                break;
            case TOKENIZER_TEXT:
                ibuf_add(ibuf, tokenizer_get_data(t));
                break;
            case TOKENIZER_NEWLINE:
                node->buf.length++;
                node->buf.tlines =
                        realloc(node->buf.tlines,
                        sizeof (char *) * node->buf.length);
                node->buf.tlines[node->buf.length - 1] = strdup(ibuf_get(ibuf));

                if (ibuf_length(ibuf) > node->buf.max_width)
                    node->buf.max_width = ibuf_length(ibuf);

                ibuf_clear(ibuf);
                ibuf_addchar(ibuf, HL_CHAR);
                ibuf_addchar(ibuf, HLG_TEXT);
                break;
            case TOKENIZER_ERROR:
                ibuf_add(ibuf, tokenizer_get_data(t));
                break;
            default:
                ret_code = -1;
                goto cleanup;
        }
    }

cleanup:
    ibuf_free(ibuf);
    return ret_code;
}

/* --------- */
/* Functions */
/* --------- */

/* See comments in highlight.h for function descriptions. */

void highlight(struct list_node *node)
{
    if (node->language == TOKENIZER_LANGUAGE_UNKNOWN) {
        /* Just copy the lines from the original buffer if no highlighting 
         * is possible */
        int i;

        node->buf.length = node->orig_buf.length;
        node->buf.max_width = node->orig_buf.max_width;
        node->buf.tlines = cgdb_malloc(sizeof (char *) * node->orig_buf.length);
        for (i = 0; i < node->orig_buf.length; i++)
            node->buf.tlines[i] = cgdb_strdup(node->orig_buf.tlines[i]);
    } else
        highlight_node(node);
}

/* highlight_line_segment: Creates a new line that is hightlighted.
 * ------------------------
 *
 *  orig:   The line that needs to be highlighted.
 *  start:  The desired starting position of the highlighted portion.
 *          The start index *is* included in the highlighted segment.
 *  end:    The desired ending position of the highlighted portion.
 *          The end index *is not* include in the highlighted segment.
 *
 *  Return Value: Null on error. Or a pointer to a new line that
 *  has highlighting. The new line MUST BE FREED.
 */
/*#define HIGHLIGHT_DEBUG*/
static char *highlight_line_segment(const char *orig, int start, int end)
{
    char *new_line = NULL;
    int length = strlen(orig), j = 0, pos = 0;
    int syn_search_recieved = 0, cur_color = HLG_TEXT;

    /* Cases not possible */
    if (start > end || orig == NULL ||
            start > length || start < 0 || end > length || end < 0)
        return NULL;

    /* The 5 is: color start (2), color end (2) and EOL */
    if ((new_line = (char *) malloc(sizeof (char) * (length + 5))) == NULL)
        return NULL;

#ifdef HIGHLIGHT_DEBUG
    /*
       for ( j = 0; j < strlen(orig); j++ ) {
       char temp[100];
       sprintf(temp, "(%d:%c)", orig[j], orig[j]);
       scr_add(gdb_win, temp);
       }
       scr_add(gdb_win, "\r\n");
       scr_refresh(gdb_win, 1);
     */
#endif

    /* This traverses the input line. It creates a new line with the section
     *     given highlighted.
     * If a highlight symbol is encountered, the end and/or start position 
     *     is incremented because the original match was against only the text,
     *     not against the color embedded text :)
     */
    for (j = 0; j < length; j++) {
        if (orig[j] == HL_CHAR) {
            if (j <= start)
                start += 2;

            if (j <= end)
                end += 2;

            cur_color = orig[j + 1];
        }

        /* Mark when the search is started and when it ends */
        if (j == start) {
            syn_search_recieved = 1;
            new_line[pos++] = HL_CHAR;
            new_line[pos++] = HLG_SEARCH;
        } else if (j == end) {
            syn_search_recieved = 0;
            new_line[pos++] = HL_CHAR;
            new_line[pos++] = cur_color;
        }

        new_line[pos++] = orig[j];

        /* If the search has started, then make all the colors the 
         * highlighted colors  */
        if (syn_search_recieved && orig[j] == HL_CHAR) {
            ++j;
            new_line[pos++] = HLG_SEARCH;
        }
    }

    new_line[pos] = '\0';

#ifdef HIGHLIGHT_DEBUG
    for (j = 0; j < strlen(new_line); j++) {
        char temp[100];

        fprintf(stderr, "(%d:%c)\r\n", new_line[j], new_line[j]);
    }
#endif

    /* Now the line has new information in it.
     * Lets traverse it again and correct the highlighting to be the 
     * reverse.
     */

    return new_line;
}

void hl_wprintw2(WINDOW * win, const char *line, int height)
{
    int length;                 /* Length of the line passed in */
    enum hl_group_kind color;   /* Color used to print current char */
    int i = 0;                      /* Loops through the line char by char */
    int j;                      /* General iterator */
    int p = 0;                      /* Count of chars printed to screen */
    int pad;                    /* Used to pad partial tabs */
    int attr;                   /* A temp variable used for attributes */
    int highlight_tabstop = cgdbrc_get(CGDBRC_TABSTOP)->variant.int_val;

    /* Jump ahead to the character at offset (process color commands too) */
    length = strlen(line);
    color = HLG_TEXT;

    /* for (i = 0, j = 0; i < length; i++) {                             */
    /*     if (line[i] == HL_CHAR && i + 1 < length) {                   */
            /* Even though we're not printing anything in this loop,  */
            /* the color attribute needs to be maintained for when we */
            /* start printing in the loop below.  This way the text   */
            /* printed will be done in the correct color.             */
    /*         color = (int) line[++i];                                  */
    /*     } else {                                                      */
    /*         j++;                                                      */
    /*     }                                                             */
    /* }                                                                 */
    /* pad = j - offset;                                                 */

    /* Pad tab spaces if offset is less than the size of a tab */
    /* for (j = 0, p = 0; j < pad && p < width; j++, p++)                */
    /*     wprintw(win, " ");                                            */

    /* Set the color appropriately */
    if (hl_groups_get_attr(hl_groups_instance, color, &attr) == -1) {
        logger_write_pos(logger, __FILE__, __LINE__,
                "hl_groups_get_attr error");
        return;
    }
    /* mvwprintw(win, height, 0, "%s", line); */

    wattron(win, attr);
    /* for(; i < length; i++){                       */
    /*     mvwprintw(win, height, i, "%c", line[i]); */
        
    /* }                                             */

    /* Print string 1 char at a time */
    for (; i < length; i++) {
        if (line[i] == HL_CHAR) {
            if (++i < length) {
                wattroff(win, attr);
                color = (int) line[i];

                if (hl_groups_get_attr(hl_groups_instance, color, &attr) == -1) {
                    logger_write_pos(logger, __FILE__, __LINE__,
                            "hl_groups_get_attr error");
                    return;
                }

                wattron(win, attr);
            }
        } else {
            mvwprintw(win, height, p, "%c", line[i]);
            ++p;
            /* wprintw(win, "%c", line[i]); */
            /* switch (line[i]) {                                                   */
            /*     case '\t':                                                       */
            /*         do {                                                         */
            /*             wprintw(win, " ");                                       */
            /*             p++;                                                     */
            /*         } while ((p + offset) % highlight_tabstop > 0 && p < width); */
            /*         break;                                                       */
            /*     default:                                                         */
            /*         wprintw(win, "%c", line[i]);                                 */
            /*         p++;                                                         */
            /* }                                                                    */
        }
    }

    /* Shut off color attribute */
    wattroff(win, attr);

}




void hl_wprintw(WINDOW * win, const char *line, int width, int offset)
{
    int length;                 /* Length of the line passed in */
    enum hl_group_kind color;   /* Color used to print current char */
    int i;                      /* Loops through the line char by char */
    int j;                      /* General iterator */
    int p;                      /* Count of chars printed to screen */
    int pad;                    /* Used to pad partial tabs */
    int attr;                   /* A temp variable used for attributes */
    int highlight_tabstop = cgdbrc_get(CGDBRC_TABSTOP)->variant.int_val;

    /* Jump ahead to the character at offset (process color commands too) */
    length = strlen(line);
    color = HLG_TEXT;

    for (i = 0, j = 0; i < length && j < offset; i++) {
        if (line[i] == HL_CHAR && i + 1 < length) {
            /* Even though we're not printing anything in this loop,
             * the color attribute needs to be maintained for when we
             * start printing in the loop below.  This way the text
             * printed will be done in the correct color. */
            color = (int) line[++i];
        } else if (line[i] == '\t') {
            /* Tab character, expand to size set by user */
            j += highlight_tabstop - (j % highlight_tabstop);
        } else {
            /* Normal character, just increment counter by one */
            j++;
        }
    }
    pad = j - offset;

    /* Pad tab spaces if offset is less than the size of a tab */
    for (j = 0, p = 0; j < pad && p < width; j++, p++)
        wprintw(win, " ");

    /* Set the color appropriately */
    if (hl_groups_get_attr(hl_groups_instance, color, &attr) == -1) {
        logger_write_pos(logger, __FILE__, __LINE__,
                "hl_groups_get_attr error");
        return;
    }

    wattron(win, attr);

    /* Print string 1 char at a time */
    for (; i < length && p < width; i++) {
        if (line[i] == HL_CHAR) {
            if (++i < length) {
                wattroff(win, attr);
                color = (int) line[i];

                if (hl_groups_get_attr(hl_groups_instance, color, &attr) == -1) {
                    logger_write_pos(logger, __FILE__, __LINE__,
                            "hl_groups_get_attr error");
                    return;
                }

                wattron(win, attr);
            }
        } else {
            switch (line[i]) {
                case '\t':
                    do {
                        wprintw(win, " ");
                        p++;
                    } while ((p + offset) % highlight_tabstop > 0 && p < width);
                    break;
                default:
                    wprintw(win, "%c", line[i]);
                    p++;
            }
        }
    }

    /* Shut off color attribute */
    wattroff(win, attr);

    for (; p < width; p++)
        wprintw(win, " ");
}

int hl_regex(const char *regex, const char **hl_lines, const char **tlines,
        const int length, char **cur_line, int *sel_line,
        int *sel_rline, int *sel_col_rbeg, int *sel_col_rend,
        int opt, int direction, int icase)
{
    regex_t t;                  /* Regular expression */
    regmatch_t pmatch[1];       /* Indexes of matches */
    int i = 0, result = 0;
    char *local_cur_line;
    int success = 0;
    int offset = 0;
    int config_wrapscan = cgdbrc_get(CGDBRC_WRAPSCAN)->variant.int_val;

    if (tlines == NULL || tlines[0] == NULL ||
            cur_line == NULL || sel_line == NULL ||
            sel_rline == NULL || sel_col_rbeg == NULL || sel_col_rend == NULL)
        return -1;

    /* Clear last highlighted line */
    if (*cur_line != NULL) {
        free(*cur_line);
        *cur_line = NULL;
    }

    /* If regex is empty, set current line to original line */
    if (regex == NULL || *regex == '\0') {
        *sel_line = *sel_rline;
        return -2;
    }

    /* Compile the regular expression */
    if (regcomp(&t, regex, REG_EXTENDED & (icase) ? REG_ICASE : 0) != 0) {
        regfree(&t);
        return -3;
    }

    /* Forward search */
    if (direction) {
        int start = *sel_rline;
        int end = length;

        offset = *sel_col_rend;
        while (!success) {
            for (i = start; i < end; i++) {
                int local_cur_line_length;

                local_cur_line = (char *) tlines[i];
                local_cur_line_length = strlen(local_cur_line);

                /* Add the position of the current line's last match */
                if (i == *sel_rline) {
                    if (offset >= local_cur_line_length)
                        continue;
                    local_cur_line += offset;
                }

                /* Found a match */
                if ((result = regexec(&t, local_cur_line, 1, pmatch, 0)) == 0) {
                    success = 1;
                    break;
                }
            }

            if (success || start == 0 || !config_wrapscan) {
                break;
            } else {
                end = start;
                start = 0;
            }
        }

    } else {                    /* Reverse search */
        int j, pos;
        int start = *sel_rline;
        int end = 0;

        offset = *sel_col_rbeg;

        /* Try each line */
        while (!success) {
            for (i = start; i >= end; i--) {
                local_cur_line = (char *) tlines[i];
                pos = strlen(local_cur_line) - 1;
                if (pos < 0)
                    continue;

                if (i == *sel_rline)
                    pos = offset - 1;

                /* Try each line, char by char starting from the end */
                for (j = pos; j >= 0; j--) {
                    if ((result = regexec(&t, local_cur_line + j, 1, pmatch,
                                            0)) == 0) {
                        if (i == *sel_rline && pmatch[0].rm_so > pos - j)
                            continue;
                        /* Found a match */
                        success = 1;
                        offset = j;
                        break;
                    }
                }

                if (success)
                    break;
            }

            if (success || start == length - 1 || !config_wrapscan) {
                break;
            } else {
                end = start;
                start = length - 1;
            }
        }

    }

    if (success) {
        /* The offset is 0 if the line was not on the original line */
        if (direction && *sel_rline != i)
            offset = 0;

        /* If final match ( user hit enter ) make position perminant */
        if (opt == 2) {
            *sel_col_rbeg = pmatch[0].rm_so + offset;
            *sel_col_rend = pmatch[0].rm_eo + offset;
            *sel_rline = i;
        }

        /* Keep the new line as the selected line */
        *sel_line = i;

        /* If the match is not perminant then give cur_line highlighting */
        if (opt != 2 && pmatch[0].rm_so != -1 && pmatch[0].rm_eo != -1)
            *cur_line =
                    highlight_line_segment(hl_lines[i],
                    pmatch[0].rm_so + offset, pmatch[0].rm_eo + offset);
    } else {
        /* On failure, the current line goes to the original line */
        *sel_line = *sel_rline;
    }

    regfree(&t);

    return success;
}
