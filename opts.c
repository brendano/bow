/* Handling command-line options that apply across the whole of libbow. */

#include <argp.h>
#include <bow/libbow.h>

/* For mkdir() and stat() */
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>

/* Global variables whose value is set by bow_argp functions, but
   which must be examined by some other function (called later) in
   order to have any effect. */

/* Flag to indicate whether ARG... files should be interpreted as HDB
   databases */
int bow_hdb = 0;

/* Remove all but the top N words by selecting words with highest
   information gain */
int bow_prune_vocab_by_infogain_n = 0;

/* Remove words that occur less than N times */
int bow_prune_vocab_by_occur_count_n = 0;

/* The weight-setting and scoring method set on the command-line. */
bow_method *bow_argp_method = NULL;

/* The directory in which we'll store word-vector data. */
const char *bow_data_dirname = NULL;

/* If non-zero, print to stdout the contribution of each word to
   each class. */
int bow_print_word_scores = 0;

/* If non-zero, use equal prior probabilities on classes when setting
   weights, calculating infogain and scoring */
int bow_uniform_class_priors = 0;

/* If non-zero, use binary occurrence counts for words. */
int bow_binary_word_counts = 0;

/* Don't lex any files with names matching this. */
const char *bow_exclude_filename = NULL;

/* Pipe the files through this shell command before lexing. */
const char *bow_lex_pipe_command = NULL;

/* File containing the annotations to display for each file */
const char *bow_annotation_filename = NULL;

/* If non-zero, check for eencoding blocks before istext() says that
   the file is text. */
int bow_istext_avoid_uuencode = 0;

/* Number of decimal places to print when printing classification scores */
int bow_score_print_precision = 10;

/* Which smoothing method to use to avoid zero word probabilities */
bow_smoothing bow_smoothing_method = bow_smoothing_laplace;

/* Remove words that occur in this many or fewer documents. */
int bow_prune_words_by_doc_count_n = 0;

/* Random seed to use for srand, if not equal to -1 */
int bow_random_seed = -1;

/* What "event-model" we will use for the probabilistic models. */
bow_event_models bow_event_model = bow_event_word;

/* What "event-model" we will use for calculating information gain of 
   words with classes. */
bow_event_models bow_infogain_event_model = bow_event_document;

/* When using the bow_event_document_then_word event model, we
   normalize the length of all the documents.  This determines the
   normalized length. */
int bow_event_document_then_word_document_length = 200;

/* Smooth words that occur k or fewer times for Good-Turing smoothing */
int bow_smoothing_goodturing_k = 7;

/* The filename containing the dirichlet alphas */
const char *bow_smoothing_dirichlet_filename = NULL;

/* Only tokenize words containing `xxx' */
int bow_xxx_words_only = 0;

/* The weighting factor for the alphas */
float bow_smoothing_dirichlet_weight = 1.0;


/* Value added to key to get the key of the opposite option.  For
   example "do not use stoplist" has key 's'; "use stoplist" has key
   's'+KEY_OPPOSITE. */
#define KEY_OPPOSITE 256

enum {
  APPEND_STOPLIST_FILE_KEY = 10000,
  PRINT_WORD_SCORES_KEY,
  UNIFORM_CLASS_PRIORS_KEY,
  NAIVEBAYES_SCORE_WITH_LOG_PROBS_KEY,
  BINARY_WORD_COUNTS_KEY,
  EXCLUDE_FILENAME_KEY,
  LEX_PIPE_COMMAND_KEY,
  ISTEXT_AVOID_UUENCODE_KEY,
  LEX_WHITE_KEY,
  LEX_ALPHANUM_KEY,
  LEX_SUFFIXING_KEY,
  LEX_INFIX_KEY,
  SHORTEST_WORD_KEY,
  FLEX_MAIL_KEY,
  FLEX_TAGGED_KEY,
  REPLACE_STOPLIST_FILE_KEY,
  SCORE_PRINT_PRECISION,
  SMOOTHING_METHOD_KEY,
  SPLIT_SEED,
  EVENT_MODEL_KEY,
  EVENT_DOC_THEN_WORD_DOC_LENGTH_KEY,
  INFOGAIN_EVENT_MODEL_KEY,
  SMOOTHING_GOODTURING_K,
  HDB_KEY,
  ANNOTATION_KEY,
  SMOOTHING_DIRICHLET_FILENAME,
  SMOOTHING_DIRICHLET_WEIGHT,
  XXX_WORDS_ONLY_KEY,
  MAX_NUM_WORDS_PER_DOCUMENT_KEY,
  USE_UNKNOWN_WORD_KEY,
};

static struct argp_option bow_options[] =
{
  {0, 0, 0, 0,
   "General options", 1},
  {"verbosity", 'v', "LEVEL", 0,
   "Set amount of info printed while running; "
   "(0=silent, 1=quiet, 2=show-progess,...5=max)"},
  {"no-backspaces", 'b', 0, 0,
   "Don't use backspace when verbosifying progress (good for use in emacs)"},
  {"data-dir", 'd', "DIR", 0,
   "Set the directory in which to read/write word-vector data "
   "(default=~/.<program_name>)."},
  {"score-precision", SCORE_PRINT_PRECISION, "NUM", 0,
   "The number of decimal digits to print when displaying document scores"},
  {"random-seed", SPLIT_SEED, "NUM", 0,
   "The non-negative integer to use for seeding the random number generator"},
  {"annotations", ANNOTATION_KEY, "FILE", 0,
   "The sarray file containing annotations for the files in the index"},

#if HAVE_HDB
  {"hdb", HDB_KEY, 0, 0,
   "Assume ARG... names are HDB databases.  May not be used with "
   "--lex-pipe-command.  Only useful with --index option.  Currently only "
   "works with rainbow and arrow"},
#endif

  {0, 0, 0, 0,
   "Lexing options", 2},
  {"skip-header", 'h', 0, 0,
   "Avoid lexing news/mail headers by scanning forward until two newlines."},
  {"no-stoplist", 's', 0, 0,
   "Do not toss lexed words that appear in the stoplist."},
  {"use-stoplist", 's'+KEY_OPPOSITE, 0, 0,
   "Toss lexed words that appear in the stoplist.  "
   "(usually the default SMART stoplist, depending on lexer)"},
  {"append-stoplist-file", APPEND_STOPLIST_FILE_KEY, "FILE", 0,
   "Add words in FILE to the stoplist."},
  {"replace-stoplist-file", REPLACE_STOPLIST_FILE_KEY, "FILE", 0,
   "Empty the default stoplist, and add space-delimited words from FILE."},
  {"no-stemming", 'S'+KEY_OPPOSITE, 0, 0,
   "Do not modify lexed words with a stemming function. "
   "(usually the default, depending on lexer)"},
  {"use-stemming", 'S', 0, 0,
   "Modify lexed words with the `Porter' stemming function."},
  {"shortest-word", SHORTEST_WORD_KEY, "LENGTH", 0,
   "Toss lexed words that are shorter than LENGTH.  Default is usually 2."},
  {"gram-size", 'g', "N", 0,
   "Create tokens for all 1-grams,... N-grams."},
  {"exclude-filename", EXCLUDE_FILENAME_KEY, "FILENAME", 0,
   "When scanning directories for text files, skip files with name "
   "matching FILENAME."},
  {"istext-avoid-uuencode", ISTEXT_AVOID_UUENCODE_KEY, 0, 0,
   "Check for uuencoded blocks before saying that the file is text, "
   "and say no if there are many lines of the same length."},
  {"lex-pipe-command", LEX_PIPE_COMMAND_KEY, "SHELLCMD", 0,
   "Pipe files through this shell command before lexing them."},
  {"xxx-words-only", XXX_WORDS_ONLY_KEY, 0, 0,
   "Only tokenize words with `xxx' in them"},
  {"max-num-words-per-document", MAX_NUM_WORDS_PER_DOCUMENT_KEY, "N", 0,
   "Only tokenize the first N words in each document."},
  {"use-unknown-word", USE_UNKNOWN_WORD_KEY, 0, 0,
   "When used in conjunction with -O or -D, captures all words with "
   "occurrence counts below threshold as the `<unknown>' token"},

  {0, 0, 0, 0,
   "Mutually exclusive choice of lexers", 3},
  {"skip-html", 'H', 0, 0,
   "Skip HTML tokens when lexing."},
  {"keep-html", 'H'+KEY_OPPOSITE, 0, OPTION_HIDDEN,
   "Treat HTML tokens the same as any other chars when lexing. (default)"},
  {"lex-white", LEX_WHITE_KEY, 0, 0,
   "Use a special lexer that delimits tokens by whitespace only, and "
   "does not change the contents of the token at all---no downcasing, "
   "no stemming, no stoplist, nothing.  Ideal for use with an externally-"
   "written lexer interfaced to rainbow with --lex-pipe-cmd."},
  {"lex-alphanum", LEX_ALPHANUM_KEY, 0, 0,
   "Use a special lexer that includes digits in tokens, delimiting tokens "
   "only by non-alphanumeric characters."},
  {"lex-suffixing", LEX_SUFFIXING_KEY, 0, 0,
   "Use a special lexer that adds suffixes depending on Email-style headers."},
  {"lex-infix-string", LEX_INFIX_KEY, "ARG", 0,
   "Use only the characters after ARG in each word for stoplisting and "
   "stemming.  If a word does not contain ARG, the entire word is used."},
  {"flex-mail", FLEX_MAIL_KEY, 0, 0,
   "Use a mail-specific flex lexer"},
  {"flex-tagged", FLEX_TAGGED_KEY, 0, 0,
   "Use a tagged flex lexer"},
  {"lex-for-usenet", 'U', 0, OPTION_HIDDEN,
   "Use a special lexer for UseNet articles, ignore some headers and "
   "uuencoded blocks."},

  {0, 0, 0, 0,
   "Feature-selection options", 4},
  {"prune-vocab-by-infogain", 'T', "N", 0,
   "Remove all but the top N words by selecting words with highest "
   "information gain."},
  {"prune-vocab-by-occur-count", 'O', "N", 0,
   "Remove words that occur less than N times."},
  {"prune-vocab-by-doc-count", 'D', "N", 0,
   "Remove words that occur in N or fewer documents."},

  {0, 0, 0, 0,
   "Weight-vector setting/scoring method options", 5},
  {"method", 'm', "METHOD", 0,
   "Set the word weight-setting method; METHOD may be one of: "},
  {"print-word-scores", PRINT_WORD_SCORES_KEY, 0, 0,
   "During scoring, print the contribution of each word to each class."},
  {"uniform-class-priors", UNIFORM_CLASS_PRIORS_KEY, 0, 0,
   "When setting weights, calculating infogain and scoring, use equal prior "
   "probabilities on classes."},
  {"binary-word-counts", BINARY_WORD_COUNTS_KEY, 0, 0,
   "Instead of using integer occurrence counts of words to set weights, "
   "use binary absence/presence."},
  {"smoothing-method", SMOOTHING_METHOD_KEY, "METHOD", 0,
   "Set the method for smoothing word probabilities to avoid zeros; "
   "METHOD may be one of: goodturing, laplace, mestimate, wittenbell"},
  {"smoothing-goodturing-k", SMOOTHING_GOODTURING_K, "NUM", 0,
   "Smooth word probabilities for words that occur NUM or less times. "
   "The default is 7."},
  {"smoothing-dirichlet-filename", SMOOTHING_DIRICHLET_FILENAME, "FILE", 0,
   "The file containing the alphas for the dirichlet smoothing."},
  {"smoothing-dirichlet-weight", SMOOTHING_DIRICHLET_WEIGHT, "NUM", 0,
   "The weighting factor by which to muliply the alphas for dirichlet "
   "smoothing."},
  {"event-model", EVENT_MODEL_KEY, "EVENTNAME", 0,
   "Set what objects will be considered the `events' of the probabilistic "
   "model.  EVENTNAME can be one of: word, document, document-then-word.  "
   "Default is `word'."},
  {"event-document-then-word-document-length", 
   EVENT_DOC_THEN_WORD_DOC_LENGTH_KEY,
   "NUM", 0,
   "Set the normalized length of documents when "
   "--event-model=document-then-word"},
  {"infogain-event-model", INFOGAIN_EVENT_MODEL_KEY, "EVENTNAME", 0,
   "Set what objects will be considered the `events' when information gain "
   "is calculated.  "
   "EVENTNAME can be one of: word, document, document-then-word.  "
   "Default is `document'."},

  {0, 0}
};

static int
parse_bow_opt (int opt, char *arg, struct argp_state *state)
{
  switch (opt)
    {
    case 'v':
      bow_verbosity_level = atoi (optarg);
      break;
    case 'b':
      /* Don't print backspaces when verbosifying at level BOW_PROGRESS. */
      bow_verbosity_use_backspace = 0;
      break;
    case 'd':
      /* Set name of the directory in which we'll store word-vector data. */
      bow_data_dirname = arg;
      break;
    case SCORE_PRINT_PRECISION:
      /* Set the number of digits to print */
      bow_score_print_precision = atoi(optarg);
      break;
    case SPLIT_SEED:
      /* Set the seed for the random number generator */
      bow_random_seed = atoi (optarg);
      if (bow_random_seed < 0)
	{
	  fprintf (stderr,
		   "--split-seed: Seed must be non-negative.\n");
	  return ARGP_ERR_UNKNOWN;
	}
      break;
#if HAVE_HDB
    case HDB_KEY:
      bow_hdb = 1;
      if (bow_lex_pipe_command)
	bow_error ("--hdb and --lex-pipe-command options cannot be used in"
		   " conjunction\n");
      break;
#endif
      
      /* Lexing options. */
    case 'h':
      /* Avoid lexing news/mail headers by scanning fwd until two newlines */
      bow_lexer_document_start_pattern = "\n\n";
      break;
    case 'g':
      /* Create tokens for all 1-grams,... N-grams */
      {
	int n = atoi (arg);
	if (n <= 0)
	  {
	    fprintf (stderr,
		     "--gram-size, -N:  gram size must be a positive int\n");
	    return ARGP_ERR_UNKNOWN;
	  }
	else if (n > 1)
	  {
	    bow_lexer_gram *lex = bow_malloc (sizeof (bow_lexer_gram));
	    memcpy (lex, bow_gram_lexer, sizeof (bow_lexer_gram));
	    lex->gram_size = n;
	    lex->lexer.next = bow_default_lexer;
	    bow_default_lexer = (bow_lexer*) lex;
	  }
	break;
      }
    case 'H':
      /* Skip HTML tokens when lexing */
      {
	bow_lexer *lex = bow_malloc (sizeof (bow_lexer));
	memcpy (lex, bow_html_lexer, sizeof (bow_lexer));
	lex->next = bow_default_lexer;
	bow_default_lexer = lex;
	break;
      }
    case LEX_WHITE_KEY:
      /* Use the whitespace lexer parameters */
      memcpy (bow_default_lexer_parameters, bow_white_lexer_parameters,
	      sizeof (bow_lexer_parameters));
      break;
    case LEX_ALPHANUM_KEY:
      /* Use the alphanum lexer */
      memcpy (bow_default_lexer_parameters, bow_alphanum_lexer_parameters,
	      sizeof (bow_lexer_parameters));
      break;
    case LEX_SUFFIXING_KEY:
      /* Use the suffixing lexer for prepending tags like `Date:' etc */
      {
	/* By default it uses the html_lexer as its underlying lexer */
	bow_lexer *ulex = bow_malloc (sizeof (bow_lexer));
	bow_lexer *lex = bow_malloc (sizeof (bow_lexer));
	memcpy (ulex, bow_html_lexer, sizeof (bow_lexer));
	ulex->next = bow_default_lexer;
	memcpy (lex, bow_suffixing_lexer, sizeof (bow_lexer));
	lex->next = ulex;
	bow_default_lexer = lex;
	break;
      }
    case LEX_INFIX_KEY:
      bow_lexer_infix_separator = arg;
      bow_lexer_infix_length = strlen (arg);
      break;
    case FLEX_MAIL_KEY:
      bow_flex_option = USE_MAIL_FLEXER;
      break;
    case FLEX_TAGGED_KEY:
      bow_flex_option = USE_TAGGED_FLEXER;
      break;
    case SHORTEST_WORD_KEY:
      /* Set the length of the shortest token that will not be tossed */
      {
	int s = atoi (arg);
	assert (s > 0);
	bow_lexer_toss_words_shorter_than = s;
	break;
      }
    case 's':
      /* Do not toss lexed words that appear in the stoplist */
      bow_lexer_stoplist_func = NULL;
      break;
    case 's'+KEY_OPPOSITE:
      /* Toss lexed words that appear in the stoplist */
      bow_lexer_stoplist_func = bow_stoplist_present;
      break;
    case 'S':
      /* Modify lexed words with the `Porter' stemming function */
      bow_lexer_stem_func = bow_stem_porter;
      break;
    case 'S'+KEY_OPPOSITE:
      /* Do not modify lexed words with a stemmiog function. (default) */
      /* Modify lexed words with the `Porter' stemming function */
      bow_lexer_stem_func = NULL;
      break;
    case APPEND_STOPLIST_FILE_KEY:
      bow_stoplist_add_from_file (arg);
      break;
    case REPLACE_STOPLIST_FILE_KEY:
      bow_stoplist_replace_with_file (arg);
      break;
    case ANNOTATION_KEY:
      bow_annotation_filename = arg;
      break;
    case 'U':
      /* Use a special lexer for UseNet articles, ignore some headers and
	 uuencoded blocks. */
      bow_error ("The -U option is broken.");
      break;
    case EXCLUDE_FILENAME_KEY:
      bow_exclude_filename = arg;
      break;
    case LEX_PIPE_COMMAND_KEY:
      bow_lex_pipe_command = arg;
      if (bow_hdb)
	bow_error ("--hdb and --lex-pipe-command options cannot be used in"
		   " conjunction\n");
      break;
    case ISTEXT_AVOID_UUENCODE_KEY:
      bow_istext_avoid_uuencode = 1;
      break;
    case XXX_WORDS_ONLY_KEY:
      bow_xxx_words_only = 1;
      break;
    case MAX_NUM_WORDS_PER_DOCUMENT_KEY:
      bow_lexer_max_num_words_per_document = atoi (arg);
      break;
    case USE_UNKNOWN_WORD_KEY:
      bow_word2int_use_unknown_word = 1;
      break;

      /* Feature selection options. */

    case 'T':
      /* Remove all but the top N words by selecting words with highest 
	 information gain */
      bow_prune_vocab_by_infogain_n = atoi (arg);
      break;
    case 'O':
      /* Remove words that occur less than N times */
      bow_prune_vocab_by_occur_count_n = atoi (arg);
      break;
    case 'D':
      /* Remove words that occur in N or fewer documents */
      bow_prune_words_by_doc_count_n = atoi (arg);
      break;

    case 'm':
      bow_argp_method = bow_method_at_name (arg);
      break;
    case SMOOTHING_METHOD_KEY:
      if (!strcmp (arg, "goodturing"))
	bow_smoothing_method = bow_smoothing_goodturing;
      else if (!strcmp (arg, "laplace"))
	bow_smoothing_method = bow_smoothing_laplace;
      else if (!strcmp (arg, "mestimate"))
	bow_smoothing_method = bow_smoothing_mestimate;
      else if (!strcmp (arg, "wittenbell"))
	bow_smoothing_method = bow_smoothing_wittenbell;
      else if (!strcmp (arg, "dirichlet"))
	bow_smoothing_method = bow_smoothing_dirichlet;
      else
	bow_error ("--smoothing-method: No such smoothing method `%s'", arg);
      break;
    case SMOOTHING_GOODTURING_K:
      bow_smoothing_goodturing_k = atoi (arg);
      break;
    case SMOOTHING_DIRICHLET_FILENAME:
      bow_smoothing_dirichlet_filename = arg;
      break;
    case SMOOTHING_DIRICHLET_WEIGHT:
      bow_smoothing_dirichlet_weight = atof (arg);
      break;
    case PRINT_WORD_SCORES_KEY:
      bow_print_word_scores = 1;
      break;
    case UNIFORM_CLASS_PRIORS_KEY:
      bow_uniform_class_priors = 1;
      break;
    case BINARY_WORD_COUNTS_KEY:
      /* Use binary absence/presence, instead of integer occurrence
         counts for words. */
      bow_binary_word_counts = 1;
      break;
    case EVENT_MODEL_KEY:
      if (!strcmp (arg, "document"))
	bow_event_model = bow_event_document;
      else if (!strcmp (arg, "word"))
	bow_event_model = bow_event_word;
      else if (!strcmp (arg, "document-then-word")
	       || !strcmp (arg, "dw"))
	bow_event_model = bow_event_document_then_word;
      else
	bow_error ("--event-model: No such event model `%s'", arg);
      break;
    case EVENT_DOC_THEN_WORD_DOC_LENGTH_KEY:
      bow_event_document_then_word_document_length = atoi (arg);
      assert (bow_event_document_then_word_document_length > 0);
      break;
    case INFOGAIN_EVENT_MODEL_KEY:
      if (!strcmp (arg, "document"))
	bow_infogain_event_model = bow_event_document;
      else if (!strcmp (arg, "word"))
	bow_infogain_event_model = bow_event_word;
      else if (!strcmp (arg, "document-then-word"))
	bow_infogain_event_model = bow_event_document_then_word;
      else
	bow_error ("--infogain_event-model: No such event model `%s'", arg);
      break;

    case ARGP_KEY_INIT:
      /* Things to do before any arguments are processed. */

      /* If the file ./.bow-stopwords exists, load the extra words into
	 the stoplist. */
      bow_stoplist_add_from_file ("./.bow-stopwords");

      /* If the file ~/.bow-stopwords exists, load the extra words into
	 the stoplist. */
      {
	const char sw_fn[] = "/.bow-stopwords";
	const char *home = getenv ("HOME");
	if (home != NULL) {
	    char filename[strlen (home) + strlen (sw_fn) + 1];
	    strcpy (filename, home);
	    strcat (filename, sw_fn);
	    bow_stoplist_add_from_file (filename);    
	}
      }
 
      /* Build the default data directory name, in case it wasn't
	 specified on the command line. */
      assert (program_invocation_short_name);
      if (!bow_data_dirname)
	{
	  char *homedir = getenv ("HOME");
	  if (homedir != NULL) {
	      char *dirname = bow_malloc (strlen (homedir) 
					  + strlen ("/.")
					  + strlen (program_invocation_short_name)
					  + 1);
	      strcpy (dirname, homedir);
	      strcat (dirname, "/.");
	      strcat (dirname, program_invocation_short_name);
	      bow_data_dirname = dirname;
	  }
	}

    case ARGP_KEY_ARG:
      break;
    case ARGP_KEY_END:
      /* Create the data directory, if it doesn't exist already. */
      {
	struct stat st;
	int e;
	e = stat (bow_data_dirname, &st);
	if (e == 0)
	  {
	    /* Assume this means the file exists. */
	    if (!S_ISDIR (st.st_mode))
	      bow_error ("`%s' already exists, but is not a directory",
			 bow_data_dirname);
	  }
#if !defined(DART) && !defined(FDART)
	else
	  {
	    if (mkdir (bow_data_dirname, 0777) == 0)
	      bow_verbosify (bow_quiet, "Created directory `%s'.\n", 
			     bow_data_dirname);
	    else if (errno != EEXIST)
	      bow_error ("Couldn't create default data directory `%s'",
			 bow_data_dirname);
	  }
#endif
      }

    default:
      return ARGP_ERR_UNKNOWN;
    }
  return 0;
}


static char *
_help_filter (int key, const char *text, void *input)
{
  char *ret;

  /* Add the names of the available methods to the help text. */
  if (key == 'm' && bow_methods)
    {
      static const int len = 1024;
      char methodnames[len];
      int i;
      bow_method *m;

      methodnames[0] = '\0';
      for (i = bow_methods->array->length-1; i >= 0; i--)
	{
	  m = bow_sarray_entry_at_index (bow_methods, i);
	  strcat (methodnames, m->name);
	  strcat (methodnames, ", ");
	}
      strcat (methodnames, "default=naivebayes.");
      assert (strlen (methodnames) < len);
      ret = malloc (strlen (text) + len);
      strcpy (ret, text);
      strcat (ret, methodnames);
      return ret;
    }
  return (char*)text;
}

/* This may be used with argp_parse to parse standard libbow startup
   options, possible chained onto the end of a user argp structure.  */
const struct argp bow_argp =
{
  bow_options,			/* data structure describing cmdline options */
  parse_bow_opt,		/* the function to handle the options */
  0,				/* non-option argument documention */
  0,				/* extra text printed before and after help */
  0,				/* argp children */
  _help_filter
};


#define MAX_NUM_CHILDREN 100
struct argp_child bow_argp_children[MAX_NUM_CHILDREN] =
{
  {
    &bow_argp,			/* the argp structure */
    0,				/* flags for child */
    "Libbow options",		/* optional header */
    999				/* group (general lib flags at end of help)*/
  },
  {0}
};

/* The number of children already initialized in the const assignment above. */
static int bow_argp_children_length = 1;

/* Add the options in CHILD to the list of command-line options. */
void
bow_argp_add_child (struct argp_child *child)
{
  assert (bow_argp_children_length+1 < MAX_NUM_CHILDREN);
  memcpy (bow_argp_children + bow_argp_children_length,
	  child,
	  sizeof (struct argp_child));
  bow_argp_children_length++;
#if 1
  memset (bow_argp_children + bow_argp_children_length,
	  0, sizeof (struct argp_child));
#endif
}

static void
_print_version (FILE *stream, struct argp_state *state)
{
  if (argp_program_version)
    /* If this is non-zero, then the program's probably defined it, so let
       that take precedence over the default.  */
    fprintf (stream, "%s\n", argp_program_version);

  /* And because libbow is a changing, integral part, put our
     information out too. */
  fprintf (stream, "libbow %d.%d\n",
	   BOW_MAJOR_VERSION, BOW_MINOR_VERSION);
}

void (*argp_program_version_hook) (FILE *stream, struct argp_state *state)
     = _print_version;
