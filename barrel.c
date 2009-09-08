/* Managing the connection between document-vectors (wi2dvf's) and cdocs
   Copyright (C) 1997, 1998, 1999 Andrew McCallum

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
#include <values.h>

static int _bow_barrel_version = -1;
#define BOW_DEFAULT_BARREL_VERSION 3


/* Create a new, empty `bow_barrel', with cdoc's of size ENTRY_SIZE
   and cdoc free function FREE_FUNC.*/
bow_barrel *
bow_barrel_new (int word_capacity, 
		int class_capacity, int entry_size, void (*free_func)())
{
  bow_barrel *ret;

  ret = bow_malloc (sizeof (bow_barrel));
  ret->method = (rainbow_method*)
    (bow_argp_method 
     ? : bow_method_at_name (bow_default_method_name));
  ret->cdocs = bow_array_new (class_capacity, entry_size, free_func);
  ret->wi2dvf = bow_wi2dvf_new (word_capacity);
  ret->classnames = NULL;
  /* return a document barrel by default */
  ret->is_vpc = 0;
  return ret;
}

static void
_bow_barrel_cdoc_free (bow_cdoc *cdoc)
{
  if (cdoc->filename)
    free ((void*)(cdoc->filename));
  if (cdoc->class_probs)
    free ((void*)(cdoc->class_probs));
}

/* Add statistics about the document described by CDOC and WV to the
   BARREL. */
int
bow_barrel_add_document (bow_barrel *barrel,
			 bow_cdoc *cdoc,
			 bow_wv *wv)
{
  int di;

  /* Add the CDOC.  (This makes a new copy of CDOC in the array.) */
  di = bow_array_append (barrel->cdocs, cdoc);
  /* Add the words in WV. */
  bow_wi2dvf_add_di_wv (&(barrel->wi2dvf), di, wv);
  /* xxx Why is this assert here? */
  assert (barrel->classnames == NULL);
  
  return di;
}

/* Add statistics to the barrel BARREL by indexing all the documents
   found when recursively decending directory DIRNAME.  Return the number
   of additional documents indexed. */
int
bow_barrel_add_from_text_dir (bow_barrel *barrel, 
			      const char *dirname, 
			      const char *except_name,
			      const char *classname)
{
  int text_file_count, binary_file_count;
  int class;

#ifdef VPC_ONLY
  /* Used when we are building a class barrel */
  bow_cdoc cdoc;
  bow_cdoc *cdocp;
  int word_count = 0;
  int di;

  /* Function used to build a multinomial vpc barrel without building a
   * document barrel */
  int class_barrel_index_file (const char *filename, void *context)
    {
      FILE *fp;
      int num_words;

      /* If the filename matches the exception name, return immediately. */
      if (except_name && !strcmp (filename, except_name))
	return 0;

      if (!(fp = fopen (filename, "r")))
	{
	  bow_verbosify (bow_progress,
			 "Couldn't open file `%s' for reading.", filename);
	  return 0;
	}
      if (bow_fp_is_text (fp))
	{
	  /* Add all the words in this document. */
	  num_words = bow_wi2dvf_add_di_text_fp (&(barrel->wi2dvf), di, fp,
						 filename);
	  word_count += num_words;
	  text_file_count++;
	}
      else
	{
	  bow_verbosify (bow_progress,
			 "\nFile `%s' skipped because not text\n",
			 filename);
	  binary_file_count++;
	}
      fclose (fp);
      bow_verbosify (bow_progress,
		     "\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b"
		     "%6d : %8d", 
		     text_file_count, bow_num_words ());
      return 1;
    }
#endif

  int barrel_index_file (const char *filename, void *context)
    {
      FILE *fp;
      bow_cdoc cdoc;
      bow_cdoc *cdocp;
      int di;			/* a document index */
      int num_words;

      /* If the filename matches the exception name, return immediately. */
      if (except_name && !strcmp (filename, except_name))
	return 0;

      if (!(fp = fopen (filename, "r")))
	{
	  bow_verbosify (bow_progress,
			 "Couldn't open file `%s' for reading.", filename);
	  return 0;
	}
      if (bow_fp_is_text (fp))
	{
	  /* The file contains text; snarf the words and put them in
	     the WI2DVF map. */
	  cdoc.type = bow_doc_train;
	  cdoc.class = class;
	  /* Set to one so bow_infogain_per_wi_new() works correctly
	     by default. */
	  cdoc.prior = 1.0f;
	  assert (cdoc.class >= 0);
	  cdoc.filename = strdup (filename);
	  assert (cdoc.filename);
	  cdoc.class_probs = NULL;
	  /* Add the CDOC to CDOCS, and determine the "index" of this
             document. */
	  di = bow_array_append (barrel->cdocs, &cdoc);
	  /* Add all the words in this document. */
	  num_words = bow_wi2dvf_add_di_text_fp (&(barrel->wi2dvf), di, fp,
						 filename);
	  /* Fill in the new CDOC's idea of WORD_COUNT */
	  cdocp = bow_array_entry_at_index (barrel->cdocs, di);
	  cdocp->word_count = num_words;
	  text_file_count++;
	}
      else
	{
	  bow_verbosify (bow_progress,
			 "\nFile `%s' skipped because not text\n",
			 filename);
	  binary_file_count++;
	}
      fclose (fp);
      bow_verbosify (bow_progress,
		     "\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b"
		     "%6d : %8d", 
		     text_file_count, bow_num_words ());
      return 1;
    }

  if (!(barrel->classnames))
    barrel->classnames = bow_int4str_new (0);
  class = bow_str2int (barrel->classnames, classname);

#ifdef VPC_ONLY
  /* If we are building a class barrel, make one cdoc per class */
  if (barrel->is_vpc)
    {
      cdoc.type = bow_doc_train;
      cdoc.class = class;
      cdoc.filename = strdup (classname);
      cdoc.word_count = 0; /* Number of documents in this class */
      cdoc.normalizer = -1.0f;
      cdoc.prior = 1.0f; /* Need to set this later... */
      cdoc.class_probs = NULL;

      /* Add the CDOC to CDOCS, and determine the "index" of this
	 "document." */
      di = bow_array_append (barrel->cdocs, &cdoc);
    }
#endif
	 
  bow_verbosify (bow_progress,
		 "Gathering stats... files : unique-words :: "
		 "                 ");
  text_file_count = binary_file_count = 0;
#ifdef VPC_ONLY
  /* Call our special function for building a class barrel rather than
   * a document barrel.  When finished, set the priors and word counts */
  if (barrel->is_vpc)
    {
      bow_map_filenames_from_dir (class_barrel_index_file, 0, dirname, "");
      cdocp = bow_array_entry_at_index (barrel->cdocs, di);
      cdocp->prior = (float) text_file_count;
      cdocp->word_count = word_count;
    }
  else    
#endif
  bow_map_filenames_from_dir (barrel_index_file, 0, dirname, "");
  bow_verbosify (bow_progress, "\n");
  if (binary_file_count > text_file_count)
    bow_verbosify (bow_quiet,
		   "Found mostly binary files, which were ignored.\n");
  return text_file_count;
}

/* Call this on a vector-per-document barrel to set the CDOC->PRIOR's
   so that the CDOC->PRIOR's for all documents of the same class sum
   to 1. */
void
bow_barrel_set_cdoc_priors_to_class_uniform (bow_barrel *barrel)
{
  int *ci2dc;			/* class index 2 document count */
  int ci2dc_size = 100;
  int ci;
  int di;
  bow_cdoc *cdoc;
  int total_model_docs = 0;
  int num_non_zero_ci2dc_entries = 0;
  
  ci2dc = bow_malloc (sizeof (int) * ci2dc_size);
  for (ci = 0; ci < ci2dc_size; ci++)
    ci2dc[ci] = 0;
  for (di = 0; di < barrel->cdocs->length; di++)
    {
      cdoc = bow_array_entry_at_index (barrel->cdocs, di);
      if (cdoc->class >= ci2dc_size)
	{
	  /* CI2DC must grow to accommodate larger "class index" */
	  int old_size = ci2dc_size;
	  ci2dc_size *= 2;
	  ci2dc = bow_realloc (ci2dc, sizeof (int) * ci2dc_size);
	  for ( ; old_size < ci2dc_size; old_size++)
	    ci2dc[old_size] = 0;
	}
      if (cdoc->type == bow_doc_train)
	{
	  if (ci2dc[cdoc->class] == 0)
	    num_non_zero_ci2dc_entries++;
	  ci2dc[cdoc->class]++;
	  total_model_docs++;
	}
    }

  for (di = 0; di < barrel->cdocs->length; di++)
    {
      cdoc = bow_array_entry_at_index (barrel->cdocs, di);
      if (cdoc->type == bow_doc_train)
	{
	  cdoc->prior = (1.0 /
			 (num_non_zero_ci2dc_entries * ci2dc[cdoc->class]));
	  assert (cdoc->prior >= 0);
	}
    }
#if 0
  fprintf (stderr, "Infogain post-prior-setting\n");
  bow_infogain_per_wi_print (stderr, barrel, num_non_zero_ci2dc_entries, 5);
#endif

  /* Do some sanity checks. */
  {
    float prior_total = 0;
    for (di = 0; di < barrel->cdocs->length; di++)
      {
	cdoc = bow_array_entry_at_index (barrel->cdocs, di);
	if (cdoc->type == bow_doc_train)
	  prior_total += cdoc->prior;
      }
    assert (prior_total < 1.1 && prior_total > 0.9);
  }

  free (ci2dc);
}

/* Modify the BARREL by removing those entries for words that are not
   in the int/str mapping MAP. */
void
bow_barrel_prune_words_not_in_map (bow_barrel *barrel,
				   bow_int4str *map)
{
  int wi;
  int max_wi = MIN (barrel->wi2dvf->size, bow_num_words());
  
  assert (max_wi);
  /* For each word in MAP. */
  for (wi = 0; wi < max_wi; wi++)
    {
      if (bow_str2int_no_add (map, bow_int2word (wi)) == -1)
	{
	  /* Word WI is not in MAP.  Remove it from the BARREL. */
	  bow_wi2dvf_hide_wi (barrel->wi2dvf, wi);
	}
    }
}

/* Modify the BARREL by removing those entries for words that are in
   the int/str mapping MAP. */
void
bow_barrel_prune_words_in_map (bow_barrel *barrel,
			       bow_int4str *map)
{
  int i;
  int wi;
  
  /* For each word in MAP. */
  for (i = 0; i < map->str_array_length; i++)
    {
      if ((wi = bow_word2int_no_add (bow_int2str (map, i))) != -1)
	{
	  /* Word WI is in MAP.  Remove it from the BARREL. */
	  bow_wi2dvf_hide_wi (barrel->wi2dvf, wi);
	}
    }
}

/* Modify the BARREL by removing those entries for words that are not
   among the NUM_WORDS_TO_KEEP top words, by information gain.  This
   function is similar to BOW_WORDS_KEEP_TOP_BY_INFOGAIN(), but this
   one doesn't change the word-int mapping. */
void
bow_barrel_keep_top_words_by_infogain (int num_words_to_keep, 
				       bow_barrel *barrel, int num_classes)
{
  float *wi2ig;
  int wi2ig_size;
  int wi, i;
  struct wiig_list_entry {
    float ig;
    int wi;
  } *wiig_list;
  /* For sorting the above entries. */
  int compare_wiig_list_entry (const void *e1, const void *e2)
    {
      if (((struct wiig_list_entry*)e1)->ig >
	  ((struct wiig_list_entry*)e2)->ig)
	return -1;
      else if (((struct wiig_list_entry*)e1)->ig ==
	  ((struct wiig_list_entry*)e2)->ig)
	return 0;
      else return 1;
    }

  if (num_words_to_keep == 0)
    return;

  /* Unhide "document vectors" for all WI's */
  bow_wi2dvf_unhide_all_wi (barrel->wi2dvf);

  /* Get the information gain of all the words. */
  wi2ig = bow_infogain_per_wi_new (barrel, num_classes, &wi2ig_size);

  /* Make a list of the info gain numbers paired with their WI's,
     in prepartion for sorting. */
  wiig_list = alloca (sizeof (struct wiig_list_entry) * wi2ig_size);
  for (wi = 0; wi < wi2ig_size; wi++)
    {
      wiig_list[wi].wi = wi;
      wiig_list[wi].ig = wi2ig[wi];
    }
  /* Sort the list */
  qsort (wiig_list, wi2ig_size, sizeof (struct wiig_list_entry), 
	 compare_wiig_list_entry);

  num_words_to_keep = MIN (num_words_to_keep, wi2ig_size);

#if 1
  bow_verbosify (bow_progress, 
		 "Showing here top %d words by information gain; "
		 "%d put in model\n", 
		 MIN(5,num_words_to_keep), num_words_to_keep);
  for (i = 0; i < MIN(5,num_words_to_keep); i++)
    bow_verbosify (bow_progress,
		   "%20.10f %s\n", wiig_list[i].ig, 
		   bow_int2word (wiig_list[i].wi));
#endif

  bow_verbosify (bow_progress, 
		 "Removing words by information gain:          ");

  /* Hide words from the BARREL. */
  for (i = num_words_to_keep; i < wi2ig_size; i++)
    {
      /* Hide the WI from BARREL. */
      bow_wi2dvf_hide_wi (barrel->wi2dvf, wiig_list[i].wi);
      if (i % 100 == 0)
	bow_verbosify (bow_progress, "\b\b\b\b\b\b\b\b\b%9d", wi2ig_size - i); 
    }
  /* Now that we have reduce vocabulary size, don't add more words to the
     vocabulary.  For example, when doing --test-files, don't include
     in the QUERY_WV words that aren't in the current reduced vocabulary,
     the total number of words in the QUERY_WV will be too large! */
  bow_word2int_do_not_add = 1;

  bow_verbosify (bow_progress, "\n");
}

/* Set the BARREL->WI2DVF->ENTRY[WI].IDF to the sum of the COUNTS for
   the given WI among those documents in the training set. */
void
bow_barrel_set_idf_to_count_in_train (bow_barrel *barrel)
{
  bow_wi2dvf *wi2dvf = barrel->wi2dvf;
  int wi, nwi, dvi;
  bow_dv *dv;
  bow_cdoc *cdoc;

  nwi = MIN (wi2dvf->size, bow_num_words());
  for (wi = 0; wi < nwi; wi++)
    {
      dv = bow_wi2dvf_dv (wi2dvf, wi);
      if (!dv)
	continue;
      dv->idf = 0;
      for (dvi = 0; dvi < dv->length; dvi++)
	{
	  cdoc = bow_array_entry_at_index (barrel->cdocs, dv->entry[dvi].di);
	  if (cdoc->type == bow_doc_train)
	    dv->idf += dv->entry[dvi].count;
	}
    }
}

/* Return the number of unique words among those documents with TYPE
   tag (train, test, unlabeled, etc) equal to TYPE. */
int 
bow_barrel_num_unique_words_of_type (bow_barrel *doc_barrel, int type)
{
  int wi, max_wi, dvi;
  int num_unique = 0;
  bow_dv *dv;
  bow_cdoc *cdoc;

  max_wi = MIN (doc_barrel->wi2dvf->size, bow_num_words());
  for (wi = 0; wi < max_wi; wi++)
    {
      dv = bow_wi2dvf_dv (doc_barrel->wi2dvf, wi);
      for (dvi = 0; dv && dvi < dv->length; dvi++)
	{
	  cdoc = bow_array_entry_at_index (doc_barrel->cdocs,
					   dv->entry[dvi].di);
	  if (cdoc->type == type)
	    {
	      num_unique++;
	      break;
	    }
	}
    }
  return num_unique;
}



int
_bow_barrel_cdoc_write (bow_cdoc *cdoc, FILE *fp)
{
  int ret;

  ret = bow_fwrite_int (cdoc->type, fp);
  ret += bow_fwrite_float (cdoc->normalizer, fp);
  ret += bow_fwrite_float (cdoc->prior, fp);
  ret += bow_fwrite_int (cdoc->word_count, fp);
  ret += bow_fwrite_string (cdoc->filename, fp);
  if (bow_file_format_version < 5)
    ret += bow_fwrite_short (cdoc->class, fp);
  else
    ret += bow_fwrite_int (cdoc->class, fp);
  return ret;
}

int
_bow_barrel_cdoc_read (bow_cdoc *cdoc, FILE *fp)
{
  int ret;
  int type;

  ret = bow_fread_int (&type, fp);
  cdoc->type = type;
  cdoc->class_probs = NULL;
  ret += bow_fread_float (&(cdoc->normalizer), fp);
  ret += bow_fread_float (&(cdoc->prior), fp);
  ret += bow_fread_int (&(cdoc->word_count), fp);
  ret += bow_fread_string ((char**)&(cdoc->filename), fp);
  if (bow_file_format_version < 5)
    {
      short s;
      ret += bow_fread_short (&s, fp);
      cdoc->class = s;
    }
  else
    ret += bow_fread_int (&(cdoc->class), fp);
  return ret;
}

/* Create and return a `barrel' by reading data from the file-pointer FP. */
bow_barrel *
bow_barrel_new_from_data_fp (FILE *fp)
{
  bow_barrel *ret;
  int version_tag;
  int method_id;

  version_tag = fgetc (fp);
  /* xxx assert (version_tag >= 0); */
  if (version_tag <= 0)
    return NULL;
  if (_bow_barrel_version != -1 && _bow_barrel_version != version_tag)
    bow_error ("Trying to read bow_barrel's with different version numbers");
  _bow_barrel_version = version_tag;
  ret = bow_malloc (sizeof (bow_barrel));
  if (_bow_barrel_version < 3)
    {
      bow_fread_int (&method_id, fp);
      bow_error ("Can no longer read barrels earlier than version 3");
      /* ret->method = _old_bow_methods[method_id]; */
    }
  else
    {
      char *method_string;
      bow_fread_string (&method_string, fp);
      ret->method = (rainbow_method*) bow_method_at_name (method_string);
      bow_free (method_string);
    }
  ret->cdocs = 
    bow_array_new_from_data_fp ((int(*)(void*,FILE*))_bow_barrel_cdoc_read,
				 _bow_barrel_cdoc_free, fp);
  assert (ret->cdocs->length);
  if (bow_file_format_version > 5)
    ret->classnames = bow_int4str_new_from_fp (fp);
  else
    ret->classnames = NULL;  
  ret->wi2dvf = bow_wi2dvf_new_from_data_fp (fp);
  assert (ret->wi2dvf->num_words);
  return ret;
}

/* Decide whether to keep this or not.  Currently it it used by
   rainbow-h.c. */
bow_barrel *
bow_barrel_new_from_data_file (const char *filename)
{
  FILE *fp;
  bow_barrel *ret_barrel;
  int wi;
  bow_dv *dv;
  int dv_count = 0;

  fp = bow_fopen (filename, "rb");
  ret_barrel = bow_barrel_new_from_data_fp (fp);

  if (ret_barrel)
    {
      /* Read in all the dvf's so that we can close the FP. */
      for (wi = 0; wi < ret_barrel->wi2dvf->size; wi++)
	{
	  dv = bow_wi2dvf_dv (ret_barrel->wi2dvf, wi);
	  if (dv)
	    dv_count++;
	}
      ret_barrel->wi2dvf->fp = NULL;
      assert (dv_count);
    }
  fclose (fp);
  return ret_barrel;
}

/* Read a line from FP until a newline, and return a newly malloc'ed
   buffer containing the line read. */
char *
getline (FILE *fp)
{
  int bufsize = 1024;
  int buflen = 0;
  char *buf = bow_malloc (bufsize);
  int byte;

  while ((byte = fgetc (fp)) != EOF
	 && byte != '\n')
    {
      buf[buflen++] = byte;
      if (buflen >= bufsize)
	{
	  bufsize *= 2;
	  buf = bow_realloc (buf, bufsize);
	}
    }
  if (byte == EOF)
    {
      bow_free (buf);
      return NULL;
    }
  buf[buflen] = '\0';
  return buf;
}

/* Create a new barrel and fill it from contents in --print-barrel=FORMAT
   read in from FILENAME. */
bow_barrel *
bow_barrel_new_from_printed_barrel_file (const char *filename,
					 const char *format)
{
  FILE *fp;
  enum {
    word_index,
    word_string,
    word_string_and_index,
    word_empty
  } word_format = word_string_and_index;
  enum {
    binary_count,
    integer_count
  } word_count_format = integer_count;
  int sparse_format = 1;
  int di;
  bow_cdoc cdoc;
  int wi;
  float count;
  int int_count;
  char datafilename[BOW_MAX_WORD_LENGTH];
  char classname[BOW_MAX_WORD_LENGTH];
  int word_count_column;
  int num_chars_read;
  char *buf, *line;
  bow_barrel *ret;
  /* Returns 1 on success, 0 on failure. */
  int read_word_count (char **string, int *wi, float *count)
    {
      char word[BOW_MAX_WORD_LENGTH];
      int ret = 0;
      int num_chars_read;
      switch (word_format)
	{
	case word_index:
	  if (sscanf (*string, "%d %f%n", wi, count, &num_chars_read) == 2)
	    ret = 1;
	  break;
	case word_string:
	  if (sscanf (*string, "%s %f%n", word, count, &num_chars_read) == 2)
	  {
	    ret = 1;
	    *wi = bow_word2int (word);
	  }
	  break;
	case word_string_and_index:
	  if (sscanf (*string,"%s %d %f%n",word,wi,count,&num_chars_read) == 3)
	    ret = 1;
	  break;
	case word_empty:
	  if (sscanf (*string, "%f%n", count, &num_chars_read) == 1)
	  {
	    ret = 1;
	    *wi = word_count_column;
	  }
	  break;
	}
      if (word_count_format == binary_count)
	*count = (*count > 0);
      if (ret)
	*string += num_chars_read;
      return ret;
    }

  if (format && strchr (format, 'a'))
    sparse_format = 0;

  if (format && strchr (format, 'b'))
    word_count_format = binary_count;

  if (format && strchr (format, 'n'))
    word_format = word_index;
  else if (format && strchr (format, 'w'))
    word_format = word_string;
  else if (format && strchr (format, 'e'))
    word_format = word_empty;

  ret = bow_barrel_new (0, 0, sizeof (bow_cdoc), _bow_barrel_cdoc_free);
  ret->classnames = bow_int4str_new (0);

  fp = bow_fopen (filename, "r");

  /* Each time through the loop reads one line. */
  while ((buf = getline (fp)))
    {
      line = buf;
      if (sscanf (line, "%s%n", datafilename, &num_chars_read) != 1)
	bow_error ("Didn't find expected filename");
      line += num_chars_read;
      if (sscanf (line, "%s%n", classname, &num_chars_read) != 1)
	bow_error ("Didn't find expected classname");
      line += num_chars_read;
      cdoc.filename = strdup (datafilename);
      assert (cdoc.filename);
      cdoc.class = bow_str2int (ret->classnames, classname);
      cdoc.type = bow_doc_train;
      cdoc.prior = 1.0f;
      cdoc.class_probs = NULL;
      di = bow_array_append (ret->cdocs, &cdoc);
      while (read_word_count (&line, &wi, &count))
	{
	  if (count)
	    {
	      int_count = rint (count);
	      bow_wi2dvf_add_wi_di_count_weight (&(ret->wi2dvf),
						 wi, di, int_count, count);
	    }
	  else
	    assert (sparse_format == 0);
	}
      bow_free (buf);
    }

  return ret;
}

/* Write BARREL to the file-pointer FP in a machine independent format. */
void
bow_barrel_write (bow_barrel *barrel, FILE *fp)
{
  if (!barrel)
    {
      fputc (0, fp);		/* 0 version_tag means NULL barrel */
      return;
    }
  fputc (BOW_DEFAULT_BARREL_VERSION, fp);
  _bow_barrel_version = BOW_DEFAULT_BARREL_VERSION;
  bow_fwrite_string (barrel->method->name, fp);
  bow_array_write (barrel->cdocs,
		   (int(*)(void*,FILE*))_bow_barrel_cdoc_write, fp);
  bow_int4str_write (barrel->classnames, fp);
  /* The wi2dvf must be written last because when we read it, we don't
     actually read the whole thing; we only read the seek-table. */
  bow_wi2dvf_write (barrel->wi2dvf, fp);
}

/* Print barrel to FP in human-readable and awk-accessible format. */
void
bow_barrel_printf_old1 (bow_barrel *barrel, FILE *fp, const char *format)
{
  
  bow_dv_heap *heap;		/* a heap of "document vectors" */
  int current_di;
  bow_cdoc *cdoc;

  bow_verbosify (bow_progress, "Printing barrel:          ");
  heap = bow_make_dv_heap_from_wi2dvf (barrel->wi2dvf);

  /* Keep going until the heap is empty */
  while (heap->length > 0)
    {
      /* Set the current document we're working on */
      current_di = heap->entry[0].current_di;
      assert (heap->entry[0].dv->idf == heap->entry[0].dv->idf);  /* NaN */

      if (current_di % 10 == 0)
	bow_verbosify (bow_progress, "\b\b\b\b\b\b%6d", current_di);

      /* Here we should check if this di is part of some training set and
	 move on if it isn't. */
    
      /* Get the document */
      cdoc = bow_cdocs_di2doc (barrel->cdocs, current_di);

#if 0
      /* If it's not a model document, then move on to next one */
      if (cdoc->type != model)
	{
	  do 
	    {
	      bow_dv_heap_update (heap);
	    }
	  while ((current_di == heap->entry[0].current_di)
		 && (heap->length > 0));
	  
	  /* Try again */
	  continue;
	}
#endif

      fprintf (fp, "%s", cdoc->filename);
    
      /* Loop over all words in this document, printing out the
         FORMAT-requested statistics. */
      do 
	{
#if 0
	  int wi;
	  for (wi = 0; heap->entry[0].wi > wi; wi++)
	    fprintf (fp, " 0");
#endif
	  fprintf (fp, "  %s %d %d", 
		   bow_int2word (heap->entry[0].wi),
		   heap->entry[0].wi,
		   heap->entry[0].dv->entry[heap->entry[0].index].count);

	  /* Update the heap, we are done with this di, move it to its
	     new position */
	  bow_dv_heap_update (heap);
#if 0
	  for (; heap->entry[0].wi > wi; wi++)
	    fprintf (fp, " 0");
#endif
	} 
      while ((current_di == heap->entry[0].current_di)
	     && (heap->length > 0));
      fprintf (fp, "\n");
    }

  bow_free (heap);
  bow_verbosify (bow_progress, "\n"); 
}

/* Print barrel to FP in human-readable and awk-accessible format.
   Step through each CDOC in BARREL->CDOCS instead of using a heap.  
   This way we even print out the documents that have zero words. 
   This function runs much more slowly than the one above. */
void
bow_new_slow_barrel_printf (bow_barrel *barrel, FILE *fp, const char *format)
{
  int di;
  bow_cdoc *cdoc;
  bow_de *de;
  int wi, max_wi;

  bow_verbosify (bow_progress, "Printing barrel:          ");
  max_wi = barrel->wi2dvf->size;
  for (di = 0; di < barrel->cdocs->length; di++)
    {
      if (barrel->cdocs->length - di % 10 == 0)
	bow_verbosify (bow_progress, "\b\b\b\b\b\b%6d", 
		       barrel->cdocs->length - di);

      cdoc = bow_array_entry_at_index (barrel->cdocs, di);
      fprintf (fp, "%s", cdoc->filename);
      for (wi = 0; wi < max_wi; wi++)
	{
	  de = bow_wi2dvf_entry_at_wi_di (barrel->wi2dvf, wi, di);
	  if (de)
	    fprintf (fp, "  %s %d %d", 
		     bow_int2word (wi),
		     wi,
		     de->count);
	}
      fprintf (fp, "\n");
    }
  bow_verbosify (bow_progress, "\n"); 
}

/* Print barrel to FP in various formats.   Defaults are first in lists:
   s - sparse       OR  a - all 
   i - integer      OR  b - binary
   c - combination  OR  n - word index   OR  w - word string   OR e - empty
   OR
   I - UC Irvine format, same as Sahami's "feat-sel" format.
   */
/* Print document, but print only those documents for which the
   function PRINT_IF_TRUE returns non-zero. */
void
bow_barrel_printf_selected (bow_barrel *barrel, FILE *fp, 
			    const char *format,
			    int (*print_if_true)(bow_cdoc*))
{
  enum {
    word_index,
    word_string,
    word_string_and_index,
    word_empty,
    word_long
  } word_format = word_string_and_index;
  enum {
    binary_count,
    integer_count
  } word_count_format = integer_count;
  int doing_uci_format = 0;
  int doing_ipl_format = 0;
  int sparse_format = 1;
  bow_dv_heap *heap;
  bow_wv *wv;
  int di;
  bow_cdoc *cdoc;
  int wi, wvi;
  bow_dv *dv;
  int last_di;
  void print_word_count (int wi, int count)
    {
      int oi;
      const char *word;

      if (word_count_format == binary_count)
	count = (count > 0);
      switch (word_format)
	{
	case word_index:
	  printf ("%d %d  ", wi, count);
	  break;
	case word_string:
	  printf ("%s %d  ", bow_int2word (wi), count);
	  break;
	case word_string_and_index:
	  printf ("%s %d %d  ", bow_int2word (wi), wi, count);
	  break;
	case word_empty:
	  printf ("%d  ", count);
	  break;
	case word_long:
	  word = bow_int2word (wi);
	  for (oi = 0; oi < count; oi++)
	    printf("%s ", word);
	}
    }

  if (format && strchr (format, 'I'))
    {
      doing_uci_format = 1;
      sparse_format = 0;
      word_count_format = binary_count;
      word_format = word_empty;
    }

  if (format && strchr (format, 'P'))
    doing_ipl_format = 1;

  if (format && strchr (format, 'a'))
    sparse_format = 0;

  if (format && strchr (format, 'b'))
    word_count_format = binary_count;

  if (format && strchr (format, 'n'))
    word_format = word_index;
  else if (format && strchr (format, 'w'))
    word_format = word_string;
  else if (format && strchr (format, 'e'))
    word_format = word_empty;
  else if (format && strchr (format, 'l'))
    word_format = word_long;

  if (doing_uci_format)
    {
      /* Print the number of dimentions and the number of features */
      printf ("%d\n%d\n", 
	      barrel->wi2dvf->num_words, 
	      barrel->cdocs->length);
    }

  heap = bow_test_new_heap (barrel);
  wv = NULL;
  last_di = -1;
  while ((di = bow_heap_next_wv (heap, barrel, &wv, print_if_true))
	 != -1)
    {
      /* Print documents that have no words in them. while (last_di ); */
      cdoc = bow_array_entry_at_index (barrel->cdocs, di);
      if (!doing_uci_format && !doing_ipl_format)
	printf ("%s %s  ", cdoc->filename, 
		bow_barrel_classname_at_index (barrel, cdoc->class));
      else if (doing_ipl_format)
	printf ("%s %s  ", bow_barrel_classname_at_index (barrel, cdoc->class),
		cdoc->filename);

      if (sparse_format)
	{
	  for (wvi = 0; wvi < wv->num_entries; wvi++)
	    print_word_count (wv->entry[wvi].wi, wv->entry[wvi].count);
	}
      else
	{
	  for (wi = 0, wvi = 0; wi < barrel->wi2dvf->size; wi++)
	    {
	      dv = bow_wi2dvf_dv (barrel->wi2dvf, wi);
	      if (!dv)
		continue;
	      if (wv->entry[wvi].wi < wi && wvi < wv->num_entries)
		wvi++;
	      assert (wv->entry[wvi].wi >= wi || wvi >= wv->num_entries);
	      if ((wvi < wv->num_entries) && wv->entry[wvi].wi == wi)
		print_word_count (wi, wv->entry[wvi].count);
	      else
		print_word_count (wi, 0);
	    }
	}
      if (doing_uci_format)
	/* Print the class index. */
	printf (": %d", cdoc->class);
      printf ("\n");
    }
}

void
bow_barrel_printf (bow_barrel *barrel, FILE *fp, const char *format)
{
  bow_barrel_printf_selected (barrel, fp, format, bow_cdoc_yes);
}

/* Print on stdout the number of times WORD occurs in the various
   docs/classes of BARREL. */
void
bow_barrel_print_word_count (bow_barrel *barrel, const char *word)
{
  int wi;
  bow_dv *dv;
  int dvi;
  bow_cdoc *cdoc;
  
  wi = bow_word2int (word);
  if (wi == -1)
    {
      fprintf (stderr, "No such word `%s' in dictionary\n", word);
      exit (-1);
    }
  dv = bow_wi2dvf_dv (barrel->wi2dvf, wi);
  if (!dv)
    {
      fprintf (stderr, "No document vector for word `%s'\n", word);
      return;
    }
  for (dvi = 0; dvi < dv->length; dvi++) 
    {
      cdoc = bow_array_entry_at_index (barrel->cdocs, 
				       dv->entry[dvi].di);
      printf ("%9d / %9d  (%9.5f) %s\n", 
	      dv->entry[dvi].count, 
	      cdoc->word_count,
	      ((float)dv->entry[dvi].count / cdoc->word_count),
	      cdoc->filename);
    }
}

/* For copying a class barrel.  Doesn't deal with class_probs at all. */
bow_barrel *
bow_barrel_copy (bow_barrel *barrel)
{
  int ci;
  int wi;
  int dvi;
  bow_dv *dv;
  bow_dv *copy_dv;
  bow_barrel *copy = bow_barrel_new(barrel->wi2dvf->size, 
				    bow_barrel_num_classes(barrel),
				    barrel->cdocs->entry_size,
				    barrel->cdocs->free_func);

  copy->method = barrel->method;
  copy->is_vpc = 1;

  copy->classnames = bow_int4str_new(0);

  /* Initialize the CDOCS and CLASSNAMES parts of the copy.
     Create BOW_CDOC structures for each class, and append them to the
     copy->cdocs array. */
  for (ci = 0; ci < bow_barrel_num_classes(barrel) ; ci++)
    {
      bow_cdoc *old_cdoc = bow_array_entry_at_index(barrel->cdocs, ci);

      bow_cdoc cdoc;

      cdoc.type = old_cdoc->type;
      cdoc.normalizer = old_cdoc->normalizer;
      cdoc.word_count = old_cdoc->word_count;
      cdoc.prior = old_cdoc->prior;
      cdoc.filename = strdup (old_cdoc->filename);
      cdoc.class = old_cdoc->class;
      cdoc.class_probs = NULL;
      bow_array_append (copy->cdocs, &cdoc);

      bow_str2int (copy->classnames, cdoc.filename);
    }

  /* set up the wi2dvf structure */
  for (wi = 0; wi < barrel->wi2dvf->size; wi++)
    {
      dv = bow_wi2dvf_dv (barrel->wi2dvf, wi);
      if (!dv)
	continue;

      for (dvi = 0; dvi < dv->length; dvi++)
	bow_wi2dvf_add_wi_di_count_weight 
	  (&(copy->wi2dvf), 
	   wi, dv->entry[dvi].di, 
	   dv->entry[dvi].count,
	   dv->entry[dvi].weight);
      
      /* Set the IDF of the class's wi2dvf directly from the doc's
	 wi2dvf */
      
      copy_dv = bow_wi2dvf_dv (copy->wi2dvf, wi);
      copy_dv->idf = dv->idf;
    }

  return (copy);
}

/* Define an iterator over the columns of a barrel  */

struct bow_barrel_iterator_context {
  bow_barrel *barrel;
  int ci;
  bow_dv *dv;
  int dvi;
};
#define CONTEXT ((struct bow_barrel_iterator_context*)context)

static void
barrel_iterator_reset_at_wi (int wi, void *context)
{
  bow_cdoc *cdoc;
  CONTEXT->dv =  bow_wi2dvf_dv (CONTEXT->barrel->wi2dvf, wi);
  CONTEXT->dvi = 0;
  /* Advance to the first document matching our criterion */
  while (CONTEXT->dv && CONTEXT->dvi < CONTEXT->dv->length)
    {
      cdoc = bow_array_entry_at_index (CONTEXT->barrel->cdocs,
				       CONTEXT->dv->entry[CONTEXT->dvi].di);
      if (cdoc->class == CONTEXT->ci && cdoc->type == bow_doc_train)
	break;
      CONTEXT->dvi++;
    }
}

static int
barrel_iterator_advance_to_next_di (void *context)
{
  bow_cdoc *cdoc;
  if (CONTEXT->dv == NULL) 
    return 0;
  CONTEXT->dvi++;
  while (CONTEXT->dvi < CONTEXT->dv->length)
    {
      cdoc = bow_array_entry_at_index (CONTEXT->barrel->cdocs,
				       CONTEXT->dv->entry[CONTEXT->dvi].di);
      if (cdoc->class == CONTEXT->ci && cdoc->type == bow_doc_train)
	break;
      CONTEXT->dvi++;
    }
  if (CONTEXT->dvi >= CONTEXT->dv->length)
    return 0;
  return 1;
}

static int
barrel_iterator_doc_index (void *context)
{
  if (CONTEXT->dv == NULL || CONTEXT->dvi >= CONTEXT->dv->length)
    return INT_MIN;
  return CONTEXT->dv->entry[CONTEXT->dvi].di;
}

static double
barrel_iterator_count_for_doc (void *context)
{
  if (CONTEXT->dv == NULL || CONTEXT->dvi >= CONTEXT->dv->length)
    return 0.0/0;		/* NaN */
  return CONTEXT->dv->entry[CONTEXT->dvi].count;
}


bow_iterator_double *
bow_barrel_iterator_for_ci_new (bow_barrel *barrel, int ci)
{
  bow_iterator_double *ret;
  void *context;

  ret = bow_malloc (sizeof (bow_iterator_double) + 
		    sizeof (struct bow_barrel_iterator_context));
  ret->reset = barrel_iterator_reset_at_wi;
  ret->advance = barrel_iterator_advance_to_next_di;
  ret->index = barrel_iterator_doc_index;
  ret->value = barrel_iterator_count_for_doc;
  context = ret->context = (char*)ret + sizeof (bow_iterator_double);
  CONTEXT->barrel = barrel;
  CONTEXT->ci = ci;
  CONTEXT->dv = NULL;
  CONTEXT->dvi = 0;
  return ret;
}
#undef CONTEXT



/* Free the memory held by BARREL. */
void
bow_barrel_free (bow_barrel *barrel)
{
  if (barrel->wi2dvf)
    bow_wi2dvf_free (barrel->wi2dvf);
  if (barrel->cdocs)
    bow_array_free (barrel->cdocs);
  if (barrel->classnames)
    bow_int4str_free (barrel->classnames);
  bow_free (barrel);
}
