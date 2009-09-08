/* Weight-setting and scoring for P(C|w) evidence classification */

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

/* Function to assign `Naive Bayes'-style weights to each element of
   each document vector. */
void
bow_evi_set_weights (bow_barrel *barrel)
{
  int ci;
  bow_cdoc *cdoc;
  int wi;			/* a "word index" into WI2DVF */
  int max_wi;			/* the highest "word index" in WI2DVF. */
  bow_dv *dv;			/* the "document vector" at index WI */
  int dvi;			/* an index into the DV */
  int weight_setting_num_words = 0;

  /* We assume that we have already called BOW_BARREL_NEW_VPC() on
     BARREL, so BARREL already has one-document-per-class. */
    
  assert (!strcmp (barrel->method->name, "evi"));
  max_wi = MIN (barrel->wi2dvf->size, bow_num_words());

  /* Get the total number of terms in each class; store this in
     CDOC->WORD_COUNT. */
  for (ci = 0; ci < barrel->cdocs->length; ci++)
    {
      cdoc = bow_array_entry_at_index (barrel->cdocs, ci);
      cdoc->word_count = 0;
    }
  for (wi = 0; wi < max_wi; wi++) 
    {
      dv = bow_wi2dvf_dv (barrel->wi2dvf, wi);
      if (dv == NULL)
	continue;
      for (dvi = 0; dvi < dv->length; dvi++) 
	{
	  cdoc = bow_array_entry_at_index (barrel->cdocs, 
					   dv->entry[dvi].di);
	  cdoc->word_count += dv->entry[dvi].count;
	}
    }

  /* Set the weights in the BARREL's WI2DVF so that they are
     equal to P(w|C), the probability of a word given a class. */
  for (wi = 0; wi < max_wi; wi++) 
    {
      dv = bow_wi2dvf_dv (barrel->wi2dvf, wi);

      /* If the model doesn't know about this word, skip it. */
      if (dv == NULL)
	continue;

      /* Now loop through all the elements, setting their weights */
      for (dvi = 0; dvi < dv->length; dvi++) 
	{
	  cdoc = bow_array_entry_at_index (barrel->cdocs, 
					   dv->entry[dvi].di);
	  /* Here CDOC->WORD_COUNT is the total number of words in the class */
	  /* We use Laplace Estimation. */
	  dv->entry[dvi].weight = ((float)
				   (1 + dv->entry[dvi].count)
				   / (barrel->wi2dvf->num_words
				      + cdoc->word_count));
	  assert (dv->entry[dvi].weight > 0);
	}
      weight_setting_num_words++;
      /* Set the IDF.  Evi doesn't use it; make it have no effect */
      dv->idf = 1.0;
    }
#if 0
  fprintf (stderr, "wi2dvf num_words %d, weight-setting num_words %d\n",
	   barrel->wi2dvf->num_words, weight_setting_num_words);
#endif
}

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

int
bow_evi_score (bow_barrel *barrel, bow_wv *query_wv, 
	       bow_score *bscores, int bscores_len,
	       int loo_class)
{
  double *scores;		/* will become prob(class), indexed over CI */
  int ci;			/* a "class index" (document index) */
  int wvi;			/* an index into the entries of QUERY_WV. */
  int dvi;			/* an index into a "document vector" */
  int num_scores;		/* number of entries placed in SCORES */
  double score_increment = 0;
  double *pr_c;
  double *pr_c_w;		/* indexed by CI */
  int *occur_per_ci;
  int total_num_occur;

  /* Allocate space to store scores for *all* classes (documents) */
  scores = alloca (barrel->cdocs->length * sizeof (double));
  pr_c_w = alloca (barrel->cdocs->length * sizeof (double));
  pr_c = alloca (barrel->cdocs->length * sizeof (double));
  occur_per_ci = alloca (barrel->cdocs->length * sizeof (int));

  /* SCORES will start as the log() of an odds ratio, then get
     converted to a probability at the end. */

  if (bow_print_word_scores)
    printf ("%s\n",
	    "(CLASS PRIOR PROBABILIES)");

  /* Initialize the SCORES the term not in the recurrence. */
  {
    int total_num_words = 0;
    bow_cdoc *cdoc;
    for (ci = 0; ci < barrel->cdocs->length; ci++)
      {
	cdoc = bow_array_entry_at_index (barrel->cdocs, ci);
	total_num_words += cdoc->word_count;
      }
    for (ci = 0; ci < barrel->cdocs->length; ci++)
      {
	cdoc = bow_array_entry_at_index (barrel->cdocs, ci);
	pr_c[ci] = (double)cdoc->word_count / total_num_words;
	scores[ci] = log (pr_c[ci] / (1 - pr_c[ci]));
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

      /* Calculate Pr(C|w), the probability of a class given a word. */
      {
	double sum;
	for (ci = 0, dvi = 0; ci < barrel->cdocs->length; ci++)
	  {
	    pr_c_w[ci] = 1;	/* Like Laplace estimate */
	    occur_per_ci[ci] = 0;
	  }
	total_num_occur = 0;
	for (dvi = 0; dvi < dv->length; dvi++)
	  {
	    pr_c_w[dv->entry[dvi].di] += dv->entry[dvi].count;
	    occur_per_ci[dv->entry[dvi].di] += dv->entry[dvi].count;
	    total_num_occur += dv->entry[dvi].count;
	  }
	sum = 0;
	for (ci = 0, dvi = 0; ci < barrel->cdocs->length; ci++)
	  sum += pr_c_w[ci];
	for (ci = 0, dvi = 0; ci < barrel->cdocs->length; ci++)
	  pr_c_w[ci] /= sum;
      }

      if (bow_print_word_scores)
	printf ("%-30s (queryweight=%.8f)\n",
		bow_int2word (wi), 
		query_wv->entry[wvi].weight * query_wv->normalizer);

      /* Loop over all classes, putting this word's (WI's)
	 contribution into SCORES. */
      for (ci = 0, dvi = 0; ci < barrel->cdocs->length; ci++)
	{
	  bow_cdoc *cdoc;

	  cdoc = bow_array_entry_at_index (barrel->cdocs, ci);
	  assert (cdoc->type == bow_doc_train);

	  score_increment = log ((pr_c_w[ci] / (1 - pr_c_w[ci]))
				 * ((1 - pr_c[ci]) / pr_c[ci]));
	  scores[ci] += score_increment;

	  if (bow_print_word_scores)
	    printf (" %4d/%4d   %8.2e %7.2f %-30s  %10.7f\n", 
		    occur_per_ci[ci],
		    total_num_occur,
		    pr_c_w[ci],
		    score_increment, 
		    (strrchr (cdoc->filename, '/') ? : cdoc->filename),
		    scores[ci]);
	}
    }
  /* Now SCORES[] contains a EVI divergence for each class. */

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

  return num_scores;
}


rainbow_method bow_method_evi = 
{
  "evi",
  bow_evi_set_weights,
  0,				/* no weight scaling function */
  NULL, /* bow_barrel_normalize_weights_by_summing, */
  bow_barrel_new_vpc_merge_then_weight,
  bow_barrel_set_vpc_priors_by_counting,
  bow_evi_score,
  bow_wv_set_weights_to_count,
  NULL,				/* no need for extra weight normalization */
  0
};

void _register_method_evi () __attribute__ ((constructor));
void _register_method_evi ()
{
  static int done = 0;
  if (done)
    return;
  bow_method_register_with_name ((bow_method*)&bow_method_evi, 
				 "evi", 
				 sizeof (rainbow_method),
				 NULL);
  done = 1;
}
