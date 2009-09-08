/* Weight-setting and scoring implementation for Maximum Entropy classification */

/* Copyright (C) 1997, 1998, 1999 Andrew McCallum

   Written by:  Kamal Nigam (knigam@cs.cmu.edu>

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

typedef struct _maxent_coefficient {
  int power;
  double coeff;
} maxent_coefficient;

typedef struct _maxent_polynomial {
  int length;
  int size;
  maxent_coefficient entry[0];
} maxent_polynomial;

/* whether to add a gaussian prior for each lambda value */
static int maxent_gaussian_prior = 0;

/* the variance of each gaussian prior */
static float maxent_prior_variance = 0.01;

/* whether to enforce zero constraints when using a gaussian prior */
static int maxent_gaussian_prior_zero_constraints = 1;

/* if == 0, then constant variance.  if ==1 then K*log(1+N(w,c)). If ==2 then K*N(w,c) */
static int maxent_prior_vary_by_count = 0;

static int maxent_scoring_hack = 0;

/* option to add one to each empirical count when calculating constraint */
static int maxent_smooth_counts = 0;

static int maxent_logprob_constraints = 0;

/* internal variable for scoring */
static int bow_maxent_model_building = 0;

/* the function to use to identify documents for checking accuracy */
static int (*maxent_accuracy_docs)(bow_cdoc *) = NULL;
static int (*maxent_logprob_docs)(bow_cdoc *) = NULL;
static int (*maxent_halt_accuracy_docs)(bow_cdoc *) = NULL;

/* which documents to use for maxent iterations */
static int (*maxent_iteration_docs)(bow_cdoc *) = bow_cdoc_is_train;


/* the number of iterations of iterative scaling to perform */
static int maxent_num_iterations = 40;

/* the number of top words by mutual info per class to use as features. 0 means all */
static int maxent_words_per_class = 0;

/* the minimum count for a word/class feature */
static int maxent_prune_features_by_count = 0;

/* whether or not to use unlabeled docs in setting the constraints */
static int maxent_constraint_use_unlabeled = 0;

#if 0
static int maxent_print_constraints = 1;
static int maxent_print_lambdas = 1;
#endif


enum {
  MAXENT_PRINT_ACCURACY = 6000,
  MAXENT_ITERATIONS,
  MAXENT_WORDS_PER_CLASS,
  MAXENT_HALT_BY_LOGPROB,
  MAXENT_HALT_BY_ACCURACY,
  MAXENT_LOGPROB_CONSTRAINTS,
  MAXENT_SMOOTH_COUNTS,
  MAXENT_SCORING_HACK,
  MAXENT_GAUSSIAN_PRIOR,
  MAXENT_PRIOR_VARIANCE,
  MAXENT_PRUNE_FEATURES_BY_COUNT,
  MAXENT_GAUSSIAN_PRIOR_ZERO_CONSTRAINTS,
  MAXENT_PRIOR_VARY_BY_COUNT,
  MAXENT_PRIOR_VARY_BY_COUNT_LINEAR,
  MAXENT_ITERATION_DOCS,
  MAXENT_CONSTRAINT_DOCS,
};

static struct argp_option maxent_options[] =
{
  {0,0,0,0,
   "Maximum Entropy options, --method=maxent:", 55},
  {"maxent-print-accuracy", MAXENT_PRINT_ACCURACY, "TYPE", 0,
   "When running maximum entropy, print the accuracy of documents at each round.  "
   "TYPE is type of document to measure perplexity on.  See "
   "`--em-halt-using-perplexity` for choices for TYPE"},
  {"maxent-halt-by-logprob", MAXENT_HALT_BY_LOGPROB, "TYPE", 0,
   "When running maxent, halt iterations using the logprob of documents.  TYPE is type of documents"
   "to test.  See `--em-halt-using-perplexity` for choices for TYPE"},
  {"maxent-halt-by-accuracy", MAXENT_HALT_BY_ACCURACY, "TYPE", 0,
   "When running maxent, halt iterations using the accuracy of documents.  TYPE is type of documents"
   "to test.  See `--em-halt-using-perplexity` for choices for TYPE"},
  {"maxent-iterations", MAXENT_ITERATIONS, "NUM", 0,
   "The number of iterative scaling iterations to perform.  The default is 40."},
  {"maxent-keep-features-by-mi", MAXENT_WORDS_PER_CLASS, "NUM", 0,
   "The number of top words by mutual information per class to use as features.  Zero"
   "implies no pruning and is the default."},
  {"maxent-logprob-constraints", MAXENT_LOGPROB_CONSTRAINTS, 0, 0,
   "Set constraints to be the log prob of the word."},
  {"maxent-smooth-counts", MAXENT_SMOOTH_COUNTS, 0, 0,
   "Add 1 to the count of each word/class pair when calculating the constraint values."},
  {"maxent-scoring-hack", MAXENT_SCORING_HACK, 0, 0,
   "Use smoothed naive Bayes probability for zero occuring word/class pairs during scoring"},
  {"maxent-gaussian-prior", MAXENT_GAUSSIAN_PRIOR, 0, 0,
   "Add a Gaussian prior to each word/class feature constraint."},
  {"maxent-prior-variance", MAXENT_PRIOR_VARIANCE, "NUM", 0,
   "The variance to use for the Gaussian prior.  The default is 0.01."},
  {"maxent-vary-prior-by-count", MAXENT_PRIOR_VARY_BY_COUNT, 0, 0,
   "Multiply log (1 + N(w,c)) times variance when using a gaussian prior."},
  {"maxent-gaussian-prior-no-zero-constraints", MAXENT_GAUSSIAN_PRIOR_ZERO_CONSTRAINTS, 0, 0, 
   "When using a gaussian prior, do not enforce constraints that have no"
   "training data."},
  {"maxent-prune-features-by-count", MAXENT_PRUNE_FEATURES_BY_COUNT, "NUM", 0,
   "Prune the word/class feature set, keeping only those features that have"
   "at least NUM occurrences in the training set."},
  {"maxent-vary-prior-by-count-linearly", MAXENT_PRIOR_VARY_BY_COUNT_LINEAR, 0, 0,
   "Mulitple N(w,c) times variance when using a Gaussian prior."},
  {"maxent-iteration-docs", MAXENT_ITERATION_DOCS, "TYPE", 0,
   "The types of documents to use for maxent iterations.  The default is train.  "
   "TYPE is type of documents to test.  See `--em-halt-using-perplexity` "
   "for choices for TYPE"},
  {"maxent-constraint-docs", MAXENT_CONSTRAINT_DOCS, "TYPE", 0, 
   "The documents to use for setting the constraints.  The default is train. "
   "The other choice is trainandunlabeled."}, 
  {0, 0}
};

error_t
maxent_parse_opt (int key, char *arg, struct argp_state *state)
{
  switch (key)
    {
    case MAXENT_ITERATION_DOCS:
      if (!strcmp (arg, "train"))
	maxent_iteration_docs = bow_cdoc_is_train;
      else if (!strcmp (arg, "unlabeled"))
	maxent_iteration_docs = bow_cdoc_is_unlabeled;
      else if (!strcmp (arg, "trainandunlabeled"))
	maxent_iteration_docs = bow_cdoc_is_train_or_unlabeled;
      else
	bow_error("Unknown document type for --maxent-iteration-docs");
      break;
    case MAXENT_CONSTRAINT_DOCS:
      if (!strcmp (arg, "train"))
	maxent_constraint_use_unlabeled = 0;
      else if (!strcmp (arg, "trainandunlabeled"))
	maxent_constraint_use_unlabeled = 1;
      else
	bow_error("Unknown document type for --maxent-constraint-docs");
      break;
    case MAXENT_PRINT_ACCURACY:
      if (!strcmp (arg, "validation"))
	maxent_accuracy_docs = bow_cdoc_is_validation;
      else if (!strcmp (arg, "train"))
	maxent_accuracy_docs = bow_cdoc_is_train;
      else if (!strcmp (arg, "unlabeled"))
	maxent_accuracy_docs = bow_cdoc_is_unlabeled;
      else if (!strcmp (arg, "test"))
	maxent_accuracy_docs = bow_cdoc_is_test;
      else
	bow_error("Unknown document type for --maxent-print-accuracy");
      break;
    case MAXENT_HALT_BY_LOGPROB:
      if (!strcmp (arg, "validation"))
	maxent_logprob_docs = bow_cdoc_is_validation;
      else if (!strcmp (arg, "train"))
	maxent_logprob_docs = bow_cdoc_is_train;
      else if (!strcmp (arg, "unlabeled"))
	maxent_logprob_docs = bow_cdoc_is_unlabeled;
      else if (!strcmp (arg, "test"))
	maxent_logprob_docs = bow_cdoc_is_test;
      else
	bow_error("Unknown document type for --maxent-halt-by-logprob");
      break;
    case MAXENT_HALT_BY_ACCURACY:
      if (!strcmp (arg, "validation"))
	maxent_halt_accuracy_docs = bow_cdoc_is_validation;
      else if (!strcmp (arg, "train"))
	maxent_halt_accuracy_docs = bow_cdoc_is_train;
      else if (!strcmp (arg, "unlabeled"))
	maxent_halt_accuracy_docs = bow_cdoc_is_unlabeled;
      else if (!strcmp (arg, "test"))
	maxent_halt_accuracy_docs = bow_cdoc_is_test;
      else
	bow_error("Unknown document type for --maxent-halt-by-accuracy");
      break;
    case MAXENT_ITERATIONS:
      maxent_num_iterations = atoi (arg);
      break;
    case MAXENT_WORDS_PER_CLASS:
      maxent_words_per_class = atoi (arg);
      break;
    case MAXENT_LOGPROB_CONSTRAINTS:
      maxent_logprob_constraints = 1;
      break;
    case MAXENT_SMOOTH_COUNTS:
      maxent_smooth_counts = 1;
      break;
    case MAXENT_SCORING_HACK:
      maxent_scoring_hack = 1;
      break;
    case MAXENT_GAUSSIAN_PRIOR:
      maxent_gaussian_prior = 1;
      break;
    case MAXENT_PRIOR_VARIANCE:
      maxent_prior_variance = atof(arg);
      assert (maxent_prior_variance > 0);
      break;
    case MAXENT_PRUNE_FEATURES_BY_COUNT:
      maxent_prune_features_by_count = atoi (arg);
      break;
    case MAXENT_GAUSSIAN_PRIOR_ZERO_CONSTRAINTS:
      maxent_gaussian_prior_zero_constraints = 0;
      break;
    case MAXENT_PRIOR_VARY_BY_COUNT:
      maxent_prior_vary_by_count = 1;
      break;
    case MAXENT_PRIOR_VARY_BY_COUNT_LINEAR:
      maxent_prior_vary_by_count = 2;
      break;
    default:
      return ARGP_ERR_UNKNOWN;
    }
  return 0;
}

static const struct argp maxent_argp =
{
  maxent_options,
  maxent_parse_opt
};

static struct argp_child maxent_argp_child =
{
  &maxent_argp,		/* This child's argp structure */
  0,				/* flags for child */
  0,				/* optional header in help message */
  0				/* arbitrary group number for ordering */
};


/* alter the class barrel and excise dv entries that have count less
   than min_count */
void 
maxent_prune_features_by_occurrence_count (bow_barrel *barrel, int min_count)
{
  int wi;
  int max_wi = MIN (barrel->wi2dvf->size, bow_num_words());


  /* delete dv entries and dvs (if necessary) for word/class pairs
     with less than min_count occurrences */
  for (wi = 0; wi < max_wi; wi++)
    {
      bow_dv *dv = bow_wi2dvf_dv(barrel->wi2dvf, wi);
      int new_dvi = 0;
      int old_dvi = 0;

      if (!dv)
	continue;

      for (old_dvi = 0; old_dvi < dv->length; old_dvi++)
	if (dv->entry[old_dvi].count >= min_count)
	  {
	    dv->entry[new_dvi].count = dv->entry[old_dvi].count;
	    dv->entry[new_dvi].weight = dv->entry[old_dvi].weight;
	    new_dvi++;
	  }

      /* adjust the length of the dv, or remove it if empty */
      if (new_dvi == 0)
	bow_wi2dvf_hide_wi (barrel->wi2dvf, wi);
      else
	dv->length = new_dvi;
    }
}




/* alter the class barrel and excise dv's and dv entries that are not
   in the top num_per_class words per class */
void
maxent_prune_vocab_by_mutual_information (bow_barrel *barrel, int num_per_class)
{
  int ci;
  int wi;
  long total_words = 0;
  int max_wi = MIN (barrel->wi2dvf->size, bow_num_words());
  int max_ci = bow_barrel_num_classes (barrel);
  struct wiig { int wi; float ig; } **mis;
  int wpi;

  int wiig_compare (const void *wiig1, const void *wiig2)
    {
      if (((struct wiig*)wiig1)->ig > ((struct wiig*)wiig2)->ig)
	return -1;
      else if (((struct wiig*)wiig1)->ig == ((struct wiig*)wiig2)->ig)
	return 0;
      else
	return 1;
    }

  /* do this on a class barrel */

  /* malloc and initialize the double array of mutual infos */
  mis = bow_malloc (sizeof(struct wiig *) * max_ci);
  for (ci = 0; ci < max_ci ; ci ++)
    mis[ci] = bow_malloc (sizeof (struct wiig) * max_wi);

  for (ci = 0; ci < max_ci ; ci++)
    for (wi = 0 ; wi < max_wi; wi++)
      {
	mis[ci][wi].wi  = wi;
	mis[ci][wi].ig = 0;
      }

  /* first calculate total_words  */
  for (ci = 0 ; ci < max_ci ; ci++)
    {
      bow_cdoc *cdoc = bow_array_entry_at_index (barrel->cdocs, ci);
      total_words += cdoc->word_count;
    }

  /* calculate the mutual informations */
  for (wi = 0; wi < max_wi; wi++)
    {
      bow_dv *dv;
      int dvi;
      int local_total = 0;
      
      dv = bow_wi2dvf_dv (barrel->wi2dvf, wi);
      if (!dv)
	continue;

      dvi = 0;
      
      for (dvi = 0; dvi < dv->length; dvi++)
	local_total += dv->entry[dvi].count;
      
      for (dvi = 0; dvi < dv->length; dvi++)
	{
	  bow_cdoc *cdoc = bow_array_entry_at_index (barrel->cdocs, 
						     dv->entry[dvi].di);
	  
	  mis[dv->entry[dvi].di][wi].ig = fabs((double) dv->entry[dvi].count * 
	    log ((double) dv->entry[dvi].count * (double) total_words  / 
		 (double) cdoc->word_count / (double) local_total));
	    
	}
    }

  /* ok, now sort each class to bring the best infogains to the top*/
  for (ci = 0; ci < max_ci; ci++)
    qsort(mis[ci], max_wi, sizeof (struct wiig), wiig_compare);

  /* Check that we're not saving bogus words */
  for (ci = 0; ci < max_ci; ci++)
    assert (mis[ci][num_per_class - 1].ig > 0);

  for (ci = 0; ci < max_ci; ci++)
    {
      bow_verbosify (bow_progress, "\n%s\n", bow_barrel_classname_at_index (barrel, ci));
      for (wi = 0; wi < 10; wi++)
	bow_verbosify (bow_progress, "%20.10f %s\n", mis[ci][wi].ig,
		       bow_int2word (mis[ci][wi].wi));
    }

  /* now edit the class barrel, knocking out word/class pairs where appropriate */

  /* first set all word counts to be 1 */
  for (wi = 0; wi < max_wi; wi++)
    {
      bow_dv *dv = bow_wi2dvf_dv (barrel->wi2dvf, wi);
      int dvi;

      if (!dv)
	continue;
      
      for (dvi = 0; dvi < dv->length; dvi++)
	dv->entry[dvi].count = 1;
    }

  /* now set counts to be 2 where we're keeping the word/class pair */
  for (ci = 0; ci < max_ci; ci++)
    for (wpi = 0; wpi < num_per_class; wpi++)
      {
	bow_dv *dv = bow_wi2dvf_dv(barrel->wi2dvf, mis[ci][wpi].wi);
	int dvi;

	assert (dv);

	/* find the class pair for this word */
	for (dvi = 0; dvi < dv->length && ci > dv->entry[dvi].di ; dvi++);

	assert (dvi < dv->length && ci == dv->entry[dvi].di);
  
	dv->entry[dvi].count = 2;
      }

  /* now delete dv entries and dvs (if necessary) for word/class pairs with 1 */
  for (wi = 0; wi < max_wi; wi++)
    {
      bow_dv *dv = bow_wi2dvf_dv(barrel->wi2dvf, wi);
      int new_dvi = 0;
      int old_dvi = 0;

      if (!dv)
	continue;

      for (old_dvi = 0; old_dvi < dv->length; old_dvi++)
	if (dv->entry[old_dvi].count == 2)
	  {
	    dv->entry[new_dvi].count = 2;
	    dv->entry[new_dvi].weight = dv->entry[old_dvi].weight;
	    new_dvi++;
	  }

      /* adjust the length of the dv, or remove it if empty */
      if (new_dvi == 0)
	bow_wi2dvf_hide_wi (barrel->wi2dvf, wi);
      else
	dv->length = new_dvi;
    }

  for (ci = 0; ci < max_ci; ci++)
    bow_free (mis[ci]);

  bow_free (mis);

  return;
}


/* Newton's method.  max_fi is one more than the index of the largest
   coefficient given */
double
maxent_newton (maxent_polynomial *poly)
{
  double root;
  double fun_val = 100;
  double deriv_val;
  double low = 0;
  double high = 1000000;
  double dxold = 1000000;
  double dx = 1000000;
  static double epsilon=1.0E-8;
  int fi;
  int rounds = 0;

  /* initial guess of root */
  root = 1.0001;

  while (fabs(fun_val) > epsilon)
    {     
      /* calculate function at new root */
      fun_val = 0.0;
      for (fi = 0; fi < poly->length; fi++)
	fun_val += poly->entry[fi].coeff * pow (root, poly->entry[fi].power);
      if (maxent_gaussian_prior)
	fun_val += poly->entry[poly->length].coeff * log (root);

      /* calculate derivative at root */
      deriv_val = 0.0;
      for (fi = 1; fi < poly->length; fi++)
	deriv_val += poly->entry[fi].power * poly->entry[fi].coeff * 
	  pow (root, poly->entry[fi].power - 1);
      if (maxent_gaussian_prior)
	deriv_val += poly->entry[poly->length].coeff / root;

      assert (fun_val == fun_val &&
	      deriv_val == deriv_val);

      if (fun_val < 0)
	low = root;
      else
	high = root;
      
      dxold = dx;

      /* if fun_val is infinity, bisect the region.
	 otherwise, use newton.  */
      if (fun_val == HUGE_VAL ||
	  2 * fabs(fun_val) > fabs(dxold * deriv_val))
	{
	  dx = (high - low) / 2.0;
	  root = low + dx;
	}
      else
	{
	  dx = fun_val / deriv_val;
	  root -= dx;

	  /* if newton thinks to go outside the region, bisect the
	     region */
	  if (root < low || root > high)
	    {
	      dx = (high - low) / 2.0;
	      root = low + dx;
	    }
	}

      rounds++;
      if (rounds > 100)
	bow_error ("Newton's method did not converge.\n");
    }

  return (root);
}



/* Calculate the accuracy or the model prob of the barrel on a set of
   docs.  accuracy_or_logprob 1 for accuracy, else for logprob */
float
maxent_calculate_accuracy (bow_barrel *doc_barrel, bow_barrel *class_barrel, 
			   int (*test_docs)(bow_cdoc *), int accuracy_or_logprob)
{
  bow_dv_heap *test_heap;	/* we'll extract test WV's from here */
  bow_wv *query_wv;
  int di;			/* a document index */
  bow_score *hits;
  int num_hits_to_retrieve = bow_barrel_num_classes (class_barrel);
  int actual_num_hits;
  bow_cdoc *doc_cdoc;
  int num_tested = 0;
  int num_correct = 0;
  int old_model_building = 0;
  double log_prob = 0;
  

  if (accuracy_or_logprob == 1)
    {
      old_model_building = bow_maxent_model_building;
      bow_maxent_model_building = 0;
    }

  /* Create the heap from which we'll get WV's. Initialize QUERY_WV so
     BOW_TEST_NEXT_WV() knows not to try to free. */
  hits = alloca (sizeof (bow_score) * num_hits_to_retrieve);
  test_heap = bow_test_new_heap (doc_barrel);
  query_wv = NULL;
  
  /* Loop once for each test document. */
  while ((di = bow_heap_next_wv (test_heap, doc_barrel, &query_wv, 
				 test_docs))
	 != -1)
    {
      doc_cdoc = bow_array_entry_at_index (doc_barrel->cdocs, 
					   di);
      bow_wv_set_weights (query_wv, class_barrel);
      bow_wv_normalize_weights (query_wv, class_barrel);
      actual_num_hits = 
	bow_barrel_score (class_barrel, 
			  query_wv, hits,
			  num_hits_to_retrieve, -1);
      assert (actual_num_hits == num_hits_to_retrieve);
      if (doc_cdoc->class == hits[0].di)
	num_correct++;

      log_prob += log(hits[doc_cdoc->class].weight);

      num_tested++;
    }

  if (accuracy_or_logprob == 1)
    {
      bow_maxent_model_building = old_model_building;
      return (((float) num_correct) / ((float) num_tested));
    }
  else
    return (log_prob);

}


bow_barrel *
bow_maxent_new_vpc_with_weights_doc_then_word (bow_barrel *doc_barrel)
{
  bow_barrel *vpc_barrel;   /* the vector-per-class barrel */
  int wi;                   /* word index */
  int wvi;
  int max_wi;               /* max word index */
  int dvi;                  /* document vector index */
  int ci;                   /* class index */
  bow_dv *dv;               /* document vector */
  int di;                   /* document index */
  bow_dv_heap *test_heap=NULL;	/* we'll extract test WV's from here */
  bow_wv *query_wv;
  bow_score *hits;
  int actual_num_hits;
  bow_cdoc *doc_cdoc;
  bow_wi2dvf *constraint_wi2dvf;
  int max_ci;
  int rounds = 0;
  double total_count_per_ci[200];
  int total_num_docs = 0;
  float old_log_prob = -FLT_MAX;
  float new_log_prob = -FLT_MAX / 2;
  float old_accuracy = -1;
  float new_accuracy = 0;
  maxent_polynomial *newton_poly;
  int num_unlabeled = 0;
  int *unlabeled_dis = NULL;
    

  bow_maxent_model_building = 1;

  /* some sanity checks first */
  assert (200 > bow_barrel_num_classes(doc_barrel));
  assert (doc_barrel->classnames);
  assert (bow_event_model == bow_event_document_then_word);
  assert (!maxent_scoring_hack);
  assert (!maxent_logprob_constraints);
  assert (!maxent_words_per_class);
  assert (!maxent_prune_features_by_count);

  max_wi = MIN (doc_barrel->wi2dvf->size, bow_num_words ());
  max_ci = bow_barrel_num_classes (doc_barrel);

  newton_poly = bow_malloc (sizeof (maxent_polynomial) +
			    sizeof (maxent_coefficient) * 3);
  newton_poly->size = 3;
  newton_poly->length = 2;
  newton_poly->entry[0].power = 0;
  newton_poly->entry[1].power = bow_event_document_then_word_document_length;
  newton_poly->entry[2].power = -1;

  /* if we're using unlabeled data to set the constraints, then we
     need to temporarily convert these into training documents. */
  if (maxent_constraint_use_unlabeled) 
    {
      unlabeled_dis = bow_malloc (sizeof (int) * doc_barrel->cdocs->length);
      for (di = 0; di < doc_barrel->cdocs->length; di++) 
	{
	  bow_cdoc *cdoc = bow_array_entry_at_index(doc_barrel->cdocs, di);
	  if (cdoc->type == bow_doc_unlabeled)
	    {
	      unlabeled_dis[num_unlabeled] = di;
	      num_unlabeled++;
	      cdoc->type = bow_doc_train;
	    }
	}
    }

  /* get a barrel where the weights and counts are set to word counts
     based on the labeled data only */
  vpc_barrel = bow_barrel_new_vpc (doc_barrel);


  /* switch back the unlabeled documents to have their original tag */
  if (maxent_constraint_use_unlabeled)
    {
      int ui;

      for (ui = 0; ui < num_unlabeled; ui++)
	{
	  bow_cdoc *cdoc = bow_array_entry_at_index(doc_barrel->cdocs, 
						    unlabeled_dis[ui]);
	  cdoc->type = bow_doc_unlabeled;
	}

      bow_free (unlabeled_dis);
    }


  /* re-initialize the weights to 0.
     Set the constraint wi2dvf to the counts.
  */
  for (ci = 0; ci < max_ci; ci++)
    {
      bow_cdoc *cdoc = bow_array_entry_at_index (vpc_barrel->cdocs, ci);
      total_num_docs += cdoc->word_count;
    }

  
  /* Count how many training documents there are.  Exclude documents
     that have no features, as we need to ignore them for
     doc_then_word */
  query_wv = NULL;
  test_heap = bow_test_new_heap (doc_barrel);
  
  /* Iterate over each document. */
  while (-1 != (di = bow_heap_next_wv (test_heap, doc_barrel, &query_wv, 
				       bow_cdoc_is_train)))
    {
      if (query_wv->num_entries == 0)
	total_num_docs--;
    }
  

  constraint_wi2dvf = bow_wi2dvf_new (doc_barrel->wi2dvf->size);
  for (wi = 0; wi < max_wi; wi++) 
    {
      dv = bow_wi2dvf_dv (vpc_barrel->wi2dvf, wi);
      
      if (!dv)
	continue;

      if (maxent_smooth_counts)
	{
	  dvi = 0;
	  for (ci = 0; ci < max_ci; ci++)
	    {
	      
	      while (dv->entry[dvi].di < ci &&
		     dvi < dv->length)
		dvi++;

	      /* set contraint to smoothed empirical average */
	      if (dvi < dv->length && dv->entry[dvi].di == ci)
		bow_wi2dvf_set_wi_di_count_weight(&constraint_wi2dvf, wi, ci, 
						  dv->entry[dvi].count + 1,
						  (dv->entry[dvi].weight + 1.0) / 
						  (double) total_num_docs);
	      else
		bow_wi2dvf_set_wi_di_count_weight(&constraint_wi2dvf, wi, ci, 
						  1,
						  1.0 / (double) total_num_docs);
		
	      /* initialize the lambda to 0 */
	      bow_wi2dvf_set_wi_di_count_weight (&(vpc_barrel->wi2dvf),
						 wi, ci,
						 1,
						 0);

	    }
	}
      else if (maxent_gaussian_prior)
	{
	  dvi = 0;
	  for (ci = 0; ci < max_ci; ci++)
	    {
	      
	      while (dv->entry[dvi].di < ci &&
		     dvi < dv->length)
		dvi++;
	      
	      /* set contraint to smoothed empirical average */
	      if (dvi < dv->length && dv->entry[dvi].di == ci)
		{
		  bow_wi2dvf_set_wi_di_count_weight(&constraint_wi2dvf, wi, ci, 
						    dv->entry[dvi].count,
						    dv->entry[dvi].weight / 
						    (double) total_num_docs);
		  /* initialize the lambda to 0 */
		  bow_wi2dvf_set_wi_di_count_weight (&(vpc_barrel->wi2dvf),
						     wi, ci,
						     1,
						     0);
		}
	      else if (maxent_gaussian_prior_zero_constraints)
		{
		  bow_wi2dvf_set_wi_di_count_weight(&constraint_wi2dvf, wi, ci, 
						    1,
						    0);
		  /* initialize the lambda to 0 */
		  bow_wi2dvf_set_wi_di_count_weight (&(vpc_barrel->wi2dvf),
						     wi, ci,
						     1,
						     0);
		}
	      
	    }
	}
      else
	{
	  for (dvi = 0; dvi < dv->length; dvi++)
	    {
	      bow_cdoc *cdoc;
	      
	      ci = dv->entry[dvi].di;
	      
	      cdoc = bow_array_entry_at_index (vpc_barrel->cdocs, ci);
	      
	      assert (dv->entry[dvi].weight > 0);
	      
	      /* */
	      bow_wi2dvf_set_wi_di_count_weight(&constraint_wi2dvf, wi, ci, 
						dv->entry[dvi].count,
						dv->entry[dvi].weight / 
						(double) total_num_docs);
	      
	      bow_wi2dvf_set_wi_di_count_weight (&(vpc_barrel->wi2dvf),
						 wi, ci,
						 dv->entry[dvi].count,
						 0);
	    }
	}
    }

#if 0
  if (maxent_print_constraints)
    {
      bow_verbosify (bow_progress, "foo");

      for (ci = 0; ci < max_ci; ci++)
	bow_verbosify (bow_progress, " %s", bow_barrel_classname_at_index (doc_barrel, ci));
      bow_verbosify (bow_progress, "\n");
    
      for (wi = 0; wi < max_wi; wi++)
	{
	  bow_verbosify (bow_progress, "%s", bow_int2word (wi));
	  dv = bow_wi2dvf_dv (constraint_wi2dvf, wi);
	  dvi = 0;
	
	  for (ci = 0; ci < max_ci; ci++)
	    {
	      while ((ci > dv->entry[dvi].di) && (dvi < dv->length))
		dvi++;
	      if ((ci == dv->entry[dvi].di) && (dvi < dv->length))
		bow_verbosify (bow_progress, " %f", dv->entry[dvi].weight);
	      else
		bow_verbosify (bow_progress, " 0");
	    }
	  bow_verbosify (bow_progress, "\n");
	}
    }
#endif

  
  /* Lets start some maximum entropy iteration */
  while (maxent_logprob_docs ? 
	 new_log_prob > old_log_prob : 
	 (maxent_halt_accuracy_docs ? 
	  new_accuracy > old_accuracy : 
	  rounds < maxent_num_iterations)) 
    {
      bow_wi2dvf *exp_wi2dvf = bow_wi2dvf_new (doc_barrel->wi2dvf->size);
      int num_tested = 0;


      for (ci = 0; ci < max_ci; ci++)
	total_count_per_ci[ci] = 0.0;

      rounds++;
      /* classify all the documents, and put their contribution into the 
	 different lambdas */
      query_wv = NULL;
      hits = alloca (sizeof (bow_score) * max_ci);
      num_tested = 0;
      test_heap = bow_test_new_heap (doc_barrel);
	  
       /* Calculate accuracy of the validation set for halting check */
      if (maxent_accuracy_docs)
	bow_verbosify (bow_progress, "%4d Correct: %f\n", rounds,
		       maxent_calculate_accuracy(doc_barrel, vpc_barrel, maxent_accuracy_docs, 1));
	  
     /* Loop once for each training document, scoring it and recording its
	 contribution to all the E[f_{w,c}] */
      while ((di = bow_heap_next_wv (test_heap, doc_barrel, &query_wv, 
				     maxent_iteration_docs))
	     != -1)
	{
	  doc_cdoc = bow_array_entry_at_index (doc_barrel->cdocs, 
					       di);
	  bow_wv_set_weights (query_wv, vpc_barrel);
	  bow_wv_normalize_weights (query_wv, vpc_barrel);

	  // skip documents with no words
	  if (query_wv->num_entries == 0)
	    continue;
	  
	  num_tested++;
	  actual_num_hits = 
	    bow_barrel_score (vpc_barrel, 
			      query_wv, hits,
			      max_ci, -1);
	  assert (actual_num_hits == max_ci);
	  
	  for (ci = 0; ci < max_ci; ci++)
	    total_count_per_ci[ci] += hits[ci].weight;


	  /* now loop over the words in the document and all the classes,
	     adding the contribution to E[f_{w,c}] */
	  for (wvi=0; wvi < query_wv->num_entries; wvi++)
	    {
	      wi = query_wv->entry[wvi].wi;
	      
	      for (ci=0; ci < bow_barrel_num_classes (vpc_barrel); ci++)
		bow_wi2dvf_add_wi_di_count_weight 
		  (&exp_wi2dvf, wi, ci, 1,
		   hits[ci].weight * query_wv->entry[wvi].weight);
	    }
	}
      
      /* now update the lambdas.  Ignore zero constraints? */
      for (wi = 0; wi < max_wi; wi++) 
	{
	  bow_dv *vpc_dv;
	  bow_dv *constraint_dv;
	  bow_dv *exp_dv;
	  int exp_dvi = 0;

	  vpc_dv = bow_wi2dvf_dv (vpc_barrel->wi2dvf, wi);
	  constraint_dv = bow_wi2dvf_dv (constraint_wi2dvf, wi);
	  exp_dv = bow_wi2dvf_dv (exp_wi2dvf, wi);

	  /* the exp_dv can be null if we're using only some of the
             documents for the iteration step.  If there are no
             iteration docs that have this word, then we don't need to
             worry about its weight... leave it at zero */
	  if (!constraint_dv || !exp_dv)
	    continue;

	  /* the dvi goes over the constraint and the vpc; the
	     constraint and vpc wi2dvf should have exactly
	     corresponding entries.  The exp wi2dvf can have
	     a superset of the entries; */

	  for (dvi = 0; dvi < vpc_dv->length; dvi++)
	    {
	      ci = vpc_dv->entry[dvi].di;
	      
	      /* get the corresponding exp_dvi */
	      while (exp_dvi < exp_dv->length &&
		     ci > exp_dv->entry[exp_dvi].di)
		exp_dvi++;

	      assert (exp_dvi < exp_dv->length);
	      assert (ci == constraint_dv->entry[dvi].di &&
		      ci == exp_dv->entry[exp_dvi].di);
	
	      /* need to normalize this delta with M? */
#if 1
	      if (exp_dv->entry[exp_dvi].weight == 0)
		assert (constraint_dv->entry[dvi].weight == 0);
	      else 
#endif
		{
		  double delta = 0;

		  if (maxent_gaussian_prior)
		    {
		      double variance = maxent_prior_variance;
		      
		      if (maxent_prior_vary_by_count == 1)
			variance = maxent_prior_variance * 
			  log (1 + constraint_dv->entry[dvi].count);
		      else if (maxent_prior_vary_by_count == 2)
			variance = maxent_prior_variance * constraint_dv->entry[dvi].count;

		      newton_poly->entry[0].coeff = -constraint_dv->entry[dvi].weight +
			vpc_dv->entry[dvi].weight / variance;
		      newton_poly->entry[1].coeff = exp_dv->entry[exp_dvi].weight / 
			(double) num_tested;
		      newton_poly->entry[2].coeff = 1.0 / variance;
		      
		      delta = maxent_newton (newton_poly);
		      delta = log (delta);
		    }
		  else
		    {
		      if (exp_dv->entry[exp_dvi].weight != 0)
			delta = log (((double) num_tested) * constraint_dv->entry[dvi].weight /
				     (exp_dv->entry[exp_dvi].weight)) /
			  (double) bow_event_document_then_word_document_length;
		      else
			delta = 0;
		      
		      /* check that delta is not NaN */
		      assert (delta == delta);
		      assert (constraint_dv->entry[dvi].weight);
		    }

		  bow_wi2dvf_set_wi_di_count_weight 
		    (&(vpc_barrel->wi2dvf), wi, ci,
		     vpc_dv->entry[dvi].count,
		     (vpc_dv->entry[dvi].weight + delta));
		}
	    }
	}
      
      if (maxent_logprob_docs)
	{
	  old_log_prob = new_log_prob;
	  new_log_prob = maxent_calculate_accuracy(doc_barrel, vpc_barrel, maxent_logprob_docs, 2);

	  bow_verbosify (bow_progress, "Halting Log Prob: %f\n", new_log_prob);
	}
      else if (maxent_halt_accuracy_docs)
	{
	  old_accuracy = new_accuracy;
	  new_accuracy = maxent_calculate_accuracy (doc_barrel, vpc_barrel, maxent_halt_accuracy_docs, 1);

	  bow_verbosify (bow_progress, "Halting Accuracy: %f\n", new_accuracy);
	}
      bow_wi2dvf_free (exp_wi2dvf);
    }

  bow_free (newton_poly);
  bow_wi2dvf_free (constraint_wi2dvf);
  bow_maxent_model_building = 0;

#if 0
  if (maxent_print_lambdas)
    {
      bow_verbosify (bow_progress, "foo");

      for (ci = 0; ci < max_ci; ci++)
	bow_verbosify (bow_progress, " %s", bow_barrel_classname_at_index (doc_barrel, ci));
      bow_verbosify (bow_progress, "\n");
    
      for (wi = 0; wi < max_wi; wi++)
	{
	  bow_verbosify (bow_progress, "%s", bow_int2word (wi));
	  dv = bow_wi2dvf_dv (vpc_barrel->wi2dvf, wi);
	  dvi = 0;
	
	  for (ci = 0; ci < max_ci; ci++)
	    {
	      while ((ci > dv->entry[dvi].di) && (dvi < dv->length))
		dvi++;
	      if ((ci == dv->entry[dvi].di) && (dvi < dv->length))
		bow_verbosify (bow_progress, " %f", dv->entry[dvi].weight);
	      else
		bow_verbosify (bow_progress, " 0");
	    }
	  bow_verbosify (bow_progress, "\n");
	}
    }
#endif

  
  return (vpc_barrel);      
}


bow_barrel *
bow_maxent_new_vpc_with_weights (bow_barrel *doc_barrel)
{
  bow_barrel *vpc_barrel;   /* the vector-per-class barrel */
  int wi;                   /* word index */
  int max_wi;               /* max word index */
  int dvi;                  /* document vector index */
  int ci;                   /* class index */
  bow_dv *dv;               /* document vector */
  int di;                   /* document index */
  bow_dv_heap *test_heap=NULL;	/* we'll extract test WV's from here */
  bow_wv *query_wv;
  bow_score *hits;
  int actual_num_hits;
  bow_cdoc *doc_cdoc;
  bow_cdoc *cdoc;
  bow_wi2dvf *constraint_wi2dvf;
  int max_ci;
  int rounds = 0;
  int total_num_docs = 0;
  int **f_sharp;
  int max_f_sharp = 0;
  double *coefficients[200];
  bow_dv *doc_dv;
  bow_dv *constraint_dv;
  bow_dv *lambda_dv;
  int constraint_dvi;
  int doc_dvi;
  int fi;
  maxent_polynomial *newton_poly;
  double log_prob_model;
  double beta;
  double num_words_per_ci[200];
  int num_unique_words_per_ci[200];
  float old_log_prob = -FLT_MAX;
  float new_log_prob = -FLT_MAX / 2;
  float old_accuracy = -1;
  float new_accuracy = 0;

  if (bow_event_model == bow_event_document_then_word)
    return (bow_maxent_new_vpc_with_weights_doc_then_word (doc_barrel));

  bow_maxent_model_building = 1;

  /* some sanity checks first */
  assert (200 > bow_barrel_num_classes(doc_barrel));
  assert (doc_barrel->classnames);
  assert (bow_event_model == bow_event_word);
  assert (!maxent_words_per_class || !maxent_scoring_hack);
  assert (!(maxent_smooth_counts && maxent_gaussian_prior));
  assert (!maxent_words_per_class || !maxent_logprob_constraints);
  assert (!maxent_logprob_constraints);
  assert (!maxent_prior_vary_by_count);
  assert (!maxent_constraint_use_unlabeled);


  max_wi = MIN (doc_barrel->wi2dvf->size, bow_num_words ());
  max_ci = bow_barrel_num_classes (doc_barrel);
  f_sharp = bow_malloc (sizeof (int *) * doc_barrel->cdocs->length);

  for (di = 0; di < doc_barrel->cdocs->length; di++)
    f_sharp[di] = bow_malloc (sizeof (int) * max_ci);

  /* initialize f_sharp */
  for (di = 0; di < doc_barrel->cdocs->length; di++)
    for (ci = 0; ci < max_ci; ci++)
      f_sharp[di][ci] = 0;

  /* if we're doing log counts, set the document weights appropriately. 
     Otherwise, set the weights to the counts for each document. */
  if (maxent_logprob_constraints)
    {
      for (wi = 0; wi < max_wi; wi++)
	{
	  dv = bow_wi2dvf_dv (doc_barrel->wi2dvf, wi);
	  if (dv == NULL)
	    continue;
	  for (dvi = 0; dvi < dv->length; dvi++) 
	    dv->entry[dvi].weight = log (dv->entry[dvi].count + 1);
	}
    }
  else
    {
      for (wi = 0; wi < max_wi; wi++)
	{
	  dv = bow_wi2dvf_dv (doc_barrel->wi2dvf, wi);
	  if (dv == NULL)
	    continue;
	  for (dvi = 0; dvi < dv->length; dvi++) 
	    dv->entry[dvi].weight = (float) dv->entry[dvi].count;
	}
    }

  /* get a barrel where the counts are set to word counts and the
     weights are set to normalized or unnormalized counts as
     appropriate for the event model */
  vpc_barrel = bow_barrel_new_vpc (doc_barrel);

  /* if doing occurrence count pruning of features, do that now. */
  if (maxent_prune_features_by_count)
    maxent_prune_features_by_occurrence_count (vpc_barrel, 
					       maxent_prune_features_by_count);

  /* set the word count and normalizer of each class cdoc correctly.
     Use the weight here, b/c maybe doing logprob_constraints.  The word
     counts and normalizer are used by mutual information feature
     pruning.*/
  for (ci = 0; ci < max_ci; ci++)
    {
      num_words_per_ci[ci] = 0;
      num_unique_words_per_ci[ci] = 0;
    }

  for (wi = 0; wi < max_wi; wi++) 
    {
      dv = bow_wi2dvf_dv (vpc_barrel->wi2dvf, wi);
      if (dv == NULL)
	continue;
      for (dvi = 0; dvi < dv->length; dvi++) 
	{
	  num_words_per_ci[dv->entry[dvi].di] += dv->entry[dvi].weight;
	  num_unique_words_per_ci[dv->entry[dvi].di]++;
	}
    }

  for (ci = 0; ci < vpc_barrel->cdocs->length; ci++)
    {
      cdoc = bow_array_entry_at_index (vpc_barrel->cdocs, ci);
      cdoc->word_count = (int) rint (num_words_per_ci[ci]);
      cdoc->normalizer = num_unique_words_per_ci[ci];
    }

  /* If doing feature selection by mutual information, do that now.
     Ensure that cdoc->word_count set correctly beforehand.  It should
     be ok to do both kinds of feature selection pruning. */
  if (maxent_words_per_class > 0)
    maxent_prune_vocab_by_mutual_information (vpc_barrel, 
					      maxent_words_per_class);

  /* initialize cdoc->class_probs for all the docs and initialize
     total_num_docs to the number of training docs */
  for (di=0; di < doc_barrel->cdocs->length; di++)
    {
      bow_cdoc *cdoc = bow_array_entry_at_index (doc_barrel->cdocs, di);
      double *double_class_probs;
	
      if (cdoc->type == bow_doc_train)
	total_num_docs++;

      if (!cdoc->class_probs)
	cdoc->class_probs = (float *) bow_malloc (sizeof (double) * max_ci);
	
      double_class_probs = (double *) cdoc->class_probs;

      /* initialize the class_probs to all zeros */
      for (ci=0; ci < max_ci; ci++)
	double_class_probs[ci] = 0.0;
    }
  
  /* Set the constraint wi2dvf to be the (vpc weight / number of
     documents).  Re-initialize the vpc weights to 0 (initialize the
     lambdas to be zero).  */
  constraint_wi2dvf = bow_wi2dvf_new (doc_barrel->wi2dvf->size);
  for (wi = 0; wi < max_wi; wi++) 
    {
      dv = bow_wi2dvf_dv (vpc_barrel->wi2dvf, wi);
      
      if (!dv)
	continue;

      if (maxent_smooth_counts)
	{

	  dvi = 0;
	  for (ci = 0; ci < max_ci; ci++)
	    {
	     
	      while (dv->entry[dvi].di < ci &&
		     dvi < dv->length)
		dvi++;

	      /* set contraint to smoothed empirical average */
	      if (dvi < dv->length && dv->entry[dvi].di == ci)
		bow_wi2dvf_set_wi_di_count_weight(&constraint_wi2dvf, wi, ci, 
						  dv->entry[dvi].count + 1,
						  (dv->entry[dvi].weight + 1.0) / 
						  (double) total_num_docs);
	      else
		bow_wi2dvf_set_wi_di_count_weight(&constraint_wi2dvf, wi, ci, 
						  1,
						  1.0 / (double) total_num_docs);
		
	      /* initialize the lambda to 0 */
	      bow_wi2dvf_set_wi_di_count_weight (&(vpc_barrel->wi2dvf),
						 wi, ci,
						 1,
						 0);

	    }
	}
      else if (maxent_gaussian_prior)
	{
	  dvi = 0;
	  for (ci = 0; ci < max_ci; ci++)
	    {
	      
	      while (dv->entry[dvi].di < ci &&
		     dvi < dv->length)
		dvi++;
	      
	      /* set contraint to smoothed empirical average */
	      if (dvi < dv->length && dv->entry[dvi].di == ci)
		{
		  bow_wi2dvf_set_wi_di_count_weight(&constraint_wi2dvf, wi, ci, 
						    dv->entry[dvi].count,
						    dv->entry[dvi].weight / 
						    (double) total_num_docs);
		  /* initialize the lambda to 0 */
		  bow_wi2dvf_set_wi_di_count_weight (&(vpc_barrel->wi2dvf),
						     wi, ci,
						     1,
						     0);
		}
	      else if (maxent_gaussian_prior_zero_constraints)
		{
		  bow_wi2dvf_set_wi_di_count_weight(&constraint_wi2dvf, wi, ci, 
						    1,
						    0);
	      
		  /* initialize the lambda to 0 */
		  bow_wi2dvf_set_wi_di_count_weight (&(vpc_barrel->wi2dvf),
						     wi, ci,
						     1,
						     0);
		}
	    }
	}
      else
	{
	  
	  for (dvi = 0; dvi < dv->length; dvi++)
	    {
	      ci = dv->entry[dvi].di;
	      
	      assert (dv->entry[dvi].weight > 0);
	      
	      
	      /* set contraint to empirical average */
	      bow_wi2dvf_set_wi_di_count_weight(&constraint_wi2dvf, wi, ci, 
						dv->entry[dvi].count,
						dv->entry[dvi].weight / 
						(double) total_num_docs);
	      
	      /* initialize the lambda to 0 */
	      bow_wi2dvf_set_wi_di_count_weight (&(vpc_barrel->wi2dvf),
						 wi, ci,
						 dv->entry[dvi].count,
						 0);
	    }
	}
    }

  /* set f_sharp of each document/class combination to be the sum of
     all the feature weights for that class for that doc.  set
     max_f_sharp to be the maximum of all the f_sharp values.  Note
     that we're summing document word counts here, and not document
     word weights.  We'll have to do something more sneaky for logprob
     constraints when we implement it.  For now, though, this should
     be ok.*/
  
  /* walk the document wi2dvf with the constraint wi2dvf and increment */
  for (wi = 0; wi < max_wi; wi++)
    {
      doc_dv = bow_wi2dvf_dv (doc_barrel->wi2dvf, wi);
      constraint_dv = bow_wi2dvf_dv (constraint_wi2dvf, wi);
      
      if (!constraint_dv || !doc_dv)
	continue;

      for (doc_dvi = 0; doc_dvi < doc_dv->length; doc_dvi++)
	for (constraint_dvi = 0; constraint_dvi < constraint_dv->length; 
	     constraint_dvi++)
	  f_sharp[doc_dv->entry[doc_dvi].di][constraint_dv->entry[constraint_dvi].di] +=
	    doc_dv->entry[doc_dvi].count;
    }

  /* find the max_fsharp */
  for (di = 0; di < doc_barrel->cdocs->length ; di++)
    {
      bow_cdoc *doc_cdoc = bow_array_entry_at_index (doc_barrel->cdocs, di);

      if (doc_cdoc->type != bow_doc_train)
	continue;

      for (ci = 0; ci < max_ci; ci++)
	if (f_sharp[di][ci] > max_f_sharp)
	  max_f_sharp = f_sharp[di][ci];
    }

  max_f_sharp++;

  /* allocate space for the coefficients */
  for (ci = 0; ci < max_ci; ci++)
    coefficients[ci] = bow_malloc (sizeof (double) * (max_f_sharp));

  /* initialize coefficients */
  for (ci = 0; ci < max_ci; ci++)
    for (fi = 0; fi < max_f_sharp; fi++)
      coefficients[ci][fi] = 0.0;

  /* initialize the newton_poly structure */
  newton_poly = bow_malloc (sizeof (maxent_polynomial) +
			    sizeof (maxent_coefficient) * max_f_sharp);
  newton_poly->size = max_f_sharp;

  /* Lets start some maximum entropy iteration */
  while  (maxent_logprob_docs ? 
	  new_log_prob > old_log_prob : 
	  (maxent_halt_accuracy_docs ? 
	   new_accuracy > old_accuracy : 
	   rounds < maxent_num_iterations)) 
    {
      int num_tested;

      rounds++;

      /* classify all the training documents, and store the class
	 membership probs in each document's cdoc->class_probs */
      query_wv = NULL;
      hits = alloca (sizeof (bow_score) * max_ci);
      num_tested = 0;
      test_heap = bow_test_new_heap (doc_barrel);
      log_prob_model = 0;
	  
     /* Loop once for each training document, scoring it and recording its
	class probs */
      while ((di = bow_heap_next_wv (test_heap, doc_barrel, &query_wv, 
				     maxent_iteration_docs))
	     != -1)
	{
	  double *double_class_probs;

	  num_tested++;
	  doc_cdoc = bow_array_entry_at_index (doc_barrel->cdocs, 
					       di);
	  bow_wv_set_weights (query_wv, vpc_barrel);
	  bow_wv_normalize_weights (query_wv, vpc_barrel);
	  actual_num_hits = 
	    bow_barrel_score (vpc_barrel, 
			      query_wv, hits,
			      max_ci, -1);
	  assert (actual_num_hits == max_ci);
	  
	  log_prob_model += log (hits[doc_cdoc->class].weight);

	  double_class_probs = (double *) doc_cdoc->class_probs;

	  /* record the scores in class_probs */
	  for (ci = 0; ci < max_ci; ci++)
	    {
	      double_class_probs[ci] = hits[ci].weight;
	      assert (double_class_probs[ci]); 
	    }
	}


       /* Calculate accuracy of the validation set for halting check */
      if (maxent_accuracy_docs)
	bow_verbosify (bow_progress, 
		       "%4d Training Log Prob: %f Selected Correct: %f\n",
		       rounds, log_prob_model,
		       maxent_calculate_accuracy(doc_barrel, vpc_barrel, 
						 maxent_accuracy_docs, 1));

      bow_verbosify (bow_progress, "Updating lambdas :         ");

      /* now calculate a new lambda for each word feature.  */
      for (wi = 0; wi < max_wi; wi++)
	{
	  constraint_dv = bow_wi2dvf_dv (constraint_wi2dvf, wi);
	  
	  if (wi % 100 == 0)
	    bow_verbosify (bow_progress, "\b\b\b\b\b\b\b%7d", wi);

	  if (!constraint_dv)
	    continue;

	  dv = bow_wi2dvf_dv (doc_barrel->wi2dvf, wi);
	  lambda_dv = bow_wi2dvf_dv (vpc_barrel->wi2dvf, wi);
	  assert (dv && lambda_dv);
	  

	  /* collect the coefficients for all classes simultaneously */
	  for (dvi = 0; dvi < dv->length; dvi++)
	    {
	      bow_cdoc *cdoc;
	      double *double_class_probs;
	      
	      di = dv->entry[dvi].di;
	      cdoc = bow_array_entry_at_index (doc_barrel->cdocs, di);

	      if (cdoc->type != bow_doc_train)
		continue;

	      double_class_probs = (double *) cdoc->class_probs;

	      for (constraint_dvi = 0; constraint_dvi < constraint_dv->length;
		   constraint_dvi++)
		{
		  ci = constraint_dv->entry[constraint_dvi].di;

		  coefficients[ci][f_sharp[di][ci]] += double_class_probs[ci] *
		    dv->entry[dvi].weight / (double) total_num_docs;
		}
	    }


	  /* now update the lambdas */
	  for (dvi = 0; dvi < constraint_dv->length; dvi++)
	    {
	      bow_cdoc *cdoc;

	      /* set the zeroth coefficient to -constraint */
	      ci = constraint_dv->entry[dvi].di;

	      cdoc = bow_array_entry_at_index (vpc_barrel->cdocs, ci);

	      /* skip classes for which there is no training data */
	      if (!cdoc->word_count)
		continue;

	      coefficients[ci][0] -= constraint_dv->entry[dvi].weight;
	      
	      /* compile down the class coefficients into newton_poly */
	      newton_poly->length = 0;
	      for (fi = 0; fi < max_f_sharp; fi++)
		{
		  if (coefficients[ci][fi] != 0)
		    {
		      newton_poly->entry[newton_poly->length].coeff = 
			coefficients[ci][fi] ;
		      newton_poly->entry[newton_poly->length].power = fi;
		      newton_poly->length++;
		    }
		}
	      assert (newton_poly->length > 1);

	      if (maxent_gaussian_prior)
		{
		  newton_poly->entry[0].coeff += 
		    lambda_dv->entry[dvi].weight / maxent_prior_variance;

		  newton_poly->entry[newton_poly->length].power = -1;
		  newton_poly->entry[newton_poly->length].coeff = 1.0 / maxent_prior_variance;
		}


	      /* solve for beta using newton's method on the coefficients */
	      beta = maxent_newton (newton_poly);

	      /* update the right lambda */
	      assert (lambda_dv->entry[dvi].di == ci);
	      lambda_dv->entry[dvi].weight += log (beta);
	      assert (lambda_dv->entry[dvi].weight ==  lambda_dv->entry[dvi].weight);

	      /* clear out the coefficients used */
	      for (fi = 0; fi < newton_poly->length; fi++)
		coefficients[ci][newton_poly->entry[fi].power] = 0.0;
							  
	    }
	}

      bow_verbosify (bow_progress, "\b\b\b\b\b\b\b%7d\n", wi);


      /* calculate the new accuracy or log_prob for the halting check */
      if (maxent_logprob_docs)
	{
	  old_log_prob = new_log_prob;
	  new_log_prob = maxent_calculate_accuracy(doc_barrel, vpc_barrel, 
						   maxent_logprob_docs, 2);
	  bow_verbosify (bow_progress, "Halting Log Prob: %f\n", new_log_prob);
	}
      else if (maxent_halt_accuracy_docs)
	{
	  old_accuracy = new_accuracy;
	  new_accuracy = maxent_calculate_accuracy (doc_barrel, vpc_barrel, 
						    maxent_halt_accuracy_docs,
						    1);
	  bow_verbosify (bow_progress, "Halting Accuracy: %f\n", new_accuracy);
	}

    
    }

  for (di = 0; di < doc_barrel->cdocs->length; di++)
    bow_free (f_sharp[di]);

  bow_wi2dvf_free (constraint_wi2dvf);
  bow_free (f_sharp);
  bow_free (newton_poly);
  for (ci = 0; ci < max_ci; ci++)
    bow_free (coefficients[ci]);
  bow_maxent_model_building = 0;
  return (vpc_barrel);      
}


int
bow_maxent_score (bow_barrel *barrel, bow_wv *query_wv, 
		  bow_score *bscores, int bscores_len,
		  int loo_class)
{
  double *scores;		/* will become prob(class), indexed over CI */
  int ci;			/* a "class index" (document index) */
  int wvi;			/* an index into the entries of QUERY_WV. */
  int dvi;			/* an index into a "document vector" */
  double rescaler;		/* Rescale SCORES by this after each word */
  int num_scores;		/* number of entries placed in SCORES */
  int wi;                       /* word index */
  int max_wi;
  int max_ci;

  assert (bow_event_model == bow_event_word ||
	  bow_event_model == bow_event_document_then_word);
  assert (loo_class == -1);
  assert (bscores_len <= bow_barrel_num_classes (barrel));
  assert (!bow_maxent_model_building || 
	  bow_barrel_num_classes (barrel) == bscores_len);

  /* Allocate space to store scores for *all* classes (documents) */
  scores = alloca (bow_barrel_num_classes (barrel) * sizeof (double));
  max_wi = MIN (barrel->wi2dvf->size, bow_num_words());
  max_ci = bow_barrel_num_classes (barrel);

  /* Remove words not in the class_barrel */
  bow_wv_prune_words_not_in_wi2dvf (query_wv, barrel->wi2dvf);

  /* Yipes, now we are doing this twice, but we have to make sure that 
     it is re-done after any pruning of words in the QUERY_WV */
  bow_wv_set_weights (query_wv, barrel);
  bow_wv_normalize_weights (query_wv, barrel);

#if 0
  /* WhizBang Print the WV, just for debugging */
  bow_wv_fprintf (stderr, query_wv);
  fflush (stderr);
#endif

  /* Initialize the SCORES to 0. */
  for (ci=0; ci < bow_barrel_num_classes (barrel); ci++)
    scores[ci] = 0;

  
  for (wvi=0; wvi < query_wv->num_entries; wvi++)
    {
      bow_dv *dv;		/* the "document vector" for the word WI */
      
      wi = query_wv->entry[wvi].wi;
      dv = bow_wi2dvf_dv (barrel->wi2dvf, wi);
      
      /* If the model doesn't know about this word, skip it.  Is this
	 really the right thing to do for maximum entropy?  */
      if (!dv)
	continue;
      
      rescaler = DBL_MAX;
      
      if (maxent_scoring_hack)
	{
	  /* loop over all classes, using the lambda, or the NB prob if zero */
	  dvi = 0;
	  for (ci = 0; ci < max_ci; ci++)
	    {
	      while (dv->entry[dvi].di < ci && dv->length < dvi)
		dvi++;

	      if (dvi < dv->length && dv->entry[dvi].di == ci)
		scores[ci] += dv->entry[dvi].weight * query_wv->entry[wvi].weight;
	      else
		{
		  bow_cdoc *cdoc = bow_array_entry_at_index(barrel->cdocs, ci);
		  
		  if (cdoc->word_count && cdoc->normalizer)
		    scores[ci] += (1.0 / (double) (cdoc->word_count + cdoc->normalizer)) *
		      query_wv->entry[wvi].weight;
		  else
		    scores[ci] -= 10;
		}

	      /* Keep track of the minimum score updated for this word. */
	      if (rescaler > scores[ci])
		rescaler = scores[ci];
	    }
	}
      else
	{
	  /* Loop over all features using this word, putting this word's (WI's)
	     contribution into SCORES.  */
	  for (dvi = 0; dvi < dv->length; dvi++)
	    {
	      double lambda;
	      
	      ci = dv->entry[dvi].di;
	      
	      lambda = dv->entry[dvi].weight;
	      
	      /* update the scores for this word class combination */
	      scores[ci] += lambda * query_wv->entry[wvi].weight;
	      
	      /* Keep track of the minimum score updated for this word. */
	      if (rescaler > scores[ci])
		rescaler = scores[ci];
	    }
	}
      
      /* Loop over all classes, re-scaling SCORES so that they
	 don't get so small we loose floating point resolution.
	 This scaling always keeps all SCORES positive. */
      if (rescaler < 0)
	{
	  for (ci = 0; ci < barrel->cdocs->length; ci++)
	    {
	      /* Add to SCORES to bring them close to zero.  RESCALER is
		 expected to often be less than zero here. */
	      /* xxx If this doesn't work, we could keep track of the min
		 and the max, and sum by their average. */
	      scores[ci] += -rescaler;
	      assert (scores[ci] > -DBL_MAX + 1.0e5
		      && scores[ci] < DBL_MAX - 1.0e5);
	    }
	}
    }
    
  /* Rescale the SCORE one last time, this time making them all 0 or
     negative, so that exp() will work well, especially around the
     higher-probability classes. */
  rescaler = -DBL_MAX;
  for (ci = 0; ci < barrel->cdocs->length; ci++)
    if (scores[ci] > rescaler) 
      rescaler = scores[ci];
  /* RESCALER is now the maximum of the SCORES. */
  for (ci = 0; ci < barrel->cdocs->length; ci++)
    scores[ci] -= rescaler;
	   
  /* Use exp() on the SCORES */
  for (ci = 0; ci < barrel->cdocs->length; ci++)
    scores[ci] = exp (scores[ci]);

  /* Now normalize to make them probabilities */
  {
    double scores_sum = 0;
    for (ci = 0; ci < barrel->cdocs->length; ci++)
      scores_sum += scores[ci];
    for (ci = 0; ci < barrel->cdocs->length; ci++)
      {
	scores[ci] /= scores_sum;
	/* assert (scores[ci] > 0); */
      }
  }
  
  /* Return the SCORES by putting them (and the `class indices') into
     SCORES in sorted order. */
  if (!bow_maxent_model_building)
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
  else
    {
      for (ci = 0; ci < barrel->cdocs->length; ci++)
	{
	  bscores[ci].weight = scores[ci];
	  bscores[ci].di = ci ; 
	}
      num_scores = bscores_len;
    }

  return num_scores;
}


rainbow_method bow_method_maxent = 
{
  "maxent",
  NULL, /* now weight setting function */
  NULL, /* no weight scaling function */
  NULL, /* bow_barrel_normalize_weights_by_summing, */
  bow_maxent_new_vpc_with_weights,
  bow_barrel_set_vpc_priors_by_counting,
  bow_maxent_score,
  bow_wv_set_weights_by_event_model,
  NULL, /* no need for extra wv weight normalization */
  bow_barrel_free, /* is this really the right one? */
  NULL /* no parameters for the method */
};

void _register_method_maxent () __attribute__ ((constructor));
void _register_method_maxent ()
{
  bow_method_register_with_name ((bow_method*)&bow_method_maxent,
				 "maxent",
				 sizeof (rainbow_method),
				 &maxent_argp_child);
  bow_argp_add_child (&maxent_argp_child);
}
