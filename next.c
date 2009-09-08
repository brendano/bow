/* Iterating through the documents in a wi2dvf, according to document type */

/* Copyright (C) 1998, 1999 Andrew McCallum

   Written by:  Andrew McCallum

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
#include <math.h>
#include <time.h>
#include <sys/time.h>
#include <unistd.h>

/* This function sets up the data structure so we can step through the word
   vectors for each test document easily. */
bow_dv_heap *
bow_test_new_heap (bow_barrel *barrel)
{
  return bow_make_dv_heap_from_wi2dvf (barrel->wi2dvf);
}

/* We only need this struct within the following functions. */
typedef struct _bow_tmp_word_struct {
  int wi;
  int count;
  float weight;
} bow_tmp_word_struct;

/* This function takes the heap returned by bow_initialise_test_set and
   creates a word vector corresponding to the next document in the test set.
   The index of the test document is returned. If the test set is empty, 0
   is returned and *wv == NULL. Also, when the test set is exhausted, the
   heap is free'd (since it can't be used for anything else anways.
   This can't really deal with vectors which are all zero, since they
   are not represented explicitly in our data structure. Not sure what
   we should/can do. */
int
bow_heap_next_wv_guts (bow_dv_heap *heap, bow_barrel *barrel, bow_wv **wv,
		       int (*use_if_true)(bow_cdoc*))
{
  bow_array *cdocs = barrel->cdocs;
  bow_array *word_array;
  bow_cdoc *doc;
  bow_tmp_word_struct word, *wptr;
  int current_di = -1;
  int i;

  word_array = bow_array_new (50, sizeof(bow_tmp_word_struct), 0);

  /* Keep going until we exhaust the heap or we find a test document. */
  while ((heap->length > 0) && (word_array->length == 0))
    {
      current_di = heap->entry[0].current_di;
      doc = bow_cdocs_di2doc(cdocs, current_di);

      if ((*use_if_true)(doc))
	{
	  /* We have the first entry for the next document */
	  do
	    {
	      word.wi = heap->entry[0].wi;
	      word.count = 
		heap->entry[0].dv->entry[heap->entry[0].index].count;
	      word.weight = 
		heap->entry[0].dv->entry[heap->entry[0].index].weight;
	      bow_array_append (word_array, &word);
	      bow_dv_heap_update (heap);
	    }
	  while ((heap->length > 0)
		 && (current_di == heap->entry[0].current_di));
	}
      else
	{
	  /* This document did not pass the test; go on to next document */
	  do
	    {
	      bow_dv_heap_update (heap);
	    }
	  while ((heap->length > 0)
		 && (current_di == heap->entry[0].current_di));
	}
    }

  /* Here we either have a word_array or else we've run out of documents. */

  if (word_array->length != 0)
    {
      if (*wv)
	bow_wv_free (*wv);

      /* We now have all the words for this test document in the word
	 array - need to create a bow_wv */
      (*wv) = bow_wv_new (word_array->length);
      for (i = 0; i < word_array->length; i++)
	{
	  wptr = bow_array_entry_at_index (word_array, i);
	  (*wv)->entry[i].wi = wptr->wi;
	  (*wv)->entry[i].count = wptr->count;
	  (*wv)->entry[i].weight = wptr->weight;
	}
    }
  else
    {
      /* Since we've run out of docs, might as well free the test set. */
      if (*wv)
	bow_wv_free (*wv);
      current_di = -1;
      (*wv) = NULL;
    }
  /* Should be finished with the word array now */

#if 1
  /* XXX this causes a seg fault - don't know why. */
  bow_array_free (word_array);
#else
  /* This does the same job for me. */
  bow_free (word_array->entries);
  bow_free (word_array);
#endif

  return current_di;
}

static bow_wv *empty_wv = NULL;

/* Use a heap to efficiently iterate through all the documents that
   satisfy the condition USE_IF_TRUE().  Each call to this function
   provides the next word vector in *WV, returning the `document index' DI of 
   the document contained in *WV.  This function returns -1 when there
   are no more documents left. */
int
bow_heap_next_wv (bow_dv_heap *heap, bow_barrel *barrel, bow_wv **wv,
		  int (*use_if_true)(bow_cdoc*))
{
  int new_di;
  bow_cdoc *doc_cdoc;

  /* Initialize EMPTY_WV if it hasn't been already. */
  if (!empty_wv)
    {
      empty_wv = bow_wv_new (1);
      empty_wv->num_entries = 0;
    }

  /* This special -2 value set in heap.c */
  if (heap->last_di == -2)
    {
      /* This is the first time this function is being called on this heap */
      /* Initialize the *WV to NULL so that the underlying heap function's
	 free()'ing behavior will work properly */
      *wv = NULL;
      /* Set this to -1 so that when NEW_DI gets incremented, it becomes 0
	 and we get the first document. */
      new_di = -1;
    }
  else
    {
      new_di = heap->last_di;
    }

  do 
    {
      new_di++;
      if (new_di >= barrel->cdocs->length)
	{
	  /* No more satisfying documents left */
	  heap->heap_wv_di = 
	    bow_heap_next_wv_guts (heap, barrel, &(heap->heap_wv),use_if_true);
	  assert (heap->heap_wv_di == -1);
	  bow_free (heap);
	  return -1;
	}
      doc_cdoc = bow_array_entry_at_index (barrel->cdocs, new_di);
    }
  while (!(*use_if_true) (doc_cdoc));

  /* NEW_DI is now definitely the document index we will return.
     It's just a question of whether that document is empty or not. */

  if (heap->heap_wv_di < new_di && heap->heap_wv_di != -1)
    heap->heap_wv_di = 
      bow_heap_next_wv_guts (heap, barrel, &(heap->heap_wv), use_if_true);
  assert (heap->heap_wv_di >= new_di || heap->heap_wv_di == -1);
  if (heap->heap_wv_di == new_di)
    *wv = heap->heap_wv;
  else
    *wv = empty_wv;

  heap->last_di = new_di;
  return new_di;
}

int
bow_cdoc_is_train (bow_cdoc *cdoc)
{
  return (cdoc->type == bow_doc_train);
}

int
bow_cdoc_is_test (bow_cdoc *cdoc)
{
  return (cdoc->type == bow_doc_test);
}

int 
bow_cdoc_yes (bow_cdoc *cdoc)
{
  return (1);
}

int 
bow_doc_yes (bow_doc *doc)
{
  return (1);
}

/* Return nonzero iff CDOC has type != TEST */
int 
bow_cdoc_is_nontest (bow_cdoc *cdoc)
{
  return (cdoc->type != bow_doc_test);
}

int 
bow_doc_is_nontest (bow_doc *doc)
{
  return (doc->type != bow_doc_test);
}

/* Return nonzero iff CDOC has type == IGNORE */
int 
bow_cdoc_is_ignore (bow_cdoc *cdoc)
{
  return(cdoc->type == bow_doc_ignore);
}

/* Return nonzero iff CDOC has type == bow_doc_unlabeled */
int 
bow_cdoc_is_unlabeled (bow_cdoc *cdoc)
{
  return (cdoc->type == bow_doc_unlabeled);
}

/* Return nonzero iff CDOC has type == bow_doc_validation */
int 
bow_cdoc_is_validation (bow_cdoc *cdoc)
{
  return (cdoc->type == bow_doc_validation);
}

/* Return nonzero iff CDOC has type == bow_doc_pool */
int 
bow_cdoc_is_pool (bow_cdoc *cdoc)
{
  return (cdoc->type == bow_doc_pool);
}

/* Return nonzero iff CDOC has type == bow_doc_waiting */
int 
bow_cdoc_is_waiting (bow_cdoc *cdoc)
{
  return (cdoc->type == bow_doc_waiting);
}

int 
bow_test_next_wv(bow_dv_heap *heap, bow_barrel *barrel, bow_wv **wv)
{
  return (bow_heap_next_wv(heap, barrel, wv, bow_cdoc_is_test));
}

int 
bow_nontest_next_wv(bow_dv_heap *heap, bow_barrel *barrel, bow_wv **wv)
{
  return (bow_heap_next_wv(heap, barrel, wv, bow_cdoc_is_nontest));
}

int 
bow_model_next_wv(bow_dv_heap *heap, bow_barrel *barrel, bow_wv **wv)
{
  return (bow_heap_next_wv(heap, barrel, wv, bow_cdoc_is_train));
}

