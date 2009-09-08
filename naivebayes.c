/* Weight-setting and scoring implementation for Naive-Bayes classification */

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
#include <argp/argp.h>

/* Command-line options specific to NaiveBayes */

/* Default value for option "naivebayes-m-est-m".  When zero, then use
   size-of-vocabulary instead. */
double naivebayes_argp_m_est_m = 0;
int naivebayes_score_returns_doc_pr;
int naivebayes_score_unsorted;
static int naivebayes_binary_scoring = 0;
static int naivebayes_normalize_log = 0;
static int naivebayes_rescale_scores = 1;
static int naivebayes_final_rescale_scores = 1;
static int naivebayes_return_log_pr = 0;
static int naivebayes_cross_entropy = 0;

double bow_naivebayes_anneal_temperature = 1;

/* icky globals for Good-Turing discounting */
static double **bow_naivebayes_goodturing_discounts = NULL;
static bow_barrel *bow_naivebayes_goodturing_barrel = NULL;

/* icky globals for Dirichlet smoothing */
double *bow_naivebayes_dirichlet_alphas = NULL;
double bow_naivebayes_dirichlet_total = 0;

/* The integer or single char used to represent this command-line option.
   Make sure it is unique across all libbow and rainbow. */
#define NB_M_EST_M_KEY 3001
#define NB_BINARY_SCORE 3002
#define NB_NORMALIZE_LOG 3003

static struct argp_option naivebayes_options[] =
{
  {0,0,0,0,
   "Naive Bayes options, --method=naivebayes:", 30},
  {"naivebayes-m-est-m", NB_M_EST_M_KEY, "M", 0,
   "When using `m'-estimates for smoothing in NaiveBayes, use M as the "
   "value for `m'.  The default is the size of vocabulary."},
  {"naivebayes-binary-scoring", NB_BINARY_SCORE, 0, 0,
   "When using naivebayes, use hacky scoring to get good Precision-Recall "
   "curves."},
  {"naivebayes-normalize-log", NB_NORMALIZE_LOG, 0, 0,
   "When using naivebayes, return -1/log(P(C|d), normalized to sum to one "
   "instead of P(C|d).  This results in values that are not so close to "
   "zero and one."},
  {0, 0}
};

error_t
naivebayes_parse_opt (int key, char *arg, struct argp_state *state)
{
  switch (key)
    {
    case NB_M_EST_M_KEY:
      naivebayes_argp_m_est_m = atof (arg);
      break;
    case NB_BINARY_SCORE:
      naivebayes_binary_scoring = 1;
      break;
    case NB_NORMALIZE_LOG:
      naivebayes_normalize_log = 1;
      naivebayes_rescale_scores = 1;
      naivebayes_final_rescale_scores = 1;
      break;
    default:
      return ARGP_ERR_UNKNOWN;
    }
  return 0;
}

static const struct argp naivebayes_argp =
{
  naivebayes_options,
  naivebayes_parse_opt
};

static struct argp_child naivebayes_argp_child =
{
  &naivebayes_argp,		/* This child's argp structure */
  0,				/* flags for child */
  0,				/* optional header in help message */
  0				/* arbitrary group number for ordering */
};

/* End of command-line options specific to NaiveBayes */

/* Defined in goodturing.c */
extern int simple_good_turing (int length, int *freq, double *disc);

void
bow_naivebayes_initialize_goodturing (bow_barrel *barrel)
{
  int *counts = 
    bow_malloc (sizeof (int) * (bow_smoothing_goodturing_k + 1));
  int len = bow_smoothing_goodturing_k + 1;
  int k;
  int ci;
  int wi;
  int max_wi;
  int dvi;
  bow_dv *dv;
  int zero_count;
  int total_words = 0;
  
  if (NULL != bow_naivebayes_goodturing_discounts)
    {
      for (k = 0; k < bow_barrel_num_classes(barrel) ; k++)
	bow_free (bow_naivebayes_goodturing_discounts[k]);
      bow_free (bow_naivebayes_goodturing_discounts);
    }

  bow_naivebayes_goodturing_barrel = barrel;
  bow_naivebayes_goodturing_discounts = bow_malloc (sizeof (double *) * 
				     bow_barrel_num_classes(barrel));
  for (k = 0; k < bow_barrel_num_classes(barrel) ; k++)
    {
      bow_naivebayes_goodturing_discounts[k] = 
	bow_malloc (sizeof (double) * len);
    }

  max_wi = MIN (barrel->wi2dvf->size, bow_num_words ());

  for (ci = 0; ci < bow_barrel_num_classes(barrel); ci ++)
    {
      bow_cdoc *cdoc = bow_array_entry_at_index (barrel->cdocs, ci);
      
      total_words = 0;

      for (k = 0; k < len ; k++)
	{
	  bow_naivebayes_goodturing_discounts[ci][k] = 0.0;
	  counts[k] = 0;
	}
      
      zero_count = barrel->wi2dvf->num_words - cdoc->normalizer;
      counts[0] = zero_count;

      for (wi = 0; wi < max_wi; wi++)
	{
	  dv = bow_wi2dvf_dv (barrel->wi2dvf, wi);
	  if (!dv)
	    continue;

	  dvi = 0;

	  /* Find the index of entry for this class. */
	  while (dvi < dv->length && dv->entry[dvi].di < ci)
	    dvi++;

	  if (dvi < dv->length && dv->entry[dvi].di == ci)
	    {
	      /* There is an entry in DV for class CI. 
		 Note it if it's in the interesting range */
	      if (dv->entry[dvi].count > 0 &&
		  dv->entry[dvi].count < len)
		{
		  counts[dv->entry[dvi].count]++;
		  total_words += dv->entry[dvi].count;
		}
	    }
	}

      bow_verbosify(bow_progress, "Class %d:\n", ci);
      for (k = 0; k < len; k++)
	{
	  bow_verbosify(bow_progress, "(%d %d)", k, counts[k]);
	}
      bow_verbosify(bow_progress, "\n");

      /* Calculate all the discount factors */
      if (0 != simple_good_turing(len, counts, 
				  &(bow_naivebayes_goodturing_discounts[ci][0])))
	bow_error("Simple Good-Turing calculation error.");
      
      /* Distribute the weight of the zero mass evenly */
      bow_naivebayes_goodturing_discounts[ci][0] = 
	bow_naivebayes_goodturing_discounts[ci][0] * total_words / 
	(cdoc->word_count * zero_count);

      for (k = 0; k < len; k++)
	{
	  bow_verbosify(bow_progress, "(%d %f)", k, 
			bow_naivebayes_goodturing_discounts[ci][k] );
	}
      bow_verbosify(bow_progress, "\n");


    }
}

void
bow_naivebayes_load_dirichlet_alphas ()
{
  int max_wi = bow_num_words ();
  FILE *fp;
  float x;
  char s[256];
  int wi;
  
  if (bow_naivebayes_dirichlet_alphas)
    bow_free (bow_naivebayes_dirichlet_alphas);
  bow_naivebayes_dirichlet_alphas = bow_malloc (sizeof (double) * max_wi);
  for (wi = 0; wi < max_wi; wi++)
    bow_naivebayes_dirichlet_alphas[wi] = 0.0;
  
  fp = fopen (bow_smoothing_dirichlet_filename, "r");
  while (fscanf(fp, "%f %s", &x, s)==2)
    {
      wi = bow_word2int (s);
      
      assert (wi != -1);
      bow_naivebayes_dirichlet_alphas[wi] = x * bow_smoothing_dirichlet_weight;
    }
  fclose (fp);
}



/* load up the alphas */
void
bow_naivebayes_initialize_dirichlet_smoothing (bow_barrel *barrel)
{
  int max_wi = MIN (barrel->wi2dvf->size, bow_num_words ());
  int wi;
  
  bow_naivebayes_dirichlet_total = 0;

  /* make sure all the alphas are > 0 and calculate the sum */
  for (wi = 0; wi < max_wi; wi++)
    {
      bow_dv *dv = bow_wi2dvf_dv (barrel->wi2dvf, wi);
      if (dv)
	{
	  bow_naivebayes_dirichlet_total += bow_naivebayes_dirichlet_alphas[wi];
	  assert (bow_naivebayes_dirichlet_alphas[wi] > 0);
	}
    }
}

/* Return the probability of word WI in class CI. 
   If LOO_CLASS is non-negative, then we are doing 
   leave-out-one-document evaulation.  LOO_CLASS is the index
   of the class from which the document has been removed.
   LOO_WI_COUNT is the number of WI'th words that are in the document
   LOO_W_COUNT is the total number of words in the docment

   The last two argments help this function avoid searching for
   the right entry in the DV from the beginning each time.
   LAST_DV is a pointer to the DV to use.
   LAST_DVI is a pointer to the index into the LAST_DV that is
   guaranteed to have class index less than CI.
*/
double
bow_naivebayes_pr_wi_ci (bow_barrel *barrel,
			 int wi, int ci,
			 int loo_class,
			 float loo_wi_count, float loo_w_count,
			 bow_dv **last_dv, int *last_dvi)
{
  bow_dv *dv;
  bow_cdoc *cdoc;
  double num_wi_ci;		/* the number of times wi occurs in class */
  double num_w_ci;		/* the number of words in class. */
  int dvi;
  double m_est_m;
  double m_est_p;
  double pr_w_c;

  cdoc = bow_array_entry_at_index (barrel->cdocs, ci);
  if (last_dv && *last_dv)
    {
      dv = *last_dv;
      dvi = *last_dvi;
      /* No, not always true. assert (dv->entry[dvi].di <= ci); */
    }
  else
    {
      dv = bow_wi2dvf_dv (barrel->wi2dvf, wi);
      dvi = 0;
      if (last_dv)
	*last_dv = dv;
    }

  /* If the model doesn't know about this word, return 0. */
  if (!dv)
    return -1.0;

  /* Find the index of entry for this class. */
  while (dvi < dv->length && dv->entry[dvi].di < ci)
    dvi++;
  /* Remember this index value for future calls to this function */
  if (last_dvi)
    *last_dvi = dvi;

  if (dvi < dv->length && dv->entry[dvi].di == ci)
    {
      /* There is an entry in DV for class CI. */
      num_wi_ci = dv->entry[dvi].weight;
    }
  else
    {
      /* There is no entry in DV for class CI. */
      num_wi_ci = 0;
      if (loo_class == ci)
	bow_error ("There should be data for WI,CI");
    }
  num_w_ci = cdoc->word_count;

#if 0
  fprintf (stdout, "count-%-25s %f\n",
	   bow_int2word (wi), num_wi_ci);
#endif

  if (loo_class == ci)
    {
      num_wi_ci -= loo_wi_count;
      num_w_ci -= loo_w_count;
      if (!(num_wi_ci >= 0 && num_w_ci >= 0))
	bow_error ("foo %g %g\n", num_wi_ci, num_w_ci);
    }

  if (bow_event_model == bow_event_document)
    {
      /* This corresponds to adding two training pseudo-data points:
	 one that has all features, and one that has no features. */
      pr_w_c = ((num_wi_ci + 1)
		/ (num_w_ci + 2));
    }
  else if (bow_smoothing_method == bow_smoothing_laplace
	   || bow_smoothing_method == bow_smoothing_mestimate)
    {
      /* xxx This is not exactly right, because 
	 BARREL->WI2DVF->NUM_WORDS might have changed with the
	 removal of QUERY_WV's document. */
      if (/* naivebayes_argp_m_est_m == 0 
	     || */ bow_smoothing_method == bow_smoothing_laplace)
	m_est_m = barrel->wi2dvf->num_words;
      else
	m_est_m = naivebayes_argp_m_est_m;
      m_est_p = 1.0 / barrel->wi2dvf->num_words;
      pr_w_c = ((num_wi_ci + m_est_m * m_est_p)
		/ (num_w_ci + m_est_m));
    }
  else if (bow_smoothing_method == bow_smoothing_wittenbell)
    {
      /* Here CDOC->NORMALIZER is the number of unique terms in the class */
      if (num_wi_ci > 0)
	pr_w_c =
	  (num_wi_ci / (num_w_ci + cdoc->normalizer));
      else
	{
	  if (cdoc->word_count)
	    /* There is training data for this class */
	    pr_w_c = 
	      (cdoc->normalizer
	       / ((num_w_ci + cdoc->normalizer)
		  * (barrel->wi2dvf->num_words - cdoc->normalizer)));
	  else
	    /* There no training data for this class */
	    pr_w_c = 1.0 / barrel->wi2dvf->num_words;
	}
    }
  else if (bow_smoothing_method == bow_smoothing_goodturing)
    {
      assert(barrel == bow_naivebayes_goodturing_barrel);
      /* don't adjust if above k */
      if (num_wi_ci > bow_smoothing_goodturing_k)
	pr_w_c = num_wi_ci / num_w_ci;
      /* if zero, just grab the stored weight */
      else if (num_wi_ci == 0)
	pr_w_c = bow_naivebayes_goodturing_discounts[ci][0];
      /* else adjust by discount factor */
      else
	pr_w_c = bow_naivebayes_goodturing_discounts[ci][(int) num_wi_ci] * 
	  num_wi_ci / num_w_ci;
    }
  else if (bow_smoothing_method == bow_smoothing_dirichlet)
    {
      pr_w_c = (num_wi_ci + bow_naivebayes_dirichlet_alphas[wi]) / 
	(num_w_ci + bow_naivebayes_dirichlet_total);
    }
  else
    {
      bow_error ("Naivebayes does not implement smoothing method %d",
		 bow_smoothing_method);
      pr_w_c = 0;		/* to avoid gcc warning */
    }

#if 0
  if (pr_w_c <= 0)
    bow_error ("A negative word probability was calculated. "
	       "This can happen if you are using\n"
	       "--test-files-loo and the test files are "
	       "not being lexed in the same way as they\n"
	       "were when the model was built");
  assert (pr_w_c > 0 && pr_w_c <= 1);
#endif

  return pr_w_c;
}

double
bow_naivebayes_total_word_count_for_ci (bow_barrel *class_barrel, int ci)
{
  double ret = 0;
  int max_wi, wi, dvi;
  bow_dv *dv;

  max_wi = MIN (class_barrel->wi2dvf->size, bow_num_words());
  for (wi = 0; wi < max_wi; wi++)
    {
      dv = bow_wi2dvf_dv (class_barrel->wi2dvf, wi);
      for (dvi = 0; dv && dvi < dv->length; dvi++)
	if (dv->entry[dvi].di == ci)
	  ret += dv->entry[dvi].weight;
    }
  return ret;
}

void
bow_naivebayes_print_word_probabilities_for_class (bow_barrel *barrel,
						   const char *classname)
{
  int wi;
  int ci = bow_str2int_no_add (barrel->classnames, classname);
  double pr_w;

  assert (ci >= 0);
  for (wi = 0; wi < barrel->wi2dvf->size; wi++)
    {
      pr_w = bow_naivebayes_pr_wi_ci (barrel, wi, ci, -1, 0, 0, NULL, NULL);
      if (pr_w >= 0)
	printf ("%20.18f %s\n", pr_w,
		bow_int2word (wi));
    }
  printf ("%-30s  %10.8f\n", "total_count", 
	  bow_naivebayes_total_word_count_for_ci (barrel, ci));
}

bow_wa *
bow_naivebayes_new_odds_ratio_for_ci (bow_barrel *barrel,
				      int the_ci)
{
  bow_wa *ret;
  int wi;
  int ci;
  int max_wi;
  bow_cdoc *cdoc;
  double pr_wi_c;
  double pr_wi_not_c;
  double class_prior_ratio;
  double pr_wi;
  double pr_not_wi;
  double ig;
  bow_dv *dv;
  int dvi;

  cdoc = bow_array_entry_at_index (barrel->cdocs, the_ci);
  class_prior_ratio = cdoc->prior / (1.0 - cdoc->prior);
  max_wi = MIN (barrel->wi2dvf->size, bow_num_words());
  ret = bow_wa_new (max_wi+2);

  for (wi = 0; wi < max_wi; wi++) 
    {
      dv = bow_wi2dvf_dv (barrel->wi2dvf, wi);
      /* If the model doesn't know about this word, skip it. */
      if (dv == NULL)
	continue;
      pr_wi_c = 0;
      pr_wi_not_c = 0;
      for (ci = 0, dvi = 0; ci < barrel->cdocs->length; ci++)
	{
	  if (the_ci == ci)
	    pr_wi_c = bow_naivebayes_pr_wi_ci (barrel, wi, ci, -1, 0, 0,
					       &dv, &dvi);
	  else
	    pr_wi_not_c += bow_naivebayes_pr_wi_ci (barrel, wi, ci, -1, 0, 0,
						    &dv, &dvi);
	}
      pr_wi = pr_wi_c + pr_wi_not_c;
      pr_not_wi = (1 - pr_wi);
#if 0
      ig = (-(pr_wi * log (pr_wi) + pr_not_wi * log (pr_not_wi))
	    + ((pr_wi_c * log (pr_wi_c) + (1-pr_wi_c) * log (1-pr_wi_c))));
#endif
      ig = pr_wi_c * log (pr_wi_c / pr_wi_not_c);
      bow_wa_append (ret, wi, ig);
    }
  bow_wa_sort (ret);
  return ret;
}

/* Print the top N words by odds ratio for each class. */
void
bow_naivebayes_print_odds_ratio_for_all_classes (bow_barrel *barrel, int n)
{
  int ci;
  bow_cdoc *cdoc;
  bow_wa *wa;

  for (ci = 0; ci < barrel->cdocs->length; ci++)
    {
      cdoc = bow_array_entry_at_index (barrel->cdocs, ci);
      wa = bow_naivebayes_new_odds_ratio_for_ci (barrel, ci);
      fprintf (stderr, "%s [%d words]\n", cdoc->filename, cdoc->word_count);
      bow_wa_fprintf (wa, stderr, n);
      bow_wa_free (wa);
    }
}

void
bow_naivebayes_print_odds_ratio_for_class (bow_barrel *barrel,
					   const char *classname)
{
  int wi;
  int the_ci;
  int ci;
  int max_wi;
  bow_cdoc *cdoc;
  double pr_wi_c;
  double pr_wi_not_c;
  double class_prior_ratio;
  bow_dv *dv;
  int dvi;

  the_ci = bow_str2int_no_add (barrel->classnames, classname);
  if (the_ci == -1)
    bow_error ("%s: Classname `%s' not found",
	       __PRETTY_FUNCTION__, classname);
  cdoc = bow_array_entry_at_index (barrel->cdocs, the_ci);
  class_prior_ratio = cdoc->prior / (1.0 - cdoc->prior);
  max_wi = MIN (barrel->wi2dvf->size, bow_num_words());
  for (wi = 0; wi < max_wi; wi++) 
    {
      dv = bow_wi2dvf_dv (barrel->wi2dvf, wi);
      /* If the model doesn't know about this word, skip it. */
      if (dv == NULL)
	continue;
      pr_wi_c = 0;
      pr_wi_not_c = 0;
      for (ci = 0, dvi = 0; ci < bow_barrel_num_classes (barrel); ci++)
	{
	  if (the_ci == ci)
	    pr_wi_c = bow_naivebayes_pr_wi_ci (barrel, wi, ci, -1, 0, 0,
					       &dv, &dvi);
	  else
	    pr_wi_not_c += bow_naivebayes_pr_wi_ci (barrel, wi, ci, -1, 0, 0,
						    &dv, &dvi);
	}
      printf ("%.10f %s\n",
	      pr_wi_c * log (pr_wi_c / pr_wi_not_c),
	      bow_int2word (wi));
    }
}

/* Get the total number of terms in each class; store this in
   CDOC->WORD_COUNT. */
void
bow_naivebayes_set_cdoc_word_count_from_wi2dvf_weights (bow_barrel *barrel)
{
  int ci;
  bow_cdoc *cdoc;
  int wi, max_wi;
  bow_dv *dv;
  int dvi;
  int num_classes = bow_barrel_num_classes (barrel);
  double num_words_per_ci[num_classes];

  for (ci = 0; ci < num_classes; ci++)
    num_words_per_ci[ci] = 0;
  max_wi = MIN (barrel->wi2dvf->size, bow_num_words());
  for (wi = 0; wi < max_wi; wi++) 
    {
      dv = bow_wi2dvf_dv (barrel->wi2dvf, wi);
      if (dv == NULL)
	continue;
      for (dvi = 0; dvi < dv->length; dvi++) 
	{
	  cdoc = bow_array_entry_at_index (barrel->cdocs, 
					   dv->entry[dvi].di);
	  ci = dv->entry[dvi].di;
	  assert (ci < num_classes);
	  num_words_per_ci[ci] += dv->entry[dvi].weight;
	}
    }
  for (ci = 0; ci < barrel->cdocs->length; ci++)
    {
      cdoc = bow_array_entry_at_index (barrel->cdocs, ci);
      cdoc->word_count = (int) rint (num_words_per_ci[ci]);
    }
}


/* Function to assign `Naive Bayes'-style weights to each element of
   each document vector. */
void
bow_naivebayes_set_weights (bow_barrel *barrel)
{
  int ci;
  bow_cdoc *cdoc;
  int wi;			/* a "word index" into WI2DVF */
  int max_wi;			/* the highest "word index" in WI2DVF. */
  bow_dv *dv;			/* the "document vector" at index WI */
  int dvi;			/* an index into the DV */
  int weight_setting_num_words = 0;
  double *pr_all_w_c = alloca (barrel->cdocs->length * sizeof (double));
  double pr_w_c;
  int total_num_words = 0;
  /* Gather the word count here instead of directly of in CDOC->WORD_COUNT
     so we avoid round-off error with each increment.  Remember,
     CDOC->WORD_COUNT is a int! */
  float num_words_per_ci[bow_barrel_num_classes (barrel)];
  int barrel_is_empty = 0;

  /* We assume that we have already called BOW_BARREL_NEW_VPC() on
     BARREL, so BARREL already has one-document-per-class. */

#if 0    
  assert (!strcmp (barrel->method->name, "naivebayes")
	  || !strcmp (barrel->method->name, "crossentropy")
	  || !strcmp (barrel->method->name, "active"));
#endif
  max_wi = MIN (barrel->wi2dvf->size, bow_num_words());

  /* The CDOC->PRIOR should have been set in bow_barrel_new_vpc();
     verify it. */
  /* Get the total number of unique terms in each class; store this in
     CDOC->NORMALIZER. */
  for (ci = 0; ci < barrel->cdocs->length; ci++)
    {
      cdoc = bow_array_entry_at_index (barrel->cdocs, ci);
      assert (cdoc->prior >= 0);
      pr_all_w_c[ci] = 0;
      cdoc->normalizer = 0;
      num_words_per_ci[ci] = 0;
    }

  /* Set the CDOC->WORD_COUNT for each class.  If we are using a
     document (binomial) model, then we'll just use the value of
     WORD_COUNT set in bow_barrel_new_vpc(), which is the total number
     of *documents* in the class, not the number of words. */
  /* Calculate P(w); store this in DV->IDF. */
  if (bow_event_model != bow_event_document)
    {
      /* Get the total number of terms in each class; store this in
	 CDOC->WORD_COUNT. */
      /* Calculate the total number of unique words, and make sure it is
	 the same as BARREL->WI2DVF->NUM_WORDS. */
      int num_unique_words = 0;

      for (wi = 0; wi < max_wi; wi++) 
	{
	  dv = bow_wi2dvf_dv (barrel->wi2dvf, wi);
	  if (dv == NULL)
	    continue;
	  num_unique_words++;
	  dv->idf = 0.0;
	  for (dvi = 0; dvi < dv->length; dvi++) 
	    {
	      cdoc = bow_array_entry_at_index (barrel->cdocs, 
					       dv->entry[dvi].di);
	      ci = dv->entry[dvi].di;
	      num_words_per_ci[ci] += dv->entry[dvi].weight;
	      cdoc->normalizer++;
	      dv->idf += dv->entry[dvi].weight;
	      total_num_words += dv->entry[dvi].weight;
	    }
	}
      for (ci = 0; ci < barrel->cdocs->length; ci++)
	{
	  cdoc = bow_array_entry_at_index (barrel->cdocs, ci);
	  cdoc->word_count = (int) rint (num_words_per_ci[ci]);
	}
      assert (num_unique_words == barrel->wi2dvf->num_words);

      /* Normalize the DV->IDF to sum to one across all words, so it is
	 P(w). */
      if (total_num_words)
	{
	  for (wi = 0; wi < max_wi; wi++) 
	    {
	      dv = bow_wi2dvf_dv (barrel->wi2dvf, wi);
	      if (dv == NULL)
		continue;
	      dv->idf /= total_num_words;
	    }
	}
      else
	{
	  barrel_is_empty = 1;
	  bow_verbosify (bow_progress, "Zero words in class barrel\n");
	}
    }

  /* initialize smoothing methods, if necessary */
  if (bow_smoothing_method == bow_smoothing_goodturing)
    bow_naivebayes_initialize_goodturing (barrel);
  else if (bow_smoothing_method == bow_smoothing_dirichlet)
    {
      bow_naivebayes_load_dirichlet_alphas ();
      bow_naivebayes_initialize_dirichlet_smoothing (barrel);
    }

  if (bow_event_model != bow_event_document && !barrel_is_empty)
    {
      /* Now loop through all the classes, verifying the
	 the probability of all in each class sums to one. */
      total_num_words = 0;
      for (wi = 0; wi < max_wi; wi++) 
	{
	  dv = bow_wi2dvf_dv (barrel->wi2dvf, wi);
	  /* If the model doesn't know about this word, skip it. */
	  if (dv == NULL)
	    continue;
	  for (ci = 0; ci < barrel->cdocs->length; ci++)
	    {
	      pr_w_c = bow_naivebayes_pr_wi_ci (barrel, wi, ci, -1, 0, 0,
						NULL, NULL);
	      cdoc = bow_array_entry_at_index (barrel->cdocs, ci);
	      assert (pr_w_c <= 1);
	      pr_all_w_c[ci] += pr_w_c;
	    }
	  weight_setting_num_words++;
	}
      for (ci = 0; ci < barrel->cdocs->length; ci++)
	{
	  /* Is this too much round-off error to expect? */
	  assert (pr_all_w_c[ci] < 1.01 && pr_all_w_c[ci] > 0.99);
	}
    }

#if 0
  fprintf (stderr, "wi2dvf num_words %d, weight-setting num_words %d\n",
	   barrel->wi2dvf->num_words, weight_setting_num_words);
#endif
}

#define IMPOSSIBLE_SCORE_FOR_ZERO_CLASS_PRIOR 999.99

int
bow_naivebayes_score (bow_barrel *barrel, bow_wv *query_wv, 
		      bow_score *bscores, int bscores_len,
		      int loo_class)
{
  double *scores;		/* will become prob(class), indexed over CI */
  int ci;			/* a "class index" (document index) */
  int wvi;			/* an index into the entries of QUERY_WV. */
  int dvi;			/* an index into a "document vector" */
  double pr_w_c;		/* P(w|C), prob a word is in a class */
  double log_pr_tf;		/* log(P(w|C)^TF), ditto, log() of it */
  double rescaler;		/* Rescale SCORES by this after each word */
  double new_score;		/* a temporary holder */
  int num_scores = 0;		/* number of entries placed in SCORES */
  int num_words_in_query = 0;
  double pr_w_d;		/* P(w|d) */
  double h_w_d;			/* entropy of P(W|d) */
  int wi;
  int hi;
  int max_wi;
  double query_wv_total_weight;
  
  /* Binomial event model with LOO processing doesn't work yet. */
  assert (bow_event_model != bow_event_document
	  || loo_class == -1);
  
  max_wi = MIN (barrel->wi2dvf->size, bow_num_words());

  /* Allocate space to store scores for *all* classes (documents) */
  scores = alloca (barrel->cdocs->length * sizeof (double));

  /* Instead of multiplying probabilities, we will sum up
     log-probabilities, (so we don't loose floating point resolution),
     and then take the exponent of them to get probabilities back. */

  /* Initialize the SCORES to the class prior probabilities. */
  if (bow_print_word_scores)
    printf ("%s\n",
	    "(CLASS PRIOR PROBABILIES)");

  for (hi = 0; hi < bscores_len; hi++)
    bscores[hi].name = NULL;

  /* Initialize log-probabilities to 0 (unless class prior is zero) */
  for (ci = 0; ci < barrel->cdocs->length; ci++)
    {
      bow_cdoc *cdoc;
      cdoc = bow_array_entry_at_index (barrel->cdocs, ci);
      if (cdoc->prior == 0)
	scores[ci] = IMPOSSIBLE_SCORE_FOR_ZERO_CLASS_PRIOR;
      else
	scores[ci] = 0;
    }

  /* If we are doing leave-one-out evaluation, get the total number of
     words in this query. */
  if (1 || loo_class >= 0 || naivebayes_cross_entropy)
    {
      bow_dv *dv;
      num_words_in_query = 0;
      for (wvi = 0; wvi < query_wv->num_entries; wvi++)
	{
	  /* Only count those words that are in the model's vocabulary. */
	  dv = bow_wi2dvf_dv (barrel->wi2dvf, query_wv->entry[wvi].wi);
	  if (dv)
	    num_words_in_query += query_wv->entry[wvi].count;
	}
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
  if (bow_event_model == bow_event_document_then_word)
    query_wv_total_weight = bow_event_document_then_word_document_length;
  else
    query_wv_total_weight = num_words_in_query;

  /* Put contribution of the words into SCORES.  If we are using the
     document event model, then loop over all words in the vocabulary,
     otherwise, just loop over all the words in the QUERY_WV
     document. */
  h_w_d = 0;
  for (wvi = 0, wi = 0;
       ((bow_event_model == bow_event_document)
	? (wi < max_wi)
	: (wvi < query_wv->num_entries));
       ((bow_event_model == bow_event_document)
	? (wi++)
	: (wvi++)))
    {
      bow_dv *dv;		/* the "document vector" for the word WI */

      /* Get information about this word. */
      
      /* Align WI and WVI in ways that depend on whether we are looping
	 over all words in the vocabulary or over words in the query. */
      if (bow_event_model == bow_event_document)
	{
	  if (query_wv->entry[wvi].wi < wi
	      && wvi < query_wv->num_entries)
	    {
	      assert (query_wv->entry[wvi].wi == wi-1);
	      wvi++;
	    }
	}
      else
	{
	  wi = query_wv->entry[wvi].wi;
	}
      dv = bow_wi2dvf_dv (barrel->wi2dvf, wi);

      /* If the model doesn't know about this word, skip it. */
      if (!dv)
	continue;

      if (wi == query_wv->entry[wvi].wi && query_wv->num_entries)
	{
	  pr_w_d = ((double)query_wv->entry[wvi].count) / num_words_in_query;
	  h_w_d -= pr_w_d * log (pr_w_d);
	}

      if (bow_print_word_scores)
	printf ("%-30s (queryweight=%.8f)\n",
		bow_int2word (wi), 
		query_wv->entry[wvi].weight * query_wv->normalizer);

      rescaler = DBL_MAX;

      /* Loop over all classes, putting this word's (WI's)
	 contribution into SCORES. */
      for (ci = 0, dvi = 0; ci < barrel->cdocs->length; ci++)
	{
	  if (scores[ci] == IMPOSSIBLE_SCORE_FOR_ZERO_CLASS_PRIOR)
	    continue;
	  pr_w_c = bow_naivebayes_pr_wi_ci (barrel, wi, ci, 
					    loo_class, 
					    query_wv->entry[wvi].weight, 
					    query_wv_total_weight,
					    &dv, &dvi);
	  /* If this is a word that does not occur in the document,
	     then use the probability it does not occur in the class.
	     This occurs only if we are using the document event model. */
	  if (query_wv->num_entries == 0 || wi != query_wv->entry[wvi].wi)
	    pr_w_c = 1.0 - pr_w_c;
	  assert (pr_w_c > 0 && pr_w_c <= 1);

	  /* Put the probability in log-space */
	  log_pr_tf = log (pr_w_c);
	  assert (log_pr_tf > -FLT_MAX + 1.0e5);

	  /* Take into consideration the number of times it occurs in 
	     the query document */
	  if (bow_event_model != bow_event_document)
	    log_pr_tf *= query_wv->entry[wvi].weight;
	  assert (log_pr_tf > -FLT_MAX + 1.0e5);

	  scores[ci] += log_pr_tf;

	  if (bow_print_word_scores)
	    {
	      bow_cdoc *cdoc = bow_array_entry_at_index (barrel->cdocs, ci);
	      printf (" %8.2e %7.2f %-40s  %10.9f\n", 
		      pr_w_c,
		      log_pr_tf, 
		      (strrchr (cdoc->filename, '/') ? : cdoc->filename),
		      scores[ci]);
	    }

	  /* Keep track of the minimum score updated for this word. */
	  if (rescaler > scores[ci])
	    rescaler = scores[ci];
	}

      /* Loop over all classes, re-scaling SCORES so that they
	 don't get so small we loose floating point resolution.
	 This scaling always keeps all SCORES positive. */
      if (naivebayes_rescale_scores && rescaler < 0 &&
	  !naivebayes_score_returns_doc_pr)
	{
	  for (ci = 0; ci < barrel->cdocs->length; ci++)
	    {
	      /* Add to SCORES to bring them close to zero.  RESCALER is
		 expected to often be less than zero here. */
	      /* xxx If this doesn't work, we could keep track of the min
		 and the max, and sum by their average. */
	      if (scores[ci] != IMPOSSIBLE_SCORE_FOR_ZERO_CLASS_PRIOR)
		scores[ci] += -rescaler;
	      assert (scores[ci] > -DBL_MAX + 1.0e5
		      && scores[ci] < DBL_MAX - 1.0e5);
	    }
	}
    }
  /* Now SCORES[] contains a (unnormalized) log-probability of the
     document for each class. */

  /* Anneal the probability */
  if (bow_naivebayes_anneal_temperature != 1)
    {
      for (ci = 0; ci < barrel->cdocs->length; ci++)
	if (scores[ci] != IMPOSSIBLE_SCORE_FOR_ZERO_CLASS_PRIOR)
	  {
#if 0
	    scores[ci] /= (query_wv_total_weight
			   + bow_naivebayes_anneal_temperature);
#elif 0
	    scores[ci] /= 1 + log (query_wv_total_weight + 1);
#elif 0
	    scores[ci] /= ((pow (query_wv_total_weight, 0.9) + 1) / 3);
#elif 1
	    scores[ci] /= ((query_wv_total_weight + 1) / 7);
#else
	    scores[ci] /= bow_naivebayes_anneal_temperature;
#endif
	    assert (scores[ci] > -FLT_MAX + 1.0e5);
	  }
    }

  /* Incorporate the class prior */
  if (!naivebayes_score_returns_doc_pr && !bow_uniform_class_priors)
    {
      for (ci = 0; ci < barrel->cdocs->length; ci++)
	{
	  bow_cdoc *cdoc;
	  cdoc = bow_array_entry_at_index (barrel->cdocs, ci);
	  assert (cdoc->prior >= 0.0f && cdoc->prior <= 1.0f);
	  if (cdoc->prior == 0)
	    assert (scores[ci] == IMPOSSIBLE_SCORE_FOR_ZERO_CLASS_PRIOR);
	  else
	    scores[ci] += log (cdoc->prior);
	  assert (scores[ci] > -FLT_MAX + 1.0e5);
	  if (bow_print_word_scores)
	    printf ("%16s %-40s  %10.9f\n", 
		    "",
		    (strrchr (cdoc->filename, '/') ? : cdoc->filename),
		    scores[ci]);
	}
    }

  /* Rescale the SCORE one last time, this time making them all -2 or
     more negative, so that exp() will work well, especially around
     the higher-probability classes. */
  if (naivebayes_final_rescale_scores && !naivebayes_score_returns_doc_pr)
    {
      rescaler = -DBL_MAX;
      for (ci = 0; ci < barrel->cdocs->length; ci++)
	if (scores[ci] > rescaler
	    && scores[ci] != IMPOSSIBLE_SCORE_FOR_ZERO_CLASS_PRIOR)
	  rescaler = scores[ci];
      rescaler += 2.0;
      /* rescaler += 2.5; */
      /* RESCALER is now the maximum of the SCORES. */
      for (ci = 0; ci < barrel->cdocs->length; ci++)
	if (scores[ci] != IMPOSSIBLE_SCORE_FOR_ZERO_CLASS_PRIOR)
	  scores[ci] -= rescaler;
    }

  if (naivebayes_cross_entropy)
    {
      for (ci = 0; ci < barrel->cdocs->length; ci++)
	{
	  scores[ci] /= (num_words_in_query + 1);
	  /* This makes it into KL divergence  scores[ci] += h_w_d; */
	}
    }
  else if (naivebayes_binary_scoring)
    {
      int low_score_index;

      assert (barrel->cdocs->length == 2);
      if (scores[0] <= scores[1])
	low_score_index = 0;
      else
	low_score_index = 1;
      if (scores[1-low_score_index] != IMPOSSIBLE_SCORE_FOR_ZERO_CLASS_PRIOR)
	scores[1-low_score_index] = -1.0 * scores[low_score_index];
    }
  else if (!naivebayes_return_log_pr)
    {
      if (naivebayes_normalize_log)
	{
	  for (ci = 0; ci < barrel->cdocs->length; ci++)
	    {
	      assert (scores[ci] < 0);
	      /* scores[ci] = -1.0 / scores[ci]; */
	      scores[ci] = -1.0 / (scores[ci] * scores[ci] * scores[ci]);
	      /* scores[ci] = 1.0 / pow (-scores[ci], 2.7); */
	    }
	}
      else
	{
	  /* Use exp() on the SCORES to get probabilities from
	     log-probabilities. */
	  for (ci = 0; ci < barrel->cdocs->length; ci++)
	    {
	      new_score = exp (scores[ci]);
	      /* assert (new_score > 0 && new_score < DBL_MAX - 1.0e5); */
	      if (scores[ci] != IMPOSSIBLE_SCORE_FOR_ZERO_CLASS_PRIOR)
		scores[ci] = new_score;
	    }
	}

      /* Normalize the SCORES so they all sum to one. */
      if (!naivebayes_score_returns_doc_pr) 
	{
	  double scores_sum = 0;
	  for (ci = 0; ci < barrel->cdocs->length; ci++)
	    if (scores[ci] != IMPOSSIBLE_SCORE_FOR_ZERO_CLASS_PRIOR)
	      scores_sum += scores[ci];
	  for (ci = 0; ci < barrel->cdocs->length; ci++)
	    {
	      if (scores[ci] != IMPOSSIBLE_SCORE_FOR_ZERO_CLASS_PRIOR)
		scores[ci] /= scores_sum;
	      /* assert (scores[ci] > 0); */
	    }
	}
    }
 
  if (naivebayes_score_unsorted) { 
    for (ci=0; ci<barrel->cdocs->length; ci++) {
      bscores[ci].weight = scores[ci];
    }
  } else {
    /* Return the SCORES by putting them (and the `class indices') into
       SCORES in sorted order. */

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
  }

  return num_scores;
}


bow_params_naivebayes bow_naivebayes_params =
{
  bow_no,			/* no uniform priors */
  bow_yes,			/* normalize_scores */
};

rainbow_method bow_method_naivebayes = 
{
  "naivebayes",
  bow_naivebayes_set_weights,
  0,				/* no weight scaling function */
  NULL, /* bow_barrel_normalize_weights_by_summing, */
  bow_barrel_new_vpc_merge_then_weight,
  bow_barrel_set_vpc_priors_by_counting,
  bow_naivebayes_score,
  bow_wv_set_weights_to_count,
  NULL,				/* no need for extra weight normalization */
  bow_barrel_free,
  &bow_naivebayes_params
};

void _register_method_naivebayes () __attribute__ ((constructor));
void _register_method_naivebayes ()
{
  bow_method_register_with_name ((bow_method*)&bow_method_naivebayes,
				 "naivebayes",
				 sizeof (rainbow_method),
				 &naivebayes_argp_child);
  bow_argp_add_child (&naivebayes_argp_child);
}
