/* Weight-setting and scoring implementation for EM classification */

/* Copyright (C) 1997, 1998, 1999 Andrew McCallum

   Written by:  Kamal Nigam <knigam@cs.cmu.edu>

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
#include <stdlib.h>
#include <bow/em.h>
#include <bow/naivebayes.h>



/* EM-specific types */

/* a specification for how to convert naive bayes scores into probabilities */
typedef enum
{ 
  simple,			/* 1 or 0 based on winning class */
  nb_score			/* score directly from naivebayes */
} bow_em_stat_method;

/* a specification for how to use the unlabeled data when setting the EM
   starting point */
typedef enum
{ 
  em_start_zero,   /* unlabeled docs have no effect on starting point */
  em_start_even,   /* unlabeled docs distributed evenly */
  em_start_prior,  /* unlabeled docs distributed according to labeled prior */
  em_start_random  /* unlabeled docs distributed randomly */
} bow_em_unlabeled_start_method;

/* a specification for how to use the unlabeled data when setting the EM
   starting point for multi-hump negative class */
typedef enum 
{
  bow_em_init_spiked,  /* distribute each doc to one class */
  bow_em_init_spread    /* distribute each doc across classes */
} bow_em_multi_hump_init_method;


/* some forward definitions */
void bow_em_print_word_distribution (bow_barrel *vpc_barrel, 
				     int em_runs, int num_classes);
double em_calculate_perplexity (bow_barrel *doc_barrel, 
				 bow_barrel *class_barrel);
float em_calculate_accuracy (bow_barrel *doc_barrel, bow_barrel *class_barrel);
void bow_em_set_weights (bow_barrel *barrel);

/* Global Variables */

/* hack for binary scoring method */
static int bow_em_making_barrel = 0;

/* hack for scoring for perplexity calculation */
int bow_em_calculating_perplexity = 0;

/* ci of binary positive class */
static int binary_pos_ci = -1;

/* Command-line options specific to EM.  See em_optinos for documentation*/
static char * em_binary_pos_classname = NULL;
static char * em_binary_neg_classname = NULL;
static int em_compare_to_nb = 0;
static bow_em_stat_method em_stat_method = nb_score;
int bow_em_num_em_runs = 7;
static int bow_em_print_probs = 0;
static int bow_em_print_word_vector = 0;
static int bow_em_binary_case = 0;
static float unlabeled_normalizer = 1.0;
static int bow_em_multi_hump_neg = 0;
bow_em_perturb_method bow_em_perturb_starting_point = 0;
int em_cross_entropy = 0;
static int em_anneal = 0;
static float em_temperature = 200;
static float em_temp_reduction = 0.9;
static bow_em_unlabeled_start_method em_unlabeled_start = em_start_zero;
static bow_em_multi_hump_init_method em_multi_hump_init = 
         bow_em_init_spread;
static int em_halt_using_perplexity = 0;
static int (* em_perplexity_docs)(bow_cdoc *) = NULL;
static int em_perplexity_loo = 0;
static int bow_em_anneal_normalizer = 0;
static int em_halt_using_accuracy = 0;
static int (* em_accuracy_docs)(bow_cdoc *) = NULL;
static int em_accuracy_loo = 0;
static int em_labeled_for_start_only = 0;
static int em_set_vocab_from_unlabeled = 0;

/* The integer or single char used to represent this command-line option.
   Make sure it is unique across all libbow and rainbow. */
enum {
  EM_COMPARE_TO_NB = 2222,
  EM_STAT_METHOD,
  EM_NUM_RUNS,
  EM_PRINT_PROBS,
  EM_BINARY_POS_CLASS, 
  EM_BINARY_NEG_CLASS, 
  EM_PRINT_TOP_WORDS, 
  EM_BINARY, 
  EM_UNLABELED_NORMALIZER, 
  EM_MULTI_HUMP, 
  EM_PERTURB_STARTING_POINT, 
  EM_NO_PERTURB, 
  EM_CROSSENTROPY,
  EM_ANNEAL,
  EM_TEMPERATURE,
  EM_TEMP_REDUCE,
  EM_UNLABELED_START,
  EM_MULTI_HUMP_INIT,
  EM_HALT_USING_PERPLEXITY,
  EM_ANNEAL_NORMALIZER,
  EM_PRINT_PERPLEXITY,
  EM_HALT_USING_ACCURACY,
  EM_PRINT_ACCURACY,
  EM_LABELED_FOR_START_ONLY,
  EM_SET_VOCAB_FROM_UNLABELED
};

static struct argp_option em_options[] =
{
  {0,0,0,0,
   "EM options:", 60},
  {"em-compare-to-nb", EM_COMPARE_TO_NB, 0, 0,
   "When building an EM class barrel, show doc stats for the naivebayes"
   "barrel equivalent.  Only use in conjunction with --test."},
  {"em-stat-method", EM_STAT_METHOD, "STAT", 0,
   "The method to convert scores to probabilities."
   "The default is 'nb_score'."},
  {"em-num-iterations", EM_NUM_RUNS, "NUM", 0, 
   "Number of EM iterations to run when building model."},
  {"em-save-probs", EM_PRINT_PROBS, 0, 0,
   "On each EM iteration, save all P(C|w) to a file."},
  {"em-binary-pos-classname", EM_BINARY_POS_CLASS, "CLASS", 0, 
   "Specify the name of the positive class if building a binary classifier."},
  {"em-binary-neg-classname", EM_BINARY_NEG_CLASS, "CLASS", 0, 
   "Specify the name of the negative class if building a binary classifier."},
  {"em-print-top-words", EM_PRINT_TOP_WORDS, 0, 0,
   "Print the top 10 words per class for each EM iteration."},
  {"em-binary", EM_BINARY, 0, 0,
   "Do special tricks for the binary case."},
  {"em-unlabeled-normalizer", EM_UNLABELED_NORMALIZER, "NUM", 0,
   "Number of unlabeled docs it takes to equal a labeled doc."
   "Defaults to one."},
  {"em-multi-hump-neg", EM_MULTI_HUMP, "NUM", 0,
   "Use NUM center negative classes. Only use in binary case."
   "Must be using scoring method nb_score."},
  {"em-perturb-starting-point", EM_PERTURB_STARTING_POINT, "TYPE", 0,
   "Instead of starting EM with P(w|c) from the labeled training data, "
   "start from values that are randomly sampled from the multinomial "
   "specified by the labeled training data.  TYPE specifies what "
   "distribution to use for the perturbation; choices are `gaussian' "
   "`dirichlet', and `none'.  Default is `none'."},
  {"em-crossentropy", EM_CROSSENTROPY, 0, 0,
   "Use crossentropy instead of naivebayes for scoring."},
  {"em-anneal", EM_ANNEAL, 0, 0,
   "Use Deterministic annealing EM."},
  {"em-temperature", EM_TEMPERATURE, "NUM", 0,
   "Initial temperature for deterministic annealing.  Default is 200."},
  {"em-temp-reduce", EM_TEMP_REDUCE, "NUM", 0,
   "Temperature reduction factor for deterministic annealing.  Default is 0.9."},
  {"em-unlabeled-start", EM_UNLABELED_START, "TYPE", 0, 
   "When initializing the EM starting point, how the unlabeled docs"
   " contribute.  Default is `zero'.  Other choices are `prior' `random' "
   " and `even'."},
  {"em-multi-hump-init", EM_MULTI_HUMP_INIT, "METHOD", 0,
   "When initializing mixture components, how to assign component probs "
   "to documents.  Default is `spread'.  Other choices are `spiked'."},
  {"em-halt-using-perplexity", EM_HALT_USING_PERPLEXITY, "TYPE", 0,
   "When running EM, halt when perplexity plataeus.  TYPE is type of document "
   "to measure perplexity on.  Choices are `validation', `train', `test', "
   "`unlabeled',  `trainandunlabeled' and `trainandunlabeledloo'"},
  {"em-anneal-normalizer", EM_ANNEAL_NORMALIZER, 0, 0, 
   "When running EM, do deterministic annealing-ish stuff with the unlabeled "
   "normalizer."},
  {"em-print-perplexity", EM_PRINT_PERPLEXITY, "TYPE", 0,
   "When running EM, print the perplexity of documents at each round.  "
   "TYPE is type of document to measure perplexity on.  See "
   "`--em-halt-using-perplexity` for choices for TYPE"},
  {"em-halt-using-accuracy", EM_HALT_USING_ACCURACY, "TYPE", 0,
   "When running EM, halt when accuracy plateaus.   TYPE is type of document "
   "to measure perplexity on.  Choices are `validation', `train', `test', "
   "`unlabeled' and `trainandunlabeled' and `trainandunlabeledloo'"},
  {"em-print-accuracy", EM_PRINT_ACCURACY, "TYPE", 0,
   "When running EM, print the accuracy of documents at each round.  "
   "TYPE is type of document to measure perplexity on.  See "
   "`--em-halt-using-perplexity` for choices for TYPE"},
  {"em-labeled-for-start-only", EM_LABELED_FOR_START_ONLY, 0, 0,
   "Use the labeled documents to set the starting point for EM, but"
   "ignore them during the iterations"},
  {"em-set-vocab-from-unlabeled", EM_SET_VOCAB_FROM_UNLABELED, 0, 0,
   "Remove words from the vocabulary not used in the unlabeled data"},
  {0, 0}
};

error_t
em_parse_opt (int key, char *arg, struct argp_state *state)
{
  switch (key)
    {
    case EM_COMPARE_TO_NB:
      em_compare_to_nb = 1;
      break;
    case EM_STAT_METHOD:
      if (!strcmp(arg, "nb_score"))
	em_stat_method = nb_score;
      else if (!strcmp(arg, "simple"))
	em_stat_method = simple;
      else
	bow_error("Invalid argument for --em-stat-method");
      break;
    case EM_NUM_RUNS:
      bow_em_num_em_runs = atoi(arg);
      break;
    case EM_PRINT_PROBS:
      bow_em_print_probs = 1;
      break;
    case EM_BINARY_POS_CLASS:
      em_binary_pos_classname = arg;
      break;
    case EM_BINARY_NEG_CLASS:
      em_binary_neg_classname = arg;
      break;
    case EM_PRINT_TOP_WORDS:
      bow_em_print_word_vector = 1;
      break;
    case EM_BINARY:
      bow_em_binary_case = 1;
      break;
    case EM_UNLABELED_NORMALIZER:
      unlabeled_normalizer = 1.0 / atoi(arg);
      break;
    case EM_MULTI_HUMP:
      bow_em_multi_hump_neg = atoi(arg);
      break;
    case EM_PERTURB_STARTING_POINT:
      if (!strcmp (arg, "none"))
	bow_em_perturb_starting_point = bow_em_perturb_none;
      else if (!strcmp (arg, "gaussian"))
	bow_em_perturb_starting_point = bow_em_perturb_with_gaussian;
      else if (!strcmp (arg, "dirichlet"))
	bow_em_perturb_starting_point = bow_em_perturb_with_dirichlet;
      else
	bow_error ("Bad arg to --perturb-starting-point"); 
      break;
    case EM_CROSSENTROPY:
      em_cross_entropy = 1;
      break;
    case EM_ANNEAL:
      em_anneal = 1;
      break;
    case EM_TEMPERATURE:
      em_temperature = atoi (arg);
      break;
    case EM_TEMP_REDUCE:
      em_temp_reduction = atof (arg);
      break;
    case EM_UNLABELED_START:
      if (!strcmp (arg, "zero"))
	em_unlabeled_start = em_start_zero;
      else if (!strcmp (arg, "prior"))
	em_unlabeled_start = em_start_prior;
      else if (!strcmp (arg, "even"))
	em_unlabeled_start = em_start_even;
      else if (!strcmp (arg, "random"))
	em_unlabeled_start = em_start_random;
      else 
	bow_error ("Bad arg to --em-unlabled-start"); 
      break;
    case EM_MULTI_HUMP_INIT:
      if (!strcmp(arg, "spread"))
	em_multi_hump_init = bow_em_init_spread;
      else if (!strcmp (arg, "spiked"))
	em_multi_hump_init = bow_em_init_spiked;
      else
	bow_error ("Bad arg to --em-multi-hump-init");
      break;
    case EM_HALT_USING_PERPLEXITY:
      em_halt_using_perplexity = 1;
      /* Intentional lack of a break here */
    case EM_PRINT_PERPLEXITY:
      if (!strcmp (arg, "validation"))
	em_perplexity_docs = bow_cdoc_is_validation;
      else if (!strcmp (arg, "train"))
	em_perplexity_docs = bow_cdoc_is_train;
      else if (!strcmp (arg, "unlabeled"))
	em_perplexity_docs = bow_cdoc_is_unlabeled;
      else if (!strcmp (arg, "test"))
	em_perplexity_docs = bow_cdoc_is_test;
      else if (!strcmp (arg, "trainandunlabeled"))
	em_perplexity_docs = bow_cdoc_is_train_or_unlabeled;
      else if (!strcmp (arg, "trainandunlabeledloo"))
	{
	  em_perplexity_docs = bow_cdoc_is_train_or_unlabeled;
	  em_perplexity_loo = 1;
	}
      else
	bow_error("Unknown document type for --em-halt-using-perplexity");
      break;
    case EM_HALT_USING_ACCURACY:
      em_halt_using_accuracy = 1;
      /* Intentional lack of break here */
    case EM_PRINT_ACCURACY:
      if (!strcmp (arg, "validation"))
	em_accuracy_docs = bow_cdoc_is_validation;
      else if (!strcmp (arg, "train"))
	em_accuracy_docs = bow_cdoc_is_train;
      else if (!strcmp (arg, "test"))
	em_accuracy_docs = bow_cdoc_is_test;
      else if (!strcmp (arg, "trainloo"))
	{
	  em_accuracy_docs = bow_cdoc_is_train;
	  em_accuracy_loo = 1;
	}
      else
	bow_error("Unknown document type for --em-halt-using-accuracy");
      break;
    case EM_ANNEAL_NORMALIZER:
      bow_em_anneal_normalizer = 1;
      unlabeled_normalizer = 0;
      break;
    case EM_LABELED_FOR_START_ONLY:
      em_labeled_for_start_only = 1;
      break;
    case EM_SET_VOCAB_FROM_UNLABELED:
      em_set_vocab_from_unlabeled = 1;
      break;
    default:
      return ARGP_ERR_UNKNOWN;
    }
  return 0;
}

static const struct argp em_argp =
{
  em_options,
  em_parse_opt
};

static struct argp_child em_argp_child =
{
  &em_argp,		/* This child's argp structure */
  0,			/* flags for child */
  0,			/* optional header in help message */
  0			/* arbitrary group number for ordering */
};

/* End of command-line options specific to EM */

/* return 1 for all docs to be tested by EM during the E-step when 
   doing multi-hump negative class */
int 
bow_cdoc_is_multi_hump_doc (bow_cdoc *cdoc)
{
  return((cdoc->type == bow_doc_unlabeled) ||
	 (cdoc->type == bow_doc_train && cdoc->class != binary_pos_ci));
}



/* Given a fully-specified file path name (all the way from `/'),
   return just the last filename part of it. */
static inline const char *
filename_to_classname (const char *filename)
{
  const char *ret;
  ret = strrchr (filename, '/');
  if (ret)
    return ret + 1;
  return filename;
}

int
bow_em_pr_struct_compare (const void *x, const void *y)
{
  if (((bow_em_pr_struct *)x)->score > ((bow_em_pr_struct *)y)->score)
    return -1;
  else if (((bow_em_pr_struct *)x)->score == ((bow_em_pr_struct *)y)->score)
    return 0;
  else
    return 1;
}

/* Return a random number sampled from a gaussian with MEAN and VARIANCE. */
/* From "Recipies in C", page 289. */
double
bow_em_gaussian (double mean, double variance)
{
  static int iset = 0;
  static double gset;
  double fac, rsq, v1, v2;
  double gaussian_zero_one;	/* random gaussian with mean=0, variance=1 */

  bow_random_set_seed (); 
  if (iset == 0)
    {
      do
	{
	  v1 = 2.0 * bow_random_double (0.0, 1.0) - 1.0;
	  v2 = 2.0 * bow_random_double (0.0, 1.0) - 1.0;
	  rsq = v1 * v1 + v2 * v2;
	}
      while (rsq >= 1.0 || rsq == 0.0);
      fac = sqrt (-2.0 * log (rsq)/rsq);
      gset = v1 * fac;
      iset = 1;
      gaussian_zero_one = v2 * fac;
    }
  else
    {
      iset = 0;
      gaussian_zero_one = gset;
    }
  return gaussian_zero_one * sqrt (variance) + mean;
}

/* From Numerical "Recipes in C", page 292 */
double
bow_gamma_distribution (int ia)
{
  int j;
  double am, e, s, v1, v2, x, y;

  assert (ia >= 1) ;
  if (ia < 6) 
    {
      x = 1.0;
      for (j = 1; j <= ia; j++)
	x *= bow_random_01 ();
      x = - log (x);
    }
  else
    {
      do
	{
	  do
	    {
	      do
		{
		  v1 = 2.0 * bow_random_01 () - 1.0;
		  v2 = 2.0 * bow_random_01 () - 1.0;
		}
	      while (v1 * v1 + v2 * v2 > 1.0);
	      y = v2 / v1;
	      am = ia - 1;
	      s = sqrt (2.0 * am + 1.0);
	      x = s * y + am;
	    }
	  while (x <= 0.0);
	  e = (1.0 + y * y) * exp (am * log (x/am) - s * y);
	}
      while (bow_random_01 () > e);
    }
  return x;
}

/* Change the weights by sampling from the multinomial distribution
   specified by the training data.  Start from the current values of
   the DV WEIGHTS.  Typically this would be called after iteration 1
   of EM, before the unlabeled documents were included in the
   WEIGHTS. */
void
bow_em_perturb_weights (bow_barrel *doc_barrel, bow_barrel *vpc_barrel)
{
  double variance;
  double num_words_per_ci[bow_barrel_num_classes (vpc_barrel)];
  int ci, wi, dvi, max_wi;
  bow_dv *dv;
  bow_cdoc *cdoc;
  double pr_w_c;

  if (bow_em_perturb_starting_point == bow_em_perturb_none)
    return;

  bow_random_set_seed ();

  max_wi = MIN (doc_barrel->wi2dvf->size, bow_num_words ());

  /* Perturb the counts (which are stored in WEIGHT) */
  for (wi = 0; wi < max_wi; wi++)
    {
      dv = bow_wi2dvf_dv (vpc_barrel->wi2dvf, wi);
      if (!dv)
	continue;
      for (dvi = 0; dvi < dv->length; dvi++)
	{
	  /* WEIGHT can be zero if the prob of a class for the doc
	     that had this word was zero */
	  if (bow_em_perturb_starting_point 
	      == bow_em_perturb_with_gaussian)
	    {
	      if (0 != dv->entry[dvi].weight)
		{
		  cdoc = bow_array_entry_at_index (vpc_barrel->cdocs, 
						   dv->entry[dvi].di);
		  pr_w_c = dv->entry[dvi].weight / cdoc->normalizer;
		  variance = cdoc->normalizer * pr_w_c * (1 - pr_w_c);
		  dv->entry[dvi].weight =
		    bow_em_gaussian (dv->entry[dvi].weight, variance);
		  if (dv->entry[dvi].weight < 0)
		    dv->entry[dvi].weight = 0;
		}
	    }
	  else if (bow_em_perturb_starting_point
		   == bow_em_perturb_with_dirichlet)
	    {
	      dv->entry[dvi].weight =
		bow_gamma_distribution (dv->entry[dvi].weight + 1);
	      /* The +1 is assuming we are using LaPlace smoothing */
	      /* xxx I hope that we are still multiplying weights by
		 200 (for a length 200 document), otherwise weight
		 will always get rounded down into nothing, because
		 bow_gamma_distribution only takes int's */
	    }
	}
    }

  /* Reset the CDOC->WORD_COUNT and CDOC->NORMALIZER */
  for (ci = 0; ci < bow_barrel_num_classes (vpc_barrel); ci++)
    {
      cdoc = bow_array_entry_at_index (vpc_barrel->cdocs, ci);
      cdoc->normalizer = 0;
      num_words_per_ci[ci] = 0;
    }
  for (wi = 0; wi < max_wi; wi++)
    {
      dv = bow_wi2dvf_dv (vpc_barrel->wi2dvf, wi);
      if (!dv)
	continue;
      for (dvi = 0; dvi < dv->length; dvi++)
	{
	  /* WEIGHT can be zero if the prob of a class for the doc
	     that had this word was zero */
	  if (0 != dv->entry[dvi].weight)
	    {
	      cdoc = bow_array_entry_at_index (vpc_barrel->cdocs, 
					       dv->entry[dvi].di);
	      num_words_per_ci[dv->entry[dvi].di] += 
		dv->entry[dvi].weight;
#if 0
	      /* Now using normalizer for non-int word_count */
	      cdoc->normalizer++;
#endif
	    }
	}
    }
  for (ci = 0; ci < bow_barrel_num_classes (vpc_barrel); ci++)
    {
      bow_cdoc *cdoc =
	bow_array_entry_at_index (vpc_barrel->cdocs, ci);
      cdoc->normalizer = num_words_per_ci[ci];
#if 0
      cdoc->word_count = (int) rint(num_words_per_ci[ci]);
      assert (cdoc->word_count >= 0);
#endif
      assert (cdoc->normalizer >= 0);
    }
}
  


/* Create a class barrel with EM-style clustering on unlabeled
   docs */
bow_barrel *
bow_em_new_vpc_with_weights (bow_barrel *doc_barrel)
{
  bow_barrel *vpc_barrel;   /* the vector-per-class barrel */
  int wi;                   /* word index */
  int max_wi;               /* max word index */
  int dvi;                  /* document vector index */
  int ci;                   /* class index */
  bow_dv *dv;               /* document vector */
  int di;                   /* document index */
  int binary_neg_ci = -1;
  bow_dv_heap *test_heap=NULL;	/* we'll extract test WV's from here */
  bow_wv *query_wv;
  bow_score *hits;
  int actual_num_hits;
  int hi;		     /* hit index */
  bow_cdoc *doc_cdoc;
  int num_tested;
  int em_runs = 0;
  int num_train_docs = 0;
  int num_unlabeled_docs = 0;
  int max_new_ci;
  int max_old_ci;
  int (* bow_cdoc_next_em_doc)(bow_cdoc *) = bow_cdoc_is_unlabeled;
  double old_perplexity = DBL_MAX;
  double new_perplexity = DBL_MAX / 2;
  double old_accuracy = -2;
  double new_accuracy = -1;
  /*bow_wi2dvf *prev_wi2dvf = NULL;*/
  /*float prev_priors[200];*/
  /*int prev_word_counts[200];*/
  /*float prev_normalizers[200];*/
  float total_weight;
  float labeled_weight_fraction;
  float new_labeled_fraction;
	  


  /* some sanity checks first */
  assert(200 > bow_barrel_num_classes(doc_barrel));
  assert(200 > bow_em_multi_hump_neg + 1);
  assert (!bow_em_multi_hump_neg || 
	  (bow_em_binary_case && em_stat_method == nb_score));	  
  assert (!strcmp(doc_barrel->method->name, "em") ||
	  !strcmp(doc_barrel->method->name, "active"));
  assert (doc_barrel->classnames);
  assert (!(bow_em_perturb_starting_point && em_anneal));
  assert (em_stat_method == nb_score || bow_em_multi_hump_neg == 0);
  assert (bow_em_multi_hump_neg == 0 || em_labeled_for_start_only == 0);

  /* this option is broken */
  assert (!em_halt_using_perplexity);

  /* initialize some variables */
  bow_em_making_barrel = 1;

  if (bow_smoothing_method == bow_smoothing_dirichlet)
    bow_naivebayes_load_dirichlet_alphas ();

  max_old_ci = bow_barrel_num_classes(doc_barrel);

  if (bow_em_multi_hump_neg)
    max_new_ci = bow_em_multi_hump_neg + 1;
  else 
    max_new_ci = max_old_ci;

  if (bow_em_multi_hump_neg > 1)
    bow_cdoc_next_em_doc = bow_cdoc_is_multi_hump_doc;
  
  max_wi = MIN (doc_barrel->wi2dvf->size, bow_num_words ());
  /*  assert(doc_barrel->wi2dvf->size == bow_num_words ()); */

  /* remove words from vocab if using only the unlabeled vocab */
  if (em_set_vocab_from_unlabeled)
    {
      int removed = 0;
      int kept = 0;

      for (wi = 0; wi < max_wi; wi++)
	{
	  int found = 0;
	  
	  bow_dv *dv = bow_wi2dvf_dv (doc_barrel->wi2dvf, wi);
	  if (!dv)
	    continue;
	  dvi = 0;
	  while (dvi < dv->length)
	    {
	      bow_cdoc *cdoc = bow_array_entry_at_index (doc_barrel->cdocs, 
							 dv->entry[dvi].di);
	      
	      if (cdoc->type == bow_doc_unlabeled)
		{
		  found = 1;
		  break;
		}
	      dvi++;
	    }
	  
	  if (!found) 
	    {
	      bow_wi2dvf_hide_wi (doc_barrel->wi2dvf, wi);
	      removed++;
	    }
	  else
	    kept++;
	}
      bow_verbosify (bow_progress, "Removed %d words using unlabeled data; %d remaining\n", 
		     removed, kept);
    }

  /* Count the number of training and unlabeled documents */
  for (di=0; di < doc_barrel->cdocs->length; di++)
    {
      bow_cdoc *cdoc = bow_array_entry_at_index (doc_barrel->cdocs, di);
	
      if (cdoc->type == bow_doc_train)
	num_train_docs++;
      else if (cdoc->type == bow_doc_unlabeled)
	num_unlabeled_docs++;
    }

  /* Identify the binary positive and negative class */
  if (bow_em_binary_case)
    {
      assert (em_binary_pos_classname != NULL);
      assert (em_binary_neg_classname != NULL);

      for (ci = 0; ci < max_old_ci; ci++)
	{
	  if (em_binary_pos_classname != NULL &&
	      -1 == binary_pos_ci &&
	      !strcmp(em_binary_pos_classname, 
		      filename_to_classname
		      (bow_barrel_classname_at_index (doc_barrel, ci))))
	    {
	      binary_pos_ci = ci;
	    }
	  
	  if (em_binary_neg_classname != NULL &&
	      -1 == binary_neg_ci &&
	      !strcmp(em_binary_neg_classname, 
		      filename_to_classname
		      (bow_barrel_classname_at_index (doc_barrel, ci))))
	    {
	      binary_neg_ci = ci;
	    }
	}

      if (binary_pos_ci == -1)
	bow_error ("No such binary positive class %s.", 
		   em_binary_pos_classname);
      
      if (binary_neg_ci == -1)
	bow_error ("No such binary negative class %s.", 
		   em_binary_neg_classname);
    }

  /* should the free function be a real one? */

  /* Create an empty barrel; we fill it with vector-per-class
     data and return it. */
  vpc_barrel = bow_barrel_new (doc_barrel->wi2dvf->size,
			       doc_barrel->cdocs->length-1,
			       doc_barrel->cdocs->entry_size,
			       doc_barrel->cdocs->free_func); 
  vpc_barrel->method = doc_barrel->method;
  vpc_barrel->classnames = bow_int4str_new (0);

  /* setup the cdoc structure for the class barrel, except for the
     word counts and normalizer, which we'll do later.  */
  for (ci = 0; ci < max_old_ci; ci++)
    {
      bow_cdoc cdoc;
      
      /* create the cdoc structure */
      cdoc.type = bow_doc_train;
      cdoc.normalizer = -0.0f; /* just a temporary measure */
      cdoc.word_count = 0; /* just a temporary measure */
      cdoc.filename = strdup (bow_barrel_classname_at_index (doc_barrel, 
							     ci));
      bow_barrel_add_classname(vpc_barrel, cdoc.filename);
      if (!cdoc.filename)
	bow_error ("Memory exhausted.");
      cdoc.class_probs = NULL;
      cdoc.class = ci;
      bow_array_append (vpc_barrel->cdocs, &cdoc);
    }

  /* if multi-hump, then add a cdoc for each of the other negative
     humps as well */
  if (bow_em_multi_hump_neg)
    {
      for (ci = max_old_ci; ci < max_new_ci; ci++)
	{
	  bow_cdoc cdoc;
	  char *name = bow_malloc (sizeof (char) * 
				   (strlen(em_binary_neg_classname) + 10));

	  cdoc.type = bow_doc_train;
	  cdoc.normalizer = 0.0f; /* just a temporary measure */
	  cdoc.word_count = 0; /* just a temporary measure */
	  sprintf(name, "%s%d", em_binary_neg_classname, ci);	  
	  cdoc.filename = name;
	  bow_barrel_add_classname(vpc_barrel, cdoc.filename);
	  if (!cdoc.filename)
	    bow_error ("Memory exhausted.");
	  cdoc.class_probs = NULL;
	  cdoc.class = ci;
	  bow_array_append (vpc_barrel->cdocs, &cdoc);
	}
    }	  

  /* if we're comparing to naivebayes, do that now */
  if (em_compare_to_nb == 1)
    bow_em_compare_to_nb(doc_barrel);
  
  /* Set word_count for docs correctly.  Do this after comparing to NB b/c
     making a NB class barrel messes with the word counts.  */
  {
    /* Create the heap from which we'll get WV's. */
    query_wv = NULL;
    test_heap = bow_test_new_heap (doc_barrel);
    
    /* Loop once for each document. */
    while (-1 != (di = bow_heap_next_wv (test_heap, doc_barrel, &query_wv, 
					 bow_cdoc_yes)))
      {
	int word_count = 0;
	int wvi;

	doc_cdoc = bow_array_entry_at_index (doc_barrel->cdocs, 
					     di);
	for (wvi = 0; wvi < query_wv->num_entries; wvi++)
	  word_count += query_wv->entry[wvi].count;
	doc_cdoc->word_count = word_count;
      }
  }

  /* initialize the EM starting point */
  {

    /* cycle through the document barrel and make sure that each
       document has a correctly initialized class_probs structure.
       set class_probs of train docs.  Note that these class_probs
       indexes are indexes into the NEW class indexes not the OLD
       ones!*/
    for (di=0; di < doc_barrel->cdocs->length; di++)
      {
	bow_cdoc *cdoc = bow_array_entry_at_index (doc_barrel->cdocs, di);
	
	if (!cdoc->class_probs)
	  cdoc->class_probs = bow_malloc (sizeof (float) * max_new_ci);
	
	/* initialize the class_probs to all zeros */
	for (ci=0; ci < max_new_ci; ci++)
	  cdoc->class_probs[ci] = 0.0;
	
	/* if it's a known doc, set its class_probs that way */
	if (cdoc->type == bow_doc_train)
	  cdoc->class_probs[cdoc->class] = 1.0;
      }

    /* redistribute class probs of negative docs if multi-hump */
    if (bow_em_multi_hump_neg)
      {
	if (em_multi_hump_init == bow_em_init_spiked)
	  {
	    int counts[500];
	    int n;
	    int yet_to_find = 0; 

	    assert (bow_em_multi_hump_neg < 500);

	    /* Count the number of negative documents */
	    for (di=0; di < doc_barrel->cdocs->length; di++)
	      {
		bow_cdoc *cdoc = bow_array_entry_at_index (doc_barrel->cdocs, di);
		
		if (cdoc->class == binary_neg_ci)
		  yet_to_find++;
	      }

	    /* set the number of docs per negative hump */
	    assert(yet_to_find >= bow_em_multi_hump_neg);
	    for (n=0; n < bow_em_multi_hump_neg; n++)
	      counts[n] = 0;
	    for (n=0; n < yet_to_find; n++)
	      counts[n % bow_em_multi_hump_neg]++;

	    /* reassign the negative docs */
	    for (di=0; di < doc_barrel->cdocs->length; di++)
	      {
		bow_cdoc *cdoc = 
		  bow_array_entry_at_index (doc_barrel->cdocs, di);
		int new_class;
	  
		if (cdoc->type != bow_doc_train || 
		    cdoc->class == binary_pos_ci)
		  continue;

		assert(yet_to_find > 0);
		/* find a new class */
		for (new_class = rand() % bow_em_multi_hump_neg;
		     counts[new_class] == 0;
		     new_class = rand() % bow_em_multi_hump_neg);

		yet_to_find--;
		counts[new_class]--;

		/* assign it to the right hump */
		if (new_class != 0)
		  {
		    cdoc->class_probs[new_class + 1] = 1.0;
		    cdoc->class_probs[binary_neg_ci] = 0.0;
		  }
	      }
	    assert(yet_to_find == 0);
	  }
	else if (em_multi_hump_init == bow_em_init_spread)
	  {
	    bow_random_set_seed();
	    
	    /* spread each negative doc randomly over neg components */
	    for (di=0; di < doc_barrel->cdocs->length; di++)
	      {
		bow_cdoc *cdoc = 
		  bow_array_entry_at_index (doc_barrel->cdocs, di);
		float total = 0;
		
		if (cdoc->type != bow_doc_train || cdoc->class == binary_pos_ci)
		  continue;
		
		for (ci=0; ci < max_new_ci; ci++)
		  {
		    if (ci == binary_pos_ci)
		      cdoc->class_probs[ci] = 0.0;
		    else
		      {
			cdoc->class_probs[ci] = (float) (rand() % 100) + 1;
			total += cdoc->class_probs[ci];
		      }
		  }
		
		for (ci=0; ci < max_new_ci; ci++)
		  {
		    cdoc->class_probs[ci] /= total ;
		  }
	      }
	  }
	else
	  bow_error ("No initialization for this type");
      }
    
    /* set priors using just the known docs if we'll need them 
       for setting class_probs */
    if (em_unlabeled_start == em_start_prior)
      {
	assert (num_train_docs > 0);
	assert (!bow_uniform_class_priors);
	(*doc_barrel->method->vpc_set_priors) (vpc_barrel, doc_barrel);
      }
    else
      {
	for (ci = 0; ci < max_new_ci; ci++)
	  {
	    bow_cdoc *cdoc = bow_array_entry_at_index(vpc_barrel->cdocs, ci);

	    cdoc->prior = 0.0;
	  }
      }


    /* set the class probs of all the unlabeled docs to determine the EM
       starting point */
    for (di=0; di < doc_barrel->cdocs->length; di++)
      {
	bow_cdoc *cdoc = bow_array_entry_at_index (doc_barrel->cdocs, di);

	if (cdoc->type != bow_doc_unlabeled)
	  continue;

	if (em_unlabeled_start == em_start_zero)
	  {
	    /* set class_probs as all zeros (ignore them for first M step) */
	    for (ci=0; ci < max_new_ci; ci++)
	      cdoc->class_probs[ci] = 0.0;
	  }
	else if (em_unlabeled_start == em_start_random)
	  {
	    float total = 0;

	    /* if there are no labeled docs, randomly assign class probs */
	    bow_random_set_seed();
	  
	    for (ci=0; ci < max_new_ci; ci++)
	      {
		cdoc->class_probs[ci] = (float) (rand() % 100);
		total += cdoc->class_probs[ci];
	      }
	  
	    for (ci=0; ci < max_new_ci; ci++)
	      {
		cdoc->class_probs[ci] *= unlabeled_normalizer / total ;
	      }
	  }  
	else if (em_unlabeled_start == em_start_prior)
	  {
	    /* distribute class_probs according to priors on just the known */
	    assert (!bow_em_multi_hump_neg && !bow_uniform_class_priors);
	    assert (num_train_docs > 0);

	    for (ci=0; ci < max_new_ci; ci++)
	      {
		bow_cdoc *class_cdoc = bow_array_entry_at_index 
		  (vpc_barrel->cdocs, ci);
	      
		cdoc->class_probs[ci] = class_cdoc->prior * 
		  unlabeled_normalizer;
	      }
	  }
	else if (em_unlabeled_start == em_start_even)
	  {
	    /* distribute class_probs evenly across all classes */
	    for (ci=0; ci < max_new_ci; ci++)
	      {
		cdoc->class_probs[ci] = unlabeled_normalizer / 
		  bow_barrel_num_classes(vpc_barrel);
	      }
	  }
	else
	  bow_error ("No such value for em_unlabeled_start");
      }
  }

  /* let's do some EM */
  while (em_anneal
	 ? em_temperature >= 1.0
	 : (em_halt_using_perplexity 
	    ? (old_perplexity > new_perplexity &&
	       ABS (new_perplexity - old_perplexity) > 0.05)
	    : (em_halt_using_accuracy
	       ? old_accuracy < new_accuracy
	       : em_runs < bow_em_num_em_runs)))
    {
      em_runs++;

      /* the M-step */
      bow_verbosify (bow_progress, 
		     "Making class barrel by counting words:       ");


      if (vpc_barrel->wi2dvf != NULL)
	bow_wi2dvf_free(vpc_barrel->wi2dvf);

#if 0      
      /* save the previous wi2dvf */
      if (prev_wi2dvf != NULL)
	bow_wi2dvf_free(prev_wi2dvf);
      prev_wi2dvf = vpc_barrel->wi2dvf;
      for (ci = 0; ci < max_new_ci; ci++)
	{
	  bow_cdoc *cdoc = bow_array_entry_at_index(vpc_barrel->cdocs, ci);
	  
	  prev_priors[ci] = cdoc->prior;
	  prev_word_counts[ci] = cdoc->word_count;
	  prev_normalizers[ci] = cdoc->normalizer;
	}
#endif



      /* get a new wi2dvf structure for our class barrel */
      vpc_barrel->wi2dvf = bow_wi2dvf_new (doc_barrel->wi2dvf->size);
      
      /* Initialize the WI2DVF part of the VPC_BARREL.  Sum together the
	 counts and weights for individual documents, grabbing only the
	 training and unlabeled documents. */
      for (wi = 0; wi < max_wi; wi++)
	{
	  dv = bow_wi2dvf_dv (doc_barrel->wi2dvf, wi);
	  if (!dv)
	    continue;

#if 0
	  /* create the dv in the class barrel if there's an
	     entry in the doc barrel.  This ensures that
	     perplexity calculations happen correctly. */
	  vpc_barrel->wi2dvf->entry[wi].dv = bow_dv_new (0);
	  vpc_barrel->wi2dvf->entry[wi].seek_start = 2;
	  (vpc_barrel->wi2dvf->num_words)++;
#endif

	  for (dvi = 0; dvi < dv->length; dvi++)
	    {
	      bow_cdoc *cdoc; 
	      
	      di = dv->entry[dvi].di;
	      cdoc = bow_array_entry_at_index (doc_barrel->cdocs, di);

	      if (cdoc->type == bow_doc_train ||
		  cdoc->type == bow_doc_unlabeled)
		{
		  assert(cdoc->word_count > 0);
		  for (ci=0; ci < max_new_ci; ci++)
		    {
		      /* it's important to do this even when class_prob is 0 to 
			 ensure that perplexity calculations happen ok. */

#if 0
		      if (cdoc->class_probs[ci] > 0)
#endif
			{
			  if (bow_event_model == bow_event_document_then_word)
			    bow_wi2dvf_add_wi_di_count_weight 
			      (&(vpc_barrel->wi2dvf), 
			       wi, ci, 
			       1,  /* hopelessly dummy value */
			       (cdoc->class_probs[ci] *
				(float) dv->entry[dvi].count * 
				(float) bow_event_document_then_word_document_length / 
				(float) cdoc->word_count));
			  else if (bow_event_model == bow_event_word)
			    {
			      float addition = cdoc->class_probs[ci] *
				(float) dv->entry[dvi].count;
			      bow_wi2dvf_add_wi_di_count_weight 
				(&(vpc_barrel->wi2dvf), 
				 wi, ci, 
				 1,  /* hopelessly dummy value */
				 addition);
			    }
			  else
			    bow_error("No implementation of this event model.");
			}
		    }
		}
	    }

	  if (wi % 100 == 0)
	    bow_verbosify (bow_progress, "\b\b\b\b\b\b%6d", max_wi - wi);
	}

      bow_verbosify (bow_progress, "\n");
      
      /* set the dv->idf, normalizer and word_count */
      bow_em_set_weights (vpc_barrel);

      /* set priors */
      if (doc_barrel->method->vpc_set_priors && !bow_uniform_class_priors)
	(*doc_barrel->method->vpc_set_priors) (vpc_barrel, doc_barrel);
      
      /* If on first EM run, and doing perturbed starting points
	 (e.g. for active learning), then perturb the the weights
	 using the variance */
      if (em_runs == 1 && bow_em_perturb_starting_point)
	bow_em_perturb_weights (doc_barrel, vpc_barrel);

      /* print top words by class */
      if (bow_em_print_word_vector)
	bow_em_print_log_odds_ratio(vpc_barrel, 20);

      /* Print the P(C|w) distribution to a file so that we can later
	 calculate the KL-divergence between the current distribution
	 and the "correct" distribution.  */
      if (bow_em_print_probs)
	bow_em_print_word_distribution(vpc_barrel, em_runs, 
				       bow_barrel_num_classes(vpc_barrel));

      /* if we're ignoring the labeled data during the iterations, then
	 zero out their class probs now */
      if (em_runs == 1 && em_labeled_for_start_only)
	{
	  for (di=0; di < doc_barrel->cdocs->length; di++)
	    {
	      bow_cdoc *cdoc = bow_array_entry_at_index (doc_barrel->cdocs, di);
	      if (cdoc->type == bow_doc_train)
		cdoc->class_probs[cdoc->class] = 0.0;
	    }
	}

      /* OK.  we're done with our M-step.  We have a new vpc barrel to 
	 use.  Let's now do the E-step, and classify all our documents. */

      /* Calculate perplexity of the validation set for halting check */
      if (em_perplexity_docs)
	{
	  old_perplexity = new_perplexity;
	  new_perplexity = em_calculate_perplexity (doc_barrel, vpc_barrel);
	  bow_verbosify(bow_progress, "Perplexity = %f\n", new_perplexity);
	}

      /* Calculate accuracy of the validation set for halting check */
      if (em_accuracy_docs)
	{
	  old_accuracy = new_accuracy;
	  new_accuracy = em_calculate_accuracy (doc_barrel, vpc_barrel);
	  bow_verbosify (bow_progress, "Correct: %f\n", new_accuracy);
	}
	  
      /* adjust the normalizer if we're annealing it. */
      if (bow_em_anneal_normalizer)
	{
	  float new_unlabeled_fraction;
	      
	  total_weight = ((float) num_train_docs) + 
	    (unlabeled_normalizer * (float) num_unlabeled_docs);
	  labeled_weight_fraction = (float) num_train_docs / 
	    total_weight;
	  
	  /* increase weight of unlabeled data by factor of
	     1.1, unless it's the first round; then bump it
	     away from zero slightly */
	  if (labeled_weight_fraction == 1.0)
	    {
	      new_labeled_fraction = 0.98;
	      new_unlabeled_fraction = 0.02;
	    }
	  else
	    {
	      new_unlabeled_fraction = 1.1 * (1.0 - labeled_weight_fraction);
	      new_labeled_fraction =  1.0 -  new_unlabeled_fraction;
	    }

	  unlabeled_normalizer = ((num_train_docs / new_labeled_fraction) - 
				  num_train_docs) / num_unlabeled_docs;

	  /* halt normalizer annealing when one labeled document
	     is the same as one unlabeled document */
	  if (new_unlabeled_fraction >= 1.0 ||
	      unlabeled_normalizer >= 1.0)
	    {
	      unlabeled_normalizer = 1.0;
	      bow_em_anneal_normalizer = 0;
	      em_runs = 1;
	    }

	  assert (unlabeled_normalizer >= 0 &&
		  unlabeled_normalizer <= 1 );

	  bow_verbosify (bow_progress, 
			 "Updating total labeled weight to %f (normalizer = %f).\n",
			 new_labeled_fraction, unlabeled_normalizer);
	}


      /* only do the e-step if not the last round */
      if (em_anneal
	  ? 1
	  : (em_halt_using_perplexity 
	     ? (old_perplexity > new_perplexity &&
		ABS(new_perplexity - old_perplexity) > 0.05)
	     : (em_halt_using_accuracy
		? old_accuracy < new_accuracy
		: em_runs < bow_em_num_em_runs)))
	{
	  /* now classify the unknown documents */
	  bow_verbosify(bow_progress, "\nClassifying unlabeled documents:       ");
	  
	  /* Initialize QUERY_WV so BOW_TEST_NEXT_WV() knows not to
             try to free.  Create the heap from which we'll get
             WV's. */
	  query_wv = NULL;
	  hits = alloca (sizeof (bow_score) * max_new_ci);
	  num_tested = 0;
	  test_heap = bow_test_new_heap (doc_barrel);
	  
	  /* Loop once for each unlabeled document. */
	  while ((di = bow_heap_next_wv (test_heap, doc_barrel, &query_wv, 
					 bow_cdoc_next_em_doc))
		 != -1)
	    {
	      doc_cdoc = bow_array_entry_at_index (doc_barrel->cdocs, 
						   di);
	      bow_wv_set_weights (query_wv, vpc_barrel);
	      bow_wv_normalize_weights (query_wv, vpc_barrel);
	      actual_num_hits = 
		bow_barrel_score (vpc_barrel, 
				  query_wv, hits,
				  max_new_ci, (int) NULL);
	      assert (actual_num_hits == max_new_ci);

	      if (em_stat_method == simple)
		{
		  /* set the class probs to 1 for the maximally likely class */
		  for (ci = 0; ci < max_new_ci; ci++)
		    doc_cdoc->class_probs[ci] = 0.0;
		  
		  doc_cdoc->class_probs[hits[0].di] = unlabeled_normalizer;
		}
	      else if (em_stat_method == nb_score)
		{
		  /* set the class probs to the naive bayes score */
		  for (hi = 0; hi < actual_num_hits; hi++)
		    doc_cdoc->class_probs[hits[hi].di] = unlabeled_normalizer *
		      hits[hi].weight;
		  
		  /* this is a neg training doc.  Zero out the pos
		     component. */		      
		  if (bow_em_multi_hump_neg > 1 &&
		      doc_cdoc->type == bow_doc_train)
		    {
		      double new_total = 0;

		      doc_cdoc->class_probs[binary_pos_ci] = 0;
		      for (ci = 0; ci < max_new_ci; ci++)
			new_total += doc_cdoc->class_probs[ci];

		      if (new_total != 0)
			{
			  for (ci = 0; ci < max_new_ci; ci++)
			    doc_cdoc->class_probs[ci] = unlabeled_normalizer *
			      doc_cdoc->class_probs[ci] / new_total;
			}
		      else
			{
			  /* blech.  we got hosed on roundoff. */
			  for (ci = 0; ci < max_new_ci; ci++)
			    doc_cdoc->class_probs[ci] = 
			      (float) unlabeled_normalizer /
			      ((float) max_new_ci - 1.0);
			  doc_cdoc->class_probs[binary_pos_ci] = 0;
			}
		    }
		}
	      else
		bow_error ("No method for this type.");
	      
	      if (num_tested % 100 == 0)
		bow_verbosify (bow_progress, "\b\b\b\b\b\b%6d", num_tested);
	      
	      num_tested++;	  
	    }
	  
	  bow_verbosify(bow_progress, "\b\b\b\b\b\b%6d\n", num_tested);
	}

      /* Lower the temperature if doing DA */
      if (em_anneal)
	{
	  em_temperature *= em_temp_reduction;

	  /* if temperature hits bottom, finish up */
	  if (em_temperature < 1.0)
	    {
	      em_temperature = 1.0;
	      em_anneal = 0;
	      em_runs = 1;
	    }
	  bow_verbosify (bow_progress, "Lowering temperature to %f\n", 
			 em_temperature);
	}
    }
  
  /* don't free class_probs for now.  Need them if doing LOO */
#if 0
  /* fix back up the doc barrel... dealloc class_probs (wrong size!) */  
  for (di=0; di < doc_barrel->cdocs->length; di++)
    {
      bow_cdoc *cdoc = bow_array_entry_at_index (doc_barrel->cdocs, di);

      bow_free(cdoc->class_probs);
      cdoc->class_probs = NULL;
    }
#endif  

#if 0
  /* if halting by perplexity reduction, return the previous
     round's barrel */
  if (em_halt_using_perplexity)
    {
      bow_wi2dvf_free(vpc_barrel->wi2dvf);
      vpc_barrel->wi2dvf = prev_wi2dvf;

      for (ci = 0; ci < max_new_ci; ci++)
	{
	  bow_cdoc *cdoc = bow_array_entry_at_index(vpc_barrel->cdocs, ci);
	  
	  cdoc->prior = prev_priors[ci];
	  cdoc->word_count = prev_word_counts[ci];
	  cdoc->normalizer = prev_normalizers[ci];
	}
    }
#endif

  bow_em_making_barrel = 0;  
  return vpc_barrel;
}


/* Calculate the perplexity of specified documents */
double
em_calculate_perplexity (bow_barrel *doc_barrel, bow_barrel *class_barrel)
{
  bow_dv_heap *test_heap;	/* we'll extract test WV's from here */
  bow_wv *query_wv;
  int di;			/* a document index */
  bow_score *hits;
  int num_hits_to_retrieve = bow_barrel_num_classes (class_barrel);
  int actual_num_hits;
  bow_cdoc *doc_cdoc;
  double log_prob_of_data = 0;
  double *class_probs;
  int hi;
  int ci;
  double rescaler;
  double scores_sum;
  double num_data_words = 0;
  int num_tested = 0;
  int wvi;
  bow_dv *dv;

  
  /* turn this on so scoring knows to return perplexities */
  bow_em_calculating_perplexity = 1;

  bow_verbosify(bow_progress, "\nCalculating perplexity:       ");

  /* Create the heap from which we'll get WV's. Initialize QUERY_WV so
     BOW_HEAP_NEXT_WV() knows not to try to free. */
  hits = alloca (sizeof (bow_score) * num_hits_to_retrieve);
  class_probs = alloca (sizeof (double) * num_hits_to_retrieve);
  test_heap = bow_test_new_heap (doc_barrel);
  query_wv = NULL;

  /* Loop once for each validation document. */
  while ((di = bow_heap_next_wv (test_heap, doc_barrel, &query_wv, 
				 em_perplexity_docs))
	 != -1)
    {
      doc_cdoc = bow_array_entry_at_index (doc_barrel->cdocs, 
					   di);
      bow_wv_set_weights (query_wv, class_barrel);
      bow_wv_normalize_weights (query_wv, class_barrel);
      actual_num_hits = 
	bow_barrel_score (class_barrel, 
			  query_wv, hits,
			  num_hits_to_retrieve, 
			  (em_perplexity_loo
			   ? (int) doc_cdoc->class_probs
			   : (int) NULL));
      assert (actual_num_hits == num_hits_to_retrieve);

      /* calculate class probabilities by normalizing scores 
         and adding in the class priors */
      {
	for (ci = 0; ci < num_hits_to_retrieve; ci++)
	  class_probs[ci] = 2;

	for (hi = 0; hi < num_hits_to_retrieve; hi++)
	  class_probs[hits[hi].di] = hits[hi].weight;
	
	/* check they all got set ok */
	for (ci = 0; ci < num_hits_to_retrieve; ci++)
	  assert (class_probs[ci] != 2);
	
	/* add in the class priors */
	for (ci = 0; ci < num_hits_to_retrieve; ci++)
	  {
	    bow_cdoc *cdoc = bow_array_entry_at_index(class_barrel->cdocs, 
						      ci);

	    class_probs[ci] += log (cdoc->prior);
	  }

	/* Rescale the class_probs  making them all 0 or
	   negative, so that exp() will work well, especially around the
	   higher-probability classes. */
	rescaler = -DBL_MAX;
	for (ci = 0; ci < num_hits_to_retrieve; ci++)
	  if (class_probs[ci] > rescaler) 
	    rescaler = class_probs[ci];
	/* RESCALER is now the maximum of the class_probs. */
	for (ci = 0; ci < num_hits_to_retrieve; ci++)
	  class_probs[ci] -= rescaler;
	
	/* Use exp() on the class_probs to get probabilities from
	   log-probabilities. */
	for (ci = 0; ci < num_hits_to_retrieve; ci++)
	    class_probs[ci] = exp (class_probs[ci]);
	
	/* If multi-hump neg, zero out the positive class */
	if (doc_cdoc->type == bow_doc_train &&
	    bow_em_multi_hump_neg > 1 &&
	    doc_cdoc->class != binary_pos_ci)
	  class_probs[binary_pos_ci] = 0;

	/* Normalize the class_probs so they all sum to one. */
	scores_sum = 0;
	for (ci = 0; ci < num_hits_to_retrieve; ci++)
	  scores_sum += class_probs[ci];
	for (ci = 0; ci < num_hits_to_retrieve; ci++)
	  class_probs[ci] /= scores_sum;
      }

      /* add in the contribution of this document.  For training docs,
         only count the contribution of their class, since the class
         label is known. */
      if (doc_cdoc->type != bow_doc_train ||
	  (doc_cdoc->type == bow_doc_train &&
	   bow_em_multi_hump_neg > 1 &&
	   doc_cdoc->class != binary_pos_ci))
	{
	  for (hi = 0; hi < num_hits_to_retrieve; hi++)
	    log_prob_of_data += class_probs[hits[hi].di] * hits[hi].weight;
	}
      else 
	{
	  for (hi = 0; hi < num_hits_to_retrieve; hi++)
	    {
	      if (hits[hi].di == doc_cdoc->class)
		{
		  log_prob_of_data += hits[hi].weight;
		  break;
		}
	    }
	}

#if 0
      if (bow_event_model == bow_event_document_then_word)
	assert (query_wv->normalizer == 
		bow_event_document_then_word_document_length );

      num_data_words += query_wv->normalizer;

#endif

      /* calculate the number of words shared between the model and the doc */
      for (wvi = 0; wvi < query_wv->num_entries; wvi++)
	{
	  dv = bow_wi2dvf_dv (class_barrel->wi2dvf, query_wv->entry[wvi].wi);
	  if (!dv)
	    continue;

	  num_data_words += query_wv->entry[wvi].weight;
	}


      if (num_tested % 100 == 0)
	bow_verbosify (bow_progress, "\b\b\b\b\b\b%6d", num_tested);
      
      num_tested++;	  
    }

  bow_verbosify(bow_progress, "\b\b\b\b\b\b%6d\n", num_tested);
  bow_verbosify (bow_progress, "Docs = %d, Words = %f, l(data) = %f\n",
		 num_tested, num_data_words, log_prob_of_data);

  /* convert log prob to perplexity and return */
  bow_em_calculating_perplexity = 0;
  return exp (-log_prob_of_data / num_data_words);
}


/* Calculate the accuracy of the barrel on the test set */
float
em_calculate_accuracy (bow_barrel *doc_barrel, bow_barrel *class_barrel)
{
  bow_dv_heap *test_heap;	/* we'll extract test WV's from here */
  bow_wv *query_wv;
  int di;			/* a document index */
  bow_score *hits;
  int num_hits_to_retrieve = 1;
  int actual_num_hits;
  bow_cdoc *doc_cdoc;
  int num_tested = 0;
  int num_correct = 0;

  /* Create the heap from which we'll get WV's. Initialize QUERY_WV so
     BOW_TEST_NEXT_WV() knows not to try to free. */
  hits = alloca (sizeof (bow_score) * num_hits_to_retrieve);
  test_heap = bow_test_new_heap (doc_barrel);
  query_wv = NULL;

  /* Loop once for each test document. */
  while ((di = bow_heap_next_wv (test_heap, doc_barrel, &query_wv, 
				 em_accuracy_docs))
	 != -1)
    {
      doc_cdoc = bow_array_entry_at_index (doc_barrel->cdocs, 
					   di);
      bow_wv_set_weights (query_wv, class_barrel);
      bow_wv_normalize_weights (query_wv, class_barrel);
      actual_num_hits = 
	bow_barrel_score (class_barrel, 
			  query_wv, hits,
			  num_hits_to_retrieve, 
			  (em_accuracy_loo
			   ? (int) doc_cdoc->class_probs
			   : (int) NULL));
      assert (actual_num_hits == num_hits_to_retrieve);
      if (doc_cdoc->class == hits[0].di)
	num_correct++;

      num_tested++;
    }

  return (((float) num_correct) / ((float) num_tested));
}



/* Run test trials, outputing results to TEST_FP.  The results are
   indended to be read and processed by the Perl script
   ./rainbow-stats. */
void
bow_em_compare_to_nb (bow_barrel *doc_barrel)
{
  bow_dv_heap *test_heap;	/* we'll extract test WV's from here */
  bow_wv *query_wv;
  int di;			/* a document index */
  bow_score *hits;
  int num_hits_to_retrieve = bow_barrel_num_classes (doc_barrel);
  int actual_num_hits;
  int hi;			/* hit index */
  bow_cdoc *doc_cdoc;
  bow_cdoc *class_cdoc;
  FILE *test_fp = stdout;
  bow_barrel *class_barrel;

  /* Re-create the vector-per-class barrel in accordance with the
     new train/test settings. */
  doc_barrel->method = (rainbow_method*) bow_method_at_name ("naivebayes");  
  class_barrel = 
    bow_barrel_new_vpc_with_weights (doc_barrel);

  /* Create the heap from which we'll get WV's. Initialize QUERY_WV so
     BOW_TEST_NEXT_WV() knows not to try to free. */
  test_heap = bow_test_new_heap (doc_barrel);
  query_wv = NULL;
  hits = alloca (sizeof (bow_score) * num_hits_to_retrieve);

  fprintf(test_fp, "#0\n");

  /* Loop once for each test document. */
  while ((di = bow_test_next_wv (test_heap, doc_barrel, &query_wv))
	 != -1)
    {
      doc_cdoc = bow_array_entry_at_index (doc_barrel->cdocs, di);
      class_cdoc = bow_array_entry_at_index (class_barrel->cdocs, 
					     doc_cdoc->class);
      bow_wv_set_weights (query_wv, class_barrel);
      bow_wv_normalize_weights (query_wv, class_barrel);
      actual_num_hits = 
	bow_barrel_score (class_barrel, 
			  query_wv, hits,
			  num_hits_to_retrieve, -1);
      assert (actual_num_hits == num_hits_to_retrieve);
      fprintf (test_fp, "%s %s ", 
	       doc_cdoc->filename, 
	       filename_to_classname(class_cdoc->filename)); 
      for (hi = 0; hi < actual_num_hits; hi++)
	{
	  class_cdoc = 
	    bow_array_entry_at_index (class_barrel->cdocs,
				      hits[hi].di);
	  fprintf (test_fp, "%s:%.*g ", 
		   filename_to_classname (class_cdoc->filename),
		   bow_score_print_precision,
		   hits[hi].weight); 
	}
      fprintf (test_fp, "\n");
    }

  bow_barrel_free (class_barrel);
  doc_barrel->method = (rainbow_method*) bow_method_at_name ("em");
}



/* Function to assign `Naive Bayes'-style weights to each element of
   each document vector. */
void
bow_em_print_log_odds_ratio (bow_barrel *barrel, int num_to_print)
{
  int ci;
  bow_cdoc *cdoc;
  int wi;			/* a "word index" into WI2DVF */
  int max_wi;			/* the highest "word index" in WI2DVF. */
  bow_dv *dv;			/* the "document vector" at index WI */
  int dvi;			/* an index into the DV */
  int weight_setting_num_words = 0;
  int total_num_words = 0;
  struct lorth { int wi; float lor; } lors[barrel->cdocs->length][num_to_print];
  int wci;

  bow_error("Can't use this while normalizer is being used for non-integral word_count");

  /* We assume that we have already called BOW_BARREL_NEW_VPC() on
     BARREL, so BARREL already has one-document-per-class. */

  max_wi = MIN (barrel->wi2dvf->size, bow_num_words());

  for (ci = 0; ci < barrel->cdocs->length; ci++)
    for (wci = 0; wci < num_to_print; wci++)
      {
	lors[ci][wci].lor = 0.0;
	lors[ci][wci].wi = -1;
      }

  /* assume that word_count, normalizer are already set */

  /* Calculate the total number of occurrences of each word; store this
     int DV->IDF. */

  for (wi = 0; wi < max_wi; wi++) 
    {
      dv = bow_wi2dvf_dv (barrel->wi2dvf, wi);
      if (dv == NULL)
	continue;
      dv->idf = 0;
      for (dvi = 0; dvi < dv->length; dvi++) 
	{
	  /* Is cdoc used for anything? - Jason */
	  cdoc = bow_array_entry_at_index (barrel->cdocs, 
					   dv->entry[dvi].di);
	  total_num_words += dv->entry[dvi].weight;
	  dv->idf += dv->entry[dvi].weight;
	}
    }


  bow_verbosify(bow_progress, "Calculating word weights:        ");

  /* Set the weights in the BARREL's WI2DVF so that they are
     equal to P(w|C), the probability of a word given a class. */
  for (wi = 0; wi < max_wi; wi++) 
    {
      double pr_w = 0.0;
      dv = bow_wi2dvf_dv (barrel->wi2dvf, wi);

      if (wi % 100 == 0)
	bow_verbosify(bow_progress, "\b\b\b\b\b\b%6d", wi);

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
	  pr_w_c = ((double)dv->entry[dvi].weight 
		    / (cdoc->word_count + cdoc->normalizer));
	  pr_w_c = (((double)dv->entry[dvi].weight + 1)
		    / (cdoc->word_count + barrel->wi2dvf->num_words));
	  pr_w_not_c = ((dv->idf - dv->entry[dvi].weight 
			 + barrel->cdocs->length - 1)
			/ 
			(total_num_words - cdoc->word_count
			 + (barrel->wi2dvf->num_words
			    * (barrel->cdocs->length - 1))));
	  log_likelihood_ratio = log (pr_w_c / pr_w_not_c);
	
	  wci = num_to_print - 1;

	  while (wci >= 0 && 
		 (lors[dv->entry[dvi].di][wci].lor < pr_w_c * log_likelihood_ratio))
	    wci--;

	  if (wci < num_to_print - 1)
	    {
	      int new_wci = wci + 1;

	      for (wci = num_to_print-1; wci > new_wci; wci--)
		{
		  lors[dv->entry[dvi].di][wci].lor = 
		    lors[dv->entry[dvi].di][wci - 1].lor;
		  lors[dv->entry[dvi].di][wci].wi = 
		    lors[dv->entry[dvi].di][wci - 1].wi;
		}

	      lors[dv->entry[dvi].di][new_wci].lor = pr_w_c * log_likelihood_ratio;
	      lors[dv->entry[dvi].di][new_wci].wi = wi;
	    }
	}
      weight_setting_num_words++;
      /* Set the IDF.  Kl doesn't use it; make it have no effect */
      dv->idf = 1.0;
    }

  for (ci = 0; ci < barrel->cdocs->length; ci++)
    {
      bow_cdoc *cdoc = bow_array_entry_at_index(barrel->cdocs, ci);

      bow_verbosify(bow_progress, "\n%s\n", filename_to_classname(cdoc->filename));
      for (wci = 0; wci < num_to_print; wci++)
	fprintf(stderr, "%1.4f %s\n", lors[ci][wci].lor, 
		bow_int2word (lors[ci][wci].wi));
    }
}


/* Print the P(C|w) distribution to a file so that we can later
   calculate the KL-divergence between the current distribution
   and the "correct" distribution.  */
void
bow_em_print_word_distribution (bow_barrel *vpc_barrel, int em_runs, 
				int num_classes)
{
  char filename[1024];
  FILE *fp;
  const char *word;
  int wi;
  bow_dv *dv;
  int c;			/* a class index */
  float total_word_count;
  int dvi;

  /* Open the file. */
  sprintf (filename, "pcw%02d", em_runs);
  fp = bow_fopen (filename, "w");

  /* Print the distribution for each word in the VPC_BARREL */
  for (wi = 0; wi < vpc_barrel->wi2dvf->size; wi++)
    {
      dv = bow_wi2dvf_dv (vpc_barrel->wi2dvf, wi);
      if (!dv)
	continue;
      word = bow_int2word (wi);
      fprintf (fp, "%s ", word);
      total_word_count = 0;
      for (dvi = 0; dvi < dv->length; dvi++)
	total_word_count += dv->entry[dvi].weight;
      /* Print the probability for each class; don't smooth. */
      for (c = 0, dvi = 0; c < num_classes; c++)
	{
	  while (dv->entry[dvi].di < c && dvi < dv->length)
	    dvi++;
	  if (dvi < dv->length && dv->entry[dvi].di == c)
	    fprintf (fp, "%g ", 
		     dv->entry[dvi].weight / total_word_count);
	  else
	    fprintf (fp, "0 ");
	}
      fprintf (fp, "\n");
    }
  fclose (fp);
}




/* Set the class prior probabilities by counting the number of
   documents of each class. note this counts all train and unlabeled
   docs.  Note that we're doing an m-estimate thing-y by starting
   out as one doc each per class. */
void
bow_em_set_priors_using_class_probs (bow_barrel *vpc_barrel,
				     bow_barrel *doc_barrel)
     
{
  float prior_sum = 0;
  int ci;
  int max_ci = vpc_barrel->cdocs->length - 1;
  int di;

  /* Zero them. */
  for (ci = 0; ci <= max_ci; ci++)
    {
      bow_cdoc *cdoc;
      cdoc = bow_array_entry_at_index (vpc_barrel->cdocs, ci);
      cdoc->prior = 1;
    }

  //prior_sum = max_ci;

  /* Add in document class_probs. */
  for (di = 0; di < doc_barrel->cdocs->length; di++)
    {
      bow_cdoc *doc_cdoc = bow_array_entry_at_index (doc_barrel->cdocs, di);
      bow_cdoc *vpc_cdoc;
      
      if (doc_cdoc->type == bow_doc_train ||
	  doc_cdoc->type == bow_doc_unlabeled)
	{
	  /* note that class probs correspond to CLASS barrel class indices */
	  for (ci = 0; ci <= max_ci; ci++)
	    {
	      vpc_cdoc = bow_array_entry_at_index (vpc_barrel->cdocs, ci); 
	      vpc_cdoc->prior += doc_cdoc->class_probs[ci];
	    }
	}
    }
  
  /* Sum them all. */
  for (ci = 0; ci <= max_ci; ci++)
    {
      bow_cdoc *cdoc;
      cdoc = bow_array_entry_at_index (vpc_barrel->cdocs, ci);
      assert (cdoc->prior == cdoc->prior);
      prior_sum += cdoc->prior;
    }

  /* Normalize to set the prior. */
  for (ci = 0; ci <= max_ci; ci++)
    {
      bow_cdoc *cdoc;
      cdoc = bow_array_entry_at_index (vpc_barrel->cdocs, ci);
      if (prior_sum != 0)
	cdoc->prior /= prior_sum;
      else
	cdoc->prior = 1.0 / (float) max_ci;
      assert (cdoc->prior > 0.0 && cdoc->prior < 1.0);
    }
}


/* Return the probability of word WI in class CI. 
   If LOO_CLASS_PROBS is not NULL, then we are doing 
   leave-out-one-document evaulation.  LOO_CLASS_PROBS are the probs
   of the classes from which the document has been removed.
   LOO_WI_COUNT is the number of WI'th words that are in the document
   LOO_W_COUNT is the total number of words in the docment

   The last two argments help this function avoid searching for
   the right entry in the DV from the beginning each time.
   LAST_DV is a pointer to the DV to use.
   LAST_DVI is a pointer to the index into the LAST_DV that is
   guaranteed to have class index less than CI.
*/
double
bow_em_pr_wi_ci (bow_barrel *barrel,
			 int wi, int ci,
			 float *loo_class_probs,
			 float loo_wi_count, float loo_w_count,
			 bow_dv **last_dv, int *last_dvi)
{
  bow_dv *dv;
  bow_cdoc *cdoc;
  float num_wi_ci;		/* the number of times wi occurs in class */
  float num_w_ci;		/* the number of words in class. */
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
      if (loo_class_probs &&
	  loo_class_probs[ci] > 0)
	bow_error ("There should be data for WI,CI");
    }
  num_w_ci = cdoc->normalizer;

  assert (num_wi_ci >= 0 && num_w_ci >=0);

  if (loo_class_probs != NULL &&
      loo_class_probs[ci] > 0)
    {
      float reduction;
      
      reduction = ((float) loo_class_probs[ci]) * ((float) loo_wi_count);
      num_wi_ci -= reduction;
      reduction = loo_class_probs[ci] * loo_w_count;
      num_w_ci -= reduction;
      /* be a little flexible with roundoff error.  Float's hold only
         seven significant digits or so */
#if 1
      if (num_wi_ci < 0 && num_wi_ci >= -0.00001)
	num_wi_ci = 0;
      if (num_w_ci < 0 && num_w_ci >= -0.01)
	num_w_ci = 0;
#endif
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
      if (naivebayes_argp_m_est_m == 0 
	  || bow_smoothing_method == bow_smoothing_laplace)
	m_est_m = barrel->wi2dvf->num_words;
      else
	m_est_m = naivebayes_argp_m_est_m;
      m_est_p = 1.0 / barrel->wi2dvf->num_words;
      pr_w_c = ((num_wi_ci + m_est_m * m_est_p)
		/ (num_w_ci + m_est_m));
    }
  else if (bow_smoothing_method == bow_smoothing_wittenbell)
    {
      bow_error("Can't use WittenBell while normalizer is word_count substitute");
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
  else if (bow_smoothing_method == bow_smoothing_dirichlet)
    {
      pr_w_c = (num_wi_ci + bow_naivebayes_dirichlet_alphas[wi]) / 
	(num_w_ci + bow_naivebayes_dirichlet_total);
    }
  else
    {
      bow_error ("EM does not implement smoothing method %d",
		 bow_smoothing_method);
      pr_w_c = 0;		/* to avoid gcc warning */
    }

  if (pr_w_c <= 0)
    bow_error ("A negative word probability was calculated. "
	       "This can happen if you are using\n"
	       "--test-files-loo and the test files are "
	       "not being lexed in the same way as they\n"
	       "were when the model was built");
  assert (pr_w_c > 0 && pr_w_c <= 1);

  return pr_w_c;
}



/* set the dv->idf, normalizer and word_count */
/* Function to assign `Naive Bayes'-style weights to each element of
   each document vector. */
void
bow_em_set_weights (bow_barrel *barrel)
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
  double total_num_words = 0;
  /* Gather the word count here instead of directly of in CDOC->WORD_COUNT
     so we avoid round-off error with each increment.  Remember,
     CDOC->WORD_COUNT is a int! */
  float num_words_per_ci[200];
  int barrel_is_empty = 0;

  assert (bow_barrel_num_classes (barrel) < 200);
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

  /* If we are using a document (binomial) model, then we'll just use
     the value of WORD_COUNT set in bow_barrel_new_vpc(), which is the
     total number of *documents* in the class, not the number of
     words. */
  /* Calculate P(w); store this in DV->IDF. */
  if (bow_event_model != bow_event_document)
    {
      /* Get the total number of terms in each class; store this in
	 CDOC->NORMALIZER for a non-integral value. */
      /* No longer do : Calculate the total number of unique words,
	 and make sure it is the same as BARREL->WI2DVF->NUM_WORDS. */
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
#if 0
	      /* inactive while normalizer is word_count sub */
	      cdoc->normalizer++;
#endif
	      dv->idf += dv->entry[dvi].weight;
	      total_num_words += dv->entry[dvi].weight;
	    }
	}
      for (ci = 0; ci < barrel->cdocs->length; ci++)
	{
	  cdoc = bow_array_entry_at_index (barrel->cdocs, ci);
	  cdoc->normalizer = num_words_per_ci[ci];
#if 0
	  cdoc->word_count = (int) rint (num_words_per_ci[ci]);
#endif
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

#if 0
  /* initialize Good-Turing smoothing */
  if (bow_smoothing_method == bow_smoothing_goodturing)
    bow_naivebayes_initialize_goodturing (barrel);
#endif
  
  if (bow_smoothing_method == bow_smoothing_dirichlet)
    bow_naivebayes_initialize_dirichlet_smoothing (barrel);

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
	      pr_w_c = bow_em_pr_wi_ci (barrel, wi, ci, NULL, 0, 0,
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



/* this is just naivebayes score using weights, not counts 
 Note that LOO stuff is now class_probs, not a class.  */
int
bow_em_score (bow_barrel *barrel, bow_wv *query_wv, 
	      bow_score *bscores, int bscores_len,
	      int loo_class_probs_as_int)
{
  double *scores;		/* will become prob(class), indexed over CI */
  int ci;			/* a "class index" (document index) */
  int wvi;			/* an index into the entries of QUERY_WV. */
  int dvi;			/* an index into a "document vector" */
  float pr_w_c = 0.0;			/* P(w|C), prob a word is in a class */
  double log_pr_tf;		/* log(P(w|C)^TF), ditto, log() of it */
  double rescaler;		/* Rescale SCORES by this after each word */
  double new_score;		/* a temporary holder */
  int num_scores;		/* number of entries placed in SCORES */
  int wi;                       /* word index */
  int max_wi;
  float *loo_class_probs = (float *) loo_class_probs_as_int;

  /* Allocate space to store scores for *all* classes (documents) */
  scores = alloca (barrel->cdocs->length * sizeof (double));
  max_wi = MIN (barrel->wi2dvf->size, bow_num_words());

  /* Instead of multiplying probabilities, we will sum up
     log-probabilities, (so we don't loose floating point resolution),
     and then take the exponent of them to get probabilities back. */

  /* Initialize the SCORES to the class prior probabilities. */
  if (bow_print_word_scores)
    printf ("%s\n",
	    "(CLASS PRIOR PROBABILIES)");
  if (!bow_em_calculating_perplexity)
    {
      for (ci = 0; ci < barrel->cdocs->length; ci++)
	{
	  bow_cdoc *cdoc = bow_array_entry_at_index (barrel->cdocs, ci);

	  /* Uniform prior means each class has probability 1/#classes. */
	  if (bow_uniform_class_priors)
	    scores[ci] = - log (barrel->cdocs->length);
	  else
	    {
#if 0 
	      /* For now forget about this little detail, because rainbow-h
		 trips up on it. */
	      /* LOO_CLASS is not implemented for cases in which we are
		 not doing uniform class priors. */
	      assert (loo_class == -1);
#endif
	      assert (cdoc->prior > 0.0f && cdoc->prior <= 1.0f);
	      scores[ci] = log (cdoc->prior);
	    }
	  assert (scores[ci] > -FLT_MAX + 1.0e5);
	  if (bow_print_word_scores)
	    printf ("%16s %-40s  %10.9f\n", 
		    "",
		    (strrchr (cdoc->filename, '/') ? : cdoc->filename),
		    scores[ci]);
	}
    }
  else
    {
      for (ci = 0; ci < barrel->cdocs->length; ci++)
	scores[ci] = 0;
    }


  /* Put contribution of the words into SCORES.  If we are using the
     document event model, then loop over all words in the vocabulary,
     otherwise, just loop over all the words in the QUERY_WV
     document. */

  for (wvi = 0, wi = 0;
       ((bow_event_model == bow_event_document)
	? (wi < max_wi)
	: (wvi < query_wv->num_entries));
       ((bow_event_model == bow_event_document)
	? (wi++)
	: (wvi++)))
    {
      bow_dv *dv;		/* the "document vector" for the word WI */
      


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
	wi = query_wv->entry[wvi].wi;

      dv = bow_wi2dvf_dv (barrel->wi2dvf, wi);

      /* If the model doesn't know about this word, skip it. */
      if (!dv)
	continue;

      if (bow_print_word_scores)
	printf ("%-30s (queryweight=%.8f)\n",
		bow_int2word (wi), 
		query_wv->entry[wvi].weight);

      rescaler = DBL_MAX;

      /* Loop over all classes, putting this word's (WI's)
	 contribution into SCORES. */
      for (ci = 0, dvi = 0; ci < barrel->cdocs->length; ci++)
	{
	  pr_w_c = bow_em_pr_wi_ci (barrel, wi, ci, 
				    loo_class_probs, 
				    query_wv->entry[wvi].weight, 
				    query_wv->normalizer,
				    &dv, &dvi);
	  
	  /* If this is a word that does not occur in the document,
	     then use the probability it does not occur in the class.
	     This occurs only if we are using the document event model. */
	  if (bow_event_model == bow_event_document &&
	      (query_wv->num_entries == 0 || wi != query_wv->entry[wvi].wi))
	    pr_w_c = 1.0 - pr_w_c;

	  assert (pr_w_c > 0 && pr_w_c <= 1);

	  log_pr_tf = log (pr_w_c);
	  assert (log_pr_tf > -FLT_MAX + 1.0e5);

	  /* Take into consideration the number of times it occurs in 
	     the query document */
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

      if (!bow_em_calculating_perplexity &&
	  (!em_cross_entropy || bow_em_making_barrel))
	{
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
    }
  /* Now SCORES[] contains a (unnormalized) log-probability for each class. */
  
  /* Now adjust for temperature if building the barrel, and using DA */
  if (!bow_em_calculating_perplexity && em_anneal && bow_em_making_barrel)
    {
      for (ci = 0; ci < barrel->cdocs->length; ci++)
	scores[ci] /= em_temperature;
    }

  /* Rescale the SCORE one last time, this time making them all 0 or
     negative, so that exp() will work well, especially around the
     higher-probability classes. */
  if (!bow_em_calculating_perplexity &&
      (!em_cross_entropy || bow_em_making_barrel))
    {
      rescaler = -DBL_MAX;
      for (ci = 0; ci < barrel->cdocs->length; ci++)
	if (scores[ci] > rescaler) 
	  rescaler = scores[ci];
      /* RESCALER is now the maximum of the SCORES. */
      for (ci = 0; ci < barrel->cdocs->length; ci++)
	scores[ci] -= rescaler;
    }
  
  /* do special hack in binary case so we can get meaningful P/R curves */
  if (!bow_em_calculating_perplexity && bow_em_binary_case && 
      !bow_em_making_barrel)
    {
      int low_score_index = -1;
      double best_neg_score = -DBL_MAX;
      int ci;
      int zero_index = -1;

      /* find the index of the greatest class that's less than zero. */

      for (ci = 0; ci < barrel->cdocs->length; ci ++)
	{
	  if (scores[ci] < 0 && scores[ci] > best_neg_score)
	    {
	      best_neg_score = scores[ci];
	      low_score_index = ci;
	    }
	  else if (scores[ci] >= 0)
	    {
	      assert (scores[ci] == 0);
	      if (zero_index != -1)
		{
		  low_score_index = ci;
		  best_neg_score = scores[ci];
		}
	      else
		zero_index = ci;
	    }
	}
      assert(low_score_index != -1 && zero_index != -1);
      scores[zero_index] = -1.0 * scores[low_score_index];
    }
  else if (!bow_em_calculating_perplexity)
    {
      if (em_cross_entropy && !bow_em_making_barrel)
	{
	  for (ci = 0; ci < barrel->cdocs->length; ci++)
	    scores[ci] /= (query_wv->normalizer + 1);
	}
      else
	{
	  /* Use exp() on the SCORES to get probabilities from
	     log-probabilities. */
	  for (ci = 0; ci < barrel->cdocs->length; ci++)
	    {
	      new_score = exp (scores[ci]);
	      /* assert (new_score > 0 && new_score < DBL_MAX - 1.0e5); */
	      scores[ci] = new_score;
	    }
	}

      /* Normalize the SCORES so they all sum to one. */
      if (!em_cross_entropy || bow_em_making_barrel)
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



/* what about em parameters?  How should those be used */

rainbow_method bow_method_em = 
{
  "em",
  NULL, /* bow_leave_weights_alone_since_theyre_really_counts */
  0,				/* no weight scaling function */
  NULL, /* bow_barrel_normalize_weights_by_summing, */
  bow_em_new_vpc_with_weights,
  bow_em_set_priors_using_class_probs,
  bow_em_score,
  bow_wv_set_weights_to_count,
  NULL,				/* no need for extra weight normalization */
  bow_barrel_free,
  NULL  /* is this right?  should we have em parameters? */
};

void _register_method_em () __attribute__ ((constructor));
void _register_method_em ()
{
  static int done = 0;
  if (done) 
    return;
  bow_method_register_with_name ((bow_method*)&bow_method_em,
				 "em", 
				 sizeof (rainbow_method),
				 &em_argp_child);
  bow_argp_add_child (&em_argp_child);
  done = 1;
}
