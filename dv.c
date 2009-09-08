/* Functions for manipulating "document vectors". */

/* Copyright (C) 1997, 1998 Andrew McCallum

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
#include <assert.h>

unsigned int bow_dv_default_capacity = 2;

/* The number of "document vectors" current in existance. */
unsigned int bow_dv_count = 0;

/* Create a new, empty "document vector". */
bow_dv *
bow_dv_new (int capacity)
{
  bow_dv *ret;

  if (capacity == 0)
    capacity = bow_dv_default_capacity;
  ret = bow_malloc (sizeof (bow_dv) + (sizeof (bow_de) * capacity));
  ret->length = 0;
  ret->idf = 0.0f;
  ret->size = capacity;
  bow_dv_count++;
  return ret;
}

/* Return the index into (*DV)->entries[] at which the "document
   entry" for the document with index DI can be found.  If necessary,
   allocate more space, and/or shift other document entries around
   in order to make room for it. */
static int
_bow_dv_index_for_di (bow_dv **dv, int di, int error_on_creation)
{
  int dv_index;			/* The "document vector" index at
				   which we are looking for. */
  static inline void grow_if_necessary ()
    {
      if (error_on_creation)
	bow_error ("Shouldn't be creating new entry for a weight.");
      if ((*dv)->length >= (*dv)->size)
	{
	  /* The DV must grow to accommodate another entry.  Add one
	     to corrently handle case where (*DV)->SIZE==1. */
	  (*dv)->size = ((*dv)->size + 1) * 3;
	  (*dv)->size /= 2;
	  assert ((*dv)->size > (*dv)->length);
	  (*dv) = bow_realloc ((*dv), (sizeof (bow_dv) 
				       + sizeof (bow_de) * (*dv)->size));
	}
    }
  static inline void initialize_dv_index (int dvi)
    {
      (*dv)->entry[dvi].di = di;
      (*dv)->entry[dvi].count = 0;
      (*dv)->entry[dvi].weight = 0.0f;
    }

  assert ((*dv)->length <= (*dv)->size);
  if ((*dv)->length == 0)
    {
      /* The DV is empty. */
      assert ((*dv)->size);
      ((*dv)->length)++;
      initialize_dv_index (0);
      return 0;
    }
  else if (di == (*dv)->entry[(dv_index = (*dv)->length - 1)].di)
    {
      /* An entry already exists for this DI; it's at the end. */
      return dv_index;
    }
  else if (di > (*dv)->entry[dv_index].di)
    {
      /* The entry does not already exist, and the entry belongs at
	 the end of the current DV. */
      dv_index = (*dv)->length;
      ((*dv)->length)++;
      grow_if_necessary ();
      initialize_dv_index (dv_index);
      return dv_index;
    }
  else
    {
      /* Search for the entry in the middle of the list. */
      for (dv_index = 0; 
	   (((*dv)->entry[dv_index].di < di)
	    && (dv_index < (*dv)->length));
	   dv_index++)
	{
	  if ((*dv)->entry[dv_index].di == di)
	    break;
	}
      if ((*dv)->entry[dv_index].di == di)
	{
	  /* The entry already exists in the middle of the DV. */
	  return dv_index;
	}
      else
	{
	  /* The entry should be in the middle of the DV, but it isn't 
	     there now; we'll have to push some aside to make room. */
	  int dvi;
	  assert (dv_index < (*dv)->length);
	  ((*dv)->length)++;
	  grow_if_necessary ();
	  /* Scoot some "document entries" up to make room */
	  for (dvi = (*dv)->length - 2; dvi >= dv_index; dvi--)
	    memcpy (&((*dv)->entry[dvi+1]), &((*dv)->entry[dvi]), 
		    sizeof (bow_de));
	  initialize_dv_index (dv_index);
	  return dv_index;
	}
    }
}

/* Sum the COUNT into the document vector DV at document index DI,
   creating a new entry in the document vector if necessary. */
void
bow_dv_add_di_count_weight (bow_dv **dv, int di, int count, float weight)
{
  int dv_index;			/* The "document vector" index at
				   which we are adding COUNT. */
  int new_count;

  dv_index = _bow_dv_index_for_di (dv, di, 0);
  new_count = (*dv)->entry[dv_index].count + count;
  if (new_count < (*dv)->entry[dv_index].count)
    {
      static int already_warned = 0;
      if (!already_warned)
	{
	  bow_verbosify (bow_progress,
			 "bow_dv->entry[].count overflowed int\n");
	  already_warned = 1;
	}
      (*dv)->entry[dv_index].count = (INT_MAX) - 1;
    }
  else
    (*dv)->entry[dv_index].count = new_count;
  assert ((*dv)->entry[dv_index].count > 0);

  /* If we are recording only binary word absence/presence, force the 
     word count to 0 or 1. */
  if (bow_binary_word_counts && (*dv)->entry[dv_index].count > 1)
    (*dv)->entry[dv_index].count = 1;
  /* xxx But we don't do anything special with the weight? */
  (*dv)->entry[dv_index].weight += weight;
}

/* set the count and weight to COUNT and WEIGHT in the document vector
   DV at document index DI, creating a new entry in the document
   vector if necessary. */
void
bow_dv_set_di_count_weight (bow_dv **dv, int di, int count, float weight)
{
  int dv_index;			/* The "document vector" index at
				   which we are adding COUNT. */

  assert (count >= 0);

  dv_index = _bow_dv_index_for_di (dv, di, 0);
  (*dv)->entry[dv_index].count = count;

  /* If we are recording only binary word absence/presence, force the 
     word count to 0 or 1. */
  if (bow_binary_word_counts && (*dv)->entry[dv_index].count > 1)
    (*dv)->entry[dv_index].count = 1;
  /* xxx But we don't do anything special with the weight? */
  (*dv)->entry[dv_index].weight = weight;
}

/* Return a pointer to the BOW_DE for a particular document, or return
   NULL if there is no entry for that document. */
bow_de *
bow_dv_entry_at_di (bow_dv *dv, int di)
{
  int dvi;

  for (dvi = 0; dvi < dv->length && dv->entry[dvi].di <= di; dvi++)
    {
      if (dv->entry[dvi].di == di)
	return &(dv->entry[dvi]);
    }
  return NULL;
}


/* Return the number of bytes required for writing the "document vector" DV. */
int
bow_dv_write_size (bow_dv *dv)
{
  int size;
  if (dv == NULL)
    size = sizeof (int);
  else
    {
      int di_count_size;
      if (bow_file_format_version < 5)
	di_count_size = (sizeof (short)        /* di */
			 + sizeof (short));    /* count */
      else
	di_count_size = (sizeof (int)          /* di */
			 + sizeof (int));      /* count */
      size = (sizeof (int)		       /* length */
	      + sizeof (float)	               /* idf */
	      + (dv->length		       /* for each entry */
		 * (di_count_size
		    + sizeof (float))));       /* weight */
    }
  return size;
}

/* Write "document vector" DV to the stream FP. */
void
bow_dv_write (bow_dv *dv, FILE *fp)
{
  int i;

  if (dv == NULL)
    {
      bow_fwrite_int (0, fp);
      return;
    }

  bow_fwrite_int (dv->length, fp);
  bow_fwrite_float (dv->idf, fp);
  assert (dv->idf == dv->idf);	/* testing for NaN */

  for (i = 0; i < dv->length; i++)
    {
      if (bow_file_format_version < 5)
	{
	  bow_fwrite_short (dv->entry[i].di, fp);
	  bow_fwrite_short (dv->entry[i].count, fp);
	}
      else
	{
	  bow_fwrite_int (dv->entry[i].di, fp);
	  bow_fwrite_int (dv->entry[i].count, fp);
	}
      bow_fwrite_float (dv->entry[i].weight, fp);
    }
}

/* Return a new "document vector" read from a pointer into a data file, FP. */
bow_dv *
bow_dv_new_from_data_fp (FILE *fp)
{
  int i;
  int len;
  bow_dv *ret;

  assert (feof (fp) == 0);	/* Help make sure FP hasn't been closed. */
  bow_fread_int (&len, fp);
  
  if (len == 0)
    return NULL;

  ret = bow_dv_new (len);
  bow_fread_float (&(ret->idf), fp);
  assert (ret->idf == ret->idf);	/* testing for NaN */
  ret->length = len;

  for (i = 0; i < len; i++)
    {
      if (bow_file_format_version < 5)
	{
	  short s;
	  bow_fread_short (&s, fp);
	  ret->entry[i].di = s;
	  bow_fread_short (&s, fp);
	  ret->entry[i].count = s;
	}
      else
	{
	  bow_fread_int (&(ret->entry[i].di), fp);
	  bow_fread_int (&(ret->entry[i].count), fp);
	}
      bow_fread_float (&(ret->entry[i].weight), fp);
    }
  return ret;
}

void
bow_dv_free (bow_dv *dv)
{
  bow_dv_count--;
  bow_free (dv);
}
