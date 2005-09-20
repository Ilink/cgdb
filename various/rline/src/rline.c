#include "rline.h"

#if HAVE_CONFIG_H
#include "config.h"
#endif /* HAVE_CONFIG_H */

#if HAVE_STDLIB_H 
#include <stdlib.h>
#endif  /* HAVE_STDLIB_H */

#if HAVE_STDLIB_H 
#include <stdio.h>
#endif  /* HAVE_STDLIB_H */

// includes {{{

#ifdef HAVE_LIBREADLINE
#  if defined(HAVE_READLINE_READLINE_H)
#    include <readline/readline.h>
#  elif defined(HAVE_READLINE_H)
#    include <readline.h>
#  else /* !defined(HAVE_READLINE_H) */
extern char *readline ();
#  endif /* !defined(HAVE_READLINE_H) */
char *cmdline = NULL;
#else /* !defined(HAVE_READLINE_READLINE_H) */
 /* no readline */
#endif /* HAVE_LIBREADLINE */

#ifdef HAVE_READLINE_HISTORY
#  if defined(HAVE_READLINE_HISTORY_H)
#    include <readline/history.h>
#  elif defined(HAVE_HISTORY_H)
#    include <history.h>
#  else /* !defined(HAVE_HISTORY_H) */
extern void add_history ();
extern int write_history ();
extern int read_history ();
#  endif /* defined(HAVE_READLINE_HISTORY_H) */
 /* no history */
#endif /* HAVE_READLINE_HISTORY */

// }}}

struct rline
{
  /* The input to readline. Writing to this, writes to readline. */
  FILE *input;

  /* The output of readline. When readline writes data, it goes to this
   * descritpor. Thus, reading from this, get's readline's output. */
  FILE *output;

  /* The user defined tab completion function */
  completion_cb *tab_completion;

  /* The user defined tab completion function */
  rl_command_func_t *rline_rl_last_func;
};

static void custom_deprep_term_function () {}

// Createing and Destroying a librline context. {{{
struct rline* 
rline_initialize (int slavefd, command_cb *command, completion_cb *completion)
{
  struct rline *rline = (struct rline*)malloc (sizeof(struct rline));
  if (!rline)
    return NULL;

  /* Initialize each member variable */
  rline->input = NULL;
  rline->output = NULL;

  rline->input = fdopen (slavefd, "r");
  if (!rline->input) {
    rline_shutdown (rline);
    return NULL;
  }

  rline->output = fdopen (slavefd, "w");
  if (!rline->output) {
    rline_shutdown (rline);
    return NULL;
  }

  rline->tab_completion = completion;
  rline->rline_rl_last_func = NULL;

  rl_instream = rline->input;
  rl_outstream = rline->output;

  /* Tell readline not to put the initial prompt */
  rl_already_prompted = 1;

  /* Tell readline not to catch signals */
  rl_catch_signals = 0;

  /* Tell readline what the prompt is if it needs to put it back */
  rl_callback_handler_install("(tgdb) ", command);
  rl_bind_key ('\t', completion);

  /* Set the terminal type to dumb so the output of readline can be
   * understood by tgdb */
  if (rl_reset_terminal ("dumb") == -1) {
    rline_shutdown (rline);
    return NULL;
  }

  /* For some reason, readline can not deprep the terminal.
   * However, it doesn't matter because no other application is working on
   * the terminal besides readline */
  rl_deprep_term_function = custom_deprep_term_function;

  /* These variables are here to make sure readline doesn't 
   * attempt to query the user to determine if it wants more input.
   */
  rl_completion_query_items = 90000000;
  rl_variable_bind ("page-completions", "0");

  return rline;
}

int 
rline_shutdown (struct rline *rline)
{
  if (!rline)
    return -1; /* Should this be OK? */

  if (rline->input)
    fclose (rline->input);

  if (rline->output)
    fclose (rline->output);

  free (rline);
  rline = NULL;

  return 0;
}

//@}
// }}}

// Reading and Writing the librline context. {{{
int 
rline_read_history (struct rline *rline, const char *file)
{
  if (!rline)
    return -1;

  using_history();
  read_history(file); 
  history_set_pos(history_length);

  return 0;
}

int 
rline_write_history (struct rline *rline, const char *file)
{
  if (!rline)
    return -1;

  write_history (file);

  return 0;
}

//@}
// }}}

// Functional commands {{{

int 
rline_set_prompt (struct rline *rline, const char *prompt)
{
  if (!rline)
    return -1;

  rl_set_prompt(prompt); 

  return 0;
}

int 
rline_clear (struct rline *rline)
{
  if (!rline)
    return -1;

  /* Clear whatever readline has in it's buffer. */
  rl_point = 0; 
  rl_end = 0;
  rl_mark = 0;
  rl_delete_text ( 0 , rl_end );

  return 0;
}

int 
rline_add_history (struct rline *rline, const char *line)
{
  if (!rline)
    return -1;

  if (!line)
    return -1;

  add_history (line);

  return 0;
}

int 
rline_get_prompt (struct rline *rline, char **prompt)
{
  if (!rline)
    return -1;

  if (!prompt)
    return -1;

  *prompt = rl_prompt;

  return 0;
}

int 
rline_get_current_line (struct rline *rline, char **current_line)
{
  if (!rline)
    return -1;

  if (!current_line)
    return -1;

  *current_line = rl_line_buffer;

  return 0;
}

int 
rline_rl_forced_update_display (struct rline *rline)
{
  if (!rline)
    return -1;

  rl_forced_update_display ();

  return 0;
}

int 
rline_rl_callback_read_char (struct rline *rline)
{
  if (!rline)
    return -1;

  /* Capture the last function used here.  */
  rline->rline_rl_last_func = rl_last_func;

  rl_callback_read_char ();

  return 0;
}

static tgdb_list_iterator *rline_local_iter;

char *
rline_rl_completion_entry_function (const char *c, int i)
{
  if (rline_local_iter) {
    char *local = tgdb_list_get_item (rline_local_iter);
    rline_local_iter = tgdb_list_next (rline_local_iter);
    return strdup (local);
  }

  return NULL;
}

/* This is necessary because we want to make what the user has typed
 * against a list of possibilites. For example, if the user has typed
 * 'b ma', the completion possibities should at least have 'b main'.
 * The default readline word break includes a ' ', which would not
 * result in 'b main' as the completion result.
 */
char *
rline_rl_cpvfunc_t (void)
{
  return "";
}

int 
rline_rl_complete (struct rline *rline, struct tgdb_list *list, display_callback display_cb)
{
  int size;
  int key;
  rl_command_func_t *compare_func = NULL;

  if (!rline)
    return -1;

  /* Currently, if readline output's the tab completion to rl_outstream, it will fill
   * the pty between it and CGDB and will cause CGDB to hang. */
  if (!display_cb)
    return -1;

  size = tgdb_list_size (list);

  if (size == 0) {
    rl_completion_word_break_hook = NULL;
    rl_completion_entry_function = NULL;
  } else {
    rl_completion_word_break_hook = rline_rl_cpvfunc_t;
    rl_completion_entry_function = rline_rl_completion_entry_function;
  }

  rl_completion_display_matches_hook = display_cb;

  rline_local_iter = tgdb_list_get_first (list);

  /* This is probably a hack, however it works for now.
   *
   * Basically, rl_complete is working fine. After the call to rl_complete,
   * rl_line_buffer contains the proper data. However, the CGDB main loop
   * always call rline_rl_forced_update_display, which in the case of tab 
   * completion does this, (tgdb) b ma(tgdb) b main
   *
   * Normally, this works fine because the user hits '\n', which puts the prompt
   * on the next line. In this case, the user hit's \t.
   *
   * In order work around this problem, simply putting the \r should work 
   * for now.
   *
   * This obviously shouldn't be done when readline is doing 
   *    `?' means list the possible completions.
   * style completion. Because that actuall does list all of the values on
   * a different line. In this situation the \r goes after the completion
   * is done, since only the current prompt is on that line.
   */

  /* Another confusing comparison. This checks to see if the last
   * readline function and the current readline function and the 
   * tab completion callback are all the same. This ensures that this
   * is the second time the user hit \t in a row. Instead of simply
   * calling rl_complete_internal, it's better to call, rl_completion_mode
   * because this checks to see what kind of completion should be done.
   */
  if (rline->rline_rl_last_func == rline->tab_completion &&
      rline->rline_rl_last_func == rl_last_func)
    compare_func = rline->tab_completion;
  
  key = rl_completion_mode (compare_func);

  if (key == TAB)
    fprintf (rline->output, "\r");

  rl_complete_internal (key);

  if (key != TAB)
    fprintf (rline->output, "\r");

  return 0;
}

//@}
// }}}