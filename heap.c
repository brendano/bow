/* Implementation of the ?? heap for the bow library */

/* Copyright (C) 1997, 1998, 1999 Andrew McCallum

   Written by:  Sean Slattery <slttery@cs.cmu.edu>

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

/* Some HEAP thingys */
#define PARENT(i) ((i) / 2)
#define LEFT(i) (2 * (i))
#define RIGHT(i) (2 * (i) + 1)

/* We index from 1->length - do the translation back to 0-(length-1) here */
#define HEAP_ELT(i) (heap->entry[i-1])
#define HEAP_KEY(i) (HEAP_ELT(i).current_di)
#define HEAP_WI(i)  (HEAP_ELT(i).wi) 

/* Return true if a belongs higher up the heap than b. For a Max Heap the
   compare should be >=, and for a Min Heap it should be <= */
#define HEAP_CMP(a,b) ((a) < (b))

/* Comparing the wi's mean we get the words out in order from lowest to 
   highest. */
#define HEAP_HIGHER(a,b) (HEAP_CMP(HEAP_KEY(a), HEAP_KEY(b)) || ((HEAP_KEY(a) == HEAP_KEY(b)) && (HEAP_WI(a) < HEAP_WI(b))))

/* Function to turn an array of bow_dv_heap_elements into a proper heap */
void
bow_heapify(bow_dv_heap *heap, int i)
{
  int l, r, highest;
  bow_dv_heap_element tmp;

  l = LEFT(i);
  r = RIGHT(i);

  if ((l <= heap->length) && 
      HEAP_HIGHER(l, i))
    highest = l;
  else
    highest = i;

  if ((r <= heap->length) && 
      HEAP_HIGHER(r, highest))
    highest = r;

  if (highest != i) {
    tmp = HEAP_ELT(i);
    HEAP_ELT(i) = HEAP_ELT(highest);
    HEAP_ELT(highest) = tmp;

    /* Need to recurse to fix the subtree */
    bow_heapify (heap, highest);
  }
}

/* Function to create a heap of the vectors of documents associated
   with each word in the word vector. */
bow_dv_heap *
bow_make_dv_heap_from_wv (bow_wi2dvf *wi2dvf, bow_wv *wv)
{
  int wv_index;			/* an index into the "word vector" */
  int heap_index;		/* an index into the heap we are creating */
  int wi, i;
  bow_dv_heap *heap;
  bow_dv *dv;

  heap = bow_malloc (sizeof (bow_dv_heap) 
		     + (sizeof (bow_dv_heap_element) 
			* (wv->num_entries)));

  /* Dump all the elements into the heap */
  heap_index = 0;
  for (wv_index = 0; wv_index < wv->num_entries; wv_index++)
    {
      /* Get the word index */
      wi = wv->entry[wv_index].wi;  

      /* Now fetch the list of documents associated with this word */
      /* Use this function instead of accessing the wi2dvf->entry
	 directly because the dv may need to be read from a file. */
      dv = bow_wi2dvf_dv (wi2dvf, wi);
      if (!dv)
	continue;
      heap->entry[heap_index].dv = dv;
      assert (dv->idf == dv->idf); /* Check for NaN */
      heap->entry[heap_index].wi = wi;
      heap->entry[heap_index].index = 0;
      heap->entry[heap_index].current_di = dv->entry[0].di;
      heap_index++;
    }

  /* Initialise the heap */
  heap->length = heap_index;

  /* Now need to make this baby into a heap. We'll use i to index into
     a conceptual array with indices 1..length and convert those
     references to index an array of 0..(length-1) when required. */
  for (i = (heap->length)/2; i > 0; i--) 
    bow_heapify (heap, i);

  return heap;
}

/* Function to take the top element of the heap - move it's index along and
   place it back in the heap. */
void
bow_dv_heap_update (bow_dv_heap *heap)
{
  bow_dv_heap_element *top = &(heap->entry[0]);

  /* Increment the index */
  (top->index)++;

  /* Check to make sure we have elements left to look at */
  if (top->index < top->dv->length) 
    {
      top->current_di = top->dv->entry[top->index].di;

      /* Heapify!! */
      bow_heapify (heap, 1);

    }
  else
    {
      /* Here we draft in the end of the heap and Heapify */
      heap->entry[0] = heap->entry[(heap->length) - 1];

      (heap->length)--;

      bow_heapify (heap, 1);
    }
}

/* Function to make a heap from all the vectors of documents in the
   big data structure we've built.  If EVEN_IF_HIDDEN is non-zero,
   then words that have been "hidden" (by feature selection, for
   example) will none-the-less also be included in the WV's returned
   by future calls to the heap; think carefully before you do this! */
bow_dv_heap *
bow_make_dv_heap_from_wi2dvf_hidden (bow_wi2dvf *wi2dvf, int even_if_hidden)
{
  int wi;			/* a "word index", index into WI2DVF */
  int max_wi;			/* the highest "word index" */
  int hi;			/* a "heap index", an index into the heap */
  bow_dv_heap *heap;		/* what we are creating and returning */
  bow_dv *dv;

  max_wi = MIN (wi2dvf->size, bow_num_words ());
  heap = bow_malloc (sizeof (bow_dv_heap) 
		     + (sizeof (bow_dv_heap_element) * (max_wi)));

  /* Dump all the vectors of documents into the array */
  hi = 0;
  for (wi = 0; wi < max_wi; wi++)
    {
      dv = bow_wi2dvf_dv_hidden (wi2dvf, wi, even_if_hidden);
      if (dv)
	{
	  heap->entry[hi].dv = dv;
	  heap->entry[hi].wi = wi;
	  heap->entry[hi].index = 0;
	  heap->entry[hi].current_di = dv->entry[0].di;
	  /* xxx It would be nice to check for values too high, also.*/
	  assert (dv->entry[0].di >= 0);
	  hi++;
	}
    }

  /* Initialise the Heap */
  heap->length = hi;
  for (hi = (heap->length)/2; hi > 0; hi--) 
    bow_heapify (heap, hi);

  heap->heap_wv = NULL;
  /* This special -2 value used in split.c */
  heap->heap_wv_di = -2;
  heap->last_di = -2;

  return heap;
}

/* Function to make a heap from all the vectors of documents in the
   big data structure we've built.  */
bow_dv_heap *
bow_make_dv_heap_from_wi2dvf (bow_wi2dvf *wi2dvf)
{
  return bow_make_dv_heap_from_wi2dvf_hidden (wi2dvf, 0);
}

/* Free a heap.  Seldom needs to be called from outside this function
   since it is done automatically by the bow_*_next_wv() functions. */
void
bow_dv_heap_free (bow_dv_heap *heap)
{
  bow_free (heap);
}
