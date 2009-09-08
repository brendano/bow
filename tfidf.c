/* Weight-setting and scoring implementation for TFIDF. */

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

#if !HAVE_LOG2F
#define log2f log
#endif

#if !HAVE_SQRTF
#define sqrtf sqrt
#endif

/* The number of documents with non-zero dot-product with the query. 
   Set in bow_tfidf_score(). */
int bow_tfidf_num_hit_documents;

#define DOING_LOG_COUNTS 1


/* Function to assign TFIDF weights to each element of each document
   vector. */
static void
bow_tfidf_set_weights (bow_barrel *barrel)
{
  int wi;			/* a "word index" into WI2DVF */
  int max_wi;			/* the highest "word index" in WI2DVF. */
  int ndocs;                    /* number of train documents looked at */
  double idf;			/* The IDF factor for a word */
  bow_dv *dv;			/* the "document vector" at index WI */
  double df;			/* "document frequency" */
#if 0
  int total_word_count;		/* total "document frequency" over all words */
#endif
  int dvi;			/* an index into the DV */
  bow_cdoc *cdoc;

  bow_verbosify (bow_progress, "Setting weights over words:          ");
  max_wi = MIN(barrel->wi2dvf->size, bow_num_words());

#if 0
  /* For certain cases we need to loop over all dv's to compute the
     total number of word counts across all words and all documents. */
  /* xxx Shouldn't this be changed, and put in the `for(wi..' loop below? */
  if (((bow_params_tfidf*)(barrel->method->params))->df_counts
      == bow_tfidf_occurrences)
    {
      total_word_count = 0;
      for (wi = 0; wi < max_wi; wi++) 
	{
	  dv = bow_wi2dvf_dv (barrel->wi2dvf, wi);
	  if (dv == NULL)
	    continue;
	  /* We have the document information, so we can determine which
	     documents are part the the training set. */
	  for (dvi = 0; dvi < dv->length; dvi++) 
	    {
	      cdoc = bow_cdocs_di2doc (barrel->cdocs, dv->entry[dvi].di);
	      if (cdoc->type == model)
		total_word_count += dv->entry[dvi].count;
	    }
	}
    }
#endif

  /* figure out the number of training documents */
  for (wi=ndocs=0; wi<barrel->cdocs->length; wi++) {
    cdoc = bow_array_entry_at_index (barrel->cdocs, wi);
    if (cdoc->type == bow_doc_train) {
      ndocs++;
    }
  }

  /* Loop over all vectors of documents (i.e. each word), calculate
     the IDF, then set the weights */
  for (wi = 0; wi < max_wi; wi++) 
    {
      /* Get the document vector for this word WI */
      dv = bow_wi2dvf_dv (barrel->wi2dvf, wi);

      if (dv == NULL)
	continue;

      /* Calculate the IDF, the "inverse document frequency". */

      /* First calculate document frequency. */
      df = 0;
      for (dvi = 0; dvi < dv->length; dvi++)
	{
	  cdoc = bow_array_entry_at_index (barrel->cdocs, dv->entry[dvi].di);
	  if (cdoc->type != bow_doc_train)
	    continue;
	  if (((bow_params_tfidf*)(barrel->method->params))->df_counts
	      == bow_tfidf_occurrences)
	    {
	      /* Make DF be the number of documents in which word WI occurs 
		 at least once.  (We can't just set it to DV->LENGTH because
		 we have to check to make sure each document is part of the
		 model. */
	      df++;
	    }
	  else if (((bow_params_tfidf*)(barrel->method->params))->df_counts
		   == bow_tfidf_words)
	    {
	      /* Make DF be the total number of times word WI appears
		 in any document. */
	      df += dv->entry[dvi].count;
	    }
	  else
	    bow_error ("Bad TFIDF parameter df_counts.");
	}

      /* Set IDF from DF. */
      if (df == 0) 
	{
	  /* There are no training documents with this word - ignore */
	  idf = 0.0;
	}
      else
	{
	  if (((bow_params_tfidf*)(barrel->method->params))->df_transform
	      == bow_tfidf_log)
	    idf = log2f (ndocs / df);
	  else if (((bow_params_tfidf*)(barrel->method->params))->df_transform
		   == bow_tfidf_sqrt)
	    idf = sqrtf (ndocs / df);
	  else if (((bow_params_tfidf*)(barrel->method->params))->df_transform
		   == bow_tfidf_straight)
	    idf = ndocs / df;
	  else
	    {
	      idf = 0;		/* to avoid gcc warning */
	      bow_error ("Bad TFIDF parameter df_transform.");
	    }
	}
      assert (idf == idf);	/* Make sure we don't have NaN. */

      /* Now loop through all the elements, setting their weights */
      for (dvi = 0; dvi < dv->length; dvi++)
#if DOING_LOG_COUNTS
	dv->entry[dvi].weight = log (dv->entry[dvi].count + 1) * idf;
#else
	dv->entry[dvi].weight = dv->entry[dvi].count * idf;
#endif
      
      /* Record this word's idf */
      dv->idf = idf;

      if (wi % 10 == 0)
	bow_verbosify (bow_progress, "\b\b\b\b\b\b%6d", max_wi - wi - 1); 
    }
  bow_verbosify (bow_progress, "\n");
}


/* Function to fill an array of the best matches to the document
   described by wv from the corpus in wi2dvf. There are 'best' elements
   in this array, in decreaseing order of their score. Nothing we can
   do if the we need more space to hold docs with the same scores as the
   final document in this list. The number of elements in the array is
   returned. The cdocs array is checked to make sure the
   document is in the model before getting it's vector product with
   the wv. Also, if the length field in cdocs is non-zero, then the
   product is divided by that length. */
int
bow_tfidf_score_old (bow_barrel *barrel, bow_wv *query_wv, 
		 bow_score *scores, int best, int loo_class)
{
  bow_dv_heap *heap;
  bow_cdoc *doc;
  int num_scores = 0;		/* How many elements are in this array */
  int current_di, wi, current_index, i;
  double current_score = 0.0, target_weight;
  float idf;

#if 0
  if (loo_class >= 0)
    bow_error ("PrInd cannot implement Leave-One-Out scoring.");
#endif

  /* Set the weights in the QUERY_WV.  Note: this is duplication of
     effort, since it was already done, but it was done incorrectly
     before, without the IDF. */
  bow_wv_set_weights_to_count_times_idf (query_wv, barrel);
  bow_wv_normalize_weights_by_vector_length (query_wv);

  if (query_wv->normalizer == 0)
    bow_error ("You forgot to set the weight normalizer of the QUERY_WV");

  /* Create the Heap of vectors of documents */
  heap = bow_make_dv_heap_from_wv (barrel->wi2dvf, query_wv);

  /* Keep looking at document/word entries until the heap is emptied */
  while (heap->length > 0)
    {
      /* Get the index of the document we're currently working on */
      current_di = heap->entry[0].current_di;

      /* Get the document structure */
      doc = bow_cdocs_di2doc (barrel->cdocs, current_di);
    
      /* If it's not a model document, then move on to next one */
      if (doc->type != bow_doc_train)
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

      /* Reset the index into out word vector */
      current_index = 0;

      /* Reset the weight */
      current_score = 0.0;

      /* Loop over all the words in this document, summing up the score */
      do
	{
	  wi = heap->entry[0].wi;
	  target_weight = 
	    heap->entry[0].dv->entry[heap->entry[0].index].weight;
	  /* We don't include NORMALIZER here because we multiple by it
	     all at once below. */

	  /* Find the correspoding word in our word vector */
	  while (wi > (query_wv->entry[current_index].wi))
	    current_index++;
	  assert (wi == query_wv->entry[current_index].wi);

	  /* Put in the contribution of this word */
	  /* xxx Under what conditions will IDF be zero?  Does the
	     right thing happen? */
	  idf = heap->entry[0].dv->idf;
	  assert (idf == idf);	/* testing for NaN */
	  /* xxx Why was this here?  assert (idf && idf > 0); */
	  current_score += 
	    (target_weight
	     * (query_wv->entry[current_index].weight 
		* query_wv->normalizer));

	  /* A test to make sure we haven't got NaN. */
	  assert (current_score == current_score);

	  /* Now we need to update the heap - moving this element on to its
	     new position */
	  bow_dv_heap_update (heap);
	}
      while ((current_di == heap->entry[0].current_di)
	     && (heap->length > 0));

      /* It is OK to normalize here instead of inside do-while loop 
	 above because we are summing the weights, and we can just
	 factor out the NORMALIZER. */
      assert (doc->normalizer > 0);
      current_score *= doc->normalizer;

      assert (current_score == current_score); /* checking for NaN */

      /* Store the result in the SCORES array */
      /* If we haven't filled the list, or we beat the last item in the list */
      if ((num_scores < best)
	  || (scores[num_scores - 1].weight < current_score))
	{
	  /* We're going to search up the list comparing element i-1 with
	     our current score and moving it down the list if it's worse */
	  if (num_scores < best)
	    {
	      i = num_scores;
	      num_scores++;
	    }
	  else
	    i = num_scores - 1;

	  /* Shift down all the bits of the array that need shifting */
	  for (; (i > 0) && (scores[i - 1].weight < current_score); i--)
	    scores[i] = scores[i-1];

	  /* Insert our new score */
	  scores[i].weight = current_score;
	  scores[i].di = current_di;
	}
    }

  bow_free (heap);

  /* All done - return the number of elements we have */
  return num_scores;
}



int
bow_tfidf_score (bow_barrel *barrel, bow_wv *query_wv, 
		 bow_score *scores, int scores_size, int loo_class)
{
  int num_scores = 0;		/* How many elements are in this array */
  int ci, i;
  float *lscores;
  int **wis;
  int wvi, dvi;
  bow_cdoc *cdoc;
  int num_hit_documents = 0;

#if 0
  if (loo_class >= 0)
    bow_error ("PrInd cannot implement Leave-One-Out scoring.");
#endif

  /* Yuck.  This is inefficient. */
  lscores = bow_malloc (barrel->cdocs->length * sizeof (float));
  wis = bow_malloc (barrel->cdocs->length * sizeof (int*));
  for (i = 0; i < barrel->cdocs->length; i++)
    {
      lscores[i] = 0;
      wis[i] = NULL;
    }

  /* Set the weights in the QUERY_WV.  Note: this is duplication of
     effort, since it was already done, but it was done incorrectly
     before, without the IDF. */
#if DOING_LOG_COUNTS
  bow_wv_set_weights_to_log_count_times_idf (query_wv, barrel);
#else
  bow_wv_set_weights_to_count_times_idf (query_wv, barrel);
#endif
  bow_wv_normalize_weights_by_vector_length (query_wv);

  for (wvi = 0; wvi < query_wv->num_entries; wvi++)
    {
      bow_dv *dv = bow_wi2dvf_dv (barrel->wi2dvf, query_wv->entry[wvi].wi);

      /* If the model doesn't know about this word, skip it. */
      if (!dv)
	continue;

      /* Loop over all documents/classes that contain word WI,
	 and increment their score. */
      for (dvi = 0; dvi < dv->length; dvi++)
	{
	  cdoc = bow_array_entry_at_index (barrel->cdocs, dv->entry[dvi].di);
	  lscores[dv->entry[dvi].di] += ((query_wv->entry[wvi].weight
					  * query_wv->normalizer)
					 * (dv->entry[dvi].weight
					    * cdoc->normalizer));
	  if (wis[dv->entry[dvi].di] == NULL)
	    {
	      int j;
	      wis[dv->entry[dvi].di] = bow_malloc ((query_wv->num_entries+1)
						   * sizeof (int));
	      for (j = 0; j < (query_wv->num_entries+1); j++)
		wis[dv->entry[dvi].di][j] = -1;
	    }
	  i = 0;
	  while (wis[dv->entry[dvi].di][i] >= 0)
	    i++;
	  assert (i <= query_wv->num_entries);
	  wis[dv->entry[dvi].di][i] = query_wv->entry[wvi].wi;
	  /* Why was this here? 
	     assert (wis[dv->entry[dvi].di][i] != 1); */
	}
    } 

  for (ci = 0; ci < barrel->cdocs->length; ci++)
    {
      if (lscores[ci] == 0)
	continue;
      
      num_hit_documents++;
      
      /* Store the result in the SCORES array */
      /* If we haven't filled the list, or we beat the last item in the list */
      if ((num_scores < scores_size)
	  || (scores[num_scores - 1].weight < lscores[ci]))
	{
	  /* We're going to search up the list comparing element i-1 with
	     our current score and moving it down the list if it's worse */
	  if (num_scores < scores_size)
	    {
	      i = num_scores;
	      num_scores++;
	    }
	  else
	    i = num_scores - 1;

	  /* Shift down all the bits of the array that need shifting */
	  for (; (i > 0) && (scores[i - 1].weight < lscores[ci]); i--)
	    scores[i] = scores[i-1];

	  /* Insert our new score */
	  scores[i].weight = lscores[ci];
	  scores[i].di = ci;

#if 1
	  {
	    /* Store the appearing words in NAME. */
	    char buf[BOW_MAX_WORD_LENGTH * query_wv->num_entries];
	    int j;

	    assert (wis[ci]);
	    buf[0] = '\0';
	    for (j = 0; wis[ci][j] >= 0; j++)
	      {
		strcat (buf, bow_int2word (wis[ci][j]));
		strcat (buf, " ");
	      }
	    scores[i].name = strdup (buf);
	    assert (scores[i].name);
	  }
#else
	  scores[i].name = NULL;
#endif
	}
    }

  bow_free (lscores);
  for (i = 0; i < barrel->cdocs->length; i++)
    {
      if (wis[i])
	bow_free (wis[i]);
    }
  bow_free (wis);

  bow_tfidf_num_hit_documents = num_hit_documents;

  /* All done - return the number of elements we have */
  return num_scores;
}

bow_params_tfidf bow_tfidf_params_tfidf_words =
{
  bow_tfidf_words,
  bow_tfidf_straight
};

bow_params_tfidf bow_tfidf_params_tfidf_log_words =
{
  bow_tfidf_words,
  bow_tfidf_log
};

bow_params_tfidf bow_tfidf_params_tfidf =
{
  bow_tfidf_occurrences,
  bow_tfidf_log
};

bow_params_tfidf bow_tfidf_params_tfidf_log_occur =
{
  bow_tfidf_occurrences,
  bow_tfidf_log
};

#define TFIDF_METHOD(PARAM_NAME)					\
rainbow_method bow_method_ ## PARAM_NAME =				\
{									\
  #PARAM_NAME,								\
  bow_tfidf_set_weights,						\
  0,				/* no weight scaling function */	\
  bow_barrel_normalize_weights_by_vector_length,			\
  bow_barrel_new_vpc_weight_then_merge,					\
  0,				/* no prior-setting function */		\
  bow_tfidf_score,							\
  bow_wv_set_weights_to_count_times_idf,				\
  bow_wv_normalize_weights_by_vector_length,				\
  bow_barrel_free,							\
  &bow_tfidf_params_ ## PARAM_NAME					\
};									\
void _register_method_ ## PARAM_NAME ()					\
 __attribute__ ((constructor));						\
void _register_method_ ## PARAM_NAME ()					\
{									\
  bow_method_register_with_name ((bow_method*)&                         \
                                 bow_method_ ## PARAM_NAME,		\
				 #PARAM_NAME,				\
				 sizeof (rainbow_method),               \
				 NULL);					\
}

TFIDF_METHOD(tfidf_words)
TFIDF_METHOD(tfidf_log_words)
TFIDF_METHOD(tfidf_log_occur)
TFIDF_METHOD(tfidf)
