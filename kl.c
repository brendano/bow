/* Weight-setting and scoring for Kuback-Leiber classification */

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
#include <math.h>

/* For changing weight of unseen words.
   I really should implement `deleted interpolation' */
/* M_EST_P summed over all words in the vocabulary must sum to 1.0! */
#if 1
/* This is the special case of the M-estimate that is `Laplace smoothing' */
#define M_EST_M  (barrel->wi2dvf->num_words)
#define M_EST_P  (1.0 / barrel->wi2dvf->num_words)
#define WORD_PRIOR_COUNT 1.0
#else
#define M_EST_M  (cdoc->word_count \
		  ? (((float)barrel->wi2dvf->num_words) / cdoc->word_count) \
		  : 1.0)
#define M_EST_P  (1.0 / barrel->wi2dvf->num_words)
#endif

/* Function to assign `Naive Bayes'-style weights to each element of
   each document vector. */
void
bow_kl_set_weights (bow_barrel *barrel)
{
  int ci;
  bow_cdoc *cdoc;
  int wi;			/* a "word index" into WI2DVF */
  int max_wi;			/* the highest "word index" in WI2DVF. */
  bow_dv *dv;			/* the "document vector" at index WI */
  int dvi;			/* an index into the DV */
  int weight_setting_num_words = 0;
  int total_num_words = 0;

  /* We assume that we have already called BOW_BARREL_NEW_VPC() on
     BARREL, so BARREL already has one-document-per-class. */
    
  assert (!strcmp (barrel->method->name, "kl"));
  max_wi = MIN (barrel->wi2dvf->size, bow_num_words());

  /* Get the total number of terms in each class; store this in
     CDOC->WORD_COUNT. */
  /* Get the total number of unique terms in each class; store this in
     CDOC->NORMALIZER. */
  /* Calculate the total number of occurrences of each word; store this
     in DV->IDF. */
  for (ci = 0; ci < barrel->cdocs->length; ci++)
    {
      cdoc = bow_array_entry_at_index (barrel->cdocs, ci);
      cdoc->word_count = 0;
      cdoc->normalizer = 0;
    }
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
	  cdoc->word_count += dv->entry[dvi].count;
	  total_num_words += dv->entry[dvi].count;
	  dv->idf += dv->entry[dvi].count;
	  cdoc->normalizer++;
	}
    }
  for (wi = 0; wi < max_wi; wi++) 
    {
      dv = bow_wi2dvf_dv (barrel->wi2dvf, wi);
      if (dv == NULL)
	continue;
      for (dvi = 0; dvi < dv->length; dvi++) 
	{
	  float pr_w_c;
	  cdoc = bow_array_entry_at_index (barrel->cdocs, 
					   dv->entry[dvi].di);
	  pr_w_c = (float)dv->entry[dvi].count / cdoc->word_count;
	}
    }


  /* Set the weights in the BARREL's WI2DVF so that they are
     equal to the log likelihood-ratio, Pr(w|C)/Pr(w|~C). */
  for (wi = 0; wi < max_wi; wi++) 
    {
      double pr_w = 0.0;
      dv = bow_wi2dvf_dv (barrel->wi2dvf, wi);

      /* If the model doesn't know about this word, skip it. */
      if (dv == NULL)
	continue;

      pr_w = ((double)dv->idf) / total_num_words;

      /* Now loop through all the elements, setting their weights */
      for (dvi = 0; dvi < dv->length; dvi++) 
	{
	  double pr_w_c;
	  double pr_w_not_c;
	  double log_likelihood_ratio;
	  cdoc = bow_array_entry_at_index (barrel->cdocs, 
					   dv->entry[dvi].di);
	  /* Here CDOC->WORD_COUNT is the total number of words in the class */
	  /* We use Laplace Estimation. */
	  pr_w_c = ((double)dv->entry[dvi].count 
		    / (cdoc->word_count + cdoc->normalizer));
	  pr_w_c = (((double)dv->entry[dvi].count + 1)
		    / (cdoc->word_count + barrel->wi2dvf->num_words));
	  pr_w_not_c = ((dv->idf - dv->entry[dvi].count 
			 + barrel->cdocs->length - 1)
			/ 
			(total_num_words - cdoc->word_count
			 + (barrel->wi2dvf->num_words
			    * (barrel->cdocs->length - 1))));
	  log_likelihood_ratio = log (pr_w_c / pr_w_not_c);
	  dv->entry[dvi].weight = log_likelihood_ratio;
	  dv->entry[dvi].weight = pr_w_c * log_likelihood_ratio;
	}
      weight_setting_num_words++;
      /* Set the IDF.  Kl doesn't use it; make it have no effect */
      dv->idf = 1.0;
    }

#if 0
  fprintf (stderr, "wi2dvf num_words %d, weight-setting num_words %d\n",
	   barrel->wi2dvf->num_words, weight_setting_num_words);
#endif
}

int
bow_kl_score (bow_barrel *barrel, bow_wv *query_wv, 
	      bow_score *bscores, int bscores_len,
	      int loo_class)
{
  double *scores;		/* will become prob(class), indexed over CI */
  int ci;			/* a "class index" (document index) */
  int wvi;			/* an index into the entries of QUERY_WV. */
  int dvi;			/* an index into a "document vector" */
  float pr_w_c;			/* P(w|C), prob a word is in a class */
  int num_scores;		/* number of entries placed in SCORES */
  int query_word_count;
  double score_increment = 0;
  double pr_w_d;
#define KL_AGAINST_UNCOND 0
#if KL_AGAINST_UNCOND
  int total_num_words = 0;	/* number of word occurrences in all classes */
  int total_num_w = 0;		/* number of WI occurrences in all classes */
  double pr_w;			/* unconditional probability of WI. */
#endif
  int count_w_c;
  int count_c;
  int num_smoothes = 0;
  double entropy_d = 0;

  /* Allocate space to store scores for *all* classes (documents) */
  scores = alloca (barrel->cdocs->length * sizeof (double));

  /* Calculate the total number of words in QUERY_WV.  Should we start
     at one to prevent getting a zero here?  Also, would this be
     statistically more correct, too? */
  query_word_count = 0;
  for (wvi = 0; wvi < query_wv->num_entries; wvi++)
    {
      /* Only count those words that are in the model's vocabulary. */
      if (bow_wi2dvf_dv (barrel->wi2dvf, query_wv->entry[wvi].wi))
	query_word_count += query_wv->entry[wvi].count;
    }
  if (query_word_count == 0)
    query_word_count = 1;

#if KL_AGAINST_UNCOND
  for (ci = 0; ci < barrel->cdocs->length; ci++)
    {
      bow_cdoc *cdoc = bow_array_entry_at_index (barrel->cdocs, ci);
      assert (cdoc->type == model);
      total_num_words += cdoc->word_count;
    }
#endif

  /* Initialize the SCORES to the class prior probabilities. */
  if (bow_print_word_scores)
    printf ("%s\n",
	    "(CLASS PRIOR PROBABILIES)");
  for (ci = 0; ci < barrel->cdocs->length; ci++)
    {
      bow_cdoc *cdoc = bow_array_entry_at_index (barrel->cdocs, ci);
      if (bow_uniform_class_priors)
	{
	  scores[ci] = 0.0;
	}
      else
	{
	  /* assert (cdoc->prior > 0 && cdoc->prior <= 1.0); */
	  assert (cdoc->prior >= 0 && cdoc->prior <= 1.0);
	  if (cdoc->word_count == 0)
	    /* There was no training data for this class.  Give it a
	       special, impossible value.  Positive scores are treated
	       as a special case that can never win. */
	    scores[ci] = 999.0;
	  else
	    scores[ci] = log (cdoc->prior) / query_word_count;
	}
    }

  /* Loop over each word in the word vector QUERY_WV, putting its
     contribution into SCORES. */
  for (wvi = 0; wvi < query_wv->num_entries; wvi++)
    {
      int wi;			/* the word index for the word at WVI */
      bow_dv *dv;		/* the "document vector" for the word WI */

      /* Get information about this word. */
      wi = query_wv->entry[wvi].wi;
      dv = bow_wi2dvf_dv (barrel->wi2dvf, wi);

      /* If the model doesn't know about this word, skip it.  OOV */
      if (!dv)
	continue;

      if (bow_print_word_scores)
	printf ("%-30s (queryweight=%.8f)\n",
		bow_int2word (wi), 
		query_wv->entry[wvi].weight * query_wv->normalizer);

      pr_w_d = (float)query_wv->entry[wvi].count / query_word_count;
      /* pr_w_d = 1.0 / query_wv->num_entries; xxx !!! */
      entropy_d += pr_w_d * log (pr_w_d);
#if KL_AGAINST_UNCOND
      /* Calculate the unconditional probability of word WI. */
      for (dvi = 0; dvi < dv->length; dvi++)
	total_num_w += dv->entry[dvi].count;
      pr_w = 
	(((float)total_num_w + M_EST_M * M_EST_P * barrel->cdocs->length)
	 / (total_num_words + M_EST_M * barrel->cdocs->length));
#endif

      /* Loop over all classes, putting this word's (WI's)
	 contribution into SCORES. */
      for (ci = 0, dvi = 0; ci < barrel->cdocs->length; ci++)
	{
	  bow_cdoc *cdoc;

	  cdoc = bow_array_entry_at_index (barrel->cdocs, ci);
	  assert (cdoc->type == bow_doc_train);
	  if (cdoc->word_count == 0)
	    {
	      assert (scores[ci] > 0);
	      /* xxx Why isn't this true? assert (cdoc->prior == 0); */
	      continue;
	    }

	  /* Assign PR_W_C to P(w|C), either using a DV entry, or, if
	     there is no DV entry for this class, using M-estimate 
	     smoothing */
	  if (dv)
	    while (dvi < dv->length && dv->entry[dvi].di < ci)
	      dvi++;
	  if (dv && dvi < dv->length && dv->entry[dvi].di == ci)
	    {
	      /* The count for this word in this class is non-zero. */
	      if (loo_class == ci)
		{
		  /* xxx This is not exactly right, because 
		     BARREL->WI2DVF->NUM_WORDS might have changed with the
		     removal of QUERY_WV's document. */
		  pr_w_c = ((float)
			    ((M_EST_M * M_EST_P) + dv->entry[dvi].count 
			     - query_wv->entry[wvi].count)
			    / (M_EST_M + cdoc->word_count
			       - query_word_count));
		  if (pr_w_c <= 0)
		    bow_error ("A negative word probability was calculated.\n"
			       "This can happen if you are using "
			       "--test-files-loo and the test files are\n"
			       "not being lexed in the same way as they "
			       "were when the model was built.\n"
			       "Value is %f\n"
			       "Word is `%s'\n", pr_w_c, bow_int2word (wi));
		  count_w_c = dv->entry[dvi].count-query_wv->entry[wvi].count;
		  count_c = cdoc->word_count - query_wv->entry[wvi].count;
		}
	      else
		{
		  pr_w_c = ((float)
			    ((M_EST_M * M_EST_P) + dv->entry[dvi].count)
			    / (M_EST_M + cdoc->word_count));
		  if (pr_w_c <= 0)
		    bow_error ("A negative word probability was calculated. "
			       "This can happen if you are using\n"
			       "--test-files-loo and the test files are "
			       "not being lexed in the same way as they\n"
			       "were when the model was built.");
		  assert (pr_w_c > 0 && pr_w_c <= 1);
		  count_w_c = dv->entry[dvi].count;
		  count_c = cdoc->word_count;
		  if (bow_smoothing_method == bow_smoothing_wittenbell)
		    {
		      pr_w_c = ((float)dv->entry[dvi].count 
				/ (cdoc->word_count + cdoc->normalizer));
		    }
		}
	    }
	  else
	    {
	      /* The count for this word in this class is zero. */
	      num_smoothes++;
	      if (loo_class == ci)
		{
		  /* xxx This is not exactly right, because 
		     BARREL->WI2DVF->NUM_WORDS might have changed with the
		     removal of QUERY_WV's document. */
		  pr_w_c = ((M_EST_M * M_EST_P)
			    / (M_EST_M + cdoc->word_count
			       - query_word_count));
		  assert (pr_w_c > 0 && pr_w_c <= 1);
		  count_w_c = 0;
		  count_c = cdoc->word_count - query_wv->entry[wvi].count;
		}
	      else
		{
		  pr_w_c = ((M_EST_M * M_EST_P)
			    / (M_EST_M + cdoc->word_count));
		  assert (pr_w_c > 0 && pr_w_c <= 1);
		  count_w_c = 0;
		  count_c = cdoc->word_count;
		  if (bow_smoothing_method == bow_smoothing_wittenbell)
		    {
		      if (cdoc->word_count)
			/* There is training data for this class */
			pr_w_c =
			  (cdoc->normalizer
			   / ((cdoc->word_count + cdoc->normalizer)
			      *(barrel->wi2dvf->num_words-cdoc->normalizer)));
		      else
			/* There is no training data for this class. */
			pr_w_c = 1.0 / barrel->wi2dvf->num_words;
		    }
		}
	    }
	  assert (pr_w_c > 0 && pr_w_c <= 1);

#if KL_AGAINST_UNCOND
	  /* score_increment = pr_w_d * log (pr_w_c / (pr_w)); */
	  score_increment = pr_w_d * log (pr_w_c / (pr_w * pr_w_d));
#else
	  /* score_increment = pr_w_d * log (pr_w_c); */
	  score_increment = pr_w_d * log (pr_w_c / pr_w_d);
#endif
	  assert (score_increment == score_increment);
	  scores[ci] += score_increment;
	  assert (scores[ci] == scores[ci]);

	  if (bow_print_word_scores)
	    printf (" %5d/%-6d   %8.2e %7.4f %-25s  %8.5f\n", 
		    count_w_c,
		    count_c,
		    pr_w_c,
		    score_increment, 
		    (strrchr (cdoc->filename, '/') ? : cdoc->filename),
		    scores[ci]);
	}
    }
  /* Now SCORES[] contains a KL divergence for each class. */

#if 1
  /* Normalize the SCORES so they all sum to minus one. */
  {
    double scores_sum = 0;
    for (ci = 0; ci < barrel->cdocs->length; ci++)
      {
	if (scores[ci] <= 0)
	  scores_sum += scores[ci];
      }
    if (scores_sum)
      {
	for (ci = 0; ci < barrel->cdocs->length; ci++)
	  {
	    if (scores[ci] > 0)
	      scores[ci] = -FLT_MAX;
	    else
	      scores[ci] /= -scores_sum;
	    assert (scores[ci] == scores[ci]);
	    /* assert (scores[ci] > 0); */
	  }
      }
    else
      {
	for (ci = 0; ci < barrel->cdocs->length; ci++)
	  scores[ci] = -1.0 / barrel->cdocs->length;
      }
  }
#endif

  /* Return the SCORES by putting them (and the `class indices') into
     SCORES in sorted order. */
  {
    num_scores = 0;
    for (ci = 0; ci < barrel->cdocs->length; ci++)
      {
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
  }

#if 0
  printf ("kl %8.6f %8.6f %d %d %8.6f %8.6f   ",
	  -bscores[0].weight * scores_sum, /* unnormalized high score */
	  bscores[0].weight,	/* normalized high score */
	  query_word_count,	/* document length */
	  query_wv->num_entries, /* num unique words in query */
	  (float)num_smoothes/query_word_count,
	  entropy_d);
#endif
  return num_scores;
}

rainbow_method bow_method_kl = 
{
  "kl",
  bow_kl_set_weights,
  0,				/* no weight scaling function */
  NULL, /* bow_barrel_normalize_weights_by_summing, */
  bow_barrel_new_vpc_merge_then_weight,
  bow_barrel_set_vpc_priors_by_counting,
  bow_kl_score,
  bow_wv_set_weights_to_count,
  NULL,				/* no need for extra weight normalization */
  bow_barrel_free,
  0
};

void _register_method_kl () __attribute__ ((constructor));
void _register_method_kl ()
{
  static int done = 0;
  if (done)
    return;
  bow_method_register_with_name ((bow_method*)&bow_method_kl, "kl", 
				 sizeof (rainbow_method),
				 NULL);
  done = 1;
}
