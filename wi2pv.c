/* mapping a word index to a "position vector" */

/* Copyright (C) 1998 Andrew McCallum

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
#include <bow/archer.h>

bow_wi2pv *
bow_wi2pv_new (int capacity, const char *pv_filename)
{
  char pv_pathname[BOW_MAX_WORD_LENGTH];
  bow_wi2pv *wi2pv;
  int i;

  wi2pv = bow_malloc (sizeof (bow_wi2pv));
  wi2pv->num_words = 0;
  assert (strchr (pv_filename, '/') == NULL);
  wi2pv->pv_filename = strdup (pv_filename);
  assert (wi2pv->pv_filename);
  sprintf (pv_pathname, "%s/%s", bow_data_dirname, pv_filename);
  /* Truncate the file at PV_FILENAME and open for reading and writing */
  wi2pv->fp = bow_fopen (pv_pathname, "wb+");
  /* Write something so that no PV will start at seek offset 0.
     We use seek offset 0 in pv.c to have special meaning. */
  bow_fwrite_int(0, wi2pv->fp);
  if (capacity)
    wi2pv->entry_count = capacity;
  else
    wi2pv->entry_count = 1024;
  wi2pv->entry = bow_malloc (wi2pv->entry_count * sizeof (bow_pv));
  /* Initialize the entries with a special COUNT that means "stub" */
  for (i = 0; i < wi2pv->entry_count; i++)
    wi2pv->entry[i].word_count = -1;
  return wi2pv;
}

void
bow_wi2pv_free (bow_wi2pv *wi2pv)
{
  fclose (wi2pv->fp);
  bow_free (wi2pv->entry);
  bow_free (wi2pv);
}

/* Write to disk all PV's with more than N bytes in memory */
void
bow_wi2pv_flush (bow_wi2pv *wi2pv, int n)
{
  int wi;
  for (wi = 0; wi < wi2pv->num_words; wi++)
    {
      /* Don't bother flushing it if it has less than ten bytes in it. */
      if (wi2pv->entry[wi].pvm && wi2pv->entry[wi].pvm->write_end > n)
				bow_pv_flush (&(wi2pv->entry[wi]), wi2pv->fp);
    }
  assert (bow_pvm_total_bytes == 0);
}

void
bow_wi2pv_add_wi_di_pi (bow_wi2pv *wi2pv, int wi, int di, int pi)
{
  int count;
  /* If WI is so large that there isn't an entry for it, enlarge
     the array of PV's so that there is a place for it. */
  if (wi >= wi2pv->entry_count)
    {
      int i, old_entry_count = wi2pv->entry_count;
      do
			{
				if (wi2pv->entry_count > 60000)
					wi2pv->entry_count += 20000;
				else
					wi2pv->entry_count *= 2;
			}
      while (wi > wi2pv->entry_count);
      wi2pv->entry = bow_realloc (wi2pv->entry, 
				  wi2pv->entry_count * sizeof (bow_pv));
      /* Initialize the entries with a special COUNT that means "stub" */
      for (i = old_entry_count; i < wi2pv->entry_count; i++)
				wi2pv->entry[i].word_count = -1;
    }

  /* If WI's entry is just a stub, then initialize it. */
  if (wi2pv->entry[wi].word_count < 0)
    {
      bow_pv_init (&(wi2pv->entry[wi]), wi2pv->fp);
      wi2pv->num_words++;
    }

  /* Add the DI and PI */
  bow_pv_add_di_pi (&(wi2pv->entry[wi]), di, pi, wi2pv->fp);

#if 1
  if (bow_pvm_total_bytes > bow_pvm_max_total_bytes)
    bow_wi2pv_flush (wi2pv, 0);
#endif

  if (bow_pvm_total_bytes > bow_pvm_max_total_bytes)
    {
      for (count = 10 ; 
	   count >= 0 && bow_pvm_total_bytes > bow_pvm_max_total_bytes;
	   count--)
	{
	  bow_verbosify (bow_progress, 
			 "\nFlushing inverted indices to disk (%d)", count);
	  bow_wi2pv_flush (wi2pv, count);
	}
      bow_verbosify (bow_progress, "\nIndexing files:              ");
    }
}

void
bow_wi2pv_rewind (bow_wi2pv *wi2pv)
{
  int wi;
  for (wi = 0; wi < wi2pv->entry_count; wi++)
    {
      /* Don't rewind if it is a stub (== -1) and if it has no words
         in it (== 0) */
      if (wi2pv->entry[wi].word_count > 0)
	bow_pv_rewind (&(wi2pv->entry[wi]), wi2pv->fp);
    }
}

void
bow_wi2pv_wi_next_di_pi (bow_wi2pv *wi2pv, int wi, int *di, int *pi)
{
  if (wi >= wi2pv->entry_count || wi2pv->entry[wi].word_count < 0)
    {
      *di = -1;
      *pi = -1;
    }
  else
    {
      bow_pv_next_di_pi (&(wi2pv->entry[wi]), di, pi, wi2pv->fp);
    }
}

void
bow_wi2pv_wi_unnext (bow_wi2pv *wi2pv, int wi)
{
  if (wi < wi2pv->entry_count && wi2pv->entry[wi].word_count >= 0)
    bow_pv_unnext (&(wi2pv->entry[wi]));
}

int
bow_wi2pv_wi_word_count (bow_wi2pv *wi2pv, int wi)
{
  if (wi < wi2pv->entry_count && wi2pv->entry[wi].word_count >= 0)
    return wi2pv->entry[wi].word_count;
  return 0;
}

void
bow_wi2pv_write_to_filename (bow_wi2pv *wi2pv, const char *filename)
{
  FILE *fp;
  int wi;

  fp = bow_fopen (filename, "wb");
  
  //bow_fwrite_int (wi2pv->entry_count, fp);
  bow_fwrite_int (wi2pv->num_words, fp);
  bow_fwrite_string (wi2pv->pv_filename, fp);
  for (wi = 0; wi < wi2pv->num_words; wi++)
    /* This will also flush the PV->PVM's to disk */
    bow_pv_write (&(wi2pv->entry[wi]), fp, wi2pv->fp);
  fclose (fp);

  /* Make sure that all of the cached wi/di/pi matrix is written out. */
  fflush (wi2pv->fp);
}

bow_wi2pv *
bow_wi2pv_new_from_filename (const char *filename)
{
  FILE *fp;
  bow_wi2pv *wi2pv;
  int wi;
  char *foo;
  char pv_pathname[BOW_MAX_WORD_LENGTH];

  fp = bow_fopen (filename, "rb");
  
  wi2pv = bow_malloc (sizeof (bow_wi2pv));
  //bow_fread_int (&(wi2pv->entry_count), fp);
  bow_fread_int (&(wi2pv->num_words), fp);
  wi2pv->entry_count = wi2pv->num_words;
  bow_fread_string (&foo, fp);
  wi2pv->pv_filename = foo;
  wi2pv->entry = bow_malloc (wi2pv->entry_count * sizeof (bow_pv));
  for (wi = 0; wi < wi2pv->num_words; wi++)
    bow_pv_read (&(wi2pv->entry[wi]), fp);
  fclose (fp);

  /* Open the PV_FILENAME for reading and writing, but do not truncate */
  if (strchr (wi2pv->pv_filename, '/'))
    sprintf (pv_pathname, "%s/pv", bow_data_dirname);
  else
    sprintf (pv_pathname, "%s/%s", bow_data_dirname, wi2pv->pv_filename);
  wi2pv->fp = bow_fopen (pv_pathname, "rb+");
  return wi2pv;
}

/* Close and re-open WI2PV's FILE* for its PV's.  This should be done
   after a fork(), since the parent and child will share lseek()
   positions otherwise. */
void
bow_wi2pv_reopen_pv (bow_wi2pv *wi2pv)
{
  char pv_pathname[BOW_MAX_WORD_LENGTH];

  fclose (wi2pv->fp);
  if (strchr (wi2pv->pv_filename, '/'))
    sprintf (pv_pathname, "%s/pv", bow_data_dirname);
  else
    sprintf (pv_pathname, "%s/%s", bow_data_dirname, wi2pv->pv_filename);
  wi2pv->fp = bow_fopen (pv_pathname, "rb");
}


void
bow_wi2pv_print_stats (bow_wi2pv *wi2pv)
{
  int wi, i;
  int histogram_size = 100;
  int word_count_histogram[histogram_size];
  int count;

  for (i = 0; i < histogram_size; i++)
    word_count_histogram[i] = 0;
  for (wi = 0; wi < wi2pv->entry_count; wi++)
    {
      count = bow_wi2pv_wi_word_count (wi2pv, wi);
      if (wi2pv->entry[wi].word_count >= 0)
	{
	  printf ("%10d %-30s \n", count, bow_int2word (wi));
	  if (count < histogram_size-1)
	    word_count_histogram[count]++;
	  else
	    word_count_histogram[histogram_size-1]++;
	}
    }
  /* Print the number of unique words associated with each occurrence count */
  for (i = 1; i < histogram_size; i++)
    printf ("histogram %5d %10d\n", i, word_count_histogram[i]);
}
