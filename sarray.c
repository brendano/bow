/* Arrays of C struct's that can grow.  Entries can be retrieved
   either by integer index, or by string key. */
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

int bow_sarray_default_capacity = 1024;

/* Allocate, initialize and return a new sarray structure. */
bow_sarray *
bow_sarray_new (int capacity, int entry_size, void (*free_func)())
{
  bow_sarray *ret;
  ret = malloc (sizeof (bow_sarray));
  bow_sarray_init (ret, capacity, entry_size, free_func);
  return ret;
}

/* Initialize a newly allocated sarray structure. */
void
bow_sarray_init (bow_sarray *sa, int capacity,
		 int entry_size, void (*free_func)())
{
  if (capacity == 0)
    capacity = bow_sarray_default_capacity;
  sa->array = bow_array_new (capacity, entry_size, free_func);
  sa->i4k = bow_int4str_new (capacity);
}

/* Append a new entry to the array.  Also make the entry accessible by
   the string KEYSTR.  Returns the index of the new entry. */
int
bow_sarray_add_entry_with_keystr (bow_sarray *sa, void *entry,
				  const char *keystr)
{
  int index, i;

  assert (keystr && keystr[0]);
  /* Make sure this key string is not already in the map. */
  assert (bow_str2int_no_add (sa->i4k, keystr) == -1);
  index = bow_str2int (sa->i4k, keystr);
  i = bow_array_append (sa->array, entry);
  assert (index == i);
  return index;
}

/* Append a new entry to the array.  Also make the entry accessible by
   the string KEYSTR. Reflect changes on disk.
   Returns the index of the new entry. */
int
bow_sarray_add_entry_with_keystr_inc (bow_sarray *sa, void *entry,
				      const char *keystr, int (*write_func)(void*,FILE*),
				      FILE *i4k_fp, FILE *array_fp)
{
  int index, i;

  assert (keystr && keystr[0]);
  /* Make sure this key string is not already in the map. */
  assert (bow_str2int_no_add (sa->i4k, keystr) == -1);
  index = bow_str2int (sa->i4k, keystr);
  i = bow_array_append (sa->array, entry);
  assert (index == i);

  /* Update information on disk */
  fseek (i4k_fp, 0, SEEK_END);
  fprintf (i4k_fp, "%s\n", keystr);
  bow_array_write_entry_inc (sa->array, i, write_func, array_fp); 
  fflush(i4k_fp);
  fflush(array_fp);

  return index;
}

/* Return a pointer to the entry at index INDEX. */
void *
bow_sarray_entry_at_index (bow_sarray *sa, int index)
{
  return bow_array_entry_at_index (sa->array, index);
}

/* Return a pointer to the entry associated with string KEYSTR. */
void *
bow_sarray_entry_at_keystr (bow_sarray *sa, const char *keystr)
{
  int index;
  index = bow_str2int_no_add (sa->i4k, keystr);
  if (index < 0)
    return NULL;
  return bow_array_entry_at_index (sa->array, index);
}

/* Return the string KEYSTR associated with the entry at index INDEX. */
const char *
bow_sarray_keystr_at_index (bow_sarray *sa, int index)
{
  return bow_int2str (sa->i4k, index);
}

/* Return the index of the entry associated with the string KEYSTR. */
int
bow_sarray_index_at_keystr (bow_sarray *sa, const char *keystr)
{
  return bow_str2int_no_add (sa->i4k, keystr);
}

/* Write the sarray SARRAY to the file-pointer FP, using the function
   WRITE_FUNC to write each of the entries in SARRAY. */
void
bow_sarray_write (bow_sarray *sarray, int (*write_func)(void*,FILE*), FILE *fp)
{
  bow_int4str_write (sarray->i4k, fp);
  bow_array_write (sarray->array, write_func, fp);
}

/* Return a new sarray, created by reading file-pointer FP, and using
   the function READ_FUNC to read each of the sarray entries.  The
   returned sarray will have entry-freeing-function FREE_FUNC. */
bow_sarray *
bow_sarray_new_from_data_fp (int (*read_func)(void*,FILE*), 
			     void (*free_func)(),
			     FILE *fp)
{
  bow_sarray *ret;
  ret = malloc (sizeof (bow_sarray));
  ret->i4k = bow_int4str_new_from_fp (fp);
  ret->array = bow_array_new_from_data_fp (read_func, free_func, fp);
  return ret;
}

/* Return a new sarray, created by reading file-pointers I4K_FP and ARRAY_FP, and using
   the function READ_FUNC to read each of the icremental-format array entries from
   FP_ARRAY.  The returned sarray will have entry-freeing-function FREE_FUNC. */
bow_sarray *
bow_sarray_new_from_data_fps_inc (int (*read_func)(void*,FILE*), 
				  void (*free_func)(),
				  FILE *i4k_fp, FILE *array_fp)
{
  bow_sarray *ret;

  ret = malloc (sizeof (bow_sarray));
  ret->i4k = bow_int4str_new_from_fp_inc (i4k_fp);
  ret->array = bow_array_new_from_fp_inc (read_func, free_func, array_fp);
  
  return ret;
}

/* Free the memory held by the bow_sarray SA. */
void
bow_sarray_free (bow_sarray *sa)
{
  bow_array_free (sa->array);
  bow_int4str_free (sa->i4k);
  bow_free (sa);
}
