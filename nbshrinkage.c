/* A simple version of Naive-Bayes classification */

/* Copyright (C) 1999 Andrew McCallum

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
#include <math.h>

static float nbshrinkage_total_num_words;

/* Each of these are arrays indexed by class index */
static float *nbshrinkage_lambda_local;
static float *nbshrinkage_lambda_root;
static float *nbshrinkage_lambda_uniform;

/* Get the total number of terms in each class; store this in
   CDOC->WORD_COUNT.  Also, get the total number of occurrences of
   each word across all classes and put this in WI2DVF->DV->IDF.  Also
   put the total number of words across all classes into the global
   variable NBSHRINKAGE_TOTAL_NUM_WORDS.  Also, set the LAMBDA_UNIFORM
   according the proportion of singletons (roughly following the
   Good-Turing principle), and set the other two lambdas close to uniform
   in a fixed way. */
void
bow_nbshrinkage_set_cdoc_word_count_from_wi2dvf_weights (bow_barrel *barrel)
{
  int ci;
  bow_cdoc *cdoc;
  int wi, max_wi;
  bow_dv *dv;
  int dvi;
  int num_classes = bow_barrel_num_classes (barrel);
  double num_words_per_ci[num_classes];
  int *num_global_singletons;
  int *num_local_singletons;
  int *num_local_exclusivetons;
  int total_num_singletons = 0;

  nbshrinkage_lambda_local = bow_malloc (num_classes * sizeof (float));
  nbshrinkage_lambda_root = bow_malloc (num_classes * sizeof (float));
  nbshrinkage_lambda_uniform = bow_malloc (num_classes * sizeof (float));

  num_global_singletons = alloca (num_classes * sizeof (int));
  num_local_singletons = alloca (num_classes * sizeof (int));
  num_local_exclusivetons = alloca (num_classes * sizeof (int));
  for (ci = 0; ci < num_classes; ci++)
    {
      num_global_singletons[ci] = 0;
      num_local_singletons[ci] = 0;
    }

  nbshrinkage_total_num_words = 0;
  for (ci = 0; ci < num_classes; ci++)
    num_words_per_ci[ci] = 0;
  max_wi = MIN (barrel->wi2dvf->size, bow_num_words());
  for (wi = 0; wi < max_wi; wi++) 
    {
      dv = bow_wi2dvf_dv (barrel->wi2dvf, wi);
      if (dv == NULL)
	continue;
      dv->idf = 0;
      for (dvi = 0; dvi < dv->length; dvi++) 
	{
	  cdoc = bow_array_entry_at_index (barrel->cdocs, 
					   dv->entry[dvi].di);
	  ci = dv->entry[dvi].di;
	  assert (ci < num_classes);
	  num_words_per_ci[ci] += dv->entry[dvi].weight;
	  if (cdoc->type == bow_doc_train)
	    {
	      dv->idf += dv->entry[dvi].weight;
	      nbshrinkage_total_num_words += dv->entry[dvi].weight;
	    }
	}
      assert (dv->idf > 0);
      /* Calculate the number of singletons (both global and local) */
      if (dv->idf <= 1)
	total_num_singletons++;
      for (dvi = 0; dvi < dv->length; dvi++) 
	{
	  cdoc = bow_array_entry_at_index (barrel->cdocs, 
					   dv->entry[dvi].di);
	  ci = dv->entry[dvi].di;
	  if (dv->entry[dvi].weight <= 1)
	    {
	      if (dv->idf <= 1)
		num_global_singletons[ci]++;
	      else
		num_local_singletons[ci]++;
	    }
	  else if (dv->entry[dvi].weight == dv->idf)
	    num_local_exclusivetons[ci]++;
	}
    }
  for (ci = 0; ci < barrel->cdocs->length; ci++)
    {
      cdoc = bow_array_entry_at_index (barrel->cdocs, ci);
      cdoc->word_count = (int) rint (num_words_per_ci[ci]);
    }
#if 0
  for (ci = 0; ci < num_classes; ci++)
    {
      nbshrinkage_lambda_uniform[ci] = (num_global_singletons[ci]
					/ num_words_per_ci[ci]);
      nbshrinkage_lambda_root[ci] = (num_local_singletons[ci]
				     / num_words_per_ci[ci]);
      nbshrinkage_lambda_local[ci] = (1 - nbshrinkage_lambda_uniform[ci]
				      - nbshrinkage_lambda_root[ci]);
      cdoc = bow_array_entry_at_index (barrel->cdocs, ci);
      bow_verbosify (bow_progress,
		     "Class %s\n"
		     "Total number of words = %d\n"
		     "Number of global singleton words = %d\n"
		     "Number of local singleton words = %d\n"
		     "lambda[local  ] = %f\n"
		     "lambda[root   ] = %f\n"
		     "lambda[uniform] = %f\n",
		     bow_barrel_classname_at_index (barrel, ci),
		     cdoc->word_count,
		     num_global_singletons[ci], num_local_singletons[ci],
		     nbshrinkage_lambda_local[ci],
		     nbshrinkage_lambda_root[ci],
		     nbshrinkage_lambda_uniform[ci]);
    }
#else
  for (ci = 0; ci < num_classes; ci++)
    {
      nbshrinkage_lambda_uniform[ci] = (total_num_singletons
					/ nbshrinkage_total_num_words);
      nbshrinkage_lambda_root[ci] = (1 - nbshrinkage_lambda_uniform[ci]) * .4;
      nbshrinkage_lambda_local[ci] = (1 - nbshrinkage_lambda_uniform[ci]) * .6;
    }
  bow_verbosify (bow_progress,
		 "Number of singleton words = %d\n"
		 "lambda[local  ] = %f\n"
		 "lambda[root   ] = %f\n"
		 "lambda[uniform] = %f\n",
		 total_num_singletons,
		 nbshrinkage_lambda_local[0],
		 nbshrinkage_lambda_root[0],
		 nbshrinkage_lambda_uniform[0]);
#endif
}

/* Return the probability of of word WI in the multinomial trained by 
   data from all classes. */
float
bow_nbshrinkage_pr_w_root (bow_barrel *barrel, int wi)
{
  bow_dv *dv;
  int nw;
  dv = bow_wi2dvf_dv (barrel->wi2dvf, wi);
  if (dv)
    nw = dv->idf;
  else
    nw = 0;
  return nw / nbshrinkage_total_num_words;
}

#define IMPOSSIBLE_SCORE_FOR_ZERO_CLASS_PRIOR 999.99

int
bow_nbshrinkage_score (bow_barrel *barrel, bow_wv *query_wv, 
		      bow_score *bscores, int bscores_len,
		      int loo_class)
{
  double *scores;		/* will become prob(class), indexed over CI */
  int ci;			/* a "class index" (document index) */
  int wvi;			/* an index into the entries of QUERY_WV. */
  int dvi;			/* an index into a "document vector" */
  double pr_w_given_c;		/* P(w|C), prob a word is in a class */
  double rescaler;		/* Rescale SCORES by this after each word */
  double new_score;		/* a temporary holder */
  int num_scores = 0;		/* number of entries placed in SCORES */
  int num_words_in_query = 0;	/* the total number of words in the QUERY_WV */
  int wi;			/* a word index */
  int max_wi;			/* one larger than the highest WI ever used */
  bow_dv *dv;			/* a "document vector", list of docs */
  int vocabulary_size;		/* the number of words ever used */
  double scores_sum;		/* sum of scores for normalization at end */
  bow_cdoc *cdoc;		/* information about a class */
  float count_w_in_c;		/* the # of times a word occurs in a class */
  
  /* This implementation only implements the bow_event_word and
     bow_event_document_then_word event models. */
  assert (bow_event_model != bow_event_document);
  
  max_wi = MIN (barrel->wi2dvf->size, bow_num_words());
  vocabulary_size = barrel->wi2dvf->num_words;

  /* Allocate space to store scores for *all* classes (documents) */
  scores = alloca (barrel->cdocs->length * sizeof (double));

  /* Instead of multiplying probabilities, we will sum up
     log-probabilities, (so we don't loose floating point resolution),
     and then take the exponent of them to get probabilities back. */

  /* Initialize the SCORES to the class prior probabilities. */
  for (ci = 0; ci < barrel->cdocs->length; ci++)
    {
      cdoc = bow_array_entry_at_index (barrel->cdocs, ci);
      if (cdoc->prior == 0)
	scores[ci] = IMPOSSIBLE_SCORE_FOR_ZERO_CLASS_PRIOR;
      else
	scores[ci] = log (cdoc->prior);
    }

  /* Get the total number of words in this query. */
  num_words_in_query = 0;
  for (wvi = 0; wvi < query_wv->num_entries; wvi++)
    {
      /* Only count those words that are in the model's vocabulary. */
      dv = bow_wi2dvf_dv (barrel->wi2dvf, query_wv->entry[wvi].wi);
      if (dv)
	num_words_in_query += query_wv->entry[wvi].count;
    }

  /* Set the weights of the QUERY_WV, according to the event model. */
  for (wvi = 0; wvi < query_wv->num_entries; wvi++)
    {
      if (bow_event_model == bow_event_document_then_word)
	query_wv->entry[wvi].weight = 
	  bow_event_document_then_word_document_length
	  * ((float)query_wv->entry[wvi].count) / num_words_in_query;
      else
	query_wv->entry[wvi].weight = query_wv->entry[wvi].count;
    }

  /* Put contribution of the words into SCORES.  */
  for (wvi = 0; wvi < query_wv->num_entries; wvi++)
    {
      /* Get information about this word. */
      wi = query_wv->entry[wvi].wi;
      dv = bow_wi2dvf_dv (barrel->wi2dvf, wi);

      /* If the model doesn't know about this word, skip it. */
      if (!dv)
	continue;

      rescaler = DBL_MAX;

      /* Loop over all classes, putting this word's (WI's)
	 contribution into SCORES. */
      for (ci = 0, dvi = 0; ci < barrel->cdocs->length; ci++)
	{
	  if (scores[ci] == IMPOSSIBLE_SCORE_FOR_ZERO_CLASS_PRIOR)
	    continue;

	  /* Find the number of times this word occurred in class CI */
	  while (dvi < dv->length && dv->entry[dvi].di < ci)
	    dvi++;
	  if (dvi < dv->length && dv->entry[dvi].di == ci)
	    count_w_in_c =  dv->entry[dvi].weight;
	  else 
	    count_w_in_c =  0;

	  cdoc = bow_array_entry_at_index (barrel->cdocs, ci);

	  /* Estimate P(w|c) using Shrinkage */
	  pr_w_given_c = ((nbshrinkage_lambda_local[ci]
			   * count_w_in_c / cdoc->word_count)
			  + (nbshrinkage_lambda_root[ci]
			     * bow_nbshrinkage_pr_w_root (barrel, wi))
			  + (nbshrinkage_lambda_uniform[ci]
			     * 1.0 / barrel->wi2dvf->num_words));

	  scores[ci] += query_wv->entry[wvi].weight * log (pr_w_given_c);

	  /* Keep track of the minimum score updated for this word. */
	  if (rescaler > scores[ci])
	    rescaler = scores[ci];
	}

      /* Loop over all classes, re-scaling SCORES so that they
	 don't get so small we loose floating point resolution.
	 This scaling always keeps all SCORES positive. */
      for (ci = 0; ci < barrel->cdocs->length; ci++)
	{
	  /* Add to SCORES to bring them close to zero.  RESCALER is
	     expected to often be less than zero here. */
	  if (scores[ci] != IMPOSSIBLE_SCORE_FOR_ZERO_CLASS_PRIOR)
	    scores[ci] += -rescaler;
	}
    }
  /* Now SCORES[] contains a (unnormalized) log-probability for each class. */

  /* Rescale the SCORE one last time, this time making them all -2 or
     more negative, so that exp() will work well, especially around
     the higher-probability classes. */
  rescaler = -DBL_MAX;
  for (ci = 0; ci < barrel->cdocs->length; ci++)
    if (scores[ci] > rescaler
	&& scores[ci] != IMPOSSIBLE_SCORE_FOR_ZERO_CLASS_PRIOR)
      rescaler = scores[ci];
  rescaler += 2.0;
  /* RESCALER is now the maximum of the SCORES. */
  for (ci = 0; ci < barrel->cdocs->length; ci++)
    if (scores[ci] != IMPOSSIBLE_SCORE_FOR_ZERO_CLASS_PRIOR)
      scores[ci] -= rescaler;

  /* Use exp() on the SCORES to get probabilities from
     log-probabilities. */
  for (ci = 0; ci < barrel->cdocs->length; ci++)
    {
      new_score = exp (scores[ci]);
      /* assert (new_score > 0 && new_score < DBL_MAX - 1.0e5); */
      if (scores[ci] != IMPOSSIBLE_SCORE_FOR_ZERO_CLASS_PRIOR)
	scores[ci] = new_score;
    }

  /* Normalize the SCORES so they all sum to one. */
  scores_sum = 0;
  for (ci = 0; ci < barrel->cdocs->length; ci++)
    if (scores[ci] != IMPOSSIBLE_SCORE_FOR_ZERO_CLASS_PRIOR)
      scores_sum += scores[ci];
  for (ci = 0; ci < barrel->cdocs->length; ci++)
    if (scores[ci] != IMPOSSIBLE_SCORE_FOR_ZERO_CLASS_PRIOR)
      scores[ci] /= scores_sum;
 
  /* Return the SCORES by putting them (and the `class indices') into
     SCORES in sorted order.  Do it by insertion sort because the
     number of scores that were requested to be returned may be much
     smaller than the actually number of classes. */
  num_scores = 0;
  for (ci = 0; ci < barrel->cdocs->length; ci++)
    {
      if (scores[ci] == IMPOSSIBLE_SCORE_FOR_ZERO_CLASS_PRIOR)
	scores[ci] = -DBL_MAX;
      if (num_scores < bscores_len
	  || bscores[num_scores-1].weight < scores[ci])
	{
	  /* We are going to put this score and CI into SCORES
	     because either: (1) there is empty space in SCORES, or
	     (2) SCORES[CI] is larger than the smallest score there
	     currently. */
	  int dsi;		/* an index into SCORES */
	  if (num_scores < bscores_len)
	    num_scores++;
	  dsi = num_scores - 1;
	  /* Shift down all the entries that are smaller than SCORES[CI] */
	  for (; dsi > 0 && bscores[dsi-1].weight < scores[ci]; dsi--)
	    bscores[dsi] = bscores[dsi-1];
	  /* Insert the new score */
	  bscores[dsi].weight = scores[ci];
	  bscores[dsi].di = ci;
	}
    }

  return num_scores;
}

rainbow_method bow_method_nbshrinkage = 
{
  "nbshrinkage",
  bow_nbshrinkage_set_cdoc_word_count_from_wi2dvf_weights,
  NULL,				/* no weight scaling function */
  NULL,
  bow_barrel_new_vpc_merge_then_weight,
  bow_barrel_set_vpc_priors_by_counting,
  bow_nbshrinkage_score,
  bow_wv_set_weights_to_count,
  NULL,				/* no need for extra weight normalization */
  bow_barrel_free,
  NULL
};

void _register_method_nbshrinkage () __attribute__ ((constructor));
void _register_method_nbshrinkage ()
{
  bow_method_register_with_name ((bow_method*)&bow_method_nbshrinkage,
				 "nbshrinkage",
				 sizeof (rainbow_method),
				 NULL);
}
