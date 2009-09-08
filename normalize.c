/* Functions for normalizing weights in a bow_barrel */

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

#if !HAVE_SQRTF
#define sqrtf sqrt
#endif

static float
_accumulate_for_vector_length (float total_so_far, float increment)
{
  return (total_so_far + (increment * increment));
}

static float
_finalize_for_vector_length (float total)
{
  return (1.0 / sqrtf (total));
}

static float
_accumulate_for_summing (float total_so_far, float increment)
{
  return (total_so_far + increment);
}

static float
_finalize_for_summing (float total)
{
  return (1.0 / total);
}


/* Calculate the normalizing factor by which each weight should be 
   multiplied.  Store it in each cdoc->normalizer. */
static void
_bow_barrel_normalize_weights (bow_barrel *barrel,
			       float (*accumulator)(float, float),
			       float (*finalizer)(float))
				    
{
  int current_di;		/* the index of the document for which
				   we are currently normalizing the
				   "word vector". */
  float norm_total;		/* the length of the word vector */
  float weight;			/* the weight of a single wi/di entry */
  bow_dv_heap *heap;		/* a heap of "document vectors" */
  bow_cdoc *cdoc;		/* The document we're working on */

  assert (barrel);

  heap = bow_make_dv_heap_from_wi2dvf (barrel->wi2dvf);

  bow_verbosify (bow_progress, "Normalizing weights:          ");

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

      /* If it's not a model document, then move on to next one */
      if (cdoc->type != bow_doc_train)
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
    
      /* Reset the length */
      norm_total = 0.0;

      /* Loop over all words in this document, summing up the score */
      do 
	{
	  weight = heap->entry[0].dv->entry[heap->entry[0].index].weight;
	  norm_total = (*accumulator)(norm_total, weight);

	  /* Update the heap, we are done with this di, move it to its
	     new position */
	  bow_dv_heap_update (heap);
	} 
      while ((current_di == heap->entry[0].current_di)
	     && (heap->length > 0));

      /* xxx Why isn't this always true? -am */
      /* assert (norm_total != 0); */

      /* Do final processing of, and store the result. */
      cdoc->normalizer = (*finalizer)(norm_total);

    }

  /* xxx We could actually re-set the weights using the normalizer now
     and avoid storing the normalizer.  This would be easier than
     figuring out the normalizer, because we don't have to use the heap
     again, we can just loop through all the WI's and DVI's. */

  bow_free (heap);
  bow_verbosify (bow_progress, "\n"); 
}

/* Normalize the weight-vector for each class (or document) such that
   all vectors have Euclidean length 1. */
void
bow_barrel_normalize_weights_by_vector_length (bow_barrel *barrel)
{
  _bow_barrel_normalize_weights (barrel, 
				 _accumulate_for_vector_length,
				 _finalize_for_vector_length);
}

/* Normalize the weight-vector for each class (or document) such that
   in all vectors, the elements of the vector sum to 1. */
void
bow_barrel_normalize_weights_by_summing (bow_barrel *barrel)
{
  _bow_barrel_normalize_weights (barrel, 
				 _accumulate_for_summing,
				 _finalize_for_summing);
}


/* Assign a value to the "word vector's" NORMALIZER field, such that
   when all the weights in the vector are multiplied by the
   NORMALIZER, the Euclidian length of the vector will be one. */
void
bow_wv_normalize_weights_by_vector_length (bow_wv *wv)
{
  float total = 0.0f;
  int wvi;

  if (wv->num_entries == 0)
    {
      wv->normalizer = 0;
      return;
    }

  for (wvi = 0; wvi < wv->num_entries; wvi++)
    total += wv->entry[wvi].weight * wv->entry[wvi].weight;

  if (total == 0)
    bow_error ("You forgot to set the weights before normalizing the WV.");
  wv->normalizer = 1.0 / sqrtf (total);
}

/* Assign a value to the "word vector's" NORMALIZER field, such that
   when all the weights in the vector are multiplied by the
   NORMALIZER, all the vector entries will to one. */
void
bow_wv_normalize_weights_by_summing (bow_wv *wv)
{
  float total = 0.0f;
  int wvi;

  if (wv->num_entries == 0)
    {
      wv->normalizer = 0;
      return;
    }

  for (wvi = 0; wvi < wv->num_entries; wvi++)
    total += wv->entry[wvi].weight;

  if (total == 0)
    bow_error ("You forgot to set the weights before normalizing the WV.");
  wv->normalizer = 1.0 / total;
}
