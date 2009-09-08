/* archer - a document retreival front-end to libbow. */

/* Copyright (C) 1998, 1999 Andrew McCallum

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

#define _FILE_OFFSET_BITS 64

#include <bow/libbow.h>
#include <argp.h>
#include <bow/archer.h>
#include <errno.h>		/* needed on DEC Alpha's */
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <netinet/in.h>
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <signal.h>
#include <unistd.h>
#include <fcntl.h>
#include <ctype.h>
#include <limits.h>
#include <sys/mman.h>

/* The version number of this program. */
#define ARCHER_MAJOR_VERSION 0
#define ARCHER_MINOR_VERSION 0


/* Global variables */

/* The document/word/position matrix */
bow_wi2pv *archer_wi2pv;

/* The list of documents. */
bow_sarray *archer_docs;

/* The file descriptor of the socket on which we can act as a query-server. */
int archer_sockfd;

/* The variables that are set by command-line options. */
struct archer_arg_state
{
  /* What this invocation of archer to do? */
  void (*what_doing)();
  int non_option_argi;
  int num_hits_to_print;
  FILE *query_out_fp;
  const char *dirname;
  const char *query_string;
  const char *server_port_num;
  int serve_with_forking;
  int score_is_raw_count;
} archer_arg_state;



/* Functions for creating, reading, writing a archer_doc */

int
archer_doc_write (archer_doc *doc, FILE *fp)
{
  int ret;

  ret = bow_fwrite_int (doc->tag, fp);
  ret += bow_fwrite_int (doc->word_count, fp);
  ret += bow_fwrite_int (doc->di, fp);
  return ret;
}

int
archer_doc_read (archer_doc *doc, FILE *fp)
{
  int ret;
  int tag;

  ret = bow_fread_int (&tag, fp);
  doc->tag = tag;
  ret += bow_fread_int (&(doc->word_count), fp);
  ret += bow_fread_int (&(doc->di), fp);
  return ret;
}

void
archer_doc_free (archer_doc *doc)
{
}



/* Writing and reading the word/document stats to disk. */

/* Write the stats in the directory DATA_DIRNAME. */
void
archer_archive ()
{
  char filename[BOW_MAX_WORD_LENGTH];
  FILE *fp;

  sprintf (filename, "%s/vocabulary", bow_data_dirname);
  fp = bow_fopen (filename, "wb");
  bow_words_write (fp);
  fclose (fp);

  sprintf (filename, "%s/wi2pv", bow_data_dirname);
  bow_wi2pv_write_to_filename (archer_wi2pv, filename);

  sprintf (filename, "%s/docs", bow_data_dirname);
  fp = bow_fopen (filename, "wb");
  bow_sarray_write (archer_docs, (int(*)(void*,FILE*))archer_doc_write, fp);
  fclose (fp);

  fflush (archer_wi2pv->fp);
}

/* Read the stats from the directory DATA_DIRNAME. */
void
archer_unarchive ()
{
  char filename[BOW_MAX_WORD_LENGTH];
  FILE *fp;

  bow_verbosify (bow_progress, "Loading data files...");

  sprintf (filename, "%s/vocabulary", bow_data_dirname);
  bow_words_read_from_file (filename);

  sprintf (filename, "%s/wi2pv", bow_data_dirname);
  archer_wi2pv = bow_wi2pv_new_from_filename (filename);

  sprintf (filename, "%s/docs", bow_data_dirname);
  fp = bow_fopen (filename, "rb");
  archer_docs = 
    bow_sarray_new_from_data_fp ((int(*)(void*,FILE*))archer_doc_read, 
				archer_doc_free, fp);
  fclose (fp);

  bow_verbosify (bow_progress, "\n");
}

int
archer_index_filename (const char *filename, void *unused)
{
  int di;
  archer_doc doc, *doc_ptr;
  int wi;
  int pi = 0;
  char word[BOW_MAX_WORD_LENGTH];
#define USE_FAST_LEXER 1
#if !USE_FAST_LEXER
  bow_lex *lex;
  FILE *fp;
#endif

  /* Make sure this file isn't already in the index.  If it is just
     return (after undeleting it, if necessary. */
  doc_ptr = bow_sarray_entry_at_keystr (archer_docs, filename);
  if (doc_ptr)
    {
      if (doc_ptr->word_count < 0)
	doc_ptr->word_count = -(doc_ptr->word_count);
      return 1;
    }

  /* The index of this new document is the next available index in the
     array of documents. */
  di = archer_docs->array->length;

#if !USE_FAST_LEXER
  fp = fopen (filename, "r");
  if (fp == NULL)
    {
      perror ("bow_fopen");
      return 0;
    }
  /* NOTE: This will read just the first document from the file. */
  lex = bow_default_lexer->open_text_fp (bow_default_lexer, fp, filename);
  if (lex == NULL)
    {
      fclose (fp);
      return 0;
    }
  while (bow_default_lexer->get_word (bow_default_lexer,
				      lex, word, BOW_MAX_WORD_LENGTH))
    {
      wi = bow_word2int_add_occurrence (word);
      if (wi < 0)
	continue;
      bow_wi2pv_add_wi_di_pi (archer_wi2pv, wi, di, pi);
#if 0
      /* Debugging */
      {
	int di_read, pi_read;
	bow_wi2pv_wi_next_di_pi (archer_wi2pv, wi, &di_read, &pi_read);
	assert (di_read == di);
	assert (pi_read == pi);
	if (di == 0)
	  printf ("%010d %010d %s\n", di, pi, bow_int2word (wi));
      }
#endif
      pi++;
    }
  bow_default_lexer->close (bow_default_lexer, lex);
  fclose (fp);

#else /* USE_FAST_LEXER */
  {
    int fd, c, wordlen;
    //bow_strtrie *strie;
    //int strtrie_index;
    unsigned hashid;
    char *docbuf;
    char *docbufptr;
    char *docbufptr_end;
    //size_t page_size = (size_t) sysconf (_SC_PAGESIZE);
    struct stat statbuf;

    if (!word_map)
      bow_words_set_map (NULL, 0);

    fd = open (filename, O_RDONLY);
    if (fd == -1)
      {
				perror ("archer index_filename open");
				return 0;
      }
    fstat (fd, &statbuf);
    //statbuf.st_size = 20 * 1024;
    docbuf = mmap (NULL, statbuf.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
    close (fd);
    if (docbuf == (void*)-1)
      {
				fprintf (stderr, "\narcher index_filename(%s)\n", filename);
				perror (" mmap");
				return 0;
      }
    docbufptr_end = docbuf + statbuf.st_size;

    /* One time through this loop for each word */
    for (docbufptr = docbuf;;)
      {
	hashid = wordlen = 0;
	/* Ignore characters until we get a beginning character. */
	while (!isalpha((unsigned)*docbufptr))
	  if (++docbufptr >= docbufptr_end)
	    goto done_with_file;
	/* Add alphabetics to the word */
	do
	  {
	    c = tolower((unsigned)*docbufptr);
	    word[wordlen++] = c;
	    /* The following must exactly match the behavior of
               int4str.c:_str2id */
	    hashid = 131 * hashid + c;
	    docbufptr++;
	  }
	while (wordlen < BOW_MAX_WORD_LENGTH
	       && isalnum((unsigned)*docbufptr));
	if (wordlen == BOW_MAX_WORD_LENGTH)
	  {
	    /* Word is longer than MAX, consume it and skip it. */
	    while (isalpha(*docbufptr++))
	      ;
	    continue;
	  }
	word[wordlen] = '\0';
	/* Token is now in WORD; next see if it's too short or a stopword */
	if (wordlen < 2 
	    || wordlen > 30
	    || bow_stoplist_present_hash (word, hashid))
	  continue;
	/* Get the integer index of the word in WORD */
	wi = _bow_str2int (word_map, word, hashid);
	bow_wi2pv_add_wi_di_pi (archer_wi2pv, wi, di, pi);
	if (docbufptr >= docbufptr_end)
	  break;
	pi++;
      }
  done_with_file:
    munmap (docbuf, statbuf.st_size);
  }
#endif /* USE_FAST_LEXER */

  doc.tag = bow_doc_train;
  doc.word_count = pi;
  doc.di = di;
  bow_sarray_add_entry_with_keystr (archer_docs, &doc, filename);
      
  if (di % 200 == 0)
    bow_verbosify (bow_progress, "\r%8d |V|=%10d", di, bow_num_words());

  di++;
  return pi;
}


void
archer_index ()
{
  archer_docs = bow_sarray_new (0, sizeof (archer_doc), archer_doc_free);
  archer_wi2pv = bow_wi2pv_new (0, "pv");
  bow_verbosify (bow_progress, "Indexing files:              ");
  bow_map_filenames_from_dir (archer_index_filename, NULL,
			      archer_arg_state.dirname, "");
  bow_verbosify (bow_progress, "\n");

  archer_archive ();
  /* To close the FP for FILENAME_PV */
  bow_wi2pv_free (archer_wi2pv);
}

/* Index each line of ARCHER_ARG_STATE.DIRNAME as if it were a
   separate file, named after the line number. */
void
archer_index_lines ()
{
  static const int max_line_length = 2048;
  char buf[max_line_length];
  FILE *fp;
  archer_doc doc;
  bow_lex *lex;
  char word[BOW_MAX_WORD_LENGTH];
  int wi, di, pi;
  char filename[1024];

  archer_docs = bow_sarray_new (0, sizeof (archer_doc), archer_doc_free);
  archer_wi2pv = bow_wi2pv_new (0, "pv");
  fp = bow_fopen (archer_arg_state.dirname, "r");
  bow_verbosify (bow_progress, "Indexing lines:              ");
  while (fgets (buf, max_line_length, fp))
    {
      lex = bow_default_lexer->open_str (bow_default_lexer, buf);
      if (lex == NULL)
	continue;
      di = archer_docs->array->length;
      sprintf (filename, "%08d", di);
      pi = 0;
      while (bow_default_lexer->get_word (bow_default_lexer,
					  lex, word, BOW_MAX_WORD_LENGTH))
	{
	  wi = bow_word2int_add_occurrence (word);
	  if (wi < 0)
	    continue;
	  bow_wi2pv_add_wi_di_pi (archer_wi2pv, wi, di, pi);
	}
      bow_default_lexer->close (bow_default_lexer, lex);
      doc.tag = bow_doc_train;
      doc.word_count = pi;
      doc.di = di;
      bow_sarray_add_entry_with_keystr (archer_docs, &doc, filename);
      pi++;
    }
  fclose (fp);
  bow_verbosify (bow_progress, "\n");

  archer_archive ();
  /* To close the FP for FILENAME_PV */
  bow_wi2pv_free (archer_wi2pv);
}

/* Set the special flag in FILENAME's doc structure indicating that
   this document has been removed from the index.  Return zero on
   success, non-zero on failure. */
int
archer_delete_filename (const char *filename)
{
  archer_doc *doc;

  doc = bow_sarray_entry_at_keystr (archer_docs, filename);
  if (doc)
    {
      doc->word_count = -(doc->word_count);
      return 0;
    }
  return 1;
}

bow_wa *
archer_query_hits_matching_wi (int wi, int *occurrence_count)
{
  int count = 0;
  int di, pi;
  bow_wa *wa;

  if (wi >= archer_wi2pv->entry_count && archer_wi2pv->entry[wi].word_count <= 0)
    return NULL;
  wa = bow_wa_new (0);
  bow_pv_rewind (&(archer_wi2pv->entry[wi]), archer_wi2pv->fp);
  bow_wi2pv_wi_next_di_pi (archer_wi2pv, wi, &di, &pi);
  while (di != -1)
    {
      bow_wa_add_to_end (wa, di, 1);
      count++;
      bow_wi2pv_wi_next_di_pi (archer_wi2pv, wi, &di, &pi);
    }
  *occurrence_count = count;
  return wa;
}

/* Temporary constant.  Fix this soon! */
#define MAX_QUERY_WORDS 50

bow_wa *
archer_query_hits_matching_sequence (const char *query_string,
				     const char *suffix_string)
{
  int query[MAX_QUERY_WORDS];		/* WI's in the query */
  int di[MAX_QUERY_WORDS];
  int pi[MAX_QUERY_WORDS];
  int query_len;
  int max_di, max_pi;
  int wi, i;
  bow_lex *lex;
  char word[BOW_MAX_WORD_LENGTH];
  int sequence_occurrence_count = 0;
  int something_was_greater_than_max;
  bow_wa *wa;
  float scaler;
  archer_doc *doc;

  /* Parse the query */
  lex = bow_default_lexer->open_str (bow_default_lexer, (char*)query_string);
  if (lex == NULL)
    return NULL;
  query_len = 0;
  while (bow_default_lexer->get_word (bow_default_lexer, lex,
				      word, BOW_MAX_WORD_LENGTH))
    {
      /* Add the field-restricting suffix string, e.g. "xxxtitle" */
      if (suffix_string[0])
	{
	  strcat (word, "xxx");
	  strcat (word, suffix_string);
	  assert (strlen (word) < BOW_MAX_WORD_LENGTH);
	}
      wi = bow_word2int_no_add (word);
      if (wi >= 0)
	{
	  di[query_len] = pi[query_len] = -300;
	  query[query_len++] = wi;
	}
      else if ((bow_lexer_stoplist_func
		&& !(*bow_lexer_stoplist_func) (word))
	       || (!bow_lexer_stoplist_func
		   && strlen (word) < 2))
	{
	  /* If a query term wasn't present, and its not because it
	     was in the stoplist or the word is a single char, then
	     return no hits. */
	  query_len = 0;
	  break;
	}
      /* If we have no more room for more query terms, just don't use
         the rest of them. */
      if (query_len >= MAX_QUERY_WORDS)
	break;
    }
  bow_default_lexer->close (bow_default_lexer, lex);
  if (query_len == 0)
    return NULL;

  if (query_len == 1)
    {
      wa = archer_query_hits_matching_wi (query[0], 
					  &sequence_occurrence_count);
      goto search_done;
    }

  /* Initialize the array of document scores */
  wa = bow_wa_new (0);

  /* Search for documents containing the query words in the same order
     as the query. */
  bow_wi2pv_rewind (archer_wi2pv);
  max_di = max_pi = -200;
  /* Loop while we look for matches.  We'll break out of this loop when
     any of the query words are at the end of their PV's. */
  for (;;)
    {
      /* Keep reading DI and PI from one or more of the query-word PVs
	 until none of the DIs or PIs is greater than the MAX_DI or
	 MAX_PI.  At this point the DIs and PI's should all be equal,
	 indicating a match.  Break out of this loop if all PVs are
	 at the end, (at which piont they return -1 for both DI and
	 PI). */
      do
	{
	  something_was_greater_than_max = 0;
	  for (i = 0; i < query_len; i++)
	    {
	      /* Keep looking for instances of word query[wi] */
	      while (di[i] != -1
		  && (di[i] < max_di
		      || (di[i] <= max_di && pi[i] < max_pi)))
		{
		  bow_wi2pv_wi_next_di_pi (archer_wi2pv, query[i],
					   &(di[i]), &(pi[i]));

		  /* If any of the query words is at the end of their
		     PV, then we're not going to find any more
		     matches, and we're done setting the scores.  Go
		     print the matching documents. */
		  if (di[i] == -1)
		    goto search_done;

		  /* Make it so that all PI's will be equal if the words
		     are in order. */
		  pi[i] -= i;
		  bow_verbosify (bow_verbose, "%20s %10d %10d %10d %10d\n", 
				 bow_int2word (query[i]), 
				 di[i], pi[i], max_di, max_pi);
		}
	      if (di[i] > max_di) 
		{
		  max_di = di[i];
		  max_pi = pi[i];
		  something_was_greater_than_max = 1;
		}
	      else if (pi[i] > max_pi && di[i] == max_di) 
		{
		  max_pi = pi[i];
		  something_was_greater_than_max = 1;
		}
	    }
	}
      while (something_was_greater_than_max);
      bow_verbosify (bow_verbose, 
		     "something_was_greater_than_max di=%d\n", di[0]);
      for (i = 1; i < query_len; i++)
	assert (di[i] == di[0] && pi[i] == pi[0]);
      
      /* Make sure this DI'th document hasn't been deleted.  If it
         hasn't then add this DI to the WA---the list of hits */
      doc = bow_sarray_entry_at_index (archer_docs, di[0]);
      if (doc->word_count > 0)
	{
	  bow_wa_add_to_end (wa, di[0], 1);
	  sequence_occurrence_count++;
	}

      /* Set up so that next time through we'll read the next words
         from each PV. */
      for (i = 0; i < query_len; i++)
	{
	  if (di[i] != -1)
	    di[i] = -300;
	  if (pi[i] != -1)
	    pi[i] = -300;
	}
    }
 search_done:

  if (wa->length == 0)
    {
      bow_wa_free (wa);
      return NULL;
    }

  /* Scale the scores by the log of the occurrence count of this sequence,
     and take the log of the count (shifted) to encourage documents that
     have all query term to be ranked above documents that have many 
     repetitions of a few terms. */
  if (!archer_arg_state.score_is_raw_count)
    {
      double document_frequency = wa->length;
      scaler = 1.0 / log (5 + document_frequency);
      for (i = 0; i < wa->length; i++)
	wa->entry[i].weight = scaler * log (5 + wa->entry[i].weight);
    }

  return wa;
}

/* A temporary hack.  Also, does not work for queries containing
   repeated words */
void
archer_query ()
{
  int i;
  int num_hits_to_print;
#define NUM_FLAGS 3
  enum {pos = 0,
	reg,
	neg,
	num_flags};
  struct _word_hit {
    const char *term;
    bow_wa *wa;
    int flag;
  } word_hits[num_flags][MAX_QUERY_WORDS];
  int word_hits_count[num_flags];
  int current_wai[num_flags][MAX_QUERY_WORDS];
  struct _doc_hit {
    int di;
    float score;
    const char **terms;
    int terms_count;
  } *doc_hits;
  int doc_hits_count;
  int doc_hits_size;
  bow_wa *term_wa;
  int current_di, h, f, min_di;
  int something_was_greater_than_max;
  char *query_copy, *query_remaining, *end;
  char query_string[BOW_MAX_WORD_LENGTH];
  char suffix_string[BOW_MAX_WORD_LENGTH];
  int found_flag, flag, length;

  /* For sorting the combined list of document hits */
  int compare_doc_hits (struct _doc_hit *hit1, struct _doc_hit *hit2)
    {
      if (hit1->score < hit2->score)
	return 1;
      else if (hit1->score == hit2->score)
	return 0;
      else
	return -1;
    }

  void archer_sort_hits (struct _doc_hit *hits, int hits_count, 
			 int num_to_sort)
    {
      int i, j,max_j;
      float max_score;
      struct _doc_hit tmp;
      /* Find the highest score NUM_TO_SORT times */
      for (i = 0; i < num_to_sort;  i++)
	{
	  /* Find the next highest score */
	  max_score = -FLT_MAX;
	  max_j = -1;
	  for (j = i; j < hits_count; j++)
	    {
	      if (hits[j].score > max_score)
		{
		  max_score = hits[j].score;
		  max_j = j;
		}
	    }
	  /* Move the high score into position */
	  assert (max_j >= 0);
	  tmp = hits[i];
	  hits[i] = hits[max_j];
	  hits[max_j] = tmp;
	}
    }
	

  /* Initialize the list of target documents associated with each term */
  for (i = 0; i < num_flags; i++)
    word_hits_count[i] = 0;

  /* Initialize the combined list of target documents */
  doc_hits_size = 1000;
  doc_hits_count = 0;
  doc_hits = bow_malloc (doc_hits_size * sizeof (struct _doc_hit));

  /* Process each term in the query.  Quoted sections count as one
     term here. */
  query_remaining = query_copy = strdup (archer_arg_state.query_string);
  assert (query_copy);
  /* Chop any trailing newline or carriage return. */
  end = strpbrk (query_remaining, "\n\r");
  if (end)
    *end = '\0';
  while (*query_remaining)
    {
      /* Find the beginning of the next query term, and record +/- flags */
      while (*query_remaining 
	     && (!isalnum ((unsigned char)*query_remaining)
		 && *query_remaining != ':'
		 && *query_remaining != '+'
		 && *query_remaining != '-'
		 && *query_remaining != '"'))
	query_remaining++;
      flag = reg;
      found_flag = 0;
      if (*query_remaining == '\0')
	{
	  break;
	}
      if (*query_remaining == '+')
	{
	  query_remaining++;
	  flag = pos;
	}
      else if (*query_remaining == '-')
	{
	  query_remaining++;
	  flag = neg;
	}

      /* See if there is a field-restricting tag here, and if so, deal
         with it */
      if ((end = strpbrk (query_remaining, ": \"\t"))
	  && *end == ':')
	{
	  /* The above condition ensures that a ':' appears before any
	     term-delimiters */
	  /* Find the end of the field-restricting suffix */
	  length = end - query_remaining;
	  assert (length < BOW_MAX_WORD_LENGTH);
	  /* Remember the suffix, and move ahead the QUERY_REMAINING */
	  memcpy (suffix_string, query_remaining, length);
	  suffix_string[length] = '\0';
	  query_remaining = end + 1;
	}
      else
	suffix_string[0] = '\0';

      /* Find the end of the next query term. */
      if (*query_remaining == '"')
	{
	  query_remaining++;
	  end = strchr (query_remaining, '"');
	}
      else
	{
	  end = strchr (query_remaining, ' ');
	}
      if (end == NULL)
	end = strchr (query_remaining, '\0');

      /* Put the next query term into QUERY_STRING and increment
         QUERY_REMAINING */
      length = end - query_remaining;
      length = MIN (length, BOW_MAX_WORD_LENGTH-1);
      memcpy (query_string, query_remaining, length);
      query_string[length] = '\0';
      if (*end == '"')
	query_remaining = end + 1;
      else
	query_remaining = end;
      if (length == 0)
	continue;
      /* printf ("%d %s\n", flag, query_string); */

      /* Get the list of documents matching the term */
      term_wa = archer_query_hits_matching_sequence (query_string, 
						     suffix_string);
      if (!term_wa)
	{
	  if (flag == pos)
	    /* A required term didn't appear anywhere.  Print nothing */
	    goto hit_combination_done;
	  else
	    continue;
	}

      word_hits[flag][word_hits_count[flag]].term = strdup (query_string);
      word_hits[flag][word_hits_count[flag]].wa = term_wa;
      word_hits[flag][word_hits_count[flag]].flag = flag;
      word_hits_count[flag]++;
      assert (word_hits_count[flag] < MAX_QUERY_WORDS);
      bow_verbosify (bow_progress, "%8d %s\n", term_wa->length, query_string);
    }

  /* Bring together the WORD_HITS[*], following the correct +/-
     semantics */
  current_di = 0;
  for (f = 0; f < num_flags; f++)
    for (h = 0; h < word_hits_count[f]; h++)
      current_wai[f][h] = 0;

 next_current_di:
  if (word_hits_count[pos] == 0)
    {
      /* Find a document in which a regular term appears, and align the
	 CURRENT_WAI[REG][*] to point to the document if exists in that list */
      min_di = INT_MAX;
      for (h = 0; h < word_hits_count[reg]; h++)
	{
	  if (current_wai[reg][h] != -1
	      && (word_hits[reg][h].wa->entry[current_wai[reg][h]].wi
		  < current_di))
	    {
	      if (current_wai[reg][h] < word_hits[reg][h].wa->length - 1)
		current_wai[reg][h]++;
	      else
		current_wai[reg][h] = -1;
	    }
	  assert (current_wai[reg][h] == -1
		  || (word_hits[reg][h].wa->entry[current_wai[reg][h]].wi
		      >= current_di));
	  if (current_wai[reg][h] != -1
	      && word_hits[reg][h].wa->entry[current_wai[reg][h]].wi < min_di)
	    min_di = word_hits[reg][h].wa->entry[current_wai[reg][h]].wi;
	}
      if (min_di == INT_MAX)
	goto hit_combination_done;
	
      current_di = min_di;
    }
  else
    {
      /* Find a document index in which all the +terms appear */
      /* Loop until current_wai[pos][*] all point to the same document index */
      do
	{
	  something_was_greater_than_max = 0;
	  for (h = 0; h < word_hits_count[pos]; h++)
	    {
	      while (word_hits[pos][h].wa->entry[current_wai[pos][h]].wi
		     < current_di)
		{
		  if (current_wai[pos][h] < word_hits[pos][h].wa->length - 1)
		    current_wai[pos][h]++;
		  else
		    /* We are at the end of a + list, and thus are done. */
		    goto hit_combination_done;
		}
	      if (word_hits[pos][h].wa->entry[current_wai[pos][h]].wi 
		  > current_di)
		{
		  current_di = 
		    word_hits[pos][h].wa->entry[current_wai[pos][h]].wi;
		  something_was_greater_than_max = 1;
		}
	    }
	}
      while (something_was_greater_than_max);
      /* At this point all the CURRENT_WAI[pos][*] should be pointing to the
	 same document.  Verify this. */
      for (h = 1; h < word_hits_count[pos]; h++)
	assert (word_hits[pos][h].wa->entry[current_wai[pos][h]].wi
		== word_hits[pos][0].wa->entry[current_wai[pos][0]].wi);
    }

  /* Make sure the CURRENT_DI doesn't appear in any of the -term lists. */
  for (h = 0; h < word_hits_count[neg]; h++)
    {
      /* Loop until we might have found the CURRENT_DI in this neg list */
      while (current_wai[neg][h] != -1
	     && (word_hits[neg][h].wa->entry[current_wai[neg][h]].wi
		 < current_di))
	{
	  if (current_wai[neg][h] < word_hits[neg][h].wa->length - 1)
	    current_wai[neg][h]++;
	  else
	    current_wai[neg][h] = -1;
	}
      if (word_hits[neg][h].wa->entry[current_wai[neg][h]].wi == current_di)
	{
	  current_di++;
	  goto next_current_di;
	}
    }

  /* Add this CURRENT_DI to the combinted list of hits in DOC_HITS */
  assert (current_di < archer_docs->array->length);
  doc_hits[doc_hits_count].di = current_di;
  doc_hits[doc_hits_count].score = 0;
  for (h = 0; h < word_hits_count[pos]; h++)
    doc_hits[doc_hits_count].score += 
      word_hits[pos][h].wa->entry[current_wai[pos][h]].weight;
  doc_hits[doc_hits_count].terms_count = 0;
  doc_hits[doc_hits_count].terms = bow_malloc (MAX_QUERY_WORDS*sizeof (char*));

  /* Add score value from the regular terms, if CURRENT_DI appears there */
  for (h = 0; h < word_hits_count[reg]; h++)
    {
      if (word_hits_count[pos] != 0)
	{
	  while (current_wai[reg][h] != -1
		 && (word_hits[reg][h].wa->entry[current_wai[reg][h]].wi
		     < current_di))
	    {
	      if (current_wai[reg][h] < word_hits[reg][h].wa->length - 1)
		current_wai[reg][h]++;
	      else
		current_wai[reg][h] = -1;
	    }
	}
      if (word_hits[reg][h].wa->entry[current_wai[reg][h]].wi
	  == current_di)
	{
	  doc_hits[doc_hits_count].score += 
	    word_hits[reg][h].wa->entry[current_wai[reg][h]].weight;
	  doc_hits[doc_hits_count].
	    terms[doc_hits[doc_hits_count].terms_count]
	    = word_hits[reg][h].term;
	  doc_hits[doc_hits_count].terms_count++;
	}
    }

  doc_hits_count++;
  if (doc_hits_count >= doc_hits_size)
    {
      doc_hits_size *= 2;
      doc_hits = bow_realloc (doc_hits, (doc_hits_size
					 * sizeof (struct _doc_hit)));
    }

  current_di++;
  goto next_current_di;

 hit_combination_done:

  if (doc_hits_count)
    {
      fprintf (archer_arg_state.query_out_fp, ",HITCOUNT %d\n", 
	       doc_hits_count);
      num_hits_to_print = MIN (doc_hits_count, 
			       archer_arg_state.num_hits_to_print);

      /* Sort the DOC_HITS list */
#if 1
      archer_sort_hits (doc_hits, doc_hits_count, num_hits_to_print);
#else
      qsort (doc_hits, doc_hits_count, sizeof (struct _doc_hit), 
	     (int(*)(const void*,const void*))compare_doc_hits);
#endif

      for (i = 0; i < num_hits_to_print; i++)
	{
	  fprintf (archer_arg_state.query_out_fp,
		   "%s %f ", bow_sarray_keystr_at_index (archer_docs, doc_hits[i].di), 
		   doc_hits[i].score);
	  for (h = 0; h < word_hits_count[pos]; h++)
	    fprintf (archer_arg_state.query_out_fp, 
		     "%s, ", word_hits[pos][h].term);
	  for (h = 0; h < doc_hits[i].terms_count-1; h++)
	    fprintf (archer_arg_state.query_out_fp, 
		     "%s, ", doc_hits[i].terms[h]);
	  h = doc_hits[i].terms_count - 1;
	  if (h >= 0)
	    fprintf (archer_arg_state.query_out_fp, 
		     "%s", doc_hits[i].terms[h]);
	  fprintf (archer_arg_state.query_out_fp, "\n");
	}
    }
  fprintf (archer_arg_state.query_out_fp, ".\n");
  fflush (archer_arg_state.query_out_fp);

  /* Free all the junk we malloc'ed */
  for (f = 0; f < num_flags; f++)
    for (h = 0; h < word_hits_count[f]; h++)
      bow_free ((char*)word_hits[f][h].term);
  for (h = 0; h < doc_hits_count; h++)
    bow_free (doc_hits[h].terms);
  bow_free (doc_hits);
  bow_free (query_copy);
}

/* Set up to listen for queries on a socket */
void
archer_query_socket_init (const char *socket_name, int use_unix_socket)
{
  int servlen, type, bind_ret;
  struct sockaddr_un un_addr;
  struct sockaddr_in in_addr;
  struct sockaddr *sap;

  type = use_unix_socket ? AF_UNIX : AF_INET;
  archer_sockfd = socket (type, SOCK_STREAM, 0);
  assert (archer_sockfd >= 0);
  if (type == AF_UNIX)
    {
      sap = (struct sockaddr *)&un_addr;
      bzero ((char *)sap, sizeof (un_addr));
      strcpy (un_addr.sun_path, socket_name);
      servlen = strlen (un_addr.sun_path) + sizeof(un_addr.sun_family) + 1;
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
  bind_ret = bind (archer_sockfd, sap, servlen);
  assert (bind_ret >= 0);
  bow_verbosify (bow_progress, "Listening on port %d\n", atoi (socket_name));
  listen (archer_sockfd, 5);
}


/* We assume that commands are no longer than 1024 characters in length */
/* At the moment, we assume that the only possible command is ",HITS <num>" */
void
archer_query_server_process_commands (FILE *fp, int doing_pre_fork_commands)
{
  int first;
  char buf[1024];
  int i;
  char s[1024];

  /* See if the first character of the line is the special char ',' 
     which indicates that this is a command line. */
  while ((first = fgetc (fp)))
    {
      if ((doing_pre_fork_commands && first != ';')
	  || (!doing_pre_fork_commands && first != ','))
	{
	  ungetc (first, fp);
	  return;
	}

      /* Retrieve the rest of the line, and process the command. */
      fgets ((char *) buf, 1024, fp);
      if (doing_pre_fork_commands) 
	{
	  if (sscanf (buf, "INDEX %1023s", s) == 1)
	    archer_index_filename (s, NULL);
	  else if (sscanf (buf, "DELETE %1023s", s) == 1)
	    archer_delete_filename (s);
	  else if (strstr (buf, "ARCHIVE") == buf)
	    archer_archive ();
	  else if (strstr (buf, "QUIT") == buf)
	    {
	      archer_archive ();
	      exit (0);
	    }
	  else
	    bow_verbosify (bow_progress,
			   "Unknown pre-fork command `%s'\n", buf);
	}
      else
	{
	  if (sscanf (buf, "HITS %d", &i) == 1)
	    archer_arg_state.num_hits_to_print = i;
	  else
	    bow_verbosify (bow_progress,
			   "Unknown post-fork command `%s'\n", buf);
	}
    }
}


void
archer_query_serve_one_query ()
{
  int newsockfd, clilen;
  struct sockaddr cli_addr;
  FILE *in, *out;
  int pid;
  char query_buf[BOW_MAX_WORD_LENGTH];
 
  clilen = sizeof (cli_addr);
  newsockfd = accept (archer_sockfd, &cli_addr, &clilen);
  if (newsockfd == -1)
    bow_error ("Not able to accept connections!\n");

  bow_verbosify (bow_progress, "Accepted connection\n");

  assert (newsockfd >= 0);
  in = fdopen (newsockfd, "r");
  out = fdopen (newsockfd, "w");
  archer_arg_state.query_out_fp = out;
  archer_arg_state.query_string = query_buf;

  archer_query_server_process_commands (in, 1);

  if (archer_arg_state.serve_with_forking)
    {
      if ((pid = fork()) != 0)
	{
	  /* parent - return to server mode */
	  fclose (in);
	  fclose (out);
	  close (newsockfd);
	  return;
	}
      else
	{
	  /* child - reopen the PV file so we get our own lseek() position */
	  bow_wi2pv_reopen_pv (archer_wi2pv);
	}
    }

  bow_verbosify (bow_progress, "Processing query...\n");
  while (!feof(in))
    {
      /* Strips any special commands from the beginning of the stream */
      archer_query_server_process_commands
	(in, archer_arg_state.serve_with_forking ? 0 : 1);

      fgets (query_buf, BOW_MAX_WORD_LENGTH, in);
      archer_query ();
    }

  fclose (in);
  fclose (out);
  close (newsockfd);
  bow_verbosify (bow_progress, "Closed connection.\n");
 
  /* Kill the child - don't want it hanging around, sucking up memory :) */
  if (archer_arg_state.serve_with_forking)
    exit (0);
}

void
archer_query_serve ()
{
  archer_query_socket_init (archer_arg_state.server_port_num, 0);
  for (;;)
    archer_query_serve_one_query ();
}

void
archer_print_all ()
{
  int wi;
  int di;
  int pi;

  bow_wi2pv_rewind (archer_wi2pv);
  for (wi = 0; wi < bow_num_words (); wi++)
    {
      for (;;)
	{
	  bow_wi2pv_wi_next_di_pi (archer_wi2pv, wi, &di, &pi);
	  if (di == -1)
	    break;
	  printf ("%010d %010d %s\n", di, pi, bow_int2word (wi));
	}
    }
}

void
archer_print_word_stats ()
{
  bow_wi2pv_print_stats (archer_wi2pv);
}


/* Definitions for using argp command-line processing */

const char *argp_program_version =
"archer " STRINGIFY(ARCHER_MAJOR_VERSION) "." STRINGIFY(ARCHER_MINOR_VERSION);

const char *argp_program_bug_address = "<mccallum@cs.cmu.edu>";

static char archer_argp_doc[] =
"Archer -- a document retrieval front-end to libbow";

static char archer_argp_args_doc[] = "[ARG...]";

enum {
  QUERY_SERVER_KEY = 3000,
  QUERY_FORK_SERVER_KEY,
  INDEX_LINES_KEY,
  SCORE_IS_RAW_COUNT_KEY,
};

static struct argp_option archer_options[] =
{
  {0, 0, 0, 0,
   "For building data structures from text files:", 1},
  {"index", 'i', "DIRNAME", 0,
   "Tokenize training documents found under DIRNAME, "
   "and save them to disk"},
  {"index-lines", INDEX_LINES_KEY, "FILENAME", 0,
   "Like --index, except index each line of FILENAME as if it were a "
   "separate document.  Documents are named after sequential line numbers."},

  {0, 0, 0, 0,
   "For doing document retreival using the data structures built with -i:", 2},
  {"query", 'q', "WORDS", 0, 
   "tokenize input from stdin [or FILE], then print document most like it"},
  {"query-server", QUERY_SERVER_KEY, "PORTNUM", 0,
   "Run archer in socket server mode."},
  {"query-forking-server", QUERY_FORK_SERVER_KEY, "PORTNUM", 0,
   "Run archer in socket server mode, forking a new process with every "
   "connection.  Allows multiple simultaneous connections."},
  {"num-hits-to-show", 'n', "N", 0,
   "Show the N documents that are most similar to the query text "
   "(default N=1)"},
  {"score-is-raw-count", SCORE_IS_RAW_COUNT_KEY, 0, 0,
   "Instead of using a weighted sum of logs, the score of a document "
   "will be simply the number of terms in both the query and the document."},

  {0, 0, 0, 0,
   "Diagnostics", 3},
  {"print-all", 'p', 0, 0,
   "Print, in unsorted order, all the document indices, positions and words"},
  {"print-word-stats", 's', 0, 0,
   "Print the number of times each word occurs."},

  { 0 }
};

static error_t
archer_parse_opt (int key, char *arg, struct argp_state *state)
{
  switch (key)
    {
    case 'q':
      archer_arg_state.what_doing = archer_query;
      archer_arg_state.query_string = arg;
      break;
    case 'i':
      archer_arg_state.what_doing = archer_index;
      archer_arg_state.dirname = arg;
      break;
    case INDEX_LINES_KEY:
      archer_arg_state.what_doing = archer_index_lines;
      archer_arg_state.dirname = arg;
      break;
    case 'p':
      archer_arg_state.what_doing = archer_print_all;
      break;
    case 'n':
      archer_arg_state.num_hits_to_print = atoi (arg);
      break;
    case 's':
      archer_arg_state.what_doing = archer_print_word_stats;
      break;
    case SCORE_IS_RAW_COUNT_KEY:
      archer_arg_state.score_is_raw_count = 1;
      break;
    case QUERY_FORK_SERVER_KEY:
      archer_arg_state.serve_with_forking = 1;
    case QUERY_SERVER_KEY:
      archer_arg_state.what_doing = archer_query_serve;
      archer_arg_state.server_port_num = arg;
      break;

    case ARGP_KEY_ARG:
      /* Now we consume all the rest of the arguments.  STATE->next is the
	 index in STATE->argv of the next argument to be parsed, which is the
	 first STRING we're interested in, so we can just use
	 `&state->argv[state->next]' as the value for ARCHER_ARG_STATE->ARGS.
	 IN ADDITION, by setting STATE->next to the end of the arguments, we
	 can force argp to stop parsing here and return.  */
      archer_arg_state.non_option_argi = state->next - 1;
      if (archer_arg_state.what_doing == archer_index
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

static struct argp archer_argp = 
{ archer_options, archer_parse_opt, archer_argp_args_doc,
  archer_argp_doc, bow_argp_children};


/* The main() function. */

int
main (int argc, char *argv[])
{
  /* Prevents zombie children in System V environments */
  signal (SIGCHLD, SIG_IGN);

  /* Default command-line argument values */
  archer_arg_state.what_doing = NULL;
  archer_arg_state.num_hits_to_print = 10;
  archer_arg_state.dirname = NULL;
  archer_arg_state.query_string = NULL;
  archer_arg_state.serve_with_forking = 0;
  archer_arg_state.query_out_fp = stdout;
  archer_arg_state.score_is_raw_count = 0;

  /* Parse the command-line arguments. */
  argp_parse (&archer_argp, argc, argv, 0, 0, &archer_arg_state);

  if (archer_arg_state.what_doing == NULL)
    bow_error ("No action specified on command-line.");
  if (*archer_arg_state.what_doing != archer_index
      && *archer_arg_state.what_doing != archer_index_lines)
    archer_unarchive ();

  (*archer_arg_state.what_doing) ();

  exit (0);
}
