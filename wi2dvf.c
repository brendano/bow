/* Word-index to document-vector-file */

/* Copyright (C) 1997, 1998, 1999, 2000 Andrew McCallum

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
#include <netinet/in.h>		/* for machine-independent byte-order */
#include <assert.h>
#include <string.h>

#define INIT_BOW_DVF(DVF) { DVF.seek_start = -1; DVF.dv = NULL; }

unsigned int bow_wi2dvf_default_capacity = 1024;

bow_wi2dvf *
bow_wi2dvf_new (int capacity)
{
  bow_wi2dvf *ret;
  int i;

  if (capacity == 0)
    capacity = bow_wi2dvf_default_capacity;
  ret = bow_malloc (sizeof (bow_wi2dvf) + (sizeof (bow_dvf) * capacity));
  ret->size = capacity;
  ret->num_words = 0;
  ret->fp = NULL;
  for (i = 0; i < capacity; i++)
    INIT_BOW_DVF(ret->entry[i]);
  return ret;
}

/* xxx We should think about a scheme that doesn't require keeping all
   the "document vectors" in core at the time time.  We could write
   them to disk, read them back in when we needed to add to them, then
   write them back out again.  We would need a nice caching scheme, as
   well as nice way to deal with "document vectors" that grow. */

/* Add a "word vector" WV, associated with "document index" DI, to 
   the map WI2DVF. */ 
void
bow_wi2dvf_add_di_wv (bow_wi2dvf **wi2dvf, int di, bow_wv *wv)
{
  int i, wi;
  int max_wi = bow_num_words ();

  if (max_wi > (*wi2dvf)->size)
    {
      /* There are so many unique words, we need to grow the array
	 that maps WI's to DVF's. */
      int wi = (*wi2dvf)->size;	        /* a "word index" */
      (*wi2dvf)->size = MAX (max_wi, (*wi2dvf)->size * 2);
      (*wi2dvf) = bow_realloc (*wi2dvf, 
			       (sizeof (bow_wi2dvf)
				+ (sizeof (bow_dvf) * (*wi2dvf)->size)));
      /* Initialize the new part of the realloc'ed space. */
      for ( ; wi < (*wi2dvf)->size; wi++)
	INIT_BOW_DVF((*wi2dvf)->entry[wi]);
    }

  /* Run down the "word vector", depositing each entry in the WI2DVF. */
  for (i = 0; i < wv->num_entries; i++)
    {
      wi = wv->entry[i].wi;
      assert ((*wi2dvf)->size > wi);
      if ((*wi2dvf)->entry[wi].dv == NULL)
	{
	  /* There is not yet a "document vector" for "word index" WI,
	     so create one. */
	  (*wi2dvf)->entry[wi].dv = bow_dv_new (0);
	  /* This 2 is a flag to the hide/unhide code that this DV exists. */
	  (*wi2dvf)->entry[wi].seek_start = 2;
	  ((*wi2dvf)->num_words)++;
	}
      /* Add the "document index" DI and the count associated with
         word index WI to the WI'th "document vector". */
      bow_dv_add_di_count_weight (&((*wi2dvf)->entry[wi].dv), di,
				  wv->entry[i].count, 
				  wv->entry[i].weight);
    }
}

/* Read all the words from character array DATA, and add them to WI2DVF,
   associated with document index DI. */
int
bow_wi2dvf_add_di_text_str (bow_wi2dvf **wi2dvf, int di, char *data,
			    const char *filename)
{
  char word[BOW_MAX_WORD_LENGTH]; /* buffer for reading and stemming words */
  int wi;			/* a word index */
  bow_lex *lex;
  int num_words = 0;

  lex = bow_default_lexer->open_str (bow_default_lexer, data);
  assert (lex);

  /* Loop once for each lexical token in this document. */
  while (bow_default_lexer->get_word (bow_default_lexer,
				      lex, word, BOW_MAX_WORD_LENGTH))
    {
      /* Find out the word's "index". */
      wi = bow_word2int_add_occurrence (word);
      if (wi < 0)
	continue;
      /* Increment our stats about this word/document pair. */
      bow_wi2dvf_add_wi_di_count_weight (wi2dvf, wi, di, 1, 1);
      /* Increment our count of the number of words in this document. */
      num_words++;
    }
  bow_default_lexer->close (bow_default_lexer, lex);
  return num_words;
}

/* Read all the words from file pointer FP, and add them to WI2DVF,
   associated with document index DI. */
int
bow_wi2dvf_add_di_text_fp (bow_wi2dvf **wi2dvf, int di, FILE *fp,
			   const char *filename)
{
  char word[BOW_MAX_WORD_LENGTH]; /* buffer for reading and stemming words */
  int wi;			/* a word index */
  bow_lex *lex;
  int num_words = 0;

  /* Loop once for each document in this file. */
  while ((lex = bow_default_lexer->open_text_fp (bow_default_lexer, fp,
						 filename)))
    {
      /* Loop once for each lexical token in this document. */
      while (bow_default_lexer->get_word (bow_default_lexer,
					  lex, word, BOW_MAX_WORD_LENGTH))
	{
	  /* Find out the word's "index". */
	  wi = bow_word2int_add_occurrence (word);
	  if (wi < 0)
	    continue;
	  /* Increment our stats about this word/document pair. */
	  bow_wi2dvf_add_wi_di_count_weight (wi2dvf, wi, di, 1, 1);
	  /* Increment our count of the number of words in this document. */
	  num_words++;
	}
      bow_default_lexer->close (bow_default_lexer, lex);
    }
  return num_words;
}

/* In the map WI2DVF, increase by COUNT and WEIGHT our record of the
   number times and weight that the document with "document index" DI
   contains the word with "word index" WI. */
void
bow_wi2dvf_add_wi_di_count_weight (bow_wi2dvf **wi2dvf, int wi,
				   int di, int count, float weight)
{
  if (wi >= (*wi2dvf)->size)
    {
      /* There are so many unique words, we need to grow the array
	 that maps WI's to DVF's. */
      int old_size = (*wi2dvf)->size; /* a "word vector" */
      (*wi2dvf)->size = MAX (wi+1, (*wi2dvf)->size * 2);
      (*wi2dvf) = bow_realloc (*wi2dvf, 
			       (sizeof (bow_wi2dvf)
				+ (sizeof (bow_dvf) * (*wi2dvf)->size)));
      /* Initialize the new part of the realloc'ed space. */
      for ( ; old_size < (*wi2dvf)->size; old_size++)
	INIT_BOW_DVF((*wi2dvf)->entry[old_size]);
    }
 
  /* Increment the stats for the WI/DI pair. */
  if ((*wi2dvf)->entry[wi].dv == NULL)
    {
      /* There is not yet a "document vector" for "word index" WI,
	 so create one. */
      (*wi2dvf)->entry[wi].dv = bow_dv_new (0);
      /* This 2 is a flag to the hide/unhide code that this DV exists. */
      (*wi2dvf)->entry[wi].seek_start = 2;
      ((*wi2dvf)->num_words)++;
    }
  /* Add the "document index" DI and the count associated with
     word index WI to the WI'th "document vector". */
  bow_dv_add_di_count_weight (&((*wi2dvf)->entry[wi].dv), di, count, weight);
}

/* In the map WI2DVF, set to COUNT and WEIGHT our record of the
   number times and weight that the document with "document index" DI
   contains the word with "word index" WI. */
void
bow_wi2dvf_set_wi_di_count_weight (bow_wi2dvf **wi2dvf, int wi,
				   int di, int count, float weight)
{
  if (wi >= (*wi2dvf)->size)
    {
      /* There are so many unique words, we need to grow the array
	 that maps WI's to DVF's. */
      int old_size = (*wi2dvf)->size; /* a "word vector" */
      (*wi2dvf)->size = MAX (wi+1, (*wi2dvf)->size * 2);
      (*wi2dvf) = bow_realloc (*wi2dvf, 
			       (sizeof (bow_wi2dvf)
				+ (sizeof (bow_dvf) * (*wi2dvf)->size)));
      /* Initialize the new part of the realloc'ed space. */
      for ( ; old_size < (*wi2dvf)->size; old_size++)
	INIT_BOW_DVF((*wi2dvf)->entry[old_size]);
    }
 
  /* Increment the stats for the WI/DI pair. */
  if ((*wi2dvf)->entry[wi].dv == NULL)
    {
      /* There is not yet a "document vector" for "word index" WI,
	 so create one. */
      (*wi2dvf)->entry[wi].dv = bow_dv_new (0);
      /* This 2 is a flag to the hide/unhide code that this DV exists. */
      (*wi2dvf)->entry[wi].seek_start = 2;
      ((*wi2dvf)->num_words)++;
    }
  /* Add the "document index" DI and the count associated with
     word index WI to the WI'th "document vector". */
  bow_dv_set_di_count_weight (&((*wi2dvf)->entry[wi].dv), di, count, weight);
}



/* Return a pointer to the BOW_DE for a particular word/document pair, 
   or return NULL if there is no entry for that pair. */
bow_de *
bow_wi2dvf_entry_at_wi_di (bow_wi2dvf *wi2dvf, int wi, int di)
{
  bow_dv *dv = bow_wi2dvf_dv (wi2dvf, wi);

  if (!dv)
    return NULL;
  return bow_dv_entry_at_di (dv, di);
}

/* Remove the word with index WI from the vocabulary of the map WI2DVF */
void
bow_wi2dvf_remove_wi (bow_wi2dvf *wi2dvf, int wi)
{
  bow_dv *dv;
  assert (wi < wi2dvf->size);
  bow_error ("Don't call this function.  It's broken.");
  /* xxx This could be more efficient.  Avoid reading it in, just to free it */
  dv = bow_wi2dvf_dv (wi2dvf, wi);
  if (dv)
    {
      bow_dv_free (wi2dvf->entry[wi].dv);
      (wi2dvf->num_words)--;
    }
  INIT_BOW_DVF (wi2dvf->entry[wi]);
}

#define FREE_WHEN_HIDING_WI 0

/* Temporarily hide the word with index WI from the vocabulary of the
   map WI2DVF. The function BOW_WI2DVF_DV() will no longer see the entry
   for this WI, but */
void
bow_wi2dvf_hide_wi (bow_wi2dvf *wi2dvf, int wi)
{
  assert (wi < wi2dvf->size);
#if FREE_WHEN_HIDING_WI
  if (wi2dvf->entry[wi].dv)
    {
      bow_dv_free (wi2dvf->entry[wi].dv);
      /* (wi2dvf->num_words)--; */
    }
  wi2dvf->entry[wi].dv = NULL;
#endif

  /* The token -1 is reserved to mean that the DV is uninitialized. */
  assert (!(wi2dvf->entry[wi].dv && wi2dvf->entry[wi].seek_start == -1));

  /* Make the SEEK_START negative so we won't use it in normal situations,
     but will be able to remember it and get it back when we need it. */
  if (wi2dvf->entry[wi].seek_start > 0)
    {
      wi2dvf->entry[wi].seek_start = - (wi2dvf->entry[wi].seek_start);
      (wi2dvf->num_words)--;
    }
}


/* hide all the words that exist */
void
bow_wi2dvf_hide_all_wi (bow_wi2dvf *wi2dvf)
{
  int wi;

  for (wi = 0; wi < wi2dvf->size; wi++)
    {
      bow_dv *dv = bow_wi2dvf_dv (wi2dvf, wi);
      if (dv)
	bow_wi2dvf_hide_wi (wi2dvf, wi);
    }
}


/* unhide a specific word index */
void
bow_wi2dvf_unhide_wi (bow_wi2dvf *wi2dvf, int wi)
{
  assert (wi < wi2dvf->size);
  assert (wi2dvf->entry[wi].seek_start < -1);
  wi2dvf->entry[wi].seek_start = - (wi2dvf->entry[wi].seek_start);
  (wi2dvf->num_words)++;
}

/* Hide all words occuring in only COUNT or fewer number of documents.
   Return the number of words hidden. */
int
bow_wi2dvf_hide_words_by_doc_count (bow_wi2dvf *wi2dvf, int count)
{
  int wi;
  bow_dv *dv;
  int num_hides = 0;

  if (count == 0)
    return 0;

  for (wi = 0; wi < wi2dvf->size; wi++)
    {
      dv = bow_wi2dvf_dv (wi2dvf, wi);
      if (dv && dv->length <= count)
	{
	  bow_wi2dvf_hide_wi (wi2dvf, wi);
	  num_hides++;
	}
    }
  return num_hides;
}

/* Hide all words occuring in only COUNT or fewer times.
   Return the number of words hidden. */
int
bow_wi2dvf_hide_words_by_occur_count (bow_wi2dvf *wi2dvf, int count)
{
  int wi;
  bow_dv *dv;
  int num_hides = 0;

  if (count == 0)
    return 0;

  for (wi = 0; wi < wi2dvf->size; wi++)
    {
      dv = bow_wi2dvf_dv (wi2dvf, wi);
      if (dv && bow_words_occurrences_for_wi (wi) <= count)
	{
	  bow_wi2dvf_hide_wi (wi2dvf, wi);
	  num_hides++;
	}
    }
  return num_hides;
}

/* hide all words where the prefix of the word matches the given
   prefix */
int
bow_wi2dvf_hide_words_with_prefix (bow_wi2dvf *wi2dvf, char *prefix)
{
  int wi;
  int num_hides = 0;
  int prefix_len = strlen (prefix);
  bow_dv *dv;

  /* hide all words where the prefix of the word matches the given
     prefix */
  for (wi = 0; wi < wi2dvf->size; wi++)
    {
      dv = bow_wi2dvf_dv (wi2dvf, wi);
      if (dv && 0 == strncmp (prefix, bow_int2word (wi), prefix_len))
	{
	  bow_wi2dvf_hide_wi (wi2dvf, wi);
	  num_hides++;
	}
    }
  return num_hides;
}

/* hide all words where the prefix of the word doesn't match the given
   prefix */
int
bow_wi2dvf_hide_words_without_prefix (bow_wi2dvf *wi2dvf, char *prefix)
{
  int wi;
  int num_hides = 0;
  int prefix_len = strlen (prefix);
  bow_dv *dv;

  for (wi = 0; wi < wi2dvf->size; wi++)
    {
      dv = bow_wi2dvf_dv (wi2dvf, wi);
      if (dv && 0 != strncmp (prefix, bow_int2word (wi), prefix_len))
	{
	  bow_wi2dvf_hide_wi (wi2dvf, wi);
	  num_hides++;
	}
    }
  return num_hides;
}

/* Make visible all DVF's that were hidden with BOW_WI2DVF_HIDE_WI(). */
void
bow_wi2dvf_unhide_all_wi (bow_wi2dvf *wi2dvf)
{
  int wi;

  for (wi = 0; wi < wi2dvf->size; wi++)
    {
      if (wi2dvf->entry[wi].seek_start < -1)
	{
	  wi2dvf->entry[wi].seek_start = - (wi2dvf->entry[wi].seek_start);
	  (wi2dvf->num_words)++;
	}
    }
}

/* Set the WI2DVF->ENTRY[WI].IDF to the sum of the COUNTS for the
   given WI. */
void
bow_wi2dvf_set_idf_to_count (bow_wi2dvf *wi2dvf)
{
  int wi, nwi, dvi;
  bow_dv *dv;

  nwi = MIN (wi2dvf->size, bow_num_words());
  for (wi = 0; wi < nwi; wi++)
    {
      dv = bow_wi2dvf_dv (wi2dvf, wi);
      if (!dv)
	continue;
      dv->idf = 0;
      for (dvi = 0; dvi < dv->length; dvi++)
	dv->idf += dv->entry[dvi].count;
    }
}

/* Write WI2DVF to file-pointer FP, in a machine-independent format.
   This is the format expected by bow_wi2dvf_new_from_fp(). */
void
bow_wi2dvf_write (bow_wi2dvf *wi2dvf, FILE *fp)
{
  long seek_base;
  long seek_current;
  int wi;

  bow_wi2dvf_unhide_all_wi (wi2dvf);

  /* Figure out how many bytes the WI2DVF (without the DV's) will
     take at the beginning the file. */
  seek_base = 
    (ftell (fp)			/* Where we are starting */
     + (sizeof (int)		/* for the number of "word indices" */
	+ (sizeof (int)		/* for each SEEK_START value */
	   * wi2dvf->size)));	/* multiplied by the number of WI's */

  /* Write the maximum "word index". */
  bow_fwrite_int (wi2dvf->size, fp);

  /* Figure out the correct SEEK_START values for all the DVF's,
     set them in the DVF's data structure, and write out the DVF's
     SEEK_START information. */
  for (wi = 0, seek_current = seek_base; wi < wi2dvf->size; wi++)
    {
      if (wi2dvf->entry[wi].dv == NULL)
	{
	  /* Write an indication of a NULL document vector. */
	  bow_fwrite_int (-1, fp);
	  /* Set the SEEK_START in the data structure. */
	  wi2dvf->entry[wi].seek_start = -1;
	}
      else
	{
	  /* Write the DVF's SEEK_START info. */
	  bow_fwrite_int (seek_current, fp);
	  /* Set the SEEK_START in the data structure. */
	  wi2dvf->entry[wi].seek_start = seek_current;

	  /* Add the number of bytes it will take to write the
	     WI'th "document vector" */
	  seek_current += bow_dv_write_size (wi2dvf->entry[wi].dv);
	}
    }

  /* We have now finished writing the DVF seek information; we should 
     be at the position we calculated earlier for SEEK_BASE. */
  assert (ftell (fp) == seek_base);

  /* Now write the actual "document vector" information. */
  for (wi = 0; wi < wi2dvf->size; wi++)
    {
      if (wi2dvf->entry[wi].dv != NULL)
	{
	  /* Make sure we are at the same place in the file that
	     we said we'd be. */
	  assert (ftell (fp) == wi2dvf->entry[wi].seek_start);
	  bow_dv_write (wi2dvf->entry[wi].dv, fp);
	}
    }
}

/* Write WI2DVF to a file, in a machine-independent format.  This
   is the format expected by bow_wi2dvf_new_from_file(). */
void
bow_wi2dvf_write_data_file (bow_wi2dvf *wi2dvf, const char *filename)
{
  FILE *fp;
  if (!(fp = fopen (filename, "wb")))
    bow_error ("Couldn't open file `%s' for writing.", filename);
  bow_wi2dvf_write (wi2dvf, fp);
  fclose (fp);
}

/* Create a `wi2dvf' by reading data from file-pointer FP.  This
   doesn't actually read in all the "document vectors"; it only reads
   in the DVF information, and lazily loads the actually "document
   vectors". 
   NOTE:  Remember that this doesn't actually read the entire
   wi2dvf; it reads only the seek-table.  Don't try to read 
   something else after this. */
bow_wi2dvf *
bow_wi2dvf_new_from_data_fp (FILE *fp)
{
  int size;
  bow_wi2dvf *ret;
  int wi;

  /* Read the number of "word indices" used as keys in the new WI2DVF. */
  bow_fread_int (&size, fp);

  /* Create a new WI2DVF of that size.*/
  ret = bow_wi2dvf_new (size);
  ret->fp = fp;

  /* Read all the DVF information, but not the actual "document vectors";
     We'll do that later in bow_wi2dvf_dv(). */
  for (wi = 0; wi < size; wi++)
    {
      bow_fread_int (&(ret->entry[wi].seek_start), fp);
      if (ret->entry[wi].seek_start != -1)
	(ret->num_words)++;
      ret->entry[wi].dv = NULL;
    }

  return ret;
}

/* Create a `wi2dvf' by reading data from a file.  This doesn't actually 
   read in all the "document vectors"; it only reads in the DVF 
   information, and lazily loads the actually "document vectors". */
bow_wi2dvf *
bow_wi2dvf_new_from_data_file (const char *filename)
{
  FILE *fp;
  bow_wi2dvf *ret;

  if (!(fp = fopen (filename, "rb")))
    bow_error ("Couldn't open file `%s' for reading.", filename);
  ret = bow_wi2dvf_new_from_data_fp (fp);
  /* Don't close the FP because it will still be needed for 
     reading the "document vectors", DV's. */
  return ret;
}

/* Free the memory held by the map WI2DVF. */
void
bow_wi2dvf_free (bow_wi2dvf *wi2dvf)
{
  int i;

  if (wi2dvf->fp)
    fclose (wi2dvf->fp);
  for (i = 0; i < wi2dvf->size; i++)
    {
      if (wi2dvf->entry[i].dv)
	bow_dv_free (wi2dvf->entry[i].dv);
    }
  bow_free (wi2dvf);
}

/* Return the "document vector" corresponding to "word index" WI.  This
   function will read the "document vector" out of the file passed to
   bow_wi2dvf_new_from_file() if is hasn't been read already.  If the 
   DV has been "hidden" (by feature selection, for example) it will not
   be returned unless EVEN_IF_HIDDEN is non-zero. */
bow_dv *
bow_wi2dvf_dv_hidden (bow_wi2dvf *wi2dvf, int wi, int even_if_hidden)
{
  /* If the word-index is higher than anything we know about,
     return NULL.  This could legitimately happen if the query
     document has vocabulary that wasn't in the training data. */
  if (wi >= wi2dvf->size)
    return NULL;

  /* If the "document vector" is available (it has already been read
     in, it is non-NULL), and it is not hidden (it's SEEK_START is
     greater than or equal to -1) then simply return it.  Note that
     newly created WI2DVF's that haven't been saved (like those for
     VPC_BARREL's) with have non-NULL dv's and SEEK_START's of -1. */
  if (wi2dvf->entry[wi].dv 
      && (wi2dvf->entry[wi].seek_start >= -1
	  || even_if_hidden))
    {
      assert (wi2dvf->entry[wi].dv->idf == wi2dvf->entry[wi].dv->idf);
      return wi2dvf->entry[wi].dv;
    }

  /* If the SEEK_START position of WI'th DVF is -1, then this was an
     empty "document vector", so return NULL.  If the SEEK_START
     position of the WI'th DVF is less than -1, then this document
     vector was hidden by BOW_WI2DVF_HIDE_WI(), so return NULL. */
  if (wi2dvf->entry[wi].seek_start <= -1)
    return NULL;

  /* If we want to read it in, but if this WI2DVF isn't backed by a
     data file (for example, it's being built from a directory of
     text files), then just return NULL. */
  if (wi2dvf->fp == NULL)
    return NULL;

  /* Read in the document vector. */
  assert (wi2dvf->entry[wi].seek_start > 2);
  fseek (wi2dvf->fp, wi2dvf->entry[wi].seek_start, SEEK_SET);
  wi2dvf->entry[wi].dv = bow_dv_new_from_data_fp (wi2dvf->fp);
  /* Check for NaN. */
  assert (wi2dvf->entry[wi].dv->idf == wi2dvf->entry[wi].dv->idf);

  assert (wi == wi2dvf->size - 1
	  || wi2dvf->entry[wi+1].seek_start == -1
	  || ftell (wi2dvf->fp) == ABS(wi2dvf->entry[wi+1].seek_start));

  /* Return what we just read. */
  return wi2dvf->entry[wi].dv;
}

/* Return the "document vector" corresponding to "word index" WI.
   This function will read the "document vector" out of the file
   passed to bow_wi2dvf_new_from_file() if is hasn't been read
   already.  If the DV has been "hidden" (by feature selection, for
   example) it will return NULL.  */
bow_dv *
bow_wi2dvf_dv (bow_wi2dvf *wi2dvf, int wi)
{
  return bow_wi2dvf_dv_hidden (wi2dvf, wi, 0);
}

/* Compare two maps, and return 0 if they are equal.  This function was
   written for debugging. */
int
bow_wi2dvf_compare (bow_wi2dvf *map1, bow_wi2dvf *map2)
{
  int max_wi, wi;
  bow_dv *dv1, *dv2;

  max_wi = bow_num_words ();
  /* (map1->size > map2->size) ? map1->size : map2->size; */

  /* Step through all the "word indices" in each of the maps. */
  for (wi = 0; wi < max_wi; wi++)
    {
      dv1 = bow_wi2dvf_dv (map1, wi);
      dv2 = bow_wi2dvf_dv (map2, wi);
      if (dv1 == NULL || dv2 == NULL)
	{
	  if (!(dv1 == NULL && dv2 == NULL))
	    {
	      bow_verbosify (bow_progress, "%s: Differ by NULL at wi %d\n",
			     __PRETTY_FUNCTION__, wi);
	      return 1;
	    }
	}
      else
	{
	  /* We have two non-NULL "document vectors" */
	  int max_dv_i, dv_i;

	  max_dv_i = (dv1->length > dv2->length) ? dv1->length : dv2->length;
	  for (dv_i = 0; dv_i < max_dv_i; dv_i++)
	    {
	      if (dv1->entry[dv_i].di != dv2->entry[dv_i].di
		  || dv1->entry[dv_i].count != dv2->entry[dv_i].count)
		{
		  bow_verbosify (bow_progress, 
				 "%s: Differ by entry at wi %d\n",
				 __PRETTY_FUNCTION__, wi);
		  return 2;
		}
	    }
	}
    }
  return 0;
}

/* Print statistics about the WI2DVF map to STDOUT. */
void
bow_wi2dvf_print_stats (bow_wi2dvf *map)
{
  int wi, wi_max;
  bow_dv *dv;
  /* stats on "document vector" length */
  int dvl_count, dvl_max, dvl_ave, dvl_min;
  int dvl_max_count, dvl_min_count;
  int dvl_max_wi, dvl_min_wi;
  /* stats on "document vector" count */
  /* int dvc_max, dvc_ave, dvc_min; */
  /* stats on used/unused memory */
  int de_used_count, de_unused_count;

  wi_max = bow_num_words ();
  printf ("%8d libbow's num words\n", wi_max);
  printf ("%8d num words in wi2dvf\n", map->num_words);
  /* printf ("%8d unique documents\n", bow_num_docnames ()); */

  /* Get stats on "document vector" length. */
  dv = bow_wi2dvf_dv (map, 0);
  dvl_max = dvl_ave = dvl_min = dv->length;
  dvl_max_count = dvl_min_count = 1;
  dvl_max_wi = dvl_min_wi = 0;
  dvl_count = 0;
  de_used_count = de_unused_count = 0;

  for (wi = 1; wi < wi_max; wi++)
    {
      dv = bow_wi2dvf_dv (map, wi);
      if (dv)
	{
	  dvl_count++;
	  dvl_ave += dv->length;
	  if (dv->length > dvl_max)
	    {
	      dvl_max = dv->length;
	      dvl_max_wi = wi;
	      dvl_max_count = 1;
	    }
	  else if (dv->length > dvl_max)
	    dvl_max_count++;

	  if (dv->length < dvl_min)
	    {
	      dvl_min = dv->length;
	      dvl_min_wi = wi;
	      dvl_min_count = 1;
	    }
	  else if (dv->length > dvl_max)
	    dvl_min_count++;

	  de_used_count += dv->length;
	  de_unused_count += dv->size - dv->length;
	  assert (dv->size - dv->length >= 0);
	}
    }
  printf ("%8d minimum document vector length (eg word=`%s', %d others)\n",
	  dvl_min, bow_int2word (dvl_min_wi), dvl_min_count);
  printf ("%8.1f average document vector length\n",
	  ((double)dvl_ave)/dvl_count);
  printf ("%8d maximum document vector length (eg word=`%s', %d others)\n",
	  dvl_max, bow_int2word (dvl_max_wi), dvl_max_count);
  printf ("%8d document vector entries used\n", 
	  de_used_count);
  printf ("%8d document vector entries allocated but unused\n", 
	  de_unused_count);
  printf ("%8.1f average unused document vector entries\n", 
	  ((double)de_unused_count)/dvl_count);
}

