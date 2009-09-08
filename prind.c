/* Weight-setting and scoring implementation for PrInd classification
   (Fuhr's Probabilistic Indexing) */

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
#include <argp.h>


static struct argp_option prind_options[] =
{
  {0,0,0,0,
   "Probabilistic Indexing options, --method=prind:", 80},
  {"prind-non-uniform-priors", 'U', 0, 0,
   "Make PrInd use non-uniform class priors."},
  {"prind-no-foilgain-weight-scaling", 'G', 0, 0,
   "Don't have PrInd scale its weights by Quinlan's FoilGain."},
  {"prind-no-score-normalization", 'N', 0, 0,
   "Don't have PrInd normalize its class scores to sum to one."},
  {0,0}
};

error_t
prind_parse_opt (int key, char *arg, struct argp_state *state)
{
  switch (key)
    {
    case 'U':
      /* Don't have PrTFIDF use uniform class prior probabilities */
      ((bow_params_prind*)(bow_method_prind.params))->uniform_priors
	= bow_no;
      break;
    case 'G':
      /* Don't scale weights (by foilgain or anything else) */
      {
	int i;
	rainbow_method *m;
	for (i = 0; i < bow_methods->array->length; i++)
	  {
	    m = bow_sarray_entry_at_index (bow_methods, i);
	    if (m)
	      m->scale_weights = NULL;
	  }
	break;
      }
    case 'N':
      /* Don't normalize the scores from PrInd. */
      ((bow_params_prind*)(bow_method_prind.params))->normalize_scores
	= bow_no;
      break;
    default:
      return ARGP_ERR_UNKNOWN;
    }
  return 0;
}

static const struct argp prind_argp =
{
  prind_options,
  prind_parse_opt
};

static struct argp_child prind_argp_child =
{
  &prind_argp,		/* This child's argp structure */
  0,			/* flags for child */
  0,			/* optional header in help message */
  0			/* arbitrary group number for ordering */
};


/* Function to assign `PrInd'-style weights to each element of
   each document vector. */
void
bow_prind_set_weights (bow_barrel *barrel)
{
  int ci;
  bow_cdoc *cdoc;
  int wi;			/* a "word index" into WI2DVF */
  int max_wi;			/* the highest "word index" in WI2DVF. */
  bow_dv *dv;			/* the "document vector" at index WI */
  int dvi;			/* an index into the DV */
  int total_term_count;

  /* We assume that we have already called BOW_BARREL_NEW_VPC() on
     BARREL, so BARREL already has one-document-per-class. */
    
  assert (!strcmp (barrel->method->name, "prind"));
  max_wi = MIN (barrel->wi2dvf->size, bow_num_words());

  /* The CDOC->PRIOR should have been set in bow_barrel_new_vpc();
     verify it. */
  for (ci = 0; ci < barrel->cdocs->length; ci++)
    {
      cdoc = bow_array_entry_at_index (barrel->cdocs, ci);
      assert (cdoc->prior >= 0);
    }

  /* Get the total number of terms in each class; temporarily store
     this in cdoc->word_count.  Also, get the total count for each
     term across all classes, and store it in the IDF of the
     respective word vector.  (WARNING: this is an odd use of the IDF
     variable.)  Also, get the total number of terms, across all
     terms, all classes, put it in TOTAL_TERM_COUNT. */

  for (ci = 0; ci < barrel->cdocs->length; ci++)
    {
      cdoc = bow_array_entry_at_index (barrel->cdocs, ci);
      cdoc->word_count = 0;
    }
  total_term_count = 0;
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
	  if (cdoc->type != bow_doc_train)
	    continue;
	  /* Summing total number of words in each class */
	  cdoc->word_count += dv->entry[dvi].count;
	  /* Summing total word occurrences across all classes. */
	  dv->idf += dv->entry[dvi].count;
	  assert (dv->idf > 0);
	  /* Summing total number of words, all words, all classes. */
	  total_term_count += dv->entry[dvi].count;
	}
    }

  /* Set the weights in the BARREL's WI2DVF so that they are
     equal to P(w|C)/P(w), the probability of a class given a word,
     divided by the probability of a word across all classes.   This
     combination is equal to P(C|w), but without the prior P(C). */
  for (wi = 0; wi < max_wi; wi++) 
    {
      dv = bow_wi2dvf_dv (barrel->wi2dvf, wi);
      if (dv == NULL)
	continue;
      /* Now loop through all the elements, setting their weights */
      for (dvi = 0; dvi < dv->length; dvi++) 
	{
	  float pr_x_c;
	  float pr_x;
	  cdoc = bow_array_entry_at_index (barrel->cdocs, 
					   dv->entry[dvi].di);
	  /* Skip this document/class if it is not part of the model. */
	  if (cdoc->type != bow_doc_train)
	    {
	      dv->entry[dvi].weight = 0;
	      continue;
	    }

	  /* Here CDOC->WORD_COUNT is the total number of words in the class.
	     Here DV->IDF is total num of occurrences of WI across classes. */
	  /* The probability of a word given a class */
	  pr_x_c = 
	    (float)(dv->entry[dvi].count) / cdoc->word_count;
	  /* The probability of a word across all classes. */
	  pr_x = dv->idf / total_term_count;
	  dv->entry[dvi].weight = pr_x_c / pr_x;
	  assert (dv->entry[dvi].weight >= 0);
	}

      /* Don't reset the DV->IDF; we'll use its current value later */
    }
  /* NOTE: These weights are missing the prior probabilities P(C), and
     are not normalized to sum to one. */
}

int
bow_prind_score (bow_barrel *barrel, bow_wv *query_wv, 
		 bow_score *bscores, int bscores_len, int loo_class)
{
  double *scores;		/* will become prob(class), indexed over CI */
  int ci;			/* a "class index" (document index) */
  int wvi;			/* an index into the entries of QUERY_WV. */
  int dvi;			/* an index into a "document vector" */
  float pr_w_c;			/* P(w|C), prob a word is in a class */
  float pr_c;			/* P(C), prior prob of a class */
  int num_scores;		/* number of entries placed in SCORES */

#if 0
  if (loo_class >= 0)
    bow_error ("PrInd hasn't yet implemented Leave-One-Out scoring.");
#endif

  /* Allocate space to store scores for *all* classes (documents) */
  scores = alloca (barrel->cdocs->length * sizeof (double));

  /* Initialize the SCORES to zero. */
  for (ci = 0; ci < barrel->cdocs->length; ci++)
    scores[ci] = 0;

  /* Loop over each word in the word vector QUERY_WV, putting its
     contribution into SCORES. */
  for (wvi = 0; wvi < query_wv->num_entries; wvi++)
    {
      int wi;			/* the word index for the word at WVI */
      bow_dv *dv;		/* the "document vector" for the word WI */

      /* Get information about this word. */
      wi = query_wv->entry[wvi].wi;
      dv = bow_wi2dvf_dv (barrel->wi2dvf, wi);

      /* If the model doesn't know about this word, skip it. */
      if (!dv)
	continue;

      if (bow_print_word_scores)
	printf ("%-30s (queryweight=%.8f)\n",
		bow_int2word (wi), 
		query_wv->entry[wvi].weight * query_wv->normalizer);

      /* Loop over all classes, putting this word's (WI's)
	 contribution into SCORES. */
      for (ci = 0, dvi = 0; ci < barrel->cdocs->length; ci++)
	{
	  bow_cdoc *cdoc;
	  double score_increment;

	  /* Assign PR_W_C to P(w|C)/P(w), either using a DV entry,
	     or, if there is no DV entry for this class, PR_W_C would
	     be zero; adding zero to the score wouldn't change it, so
	     we skip this class.  (NOTE: We could think about doing
	     some smoothing here, but PrInd doesn't require it, since
	     zero probability words do not cause the math to blow
	     up. */
	  while (dvi < dv->length && dv->entry[dvi].di < ci)
	    dvi++;
	  if (!(dv && dvi < dv->length && dv->entry[dvi].di == ci))
	    continue;
	  cdoc = bow_array_entry_at_index (barrel->cdocs, ci);

	  /* Skip this document/class if it is not part of the model. */
	  if (cdoc->type != bow_doc_train)
	    {
	      scores[ci] = 0;
	      continue;
	    }

	  if (((bow_params_prind*)(barrel->method->params))->uniform_priors)
	    pr_c = 1.0f;
	  else
	    pr_c = cdoc->prior;

	  pr_w_c = dv->entry[dvi].weight * cdoc->normalizer;

	  /* Here DV->IDF is an unnormalized P(wi), probability of word WI.
	     Here CDOC->PRIOR is P(C), prior probability of class. */
	  score_increment = 
	    ((pr_w_c * pr_c) *
	     (query_wv->entry[wvi].weight * query_wv->normalizer));
	  /* Check for NaN */
	  assert (score_increment == score_increment);
	  scores[ci] += score_increment;
	  if (bow_print_word_scores)
	    printf ("%8.4f %16.10f %-40s  %10.9f\n", 
		    pr_w_c,
		    score_increment,
		    (cdoc->filename 
		     ? (strrchr (cdoc->filename, '/') ? : cdoc->filename)
		     : "(NULL)"),
		    scores[ci]);
	}
    }
  /* Now SCORES[] contains a (unnormalized) probability for each class. */

  if (((bow_params_prind*)(barrel->method->params))->normalize_scores)
    {
      /* Normalize the SCORES so they all sum to one. */
      {
	double scores_sum = 0;
	for (ci = 0; ci < barrel->cdocs->length; ci++)
	  scores_sum += scores[ci];
	if (scores_sum)
	  {
	    for (ci = 0; ci < barrel->cdocs->length; ci++)
	      {
		scores[ci] /= scores_sum;
		/* Check for NaN */
		assert (scores[ci] == scores[ci]);
	      }
	  }
	else
	  {
	    for (ci = 0; ci < barrel->cdocs->length; ci++)
	      scores[ci] = 1.0 / barrel->cdocs->length;
	  }
      }
    }

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
	       because either: (1) there is empty space in SCORES,
	       or (2) SCORES[CI] is larger than the smallest score
	       there currently. */
	    int dsi;		/* an index into SCORES */
	    if (num_scores < bscores_len)
	      num_scores++;
	    dsi = num_scores - 1;
	    /* Shift down all the entries that are smaller than SCORES[CI] */
	    for (; dsi > 0 && bscores[dsi-1].weight < scores[ci]; dsi--)
	      bscores[dsi] = bscores[dsi-1];
	    /* Insert the new score */
	    bscores[dsi].weight = scores[ci];
	    /* Check for NaN. */
	    assert (bscores[dsi].weight == bscores[dsi].weight);
	    bscores[dsi].di = ci;
	  }
      }
  }

  return num_scores;
}

bow_params_prind bow_prind_params =
{
  bow_yes,			/* Use uniform priors */
  bow_yes			/* Normalize the scores */
};

rainbow_method bow_method_prind =
{
  "prind",
  bow_prind_set_weights,
  bow_barrel_scale_weights_by_foilgain,
  bow_barrel_normalize_weights_by_summing,
  bow_barrel_new_vpc_merge_then_weight,
  bow_barrel_set_vpc_priors_by_counting,
  bow_prind_score,
  bow_wv_set_weights_to_count,
  bow_wv_normalize_weights_by_summing,
  bow_barrel_free,
  &bow_prind_params
};

void _register_method_prind () __attribute__ ((constructor));
void _register_method_prind ()
{
  static int done = 0;
  if (done) 
    return;
  bow_method_register_with_name ((bow_method*)&bow_method_prind,
				 "prind", 
				 sizeof (rainbow_method),
				 NULL);
  bow_argp_add_child (&prind_argp_child);
  done = 1;
}

