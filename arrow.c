/* arrow - a document retreival front-end to libbow. */

/* Copyright (C) 1997, 1998, 1999 Andrew McCallum

   Written by:  Andrew Kachites McCallum <mccallum@cs.cmu.edu>

   This file is part of the Bag-Of-Words Library, `libbow'.

   This library is free software; you can redistribute it and/or
   modify it under the terms of the GNU Library General Public License
   as published by the Free Software Foundation, version 2.
   
   This library is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   Library General Public License for more details.

   You should have received a copy of the GNU Library General Public
   License along with this library; if not, write to the Free Software
   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111, USA */

#include <bow/libbow.h>
#include <argp.h>
#include <errno.h>		/* needed on DEC Alpha's */
#include <sys/types.h>
#include <sys/socket.h>

#ifndef WINNT
#include <sys/un.h>
#endif /* WINNT */

#include <netinet/in.h>
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <fcntl.h>

/* These are defined in bow/wicoo.c */
extern bow_wi2dvf *bow_wicoo_from_barrel  (bow_barrel *barrel);
extern void bow_wicoo_print_word_entropy (bow_wi2dvf *wicoo, int wi);

static int arrow_sockfd;

/* The version number of this program. */
#define ARROW_MAJOR_VERSION 0
#define ARROW_MINOR_VERSION 2

/* Definitions for using argp command-line processing */

const char *argp_program_version =
"arrow " STRINGIFY(ARROW_MAJOR_VERSION) "." STRINGIFY(ARROW_MINOR_VERSION);

const char *argp_program_bug_address = "<mccallum@cs.cmu.edu>";

static char arrow_argp_doc[] =
"Arrow -- a document retrieval front-end to libbow";

static char arrow_argp_args_doc[] = "[ARG...]";

enum {
  PRINT_IDF_KEY = 3000,
  QUERY_SERVER_KEY,
  QUERY_FORK_SERVER_KEY,
  COO_KEY
};

static struct argp_option arrow_options[] =
{
  {0, 0, 0, 0,
   "For building data structures from text files:", 1},
  {"index", 'i', 0, 0,
   "tokenize training documents found under ARG..., build weight vectors, "
   "and save them to disk"},

  {0, 0, 0, 0,
   "For doing document retreival using the data structures built with -i:", 2},
  {"query", 'q', "FILE", OPTION_ARG_OPTIONAL, 
   "tokenize input from stdin [or FILE], then print document most like it"},
  {"query-server", QUERY_SERVER_KEY, "PORTNUM", 0,
   "Run arrow in socket server mode."},
  {"query-forking-server", QUERY_FORK_SERVER_KEY, "PORTNUM", 0,
   "Run arrow in socket server mode, forking a new process with every "
   "connection.  Allows multiple simultaneous connections."},
  {"num-hits-to-show", 'n', "N", 0,
   "Show the N documents that are most similar to the query text "
   "(default N=1)"},
  {"compare", 'c', "FILE", 0,
   "Print the TFIDF cosine similarity metric of the query with this FILE."},

  {0, 0, 0, 0,
   "Diagnostics", 3},
  {"print-idf", PRINT_IDF_KEY, 0, 0,
   "Print, in unsorted order the IDF of all words in the model's vocabulary"},
  {"print-coo", COO_KEY, 0, 0,
   "Print word co-occurrence statistics."},

  { 0 }
};

struct arrow_arg_state
{
  /* Is this invocation of arrow to do indexing or querying? */
  enum {
    arrow_indexing, 
    arrow_querying,
    arrow_comparing,
    arrow_printing_idf,
    arrow_query_serving,
    arrow_printing_coo
  } what_doing;
  int non_option_argi;
  /* Where to find query text, or if NULL get query text from stdin */
  const char *query_filename;
  const char *compare_filename;
  /* number of closest-matching docs to print */
  int num_hits_to_show;
  const char *server_port_num;
  int serve_with_forking;
} arrow_arg_state;

static error_t
arrow_parse_opt (int key, char *arg, struct argp_state *state)
{
  switch (key)
    {
    case 'q':
      arrow_arg_state.what_doing = arrow_querying;
      arrow_arg_state.query_filename = arg;
      break;
    case 'i':
      arrow_arg_state.what_doing = arrow_indexing;
      break;
    case 'n':
      arrow_arg_state.num_hits_to_show = atoi (arg);
      break;
    case 'c':
      arrow_arg_state.what_doing = arrow_comparing;
      arrow_arg_state.compare_filename = arg;
      break;
    case PRINT_IDF_KEY:
      arrow_arg_state.what_doing = arrow_printing_idf;
    case QUERY_SERVER_KEY:
      arrow_arg_state.what_doing = arrow_query_serving;
      arrow_arg_state.server_port_num = arg;
      bow_lexer_document_end_pattern = "\n.\r\n";
      break;
    case QUERY_FORK_SERVER_KEY:
      arrow_arg_state.serve_with_forking = 1;
      arrow_arg_state.what_doing = arrow_query_serving;
      arrow_arg_state.server_port_num = arg;
      bow_lexer_document_end_pattern = "\n.\r\n";
      break;
    case COO_KEY:
      arrow_arg_state.what_doing = arrow_printing_coo;
      break;

    case ARGP_KEY_ARG:
      /* Now we consume all the rest of the arguments.  STATE->next is the
	 index in STATE->argv of the next argument to be parsed, which is the
	 first STRING we're interested in, so we can just use
	 `&state->argv[state->next]' as the value for ARROW_ARG_STATE->ARGS.
	 IN ADDITION, by setting STATE->next to the end of the arguments, we
	 can force argp to stop parsing here and return.  */
      arrow_arg_state.non_option_argi = state->next - 1;
      if (arrow_arg_state.what_doing == arrow_indexing
	  && state->next > state->argc)
	{
	  /* Zero directory names is not enough. */
	  fprintf (stderr, "Need at least one directory to index.\n");
	  argp_usage (state);
	}
      state->next = state->argc;
      break;

    default:
      return ARGP_ERR_UNKNOWN;
    }
  return 0;
}

static struct argp arrow_argp = 
{ arrow_options, arrow_parse_opt, arrow_argp_args_doc,
  arrow_argp_doc, bow_argp_children};


/* The structures that hold the data necessary for answering a query. */

bow_barrel *arrow_barrel;	/* the stats about words and documents */
/* The static structure in bow/int4word.c is also used. */


/* Writing and reading the word/document stats to disk. */

#define VOCABULARY_FILENAME "vocabulary"
#define BARREL_FILENAME "barrel"

/* Write the stats in the directory DATA_DIRNAME. */
void
arrow_archive ()
{
  char filename[BOW_MAX_WORD_LENGTH];
  char *fnp;
  FILE *fp;

  strcpy (filename, bow_data_dirname);
  strcat (filename, "/");
  fnp = filename + strlen (filename);

  strcpy (fnp, VOCABULARY_FILENAME);
  fp = bow_fopen (filename, "wb");
  bow_words_write (fp);
  fclose (fp);

  strcpy (fnp, BARREL_FILENAME);
  fp = bow_fopen (filename, "wb");
  bow_barrel_write (arrow_barrel, fp);
  fclose (fp);
}

/* Read the stats from the directory DATA_DIRNAME. */
void
arrow_unarchive ()
{
  char filename[BOW_MAX_WORD_LENGTH];
  char *fnp;
  FILE *fp;

  bow_verbosify (bow_progress, "Loading data files...");

  strcpy (filename, bow_data_dirname);
  strcat (filename, "/");
  fnp = filename + strlen (filename);

  strcpy (fnp, VOCABULARY_FILENAME);
  fp = bow_fopen (filename, "rb");
  bow_words_read_from_fp (fp);
  fclose (fp);

  strcpy (fnp, BARREL_FILENAME);
  fp = bow_fopen (filename, "rb");
  arrow_barrel = bow_barrel_new_from_data_fp (fp);
  /* Don't close this FP because we still need to read individual DV's. */

  bow_verbosify (bow_progress, "\n");
}



/* Building the word/document stats. */

/* Traverse the directories ARGV[ARROW_ARG_STATE.NON_OPTION_ARGI...],
   gathering word/document stats.  Return the number of documents
   indexed. */
int
arrow_index (int argc, char *argv[])
{
  int argi;

  /* Do all the parsing to build a barrel with word counts. */
  if (bow_prune_vocab_by_occur_count_n)
    {
      /* Parse all the documents to get word occurrence counts. */
      for (argi = arrow_arg_state.non_option_argi; argi < argc; argi++)
#if HAVE_HDB
	if (bow_hdb)
	  bow_words_add_occurrences_from_hdb (argv[argi], "");
	else
#endif
          bow_words_add_occurrences_from_text_dir (argv[argi], "");
      bow_words_remove_occurrences_less_than
	(bow_prune_vocab_by_occur_count_n);
      /* Now insist that future calls to bow_word2int*() will not
	 register new words. */
      bow_word2int_do_not_add = 1;
    }

  arrow_barrel = bow_barrel_new (0, 0, sizeof (bow_cdoc), 0);
  for (argi = arrow_arg_state.non_option_argi; argi < argc; argi++)
#if HAVE_HDB
    if (bow_hdb)
      bow_barrel_add_from_hdb (arrow_barrel, argv[argi], 0, argv[argi]);
    else
#endif
      bow_barrel_add_from_text_dir (arrow_barrel, argv[argi], 0, argv[argi]);
  if (bow_argp_method)
    arrow_barrel->method = (rainbow_method*)bow_argp_method;
  else
    arrow_barrel->method = &bow_method_tfidf;
  bow_barrel_set_weights (arrow_barrel);
  bow_barrel_normalize_weights (arrow_barrel);
  return arrow_barrel->cdocs->length;
}



/* Perform a query. */

/* Print the contents of file FILENAME. */
static inline void
print_file (const char *filename)
{
  FILE *fp;
  int byte;

  if ((fp = fopen (filename, "r")) == NULL)
    bow_error ("Couldn't open file `%s' for reading", filename);
  while ((byte = fgetc (fp)) != EOF)
    fputc (byte, stdout);
  fclose (fp);
}

/* Get some query text, and print its best-matching documents among
   those previously indexed.  The number of matching documents is
   ARROW_ARG_STATE.NUM_HITS_TO_SHOW.  If
   ARROW_ARG_STATE.QUERY_FILENAME is non-null, the query text will be
   obtained from that file; otherwise it will be prompted for and read
   from stdin. */
int
arrow_query (FILE *in, FILE *out, int num_hits_to_show)
{
  bow_score *hits;
  int actual_num_hits;
  int i;
  bow_wv *query_wv;

  hits = alloca (sizeof (bow_score) * num_hits_to_show);

  /* Get the query text, and create a "word vector" from the query text. */
  if (arrow_arg_state.query_filename)
    {
      FILE *fp;
      fp = bow_fopen (arrow_arg_state.query_filename, "r");
      /* Read in special paramter commands here. */
      query_wv = bow_wv_new_from_text_fp (fp, arrow_arg_state.query_filename);
      fclose (fp);
    }
  else
    {
      if (out == stdout)
	bow_verbosify (bow_quiet, 
		       "Type your query text now.  End with a Control-D.\n");
      /* Read in special paramter commands here. */
      query_wv = bow_wv_new_from_text_fp (in, NULL);
    }

  bow_verbosify (bow_verbose, "Read query\n");
  
  if (!query_wv)
    {
      bow_verbosify (bow_progress, "Empty query\n");
      return 0;
    }

#if 0
  /* Augment query with special suffixes */
#define NUM_SUFFIXES 6
  new_query_wv = bow_wv_new (query_wv->num_entries * NUM_SUFFIXES);
  for (i = 0; i < query_wv->num_entries; i++)
    {
      char token[BOW_MAX_WORD_LENGTH];
      char *suffix[] = {"", "xxxtitle", "xxxauthor", "xxxinstitution", "xxxabstract", "xxxreferences"};
      int repetition[] = {1, 800, 800, 400, 400, 1};

      for (j = 0; j < NUM_SUFFIXES; j++)
	{
	  sprintf (token, "%s%s", 
		   bow_int2word (query_wv->entry[i].wi),
		   suffix[j]);
	  assert (strlen (token) < BOW_MAX_WORD_LENGTH);
	  new_query_wv->entry[i*NUM_SUFFIXES+j].wi = 
	    bow_word2int (token);
	  new_query_wv->entry[i*NUM_SUFFIXES+j].count = 
	    query_wv->entry[i].count * repetition[j];
	}
    }
  bow_wv_free (query_wv);
  query_wv = new_query_wv;
#endif

  bow_wv_set_weights (query_wv, arrow_barrel);
  bow_wv_normalize_weights (query_wv, arrow_barrel);

  /* If none of the words have a non-zero IDF, just return zero. */
  if (query_wv->normalizer == 0)
    return 0;

  /* Get the best matching documents. */
  actual_num_hits = bow_barrel_score (arrow_barrel, query_wv,
				      hits, num_hits_to_show,
				      -1);

  /* Print them. */
  fprintf (out, ",HITCOUNT %d\n", bow_tfidf_num_hit_documents);
  for (i = 0; i < actual_num_hits; i++)
    {
      bow_cdoc *cdoc = bow_array_entry_at_index (arrow_barrel->cdocs, 
						 hits[i].di);
#if 1
      /* Print both the filename and the words that appeared in that file. */
      fprintf (out, "%s  %f  %s\n", cdoc->filename, 
	       hits[i].weight, hits[i].name);
      if (hits[i].name)
	bow_free ((void*)hits[i].name);
#else
      printf ("\nHit number %d, with score %g\n", i, hits[i].weight);
      print_file (cdoc->filename);
#endif
    }
  fprintf (out, ".\n");
  fflush(out);

  return actual_num_hits;
}

/* Compare two documents by cosine similarity with TFIDF weights. */
void
arrow_compare (bow_wv *wv1, bow_wv *wv2)
{
  float score = 0;
  float score_increment;
  int wvi1, wvi2;
  bow_dv *dv;
  float idf;

#if 0
  bow_wv_set_weights_to_count_times_idf (wv1, arrow_barrel);
  bow_wv_set_weights_to_count_times_idf (wv2, arrow_barrel);
#else
  bow_wv_set_weights_to_count (wv1, (bow_barrel *) NULL);
  bow_wv_set_weights_to_count (wv2, (bow_barrel *) NULL);
#endif
  bow_wv_normalize_weights_by_vector_length (wv1);
  bow_wv_normalize_weights_by_vector_length (wv2);

  /* Loop over all words in WV1, summing the score. */
  for (wvi1 = 0, wvi2 = 0; wvi1 < wv1->num_entries; wvi1++)
    {
      /* Find the WV index of this word in WV2. */
      while (wv2->entry[wvi2].wi < wv1->entry[wvi1].wi
	     && wvi2 < wv2->num_entries)
	wvi2++;
      
      /* If we found the word, add the produce to the score. */
      if (wv1->entry[wvi1].wi == wv2->entry[wvi2].wi
	  && wvi2 < wv2->num_entries)
	{
	  dv = bow_wi2dvf_dv (arrow_barrel->wi2dvf, wv1->entry[wvi1].wi);
	  if (dv)
	    idf = dv->idf;
	  else
	    idf = 0;
	  score_increment = ((wv1->entry[wvi1].weight * wv1->normalizer)
			     * (wv2->entry[wvi2].weight * wv2->normalizer));
	  score += score_increment;
	  if (bow_print_word_scores)
	    fprintf (stderr, "%10.8f   (%3d,%3d, %8.4f)  %-30s   %10.8f\n", 
		     score_increment,
		     wv1->entry[wvi1].count,
		     wv2->entry[wvi2].count,
		     idf,
		     bow_int2word (wv1->entry[wvi1].wi),
		     score);
	}
    }
  printf ("%g\n", score);
}


void
arrow_socket_init (const char *socket_name, int use_unix_socket)
{
  int servlen, type, bind_ret;
  struct sockaddr_in in_addr;
  struct sockaddr *sap;

  type = use_unix_socket ? AF_UNIX : AF_INET;
   
  arrow_sockfd = socket (type, SOCK_STREAM, 0);
  assert (arrow_sockfd >= 0);

  if (type == AF_UNIX)
    {
#ifdef WINNT
      servlen = 0;  /* so that the compiler is happy */
      sap = 0;
      assert(WINNT == 0);
#else /* !WINNT */
      struct sockaddr_un un_addr;

      sap = (struct sockaddr *)&un_addr;
      bzero ((char *)sap, sizeof (un_addr));
      strcpy (un_addr.sun_path, socket_name);
      servlen = strlen (un_addr.sun_path) + sizeof(un_addr.sun_family) + 1;
#endif /* WINNT */
    }
  else
    {
      sap = (struct sockaddr *)&in_addr;
      bzero ((char *)sap, sizeof (in_addr));
      in_addr.sin_port = htons (atoi (socket_name));
      in_addr.sin_addr.s_addr = htonl (INADDR_ANY);
      servlen = sizeof (in_addr);
    }

  sap->sa_family = type;     

  bind_ret = bind (arrow_sockfd, sap, servlen);
  assert (bind_ret >= 0);

  bow_verbosify (bow_progress, "Listening on port %d\n", atoi (socket_name));
  listen (arrow_sockfd, 5);
}


/* We assume that commands are no longer than 1024 characters in length */
/* At the moment, we assume that the only possible command is ",HITS <num>" */
void
arrow_process_commands (FILE *fd, int *num_hits)
{
  int first;
  char buf[1024];

  /* checks the first character of the line */
  while ((first = fgetc(fd)))
    {
      if (first != ',')
      {
        ungetc (first, fd);
        return;
      }

      /* retrieves the rest of the line */
      fgets ((char *) buf, 1024, fd);
      sscanf (buf, "HITS %d", num_hits);
    }
}


void
arrow_serve ()
{
  int newsockfd, clilen;
  struct sockaddr cli_addr;
  FILE *in, *out;
  int num_hits_to_show;
  int pid;
 
  clilen = sizeof (cli_addr);
  newsockfd = accept (arrow_sockfd, &cli_addr, &clilen);
  
  if (newsockfd == -1)
    bow_error ("Not able to accept connections!\n");

  bow_verbosify (bow_progress, "Accepted connection\n");

  if (arrow_arg_state.serve_with_forking)
    {
      if ((pid = fork()) != 0)
      {
        /* parent - return to server mode */
        close (newsockfd);
        return;
      }
    }

  assert(newsockfd >= 0);

  in = fdopen (newsockfd, "r");
  out = fdopen (newsockfd, "w");

  /* Get the number of hits to show */
  num_hits_to_show = arrow_arg_state.num_hits_to_show;

  bow_verbosify (bow_progress, "Processing special commands...\n");

  /* Strips any special commands from the beginning of the stream */
  arrow_process_commands (in, &num_hits_to_show);

  bow_verbosify (bow_progress, "Processing query...\n");

  while (!feof(in))
    arrow_query (in, out, num_hits_to_show);

  fclose(in);
  fclose(out);

  close(newsockfd);

  bow_verbosify (bow_progress, "Closed connection:");
 
  /* Kill the child - don't want it hanging around, sucking up memory :) */
  if (arrow_arg_state.serve_with_forking)
    exit(0);
}

/* Beware of quickly written spaghetti code! */
void
arrow_serve2 ()
{
  int newsockfd, clilen;
  struct sockaddr cli_addr;
  FILE *in, *out;
  int num_hits_to_show, actual_num_hits;
  int pid;
  char cmdbuf[128];
  char filename[256];
  char *query;
  bow_wv *query_wv;
  bow_score *hits;
  int hi;

  hits = alloca (sizeof (bow_score) * arrow_barrel->cdocs->length);
 
  clilen = sizeof (cli_addr);
  newsockfd = accept (arrow_sockfd, &cli_addr, &clilen);
  
  if (newsockfd == -1)
    bow_error ("Not able to accept connections!\n");

  bow_verbosify (bow_progress, "Accepted connection\n");

  if (arrow_arg_state.serve_with_forking)
    {
      if ((pid = fork()) != 0)
      {
        /* parent - return to server mode */
        close (newsockfd);
        return;
      }
    }

  assert(newsockfd >= 0);

  in = fdopen (newsockfd, "r");
  out = fdopen (newsockfd, "w");


  /* Read in the first word from the input.  It is expected to be a
     command: either "rank" or "query". */
 again:
  if (fscanf (in, "%s", cmdbuf) != 1)
    goto done;

  fprintf (stderr, "Doing command `%s'\n", cmdbuf);
  if (strcmp ("query", cmdbuf) == 0)
    {
      filename[0] = '\0';
      fscanf (in, "%a[^\r\n]", &query);
      fprintf (stderr, "Got query `%s'\n", query);
#if 0
      fprintf (stderr, "`query' command not yet handled!\n");
      free (query);
      goto again;
#endif
    }
  else if (strcmp ("rank", cmdbuf) == 0)
    {
      fscanf (in, "%s", filename);
      fscanf (in, "%a[^\r\n]", &query);
      fprintf (stderr, "Got filename `%s'\n", filename);
      fprintf (stderr, "Got query `%s'\n", query);
    }
  else if (strcmp ("quit", cmdbuf) == 0)
    {
      goto done;
    }
  else if (strcmp ("help", cmdbuf) == 0)
    {
      fprintf (out, "<?xml version='1.0' encoding='US-ASCII' ?>\n"
	       "<arrow-result><help>\n"
	       "   Commands available to you\n"
	       "   help                      print this message\n"
	       "   rank <filename> <query>   give rank of <filename> <query>'s results\n"
	       "   query <query>             search for <str>\n"
	       "</help></arrow-result>\n.\n");
      fflush (out);
      fscanf (in, "%a[^\r\n]", &query);
      free (query);
      goto again;
    }
  else
    {
      bow_verbosify (bow_progress, "Unrecognized command `%s'.  "
		     "Closing connection.\n",
		     cmdbuf);
      goto done;
    }

  /* Create a word vector from the query string. */
  query_wv = bow_wv_new_from_text_string (query);
  if (query_wv == NULL)
    {
      actual_num_hits = 0;
      goto print;
    }
  fprintf (stderr, "Query WV has length %d\n", query_wv->num_entries);
  free (query);

  if (filename[0])
    num_hits_to_show = arrow_barrel->cdocs->length;
  else
    num_hits_to_show = arrow_arg_state.num_hits_to_show;

  bow_wv_set_weights (query_wv, arrow_barrel);
  /* If none of the words have a non-zero IDF, just return zero. */
  if (bow_wv_weight_sum (query_wv) == 0)
    {
      actual_num_hits = 0;
      goto print;
    }
  bow_wv_normalize_weights (query_wv, arrow_barrel);


  /* Get the best matching documents. */
  actual_num_hits = bow_barrel_score (arrow_barrel, query_wv,
				      hits, num_hits_to_show, -1);
 print:
  fprintf (stderr, "Got %d hits\n", actual_num_hits);
  if (filename[0])
    {
      /* Handle a "rank" command */
      int rank, hi;
      bow_cdoc *cdoc;
      int count = actual_num_hits;
      for (rank = -1, hi = 0; hi < count; hi++)
	{
	  cdoc = bow_array_entry_at_index (arrow_barrel->cdocs, hits[hi].di);
	  if (strcmp (cdoc->filename, filename) == 0)
	    {
	      rank = hi;
	      break;
	    }
	}
      fprintf (out, "<?xml version='1.0' encoding='US-ASCII' ?>\n"
 	       "<arrow-result>\n"
 	       "<rank-result>\n"
 	       "  <count>%d</count>\n", 
 	       count);
      if (rank != -1)
	fprintf (out, "  <rank>%d</rank>\n", rank);
      fprintf (out, "</rank-result>\n"
	       "</arrow-result>\n.\n");
    }
  else
    {
      /* Handle a "query" command */
      fprintf (out, "<?xml version='1.0' encoding='US-ASCII' ?>\n"
	       "<arrow-result>\n"
	       "<hitlist>\n"
	       "<count>%d</count>\n", 
	       actual_num_hits);
      for (hi = 0; hi < actual_num_hits; hi++)
	{
	  fprintf (out, 
		   "<hit>\n"
		   "   <id>%d</id>\n"
		   "   <name>%s</name>\n"
		   "   <score>%g</score>\n"
		   "</hit>\n",
		   hits[hi].di, hits[hi].name, hits[hi].weight);
	}
    }
  fflush (out);

  for (hi = 0; hi < actual_num_hits; hi++)
    if (hits[hi].name)
      bow_free ((void*)hits[hi].name);

  /* Handle another query */
  goto again;

 done:
  fclose(in);
  fclose(out);

  close(newsockfd);

  bow_verbosify (bow_progress, "Closed connection\n");
 
  /* Kill the child - don't want it hanging around, sucking up memory :) */
  if (arrow_arg_state.serve_with_forking)
    exit(0);
}

void
arrow_coo ()
{
  int wi;
  bow_wi2dvf *wicoo;
  int num_hides;

  num_hides = bow_wi2dvf_hide_words_by_doc_count (arrow_barrel->wi2dvf, 6);
  bow_verbosify (bow_progress, "%d words hidden\n", num_hides);
  wicoo = (bow_wi2dvf*) bow_wicoo_from_barrel (arrow_barrel);

#define PRINT_WORD_PROBS 1
#if PRINT_WORD_PROBS
  {
    bow_dv *dv;
    printf ("Word probabilities:\n");
    for (wi = 0; wi < wicoo->size; wi++)
      {
	dv = bow_wi2dvf_dv (wicoo, wi);
	if (dv) 
	  printf ("_uniform %-12.7f %s\n", dv->idf, bow_int2word (wi));
      }
  }
#endif /* PRINT_WORD_PROBS */

  for (wi = 0; wi < bow_num_words (); wi++)
    {
      /* printf ("%s  new word\n", bow_int2word (wi)); */
      bow_wicoo_print_word_entropy (wicoo, wi);
    }
}



/* The main() function. */

int
main (int argc, char *argv[])
{
  /* Prevents zombie children in System V environments */
  signal (SIGCHLD, SIG_IGN);

  /* Default command-line argument values */
  arrow_arg_state.num_hits_to_show = 10;
  arrow_arg_state.what_doing = arrow_indexing;
  arrow_arg_state.query_filename = NULL;
  arrow_arg_state.serve_with_forking = 0;

  /* Parse the command-line arguments. */
  argp_parse (&arrow_argp, argc, argv, 0, 0, &arrow_arg_state);

  if (arrow_arg_state.what_doing == arrow_indexing)
    {
      if (arrow_index (argc, argv))
	arrow_archive ();
      else
	bow_error ("No text documents found.");
    }
  else
    {
      arrow_unarchive ();
#if 0
      /* xxx */
      arrow_barrel->method = &bow_method_tfidf;
      bow_barrel_set_weights (arrow_barrel);
      bow_barrel_normalize_weights (arrow_barrel);
#endif

      if (arrow_arg_state.what_doing == arrow_querying)
	{
	  arrow_query (stdin, stdout, arrow_arg_state.num_hits_to_show);
	}
      else if (arrow_arg_state.what_doing == arrow_comparing)
	{
	  bow_wv *query_wv;
	  bow_wv *compare_wv;
	  FILE *fp;

	  /* The user must specify the query filename on the command line.
	     In this case it is not optional. */
	  assert (arrow_arg_state.query_filename);

	  /* Make word vectors from the files. */
	  fp = bow_fopen (arrow_arg_state.query_filename, "r");
	  query_wv = bow_wv_new_from_text_fp (fp,
					      arrow_arg_state.query_filename);
	  fclose (fp);
	  fp = bow_fopen (arrow_arg_state.compare_filename, "r");
	  compare_wv = bow_wv_new_from_text_fp
	    (fp, arrow_arg_state.compare_filename);
	  fclose (fp);

	  arrow_compare (query_wv, compare_wv);
	}
      else if (arrow_arg_state.what_doing == arrow_printing_idf)
	{
	  int wi;
	  int max_wi = MIN (arrow_barrel->wi2dvf->size, bow_num_words());
	  bow_dv *dv;

	  for (wi = 0; wi < max_wi; wi++)
	    {
	      dv = bow_wi2dvf_dv (arrow_barrel->wi2dvf, wi);
	      if (dv)
		printf ("%9f %s\n", dv->idf, bow_int2word (wi));
	    }
	}
      else if (arrow_arg_state.what_doing == arrow_query_serving)
	{
	  arrow_socket_init (arrow_arg_state.server_port_num, 0);
	  if (arrow_arg_state.serve_with_forking)
	    {
	      /*
	      int wi;
	      bow_dv *dv;
	      */
	      /* Touch all DV's so we read them into memory before forking */
	      /* This is *very bad* unless you are dealing with a small
	       * model or need maximum performance! */
	      /*
	      for (wi = 0; wi < arrow_barrel->wi2dvf->size; wi++)
		dv = bow_wi2dvf_dv (arrow_barrel->wi2dvf, wi);
	      */
	    }

	  while (1)
	    arrow_serve2 ();
	}
      else if (arrow_arg_state.what_doing == arrow_printing_coo)
	{
	  arrow_coo ();
	}
      else
	bow_error ("Internal error");
    }

  exit (0);
}
