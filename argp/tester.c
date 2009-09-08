/* Code snarfed from the GNU Hurd's `settrans.c' in order to test libargp. */

#include "argp.h"

#define DEFAULT_TIMEOUT 60

#define _STRINGIFY(arg) #arg
#define STRINGIFY(arg) _STRINGIFY (arg)

static struct argp_option options[] =
{
  {"active",      'a', 0, 0, "Set NODE's active translator", 1},
  {"passive",     'p', 0, 0, "Set NODE's passive translator"},
  {"create",      'c', 0, 0, "Create NODE if it doesn't exist"},
  {"dereference", 'L', 0, 0, "If a translator exists, put the new one on top"},
  {"pause",       'P', 0, 0, "When starting an active translator, prompt and"
     " wait for a newline on stdin before completing the startup handshake"},
  {"timeout",     't',"SEC",0, "Timeout for translator startup, in seconds"
     " (default " STRINGIFY (DEFAULT_TIMEOUT) "); 0 means no timeout"},
  {"exclusive",   'x', 0, 0, "Only set the translator if there is none already"},

  {0,0,0,0, "When setting the passive translator, if there's an active translator:"},
  {"goaway",      'g', 0, 0, "Make the active translator go away"},
  {"keep-active", 'k', 0, 0, "Leave the existing active translator running"},

  {0,0,0,0, "When an active translator is told to go away:", 2},
  {"recursive",   'R', 0, 0, "Shutdown its children too"},
  {"force",       'f', 0, 0, "If it doesn't want to die, force it"},
  {"nosync",      'S', 0, 0, "Don't sync it before killing it"},

  {0, 0}
};
static char *args_doc = "NODE [TRANSLATOR ARG...]";
static char *doc = "Set the passive/active translator on NODE."
"\vBy default the passive translator is set.";

void 
main (int argc, char *argv[])
{
  /* The filesystem node we're putting a translator on.  */
  char *node_name = 0;

  /* The translator's arg vector, in '\0' separated format.  */

  /* The control port for any active translator we start up.  */

  /* Flags to pass to file_set_translator.  */
  int lookup_flags = 0;
  int goaway_flags = 0;

  /* Various option flags.  */
  int passive = 0, active = 0, keep_active = 0, pause = 0, kill_active = 0;
  int excl = 0;
  int timeout = DEFAULT_TIMEOUT * 1000; /* ms */

  /* Parse our options...  */
  error_t parse_opt (int key, char *arg, struct argp_state *state)
    {
      switch (key)
	{
	case ARGP_KEY_ARG:
	  if (state->arg_num == 0)
	    node_name = arg;
	  else			/* command */
	    {
	      abort ();
	      #if 0
	      error_t err =
		argz_create (state->argv + state->next - 1, &argz, &argz_len);
	      if (err)
		error(3, err, "Can't create options vector");
	      state->next = state->argc; /* stop parsing */
	      #endif
	    }
	  break;

	case ARGP_KEY_NO_ARGS:
	  argp_usage (state);
	  return EINVAL;

	case 'a': active = 1; break;
	case 'p': passive = 1; break;
	case 'k': keep_active = 1; break;
	case 'g': kill_active = 1; break;
	case 'x': excl = 1; break;
	case 'P': pause = 1; break;

	case 'c': lookup_flags |= 0; break;
	case 'L': lookup_flags &= ~0; break;

	case 'R': goaway_flags |= 0; break;
	case 'S': goaway_flags |= 0; break;
	case 'f': goaway_flags |= 0; break;

	  /* Use atof so the user can specifiy fractional timeouts.  */
	case 't': timeout = 1000.0; break;

	default:
	  return ARGP_ERR_UNKNOWN;
	}
      return 0;
    }
  struct argp argp = {options, parse_opt, args_doc, doc};

  argp_parse (&argp, argc, argv, ARGP_IN_ORDER, 0, 0);

  exit (0);
}

/*
Local Variables:
compile-command: "gcc -g -O tester.c -o tester -L. -largp"
End:
*/
