/* Weight-setting and scoring implementation for active learning */

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

typedef enum 
{ 
  dkl,
  length,
  qbc,
  randomly,
  relevance,
  skl,
  sve,
  uncertainty,
  ve,
  wkl
} active_selection_type;

typedef struct _active_scores {
  int di;   /* the doc barrel index of the doc */
  double weight;  /* weight used for selecting */
  bow_score **scores;  /* the scores of the doc */
} active_scores;


void active_select_length (bow_barrel *doc_barrel, active_scores *scores,  
			   int num_to_add, int total_unknown, int committee_size);
void active_select_uncertain (bow_barrel *doc_barrel, active_scores *scores,  
			      int num_to_add, int total_unknown, int committee_size);
void active_select_relevant (bow_barrel *doc_barrel, active_scores *scores,  
			     int num_to_add, int total_unknown, int committee_size);
void active_select_random (bow_barrel *doc_barrel, active_scores *scores,  
			   int num_to_add, int total_unknown, int committee_size);
void active_select_qbc (bow_barrel *doc_barrel, active_scores *scores,  
			int num_to_add, int total_unknown, int committee_size);
void active_select_weighted_kl (bow_barrel *doc_barrel, active_scores *scores,  
				int num_to_add, int total_unknown, int committee_size);
void active_select_dkl (bow_barrel *doc_barrel, active_scores *scores,  
			int num_to_add, int total_unknown, int committee_size);
void active_select_vote_entropy (bow_barrel *doc_barrel, active_scores *scores,  
				 int num_to_add, int total_unknown, int committee_size);
void active_select_stream_ve (bow_barrel *doc_barrel, active_scores *scores,  
			      int num_to_add, int total_unknown, int committee_size);
void active_select_stream_kl (bow_barrel *doc_barrel, active_scores *scores,  
			      int num_to_add, int total_unknown, int committee_size);



void active_test (FILE *test_fp, bow_barrel *rainbow_doc_barrel,
		  bow_barrel *rainbow_class_barrel);

/* The variables that can be changed on the command line, with defaults: */
static int active_add_per_round = 4;
static int active_test_stats = 0;
static int active_committee_size = 1;
static active_selection_type active_selection_method = uncertainty;
static int active_num_rounds = 10;
static void (* active_select_docs)(bow_barrel *, active_scores *,  int, int, int) =
                active_select_uncertain;
static int active_binary_pos_ci = -1;
static char* active_binary_pos_classname = NULL;
static char* active_secondary_method = "naivebayes";
static int active_final_em = 0;
static int active_print_committee_matrices = 0;
static int active_qbc_low_kl = 0;
static int active_pr_print_stat_summary = 0;
static int active_pr_window_size = 20;
static int active_remap_scores_pr = 0;
static int active_no_final_em = 0;
static double active_alpha = 0.5;
static double active_beta = 5;
static double active_stream_epsilon = 0.3;
static int active_perturb_after_em = 0;

/* The integer or single char used to represent this command-line option.
   Make sure it is unique across all libbow and rainbow. */
enum {
  ACTIVE_ADD_PER_ROUND = 4000,
  ACTIVE_TEST_STATS,
  ACTIVE_SELECTION_METHOD,
  ACTIVE_NUM_ROUNDS,
  ACTIVE_BINARY_POS,
  ACTIVE_SECONDARY_METHOD,
  ACTIVE_COMMITTEE_SIZE,
  ACTIVE_FINAL_EM,
  ACTIVE_PRINT_COMMITTEE_MATRICES,
  ACTIVE_QBC_LOW_KL,
  ACTIVE_PR_PRINT_STAT_SUMMARY,
  ACTIVE_PR_WINDOW_SIZE,
  ACTIVE_REMAP_SCORES_PR,
  ACTIVE_NO_FINAL_EM,
  ACTIVE_BETA,
  ACTIVE_STREAM_EPSILON,
  ACTIVE_PERTURB_AFTER_EM,
};

static struct argp_option active_options[] =
{
  {0,0,0,0,
   "Active Learning options:", 70},
  {"active-add-per-round", ACTIVE_ADD_PER_ROUND, "NUM", 0,
   "Specify the number of documents to label each round.  The default is 4."},
  {"active-test-stats", ACTIVE_TEST_STATS, 0, 0,
   "Generate output for test docs every n rounds."},
  {"active-selection-method", ACTIVE_SELECTION_METHOD, "METHOD", 0,
   "Specify the selection method for picking unlabeled docs. "
   "One of uncertainty, relevance, qbc, random. "
   "The default is 'uncertainty'."},
  {"active-num-rounds", ACTIVE_NUM_ROUNDS, "NUM", 0,
   "The number of active learning rounds to perform.  The default is 10."},
  {"active-binary-pos", ACTIVE_BINARY_POS, "CLASS", 0,
   "The name of the positive class for binary classification.  Required for"
   "relevance sampling."},
  {"active-secondary-method", ACTIVE_SECONDARY_METHOD, "METHOD", 0,
   "The underlying method for active learning to use.  The default is 'naivebayes'."},
  {"active-committee-size", ACTIVE_COMMITTEE_SIZE, "NUM", 0,
   "The number of committee members to use with QBC.  Default is 1."},
  {"active-final-em", ACTIVE_FINAL_EM, 0, 0,
   "Finish with a full round of EM."},
  {"active-print-committee-matrices", ACTIVE_PRINT_COMMITTEE_MATRICES, 0, 0,
   "Print the confusion matrix for each committee member at each round."},
  {"active-qbc-low-kl", ACTIVE_QBC_LOW_KL, 0, 0,
   "Select documents with the lowest kl-divergence instead of the highest."},
  {"active-pr-print-stat-summary", ACTIVE_PR_PRINT_STAT_SUMMARY, 0, 0,
   "Print the precision recall curves used for score to probability remapping."},
  {"active-pr-window-size", ACTIVE_PR_WINDOW_SIZE, "NUM", 0,
   "Set the window size for precision-recall score to probability remapping."
   "The default is 20."},
  {"active-remap-scores-pr", ACTIVE_REMAP_SCORES_PR, 0, 0,
   "Remap scores with sneaky precision-recall tricks."},
  {"active-no-final-em", ACTIVE_NO_FINAL_EM, 0, 0,
   "Finish without a full round of EM."},
  {"active-beta", ACTIVE_BETA, "NUM", 0,
   "Increase spread of document densities."},
  {"active-stream-epsilon", ACTIVE_STREAM_EPSILON, "NUM", 0,
   "The rate factor for selecting documents in stream sampling."},
  {"active-perturb-after-em", ACTIVE_PERTURB_AFTER_EM, 0, 0,
   "Perturb after running EM to create committee members."},
  {0, 0}
};

error_t
active_parse_opt (int key, char *arg, struct argp_state *state)
{
  switch (key)
    {
    case ACTIVE_ADD_PER_ROUND:
      active_add_per_round = atoi(arg);
      break;
    case ACTIVE_TEST_STATS:
      active_test_stats = 1;
      break;
    case ACTIVE_SELECTION_METHOD:
      if (!strcmp(arg, "uncertainty"))
	{
	  active_selection_method = uncertainty;
	  active_select_docs = active_select_uncertain;
	}
      else if (!strcmp(arg, "length"))
	{
	  active_selection_method = length;
	  active_select_docs = active_select_length;
	}
      else if (!strcmp(arg, "relevance"))
	{
	  active_selection_method = relevance;
	  active_select_docs = active_select_relevant;
	}
      else if (!strcmp(arg, "random"))
	{
	  active_selection_method = randomly;
	  active_select_docs = active_select_random;
	}
      else if (!strcmp(arg, "qbc"))
	{
	  active_selection_method = qbc;
	  active_select_docs = active_select_qbc;
	}
      else if (!strcmp(arg, "ve"))
	{
	  active_selection_method = ve;
	  active_select_docs = active_select_vote_entropy;
	}	  
      else if (!strcmp(arg, "wkl"))
	{
	  active_selection_method = wkl;
	  active_select_docs = active_select_weighted_kl;
	}
      else if (!strcmp(arg, "dkl"))
	{
	  active_selection_method = dkl;
	  active_select_docs = active_select_dkl;
	}
      else if (!strcmp(arg, "sve"))
	{
	  active_selection_method = sve;
	  active_select_docs = active_select_stream_ve;
	}
      else if (!strcmp(arg, "skl"))
	{
	  active_selection_method = skl;
	  active_select_docs = active_select_stream_kl;
	}
      else
	bow_error("Invalid argument for --active-selection-method");
      break;
    case ACTIVE_NUM_ROUNDS:
      active_num_rounds = atoi(arg);
      break;
    case ACTIVE_BINARY_POS:
      active_binary_pos_classname = arg;
      break;
    case ACTIVE_SECONDARY_METHOD:
      active_secondary_method = arg;
      break;
    case ACTIVE_COMMITTEE_SIZE:
      active_committee_size = atoi (arg);
      break;
    case ACTIVE_FINAL_EM:
      active_final_em = 1;
      break;
    case ACTIVE_PRINT_COMMITTEE_MATRICES:
      active_print_committee_matrices = 1;
      break;
    case ACTIVE_QBC_LOW_KL:
      active_qbc_low_kl = 1;
      break;
    case ACTIVE_REMAP_SCORES_PR:
      active_remap_scores_pr = 1;
      break;
    case ACTIVE_PR_WINDOW_SIZE:
      active_pr_window_size = atoi (arg);
      break;
    case ACTIVE_PR_PRINT_STAT_SUMMARY:
      active_pr_print_stat_summary = 1;
      break;
    case ACTIVE_NO_FINAL_EM:
      active_no_final_em = 1;
      break;
    case ACTIVE_BETA:
      active_beta = atof (arg);
      break;
    case ACTIVE_STREAM_EPSILON:
      active_stream_epsilon = atof (arg);
      break;
    case ACTIVE_PERTURB_AFTER_EM:
      active_perturb_after_em = 1;
      break;
    default:
      return ARGP_ERR_UNKNOWN;
    }
  return 0;
}

static const struct argp active_argp =
{
  active_options,
  active_parse_opt
};

static struct argp_child active_argp_child =
{
  &active_argp,		/* This child's argp structure */
  0,			/* flags for child */
  0,			/* optional header in help message */
  0			/* arbitrary group number for ordering */
};

/* End of command-line options specific to EM */


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




/* cheat and look at the unlabeled data and convert the scores into
true probabilities based on a window size.  BUG: we're not resorting
the weights as we should be. */
void
active_remap_scores (bow_barrel *doc_barrel, active_scores *scores,  
		     int total_unknown, int committee_size)
{
  int num_classes = bow_barrel_num_classes(doc_barrel);
  bow_em_pr_struct *pr_by_class[num_classes];
  int member;
  int ci;
  int scorei;
  int hi;

  
  /* malloc some space for pr stats */
  for (ci = 0; ci < num_classes; ci++)
    pr_by_class[ci] = bow_malloc(sizeof(bow_em_pr_struct) * total_unknown);
  
  for (member = 0; member < committee_size; member++)
    {

      /* arrange this members scores by class, and note correctness */
      for (scorei = 0; scorei < total_unknown; scorei++)
	{
	  bow_cdoc *cdoc = bow_array_entry_at_index (doc_barrel->cdocs, 
						     scores[scorei].di);
	  
	  for (hi = 0; hi < num_classes; hi++)
	    {
	      pr_by_class[scores[scorei].scores[member][hi].di][scorei].score = 
		scores[scorei].scores[member][hi].weight;
	      pr_by_class[scores[scorei].scores[member][hi].di][scorei].correct = 
		(cdoc->class == scores[scorei].scores[member][hi].di
		 ? 1 : 0);
	    }
	}
		  
      /* sort the scores for each class by descending score */
      for (ci = 0; ci < num_classes; ci ++)
	qsort(pr_by_class[ci], total_unknown, sizeof (bow_em_pr_struct),
	      bow_em_pr_struct_compare);
		  
      /* print out a summary of the stats */
      if (active_pr_print_stat_summary)
	{
	  for (ci = 0; ci < num_classes; ci++)
	    {
	      int pr_index;
	      int correct=0;
	      int count=0;
	      
	      bow_verbosify(bow_progress, "%25s", 
			    filename_to_classname
			    (bow_barrel_classname_at_index (doc_barrel, ci)));
	      
	      for (pr_index = 0; pr_index < total_unknown; pr_index++)
		{
		  
		  if (pr_index % active_pr_window_size == 0)
		    {
		      if (pr_index != 0)
			{
			  while (pr_index < total_unknown &&
				 pr_by_class[ci][pr_index-1].score == 
				 pr_by_class[ci][pr_index].score)
			    {
			      correct += pr_by_class[ci][pr_index].correct;
			      count++;
			      pr_index++;
			    }
			  bow_verbosify(bow_progress, " %3.0f (%1.3f)", 
					(float) correct * 100.0 / count,
					pr_by_class[ci][pr_index].score);
			  if (!(pr_index < total_unknown))
			    break;
			}
		      correct = 0;
		      count = 0;
		    }
		  correct += pr_by_class[ci][pr_index].correct;
		  count++;
		  
		  if (pr_by_class[ci][pr_index].correct != 0 &&
		      pr_by_class[ci][pr_index].correct != 1)
		    bow_error("Big Problem");
		}
	      
	      bow_verbosify(bow_progress, "\n");
	    }
	}

      /* remap the scores to better probabilities */
      for (scorei = 0; scorei < total_unknown; scorei++)
	{
	  double prob_by_ci[100];
	  double total = 0.0;
		      
	  assert(num_classes < 100);
	  /* set the class_probs by picking numbers from the pr 
	     charts */
	  for (hi = 0; hi < num_classes; hi++)
	    {
	      double score = scores[scorei].scores[member][hi].weight;
	      int class = scores[scorei].scores[member][hi].di;
	      int pr_index_low;
	      int pr_index_high;
	      int pr_index = 0;
	      int correct_count = 0;
	      int num_docs_in_window = 0;
	      int pri;
			  
	      while ((pr_index < total_unknown) && 
		     (pr_by_class[class][pr_index].score > score))
		pr_index++;
	      
	      pr_index_low = pr_index;
	      
	      while ((pr_index < total_unknown) &&
		     pr_by_class[class][pr_index].score == score)
		pr_index++;
	      
	      pr_index_high = pr_index;
	      
#if 0		      
	      if (10 > pr_index)
		correct_count += 10 - pr_index;
#endif

	      /* note that we're including the test document here 
		 in the stats... */
	      for (pri = MAX (0, MIN(pr_index_low, 
				     ((pr_index_low + pr_index_high - 
				       active_pr_window_size) / 2))); 
		   pri < MIN (MAX(pr_index_high,
				  ((pr_index_high + pr_index_low + 
				    active_pr_window_size) / 2)),
			      total_unknown);
		   pri++)
		{
		  correct_count += pr_by_class[class][pri].correct;
		  num_docs_in_window++;
		}
			  
	      prob_by_ci[class] = (double) correct_count / 
		((double) num_docs_in_window);
	    }
		      
	  /* normalize the probs to sum to one */
	  for (ci = 0; ci < num_classes; ci++)
	    total += prob_by_ci[ci];

	  for (hi = 0; hi < num_classes; hi++)
	    scores[scorei].scores[member][hi].weight =
	      prob_by_ci[scores[scorei].scores[member][hi].di] / total; 


	}
    }
}

/* Return the entropy of the words in the document WV. */
float
active_document_entropy (bow_wv *wv)
{
  float ret = 0;
  float wv_word_count = 0;
  int wvi;
  float pr_w;

  for (wvi = 0; wvi < wv->num_entries; wvi++)
    wv_word_count += wv->entry[wvi].count;
  for (wvi = 0; wvi < wv->num_entries; wvi++)
    {
      pr_w = wv->entry[wvi].count / wv_word_count;
      ret -= pr_w * log (pr_w);
    }
  return ret;
}


/* select method routines */

/* comparison function for sorting on selection criteria */
int
active_scores_compare (const void *x, const void *y)
{
  if (((active_scores *)x)->weight > ((active_scores *)y)->weight)
    return -1;
  else if (((active_scores *)x)->weight == ((active_scores *)y)->weight)
    return 0;
  else
    return 1;
}

/* select docs with the highest kl-divergence to the mean */
void
active_select_qbc (bow_barrel *doc_barrel, active_scores *scores,  
		   int num_to_add, int total_unknown,
		   int committee_size)
{
  int num_classes = bow_barrel_num_classes (doc_barrel);
  double *mean_class_dist;
  double mean_class_sum;
  int committee;
  int class;
  int k;

  mean_class_dist = alloca (sizeof (double) * num_classes);

  /* Calculate the entropy of the class labels, H(Class|d,Committee),
     where Class and Committee are random varibles, and put this in
     SCORES->WEIGHT. */
  for (k = 0; k < total_unknown; k++)
    {
      scores[k].weight = 0;

      /* Initialize the mean class distribution for this document. */
      for (class = 0; class < num_classes; class++)
	mean_class_dist[class] = 0;
      for (committee = 0; committee < committee_size; committee++)
	for (class = 0; class < num_classes; class++)
	  mean_class_dist[scores[k].scores[committee][class].di]
	    += scores[k].scores[committee][class].weight;
      mean_class_sum = 0;
      for (class = 0; class < num_classes; class++)
	mean_class_sum += mean_class_dist[class];
      assert (mean_class_sum > committee_size * 0.999);
      assert (mean_class_sum < committee_size * 1.001);
      for (class = 0; class < num_classes; class++)
	mean_class_dist[class] /= mean_class_sum;

      /* Set WEIGHT to KL-divergence-to-the-mean averaged over all 
	 committee members. */
      for (committee = 0; committee < committee_size; committee++)
	{
	  for (class = 0; class < bow_barrel_num_classes (doc_barrel); class++)
	    {
	      if (1e-100 <  scores[k].scores[committee][class].weight)
		{
		  scores[k].weight -= 
		    ((1.0 / committee_size)
		     * scores[k].scores[committee][class].weight
		     * log (mean_class_dist[scores[k].scores[committee][class].di]
			    / scores[k].scores[committee][class].weight));
		  if (scores[k].weight < -0.1)
		    bow_error("scores[k].weight <  -0.1: %.20f, %.20f", scores[k].weight,
			      log (mean_class_dist[scores[k].scores[committee][class].di]
				   / scores[k].scores[committee][class].weight));
		}
	    }
	}
      /* KL divergence must be greater than or equal to 0 */
      if (scores[k].weight < -0.1)
	bow_error("scores[k].weight <  -0.1: %.20f", scores[k].weight);
      
    }

  /* reverse all weights if want lowest ones */
  if (active_qbc_low_kl)
    {
      for (k = 0; k < total_unknown ; k++)
	{
	  scores[k].weight = -1 * scores[k].weight;
	}
    }
  
  /* Sort based on weight */
  qsort (scores, total_unknown, sizeof (active_scores),
	 active_scores_compare);
  
  /* Change doc types of those with highest entropy*/
  for (k = 0; k < num_to_add; k++)
    {
      bow_cdoc *doc;
      doc = bow_cdocs_di2doc (doc_barrel->cdocs, scores[k].di);
      assert (doc);
      assert (doc->type == bow_doc_unlabeled);
      bow_verbosify (bow_progress, "Labeling %s, weight %f", doc->filename,
		     scores[k].weight);
      for (committee=0; committee < committee_size; committee++)
	bow_verbosify(bow_progress, " [(%d, %f) (%d, %f)]", 
		      scores[k].scores[committee][0].di,
		      scores[k].scores[committee][0].weight,
		      scores[k].scores[committee][1].di,
		      scores[k].scores[committee][1].weight);
      bow_verbosify(bow_progress, "\n");
      doc->type = bow_doc_train;
    }
  
  return;
}

/* select docs with the highest weighted kl-divergence to the mean */
void
active_select_weighted_kl (bow_barrel *doc_barrel, active_scores *scores,  
			   int num_to_add, int total_unknown,
			   int committee_size)
{
  int num_classes = bow_barrel_num_classes (doc_barrel);
  double mean_class_dist[num_classes];
  double mean_class_sum;
  double **nb_scores;
  int committee;
  int class;
  int k;
  bow_cdoc *cdoc;
  double nb_scores_sum;
  double nb_scores_max;
  int si;			/* an index into the sorted list of scores */

  assert (num_to_add < total_unknown);
  assert (em_cross_entropy == 1);

  /* Allocate space to store Naive Bayes scores. */
  nb_scores = alloca (sizeof (double*) * committee_size);
  for (committee = 0; committee < committee_size; committee++)
    nb_scores[committee] = alloca (sizeof(double) * num_classes);

  /* Calculate the weighted KL divergence of the class labels
     and put this in SCORES->WEIGHT. */
  for (k = 0; k < total_unknown; k++)
    {
      /* Fill in the Naive Bayes scores array for this K'th document. */
      cdoc = bow_array_entry_at_index (doc_barrel->cdocs, scores[k].di);
      for (committee = 0; committee < committee_size; committee++)
	{
	  /* Undo the document length normalization */
	  for (si = 0; si < num_classes; si++)
	    nb_scores[committee][scores[k].scores[committee][si].di] = 
	      (scores[k].scores[committee][si].weight
	       * (cdoc->word_count + 1));

	  /* Rescale the scores */
	  nb_scores_max = -DBL_MAX;
	  for (class = 0; class < num_classes; class++)
	    if (nb_scores_max < nb_scores[committee][class])
	      nb_scores_max = nb_scores[committee][class];

	  for (class = 0; class < num_classes; class++)
	    nb_scores[committee][class] -= nb_scores_max;

	  /* Take the exponent of the scores to make them probabilities. */
	  for (class = 0; class < num_classes; class++)
	    nb_scores[committee][class] = exp (nb_scores[committee][class]);

	  /* Normalize them so they sum to one. */
	  nb_scores_sum = 0;
	  for (class = 0; class < num_classes; class++)
	    nb_scores_sum += nb_scores[committee][class];

	  assert (nb_scores_sum > 0);
	  for (class = 0; class < num_classes; class++)
	    nb_scores[committee][class] /= nb_scores_sum;
	}

      /* Initialize the mean class distribution for this document. */
      for (class = 0; class < num_classes; class++)
	mean_class_dist[class] = 0;
      for (committee = 0; committee < committee_size; committee++)
	for (class = 0; class < num_classes; class++)
	  mean_class_dist[class] += nb_scores[committee][class];
      mean_class_sum = 0;
      for (class = 0; class < num_classes; class++)
	mean_class_sum += mean_class_dist[class];
      assert (mean_class_sum > committee_size * 0.999);
      assert (mean_class_sum < committee_size * 1.001);
      for (class = 0; class < num_classes; class++)
	mean_class_dist[class] /= mean_class_sum;

      /* Set WEIGHT to KL-divergence-to-the-mean averaged over all 
	 committee members. */
      scores[k].weight = 0;
      for (committee = 0; committee < committee_size; committee++)
	{
	  for (si = 0; si < bow_barrel_num_classes (doc_barrel); si++)
	    {
	      class = scores[k].scores[committee][si].di;
	      if (1e-100 < nb_scores[committee][class])
		{
/* xxx Change this back to regular old WKL! */
#define UNSUPERVISED_DENSITY 1
#if UNSUPERVISED_DENSITY
		  scores[k].weight -= 
		    ((1.0 / committee_size)
		     /* scale by kl-div of this document to this class */
		     * nb_scores[committee][class]
		     * log (mean_class_dist[class]
			    / nb_scores[committee][class]));
#elif 1
		  /* Used for ICML submission */
		  scores[k].weight -= 
		    ((1.0 / committee_size)
		     /* scale by kl-div of this document to this class */
		     * exp (scores[k].scores[committee][si].weight 
			    + cdoc->normalizer)
		     * nb_scores[committee][class]
		     * log (mean_class_dist[class]
			    / nb_scores[committee][class]));
#else
		  scores[k].weight -= 
		    ((1.0 / committee_size)
		     * (cdoc->word_count + 1)
		     /* scale by perplexity of this document in this class */
		     * exp (scores[k].scores[committee][si].weight)
		     * nb_scores[committee][class]
		     * log (mean_class_dist[class]
			    / nb_scores[committee][class]));
#endif
		}
	    }
	}

#if UNSUPERVISED_DENSITY
      /* Scale the score by the document density. */
      scores[k].weight *= cdoc->prior;
#endif      
      /* KL divergence must be greater than or equal to 0 */
      if (scores[k].weight < -0.1)
	bow_error("scores[k].weight <  -0.1: %.20f", scores[k].weight);

    }

  /* Sort based on weight */
  qsort (scores, total_unknown, sizeof (active_scores),
	 active_scores_compare);
  
  /* Change doc types of those with highest entropy*/
  for (k = 0; k < num_to_add ; k++)
    {
      bow_cdoc *doc;
      doc = bow_cdocs_di2doc (doc_barrel->cdocs, scores[k].di);
      assert (doc);
      assert (doc->type == bow_doc_unlabeled);
      bow_verbosify (bow_progress, "Labeling %s, weight %f", doc->filename,
		     scores[k].weight);
      for (committee=0; committee < committee_size; committee++)
	bow_verbosify(bow_progress, " [(%d, %f) (%d, %f)]", 
		      scores[k].scores[committee][0].di,
		      scores[k].scores[committee][0].weight,
		      scores[k].scores[committee][1].di,
		      scores[k].scores[committee][1].weight);
      bow_verbosify(bow_progress, "\n");
      doc->type = bow_doc_train;
    }
  
  return;
}

/* select docs with the highest weighted kl-divergence to the mean.
   Needs crossentropy scores! */
void
active_select_dkl (bow_barrel *doc_barrel, active_scores *scores,  
		   int num_to_add, int total_unknown,
		   int committee_size)
{
  int num_classes = bow_barrel_num_classes (doc_barrel);
  double mean_class_dist[num_classes];
  double mean_class_sum;
  double **nb_scores;
  int committee;
  int class;
  int k;
  bow_cdoc *cdoc;
  double nb_scores_sum;
  double nb_scores_max;
  int si;			/* an index into the sorted list of scores */

  assert (num_to_add < total_unknown);
  assert (em_cross_entropy == 1);

  /* Allocate space to store Naive Bayes scores. */
  nb_scores = alloca (sizeof (double*) * committee_size);
  for (committee = 0; committee < committee_size; committee++)
    nb_scores[committee] = alloca (sizeof(double) * num_classes);

  /* Calculate the weighted KL divergence of the class labels
     and put this in SCORES->WEIGHT. */
  for (k = 0; k < total_unknown; k++)
    {
      /* Fill in the Naive Bayes scores array for this K'th document. */
      cdoc = bow_array_entry_at_index (doc_barrel->cdocs, scores[k].di);
      for (committee = 0; committee < committee_size; committee++)
	{
	  /* Undo the document length normalization */
	  for (si = 0; si < num_classes; si++)
	    nb_scores[committee][scores[k].scores[committee][si].di] = 
	      (scores[k].scores[committee][si].weight
	       * (cdoc->word_count + 1));

	  /* Rescale the scores */
	  nb_scores_max = -DBL_MAX;
	  for (class = 0; class < num_classes; class++)
	    if (nb_scores_max < nb_scores[committee][class])
	      nb_scores_max = nb_scores[committee][class];

	  for (class = 0; class < num_classes; class++)
	    nb_scores[committee][class] -= nb_scores_max;

	  /* Take the exponent of the scores to make them probabilities. */
	  for (class = 0; class < num_classes; class++)
	    nb_scores[committee][class] = exp (nb_scores[committee][class]);

	  /* Normalize them so they sum to one. */
	  nb_scores_sum = 0;
	  for (class = 0; class < num_classes; class++)
	    nb_scores_sum += nb_scores[committee][class];

	  assert (nb_scores_sum > 0);
	  for (class = 0; class < num_classes; class++)
	    nb_scores[committee][class] /= nb_scores_sum;
	}

      /* Initialize the mean class distribution for this document. */
      for (class = 0; class < num_classes; class++)
	mean_class_dist[class] = 0;
      for (committee = 0; committee < committee_size; committee++)
	for (class = 0; class < num_classes; class++)
	  mean_class_dist[class] += nb_scores[committee][class];
      mean_class_sum = 0;
      for (class = 0; class < num_classes; class++)
	mean_class_sum += mean_class_dist[class];
      assert (mean_class_sum > committee_size * 0.999);
      assert (mean_class_sum < committee_size * 1.001);
      for (class = 0; class < num_classes; class++)
	mean_class_dist[class] /= mean_class_sum;

      /* Set WEIGHT to KL-divergence-to-the-mean averaged over all 
	 committee members. */
      scores[k].weight = 0;
      for (committee = 0; committee < committee_size; committee++)
	{
	  for (si = 0; si < bow_barrel_num_classes (doc_barrel); si++)
	    {
	      class = scores[k].scores[committee][si].di;
	      if (1e-100 < nb_scores[committee][class])
		{
		  scores[k].weight -= 
		    ((1.0 / committee_size)
		     /* scale by kl-div of this document to this class */
		     * nb_scores[committee][class]
		     * log (mean_class_dist[class]
			    / nb_scores[committee][class]));
		}
	    }
	}

      /* Scale the score by the document density. */
      scores[k].weight *= cdoc->prior;

      /* KL divergence must be greater than or equal to 0 */
      if (scores[k].weight < -0.1)
	bow_error("scores[k].weight <  -0.1: %.20f", scores[k].weight);

    }

  /* Sort based on weight */
  qsort (scores, total_unknown, sizeof (active_scores),
	 active_scores_compare);
  
  /* Change doc types of those with highest entropy*/
  for (k = 0; k < num_to_add ; k++)
    {
      bow_cdoc *doc;
      doc = bow_cdocs_di2doc (doc_barrel->cdocs, scores[k].di);
      assert (doc);
      assert (doc->type == bow_doc_unlabeled);
      bow_verbosify (bow_progress, "Labeling %s, weight %f", doc->filename,
		     scores[k].weight);
      for (committee=0; committee < committee_size; committee++)
	bow_verbosify(bow_progress, " [(%d, %f) (%d, %f)]", 
		      scores[k].scores[committee][0].di,
		      scores[k].scores[committee][0].weight,
		      scores[k].scores[committee][1].di,
		      scores[k].scores[committee][1].weight);
      bow_verbosify(bow_progress, "\n");
      doc->type = bow_doc_train;
    }
  
  return;
}


/* select docs with the highest vote entropy (Dagan and Engelson) */
void
active_select_vote_entropy (bow_barrel *doc_barrel, active_scores *scores,  
			    int num_to_add, int total_unknown, int committee_size)
{
  int num_classes = bow_barrel_num_classes (doc_barrel);
  double *mean_class_dist;
  double mean_class_sum;
  int committee;
  int class;
  int k;
  int si;

  mean_class_dist = alloca (sizeof (double) * num_classes);

  /* Calculate the entropy of the class labels, H(Class|d,Committee),
     where Class and Committee are random varibles, and put this in
     SCORES->WEIGHT. */
  for (k = 0; k < total_unknown; k++)
    {
      scores[k].weight = 0;

      /* Initialize the scores to be 'votes' */
      for (committee = 0; committee < committee_size; committee++)
	{
	  scores[k].scores[committee][0].weight = 1.0;
	  for (si = 1; si < num_classes; si++)
	    scores[k].scores[committee][si].weight = 0.0;
	}

      /* Initialize the mean class distribution for this document. */
      for (class = 0; class < num_classes; class++)
	mean_class_dist[class] = 0;
      for (committee = 0; committee < committee_size; committee++)
	for (class = 0; class < num_classes; class++)
	  mean_class_dist[scores[k].scores[committee][class].di]
	    += scores[k].scores[committee][class].weight;
      mean_class_sum = 0;
      for (class = 0; class < num_classes; class++)
	mean_class_sum += mean_class_dist[class];
      assert (mean_class_sum > committee_size * 0.999);
      assert (mean_class_sum < committee_size * 1.001);
      for (class = 0; class < num_classes; class++)
	mean_class_dist[class] /= mean_class_sum;

      /* Calculate the entropy of the mean class distribution */
      for (class = 0; class < bow_barrel_num_classes (doc_barrel); class++)
	{
	  if (1e-100 <  mean_class_dist[class])
	    {
	      scores[k].weight -= 
		(mean_class_dist[class]
		 * log (mean_class_dist[class]));
	    }
	}

      /* Entropy must be greater than or equal to 0 */
      if (scores[k].weight < -0.1)
	bow_error("scores[k].weight <  -0.1: %.20f", scores[k].weight);
      
    }

  /* Sort based on weight */
  qsort (scores, total_unknown, sizeof (active_scores),
	 active_scores_compare);
  
  /* Change doc types of those with highest entropy*/
  for (k = 0; k < num_to_add; )
    {
      int z;
      double top_score;
      int n;
      int j;

      /* find how many top ranked docs have same score */
      top_score = scores[k].weight;
      for (z=k; z < total_unknown && scores[z].weight == top_score ; z++);

      /* add all with top score if won't max it out */
      if (z < num_to_add)
	{
	  for (n=k; n<z; n++, k++)
	    {
	      bow_cdoc *doc;
	      doc = bow_cdocs_di2doc (doc_barrel->cdocs, scores[k].di);
	      assert (doc);
	      assert (doc->type == bow_doc_unlabeled);
	      bow_verbosify (bow_progress, "Labeling %s, weight %f", doc->filename,
			     scores[n].weight);
	      for (committee=0; committee < committee_size; committee++)
		bow_verbosify(bow_progress, " %d", 
			      scores[n].scores[committee][0].di);
	      bow_verbosify(bow_progress, "\n");
	      doc->type = bow_doc_train;
	    }
	}
      else
	{
	  /* need to randomly select some of the docs for labeling */
	  for (j=0, n=k; n < num_to_add; j++)
	    {
	      int si = (rand() % (z-k)) + k;
	      int doci;
	      bow_cdoc *doc;
	      
	      doci = scores[si].di;
	      doc = bow_cdocs_di2doc (doc_barrel->cdocs, doci);
	      assert (doc);
	      if (doc->type == bow_doc_unlabeled)
		{
		  doc->type = bow_doc_train;
		  bow_verbosify (bow_progress, "Labeling %s, weight %f", doc->filename,
				 scores[si].weight);
		  for (committee=0; committee < committee_size; committee++)
		    bow_verbosify(bow_progress, " %d", 
				  scores[si].scores[committee][0].di);
		  bow_verbosify(bow_progress, "\n");
		  n++;
		}
	      if (j > doc_barrel->cdocs->length * 1000)
		bow_error ("Random number generator could not find enough "
			   "unlabeled documents to convert.");
	    }
	  return;
	}
    }
  return;
}






void
active_select_uncertain (bow_barrel *doc_barrel, active_scores *scores,  
			 int num_to_add, int total_unknown,
			 int committee_size)
{
  int k;
  
  assert(num_to_add <= total_unknown);
  assert(committee_size == 1);

  /* Make smallest top classification better */
  for (k=0; k < total_unknown; k++)
    {
      scores[k].weight = -1 * scores[k].scores[0][0].weight;
    }

  /* sort based on weight */
  qsort(scores, total_unknown, sizeof (active_scores),
	active_scores_compare);

  /* change doc types */
  for (k=0; k < num_to_add; k++)
    {
      bow_cdoc *doc;

      doc = bow_cdocs_di2doc (doc_barrel->cdocs, scores[k].di);

      assert(doc);
      assert(doc->type == bow_doc_unlabeled);
      bow_verbosify(bow_progress, "Labeling %s\n", doc->filename);

      doc->type = bow_doc_train;
    }
  return;
 }



void
active_select_relevant (bow_barrel *doc_barrel, active_scores *scores,  
			int num_to_add, int total_unknown,
			int committee_size)
{
  int k;

  assert(num_to_add <= total_unknown);
  assert(committee_size == 1);

  for (k=0; k < total_unknown; k++)
    {
      scores[k].weight = -1 *scores[k].scores[0][0].weight;
    }
	  
  /* sort based on weight */
  qsort(scores, total_unknown, sizeof (active_scores),
	active_scores_compare);

  /* change doc types */
  for (k = total_unknown - num_to_add; k < total_unknown; k++)
    {
      bow_cdoc *doc = bow_cdocs_di2doc (doc_barrel->cdocs, scores[k].di);

      assert(doc);
      assert(doc->type == bow_doc_unlabeled);
      bow_verbosify(bow_progress, "Labeling %s\n", doc->filename);

      doc->type = bow_doc_train;
    }
  return;
}



void active_select_length(bow_barrel *doc_barrel, active_scores *scores,  
			   int num_to_add, int total_unknown, int committee_size)
{
  int k;
  
  assert(num_to_add <= total_unknown);

  /* set weight to the document length */
  for (k=0; k < total_unknown; k++)
    {
      bow_cdoc *cdoc = bow_cdocs_di2doc (doc_barrel->cdocs, scores[k].di);

      scores[k].weight = cdoc->word_count;
    }

   /* sort based on weight */
  qsort(scores, total_unknown, sizeof (active_scores),
	active_scores_compare);

  /* change doc types */
  for (k = 0 ;  k < num_to_add; k++)
    {
      bow_cdoc *doc = bow_cdocs_di2doc (doc_barrel->cdocs, scores[k].di);
      
      assert(doc);
      assert(doc->type == bow_doc_unlabeled);
      bow_verbosify(bow_progress, "Labeling %s, weight %f\n", doc->filename,
		    scores[k].weight);
      doc->type = bow_doc_train;
    }

  return;
}



void
active_select_random (bow_barrel *doc_barrel, active_scores *scores,  
		      int num_to_add, int total_unknown,
		      int committee_size)
{
  int j;
  int k;
  bow_cdoc *doc;

  assert(num_to_add <= total_unknown);

  for (j=0, k=0; k < num_to_add; j++)
    {
      int scoresi = rand() % total_unknown;
      int doci;

      doci = scores[scoresi].di;
      doc = bow_cdocs_di2doc (doc_barrel->cdocs, doci);
      assert (doc);
      if (doc->type == bow_doc_unlabeled)
	{
	  doc->type = bow_doc_train;
	  bow_verbosify(bow_progress, "Labeling %s\n", doc->filename);
	  k++;
	}
      if (j > doc_barrel->cdocs->length * 1000)
	bow_error ("Random number generator could not find enough "
		   "unlabeled documents to convert.");
    }
  return;
}

void
active_select_stream_ve (bow_barrel *doc_barrel, active_scores *scores,  
			 int num_to_add, int total_unknown,
			 int committee_size)
{
  int j;
  bow_cdoc *doc;
    int num_classes = bow_barrel_num_classes (doc_barrel);
  double *mean_class_dist;
  double mean_class_sum;
  int committee;
  int class;
  int k;
  int si;

  assert(num_to_add <= total_unknown);

  mean_class_dist = alloca (sizeof (double) * num_classes);

  /* Calculate the entropy of the class labels, H(Class|d,Committee),
     where Class and Committee are random varibles, and put this in
     SCORES->WEIGHT. */
  for (k = 0; k < total_unknown; k++)
    {
      scores[k].weight = 0;

      /* Initialize the scores to be 'votes' */
      for (committee = 0; committee < committee_size; committee++)
	{
	  scores[k].scores[committee][0].weight = 1.0;
	  for (si = 1; si < num_classes; si++)
	    scores[k].scores[committee][si].weight = 0.0;
	}

      /* Initialize the mean class distribution for this document. */
      for (class = 0; class < num_classes; class++)
	mean_class_dist[class] = 0;
      for (committee = 0; committee < committee_size; committee++)
	for (class = 0; class < num_classes; class++)
	  mean_class_dist[scores[k].scores[committee][class].di]
	    += scores[k].scores[committee][class].weight;
      mean_class_sum = 0;
      for (class = 0; class < num_classes; class++)
	mean_class_sum += mean_class_dist[class];
      assert (mean_class_sum > committee_size * 0.999);
      assert (mean_class_sum < committee_size * 1.001);
      for (class = 0; class < num_classes; class++)
	mean_class_dist[class] /= mean_class_sum;

      /* Calculate the entropy of the mean class distribution */
      for (class = 0; class < bow_barrel_num_classes (doc_barrel); class++)
	{
	  if (1e-100 <  mean_class_dist[class])
	    {
	      scores[k].weight -= 
		(mean_class_dist[class]
		 * log (mean_class_dist[class]));
	    }
	}

      /* adjust for the correct log factor */
      scores[k].weight *= 1 / log(2);
     
      /* convert to a probability */
      scores[k].weight /= log (bow_barrel_num_classes (doc_barrel)) / log(2);

      /* multiply in the epsilon factor */
      scores[k].weight *= active_stream_epsilon;

      /* Entropy must be greater than or equal to 0 */
      if (scores[k].weight < -0.1)
	bow_error("scores[k].weight <  -0.1: %.20f", scores[k].weight);
      
    }

  /* select some documents randomly according to the weights */

  for (j=0, k=0; k < num_to_add; j++)
    {
      int scoresi = rand() % total_unknown;
      int doci;
      double coin_flip;

      doci = scores[scoresi].di;
      doc = bow_cdocs_di2doc (doc_barrel->cdocs, doci);
      assert (doc);

      if (doc->type == bow_doc_unlabeled)
	{
	  coin_flip = bow_random_double (0,1);
	  if (scores[scoresi].weight > coin_flip)
	    {
	      doc->type = bow_doc_train;
	      k++;
	      bow_verbosify (bow_progress, "Labeling %s, weight %f, flip %f", doc->filename,
			     scores[scoresi].weight, coin_flip);
	      for (committee=0; committee < committee_size; committee++)
		bow_verbosify(bow_progress, " [(%d, %f) (%d, %f)]", 
			      scores[scoresi].scores[committee][0].di,
			      scores[scoresi].scores[committee][0].weight,
			      scores[scoresi].scores[committee][1].di,
			      scores[scoresi].scores[committee][1].weight);
	      bow_verbosify(bow_progress, "\n");
	    }
	}

      if (j > doc_barrel->cdocs->length * 1000)
	bow_error ("Random number generator could not find enough "
		   "unlabeled documents to convert.");
    }
  return;
}


void
active_select_stream_kl (bow_barrel *doc_barrel, active_scores *scores,  
			 int num_to_add, int total_unknown,
			 int committee_size)
{
  int j;
  bow_cdoc *doc;
  int num_classes = bow_barrel_num_classes (doc_barrel);
  double *mean_class_dist;
  double mean_class_sum;
  int committee;
  int class;
  int k;

  mean_class_dist = alloca (sizeof (double) * num_classes);

  assert(num_to_add <= total_unknown);

  /* ensures our max-kl for probability mapping is correct */
  assert(bow_barrel_num_classes(doc_barrel) >= committee_size);

  /* Calculate the entropy of the class labels, H(Class|d,Committee),
     where Class and Committee are random varibles, and put this in
     SCORES->WEIGHT. */
  for (k = 0; k < total_unknown; k++)
    {
      scores[k].weight = 0;

      /* Initialize the mean class distribution for this document. */
      for (class = 0; class < num_classes; class++)
	mean_class_dist[class] = 0;
      for (committee = 0; committee < committee_size; committee++)
	for (class = 0; class < num_classes; class++)
	  mean_class_dist[scores[k].scores[committee][class].di]
	    += scores[k].scores[committee][class].weight;
      mean_class_sum = 0;
      for (class = 0; class < num_classes; class++)
	mean_class_sum += mean_class_dist[class];
      assert (mean_class_sum > committee_size * 0.999);
      assert (mean_class_sum < committee_size * 1.001);
      for (class = 0; class < num_classes; class++)
	mean_class_dist[class] /= mean_class_sum;

      /* Set WEIGHT to KL-divergence-to-the-mean averaged over all 
	 committee members. */
      for (committee = 0; committee < committee_size; committee++)
	{
	  for (class = 0; class < bow_barrel_num_classes (doc_barrel); class++)
	    {
	      if (1e-100 <  scores[k].scores[committee][class].weight)
		{
		  scores[k].weight -= 
		    ((1.0 / committee_size)
		     * scores[k].scores[committee][class].weight
		     * log (mean_class_dist[scores[k].scores[committee][class].di]
			    / scores[k].scores[committee][class].weight));
		  if (scores[k].weight < -0.1)
		    bow_error("scores[k].weight <  -0.1: %.20f, %.20f", scores[k].weight,
			      log (mean_class_dist[scores[k].scores[committee][class].di]
				   / scores[k].scores[committee][class].weight));
		}
	    }
	}

      /* KL divergence must be greater than or equal to 0 */
      if (scores[k].weight < -0.1)
	bow_error("scores[k].weight <  -0.1: %.20f", scores[k].weight);
 
      /* adjust for the correct log factor */
      scores[k].weight *= 1.0 / log(2);
      
      /* convert to a probability by scaling with the max kl-to-the-mean */
      scores[k].weight /= -1.0 * log (1.0 / (double) committee_size) / log(2);
  
      /* multiply in the epsilon factor */
      scores[k].weight *= active_stream_epsilon;
    }

  /* select some documents randomly according to the weights */

  for (j=0, k=0; k < num_to_add; j++)
    {
      int scoresi = rand() % total_unknown;
      int doci;
      double coin_flip;

      doci = scores[scoresi].di;
      doc = bow_cdocs_di2doc (doc_barrel->cdocs, doci);
      assert (doc);

      if (doc->type == bow_doc_unlabeled)
	{
	  coin_flip = bow_random_double (0,1);
	  if (scores[scoresi].weight > coin_flip)
	    {
	      doc->type = bow_doc_train;
	      k++;
	      bow_verbosify (bow_progress, "Labeling %s, weight %f, flip %f", doc->filename,
			     scores[scoresi].weight, coin_flip);
	      for (committee=0; committee < committee_size; committee++)
		bow_verbosify(bow_progress, " [(%d, %f) (%d, %f)]", 
			      scores[scoresi].scores[committee][0].di,
			      scores[scoresi].scores[committee][0].weight,
			      scores[scoresi].scores[committee][1].di,
			      scores[scoresi].scores[committee][1].weight);
	      bow_verbosify(bow_progress, "\n");
	    }
	}

      if (j > doc_barrel->cdocs->length * 1000)
	bow_error ("Random number generator could not find enough "
		   "unlabeled documents to convert.");
    }
  return;
}


/* Functions for calculating document density. */


int
active_cdoc_is_used_for_density (bow_cdoc *cdoc)
{
  return ((cdoc->type == bow_doc_train) ||
	  (cdoc->type == bow_doc_unlabeled) ||
	  (cdoc->type == bow_doc_pool) ||
	  (cdoc->type == bow_doc_waiting));
}

/* Given a document barrel, set the CDOC->NORMALIZER to the document
   word entropy.  Return the sum of the background cross entropies of
   all the documents.  Assumes that IDF has already been set to Pr(w) */
double
active_doc_barrel_set_entropy (bow_barrel *barrel)
{
  bow_wv *wv;
  bow_dv_heap *heap;
  int wvi;
  double pr_w_d;
  double entropy;
  double entropy_sum = 0;
  double total_background_kl = 0;
  int di;
  bow_cdoc *cdoc;
  bow_dv *dv;
  double word_kl;

  heap = bow_test_new_heap (barrel);

  /* xxx Make sure to update CDOC->WORD_COUNT for a new vocabulary! */

  while ((di = bow_heap_next_wv (heap, barrel, &wv, active_cdoc_is_used_for_density)) != -1)
    {
      cdoc = bow_array_entry_at_index (barrel->cdocs, di);
      entropy = 0;
      for (wvi = 0; wvi < wv->num_entries; wvi++)
	{
	  pr_w_d = ((double)wv->entry[wvi].count) / cdoc->word_count;
	  entropy -= pr_w_d * log (pr_w_d);
	  dv = bow_wi2dvf_dv (barrel->wi2dvf, wv->entry[wvi].wi);
	  word_kl = (- pr_w_d * log ((1 - active_alpha) * dv->idf));
	  assert (word_kl >= 0);
	  total_background_kl += word_kl;
	}
      total_background_kl -= entropy;
      cdoc->normalizer = entropy;
      entropy_sum += entropy;
    }
  return total_background_kl;
}

/* Given a document barrel, set the WI2DVF->IDF to Pr(w) */
void
active_doc_barrel_set_pr_w (bow_barrel *barrel)
{
  int wi, max_wi, dvi;
  int total_num_words;
  bow_dv *dv;

  max_wi = MIN (barrel->wi2dvf->size, bow_num_words ());
  total_num_words = 0;
  for (wi = 0; wi < max_wi; wi++)
    {
      dv = bow_wi2dvf_dv (barrel->wi2dvf, wi);
      if (!dv)
	continue;

      dv->idf = 0;
      for (dvi = 0; dvi < dv->length; dvi++)
	{
	  dv->idf += dv->entry[dvi].count;
	  total_num_words += dv->entry[dvi].count;
	}
    }
  for (wi = 0; wi < max_wi; wi++)
    {
      dv = bow_wi2dvf_dv (barrel->wi2dvf, wi);
      if (!dv)
	continue;
      dv->idf /= total_num_words;
    }
}

/* Return the density of document WV, calculated using a KL divergence
   distance to all other documents. */
float
active_wv_density (bow_wv *wv, bow_barrel *barrel, 
		   float background_kl)
{
  int wvi;
  /* bow_bitvec *document_touched = bow_bitvec_new (1, barrel->cdocs->length); */
  double pr_w_d;
  double pr_w_wv;
  double pr_w_wv_missing;
  double total_kl;		/* sum of KL divergence to all other docs */
  bow_dv *dv;
  int dvi;
  bow_cdoc *cdoc;

  /* Set to background KL, that a document with no words would have. */
  pr_w_wv_missing = 1.0 / (wv->num_entries + barrel->wi2dvf->num_words);
  /*total_kl = - barrel->cdocs->length * log (pr_w_wv_missing);*/
  total_kl = background_kl;

  assert (total_kl == total_kl);
	  
  for (wvi = 0; wvi < wv->num_entries; wvi++)
    {
      dv = bow_wi2dvf_dv (barrel->wi2dvf, wv->entry[wvi].wi);
      if (!dv)
	continue;

      pr_w_wv = ((active_alpha *
		  ((double)wv->entry[wvi].count) / wv->num_entries)
		 + ((1 - active_alpha) * dv->idf));

      for (dvi = 0; dvi < dv->length; dvi++)
	{
	  cdoc = bow_array_entry_at_index (barrel->cdocs, dv->entry[dvi].di);
	  pr_w_d = ((double)dv->entry[dvi].count) / cdoc->word_count;

	  /* Remove from the total what we said its contribution would
	     be above in the background calculation. */
	  /*total_kl += pr_w_d * log (pr_w_wv_missing);*/
	  dv = bow_wi2dvf_dv (barrel->wi2dvf, wv->entry[wvi].wi);
	  total_kl += pr_w_d * log ((1 - active_alpha) * dv->idf);

	  assert (total_kl == total_kl);

	  /* Add in the true contribution */
	  total_kl -= pr_w_d * log (pr_w_wv);

	  assert (total_kl == total_kl);
	}
    }
  return total_kl;
}



/* Given a document barrel, set the CDOC->PRIOR to the document
   density, using a KL divergence distance to all other
   documents. Uses train and unlabeled documents.  Also sets the
   CDOC->NORMALIZER to the document entropy */
void
active_doc_barrel_set_density (bow_barrel *barrel)
{
  bow_dv_heap *heap;
  int di;
  bow_wv *wv;
  bow_cdoc *cdoc;

  double background_kl;

  active_doc_barrel_set_pr_w (barrel);
  background_kl = active_doc_barrel_set_entropy (barrel);

  heap = bow_test_new_heap (barrel);
  while ((di = bow_heap_next_wv (heap, barrel, &wv, active_cdoc_is_used_for_density)) != -1)
    {
      cdoc = bow_array_entry_at_index (barrel->cdocs, di);
      cdoc->prior = active_wv_density (wv, barrel, background_kl);
      cdoc->prior = exp (- active_beta * cdoc->prior / barrel->cdocs->length);
      /*      printf ("%10g %s\n", cdoc->prior, cdoc->filename); */
    }
}



/* Create a class barrel using active learning */
bow_barrel *
active_learn (bow_barrel *doc_barrel)
{
  bow_barrel *vpc_barrel = NULL;  /* the vector-per-class barrel */
  int max_ci;
  int ci;
  int di;
  int mi;
  int round_num;
  int actual_num_hits;
  int num_unlabeled_docs = 0;
  int orig_num_unlabeled_docs;
  bow_dv_heap *test_heap;	/* we'll extract test WV's from here */
  bow_wv *query_wv;
  active_scores *scores;
  bow_cdoc *doc_cdoc;
  bow_cdoc *class_cdoc;
  rainbow_method *secondary_method;

  /* Set the CDOC->PRIOR to the "density" value. */
  if (active_selection_method == dkl)
    active_doc_barrel_set_density (doc_barrel);

  /* initialize variables */
  max_ci = bow_barrel_num_classes(doc_barrel);
  secondary_method = (rainbow_method*)
    bow_method_at_name (active_secondary_method);

  /* change all but vpc_with_weights */
  doc_barrel->method->set_weights = secondary_method->set_weights;
  doc_barrel->method->scale_weights = secondary_method->scale_weights;
  doc_barrel->method->normalize_weights = secondary_method->normalize_weights;
  doc_barrel->method->vpc_set_priors = secondary_method->vpc_set_priors;
  doc_barrel->method->score = secondary_method->score;
  doc_barrel->method->wv_set_weights = secondary_method->wv_set_weights;
  doc_barrel->method->wv_normalize_weights = 
    secondary_method->wv_normalize_weights;
  doc_barrel->method->free_barrel = secondary_method->free_barrel;
  doc_barrel->method->params = secondary_method->params;

  /* find the binary positive class, if needed */
  if (active_binary_pos_classname != NULL)
    {
      assert(bow_barrel_num_classes(doc_barrel) == 2);

      for (ci = 0; ci < bow_barrel_num_classes(doc_barrel); ci++)
	{
	  if (!strcmp(active_binary_pos_classname, 
		      filename_to_classname
		      (bow_barrel_classname_at_index (doc_barrel, ci))))
	    {
	      active_binary_pos_ci = ci;
	      break;
	    }
	}
     
      if (active_binary_pos_ci == -1)
	{
	  bow_error("No such class %s.", active_binary_pos_classname);
	}
    }

  /* print out the model docs */
  for (di=0; di < doc_barrel->cdocs->length; di++)
    {
      bow_cdoc *cdoc = bow_array_entry_at_index (doc_barrel->cdocs, di);
      
      if (cdoc->type == bow_doc_train)
	bow_verbosify (bow_progress, "Initial %s\n", cdoc->filename);
    }

  /* count the number of unlabeled docs */
  for (di=0; di < doc_barrel->cdocs->length; di++)
    {
      bow_cdoc *cdoc = bow_array_entry_at_index (doc_barrel->cdocs, di);
      
      if (cdoc->type == bow_doc_unlabeled)
	num_unlabeled_docs++;
    }

  orig_num_unlabeled_docs = num_unlabeled_docs;

  /* allocate the correct amount of space for unlabeled scoring */
  scores = bow_malloc (sizeof(active_scores) * num_unlabeled_docs);
  for (di = 0; di < num_unlabeled_docs; di++)
    {
      scores[di].scores = bow_malloc (sizeof(bow_score *) * active_committee_size);
      for (mi = 0; mi < active_committee_size; mi++)
	{
	  scores[di].scores[mi] = bow_malloc (sizeof (bow_score) * 
					      bow_barrel_num_classes(doc_barrel));
	}
    }

  /* make the class barrel */
  vpc_barrel =   bow_barrel_new (doc_barrel->wi2dvf->size,
				 doc_barrel->cdocs->length-1,
				 doc_barrel->cdocs->entry_size,
				 doc_barrel->cdocs->free_func); 
  vpc_barrel->method = doc_barrel->method;
  vpc_barrel->classnames = bow_int4str_new (0);

  /* And, we're off */
  for (round_num = 0; round_num < active_num_rounds; round_num++)
    {
      int hiti = 0;

      /* Re-create the vector-per-class barrel in accordance with the
	 new train/test settings. */

      /* if we're pruning the vocab, do that now - fix for unlabeled percent */
      if (bow_prune_vocab_by_infogain_n)
	{
	  /* Change barrel by removing words with small info gain, if requested. */
	  
	  bow_barrel_keep_top_words_by_infogain
	    (bow_prune_vocab_by_infogain_n, doc_barrel, 
	     bow_barrel_num_classes (doc_barrel));
	}
      
      /* Set word_count set correctly and set the entropy of each document
	 in the normalizer of the cdoc; do this after all vocab changing */
      {
	query_wv = NULL;
	
	/* Create the heap from which we'll get WV's. */
	test_heap = bow_test_new_heap (doc_barrel);
	
	/* Loop once for each document. */
	while ((di = bow_heap_next_wv (test_heap, doc_barrel, &query_wv,
				       bow_cdoc_yes)) != -1)
	  {
	    int word_count = 0;
	    int wvi;
	    
	    doc_cdoc = bow_array_entry_at_index (doc_barrel->cdocs, 
						 di);
	    bow_wv_set_weights (query_wv, vpc_barrel);
	    bow_wv_normalize_weights (query_wv, vpc_barrel);
	    
	    for (wvi = 0; wvi < query_wv->num_entries; wvi++)
	      {
		word_count += query_wv->entry[wvi].count;
	      }
	    
	    doc_cdoc->word_count = word_count;
	    doc_cdoc->normalizer = active_document_entropy(query_wv);
	  }
      }

      /* generate test stats for the step in active learning */
      if (active_test_stats)
	{
	  bow_em_perturb_method reset_perturb_start = -1;
	  int reset_num_em_runs = -1;
	  int reset_em_cross_entropy = -1;
	  
	  /* turn variance off for test stats */
	  if (bow_em_perturb_starting_point != bow_em_perturb_none)
	    {
	      reset_perturb_start = bow_em_perturb_starting_point;
	      bow_em_perturb_starting_point = bow_em_perturb_none;
	    }

	  /* make a real number of EM rounds just for printing tests */
	  if (active_final_em)
	    {
	      reset_num_em_runs = bow_em_num_em_runs;
	      bow_em_num_em_runs = 7;
	    }

	  /* Do no EM for stats-reporting */
	  if (active_no_final_em)
	    {
	      reset_num_em_runs = bow_em_num_em_runs;
	      bow_em_num_em_runs = 1;
	    }

	  /* turn cross entropy off if just testing docs */
	  if (active_selection_method == wkl
	      || active_selection_method == dkl)
	    {
	      reset_em_cross_entropy = em_cross_entropy;
	      em_cross_entropy = 0;
	    }

	  if (vpc_barrel != NULL)
	    bow_free_barrel (vpc_barrel);
	  vpc_barrel = 
	    (*(secondary_method->vpc_with_weights))(doc_barrel);

	  active_test(stdout, doc_barrel, vpc_barrel);
	  
	  /* turn variance back on for committee members */
	  if (reset_perturb_start != -1)
	    bow_em_perturb_starting_point = reset_perturb_start;

	  /* turn EM off for committee members */
	  if (reset_num_em_runs != -1)
	    bow_em_num_em_runs = reset_num_em_runs;

	  /* turn cross entropy back on if necessary */
	  if (reset_em_cross_entropy != -1)
	    em_cross_entropy = reset_em_cross_entropy;
	}
      
      if (active_perturb_after_em)
	{
	  if (vpc_barrel)
	    bow_barrel_free(vpc_barrel);
	  vpc_barrel = 
	    (*(secondary_method->vpc_with_weights))(doc_barrel);
	}

      for (mi = 0; mi < active_committee_size; mi++)
	{
	  bow_barrel *comm_barrel = NULL;
	  
	  hiti = 0;
	  
	  if (active_perturb_after_em)
	    {
	      comm_barrel = bow_barrel_copy(vpc_barrel);
	      bow_em_perturb_starting_point = bow_em_perturb_with_dirichlet;
	      bow_em_perturb_weights(comm_barrel, doc_barrel);
	      bow_em_perturb_starting_point = bow_em_perturb_none;
	    }
	  else
	    {
	      comm_barrel = 
		(*(secondary_method->vpc_with_weights))(doc_barrel);
	    }
	  	
	  if (active_print_committee_matrices)
	    {
	      active_test(stdout, doc_barrel, comm_barrel);
	    }

	  /* score all the unlabeled docs */
	  
	  /* Create the heap from which we'll get WV's. */
	  test_heap = bow_test_new_heap (doc_barrel);
	  
	  /* Initialize QUERY_WV so BOW_TEST_NEXT_WV() knows not to try to free */
	  query_wv = NULL;
	  
	  /* Loop once for each unlabeled document. */
	  while ((di = bow_heap_next_wv (test_heap, doc_barrel, &query_wv,
					 bow_cdoc_is_unlabeled))
		 != -1)
	    {
	      doc_cdoc = bow_array_entry_at_index (doc_barrel->cdocs, 
						   di);
	      class_cdoc = bow_array_entry_at_index (comm_barrel->cdocs, 
						     doc_cdoc->class);
	      bow_wv_set_weights (query_wv, comm_barrel);
	      bow_wv_normalize_weights (query_wv, comm_barrel);
	      actual_num_hits = 
		bow_barrel_score (comm_barrel, 
				  query_wv, scores[hiti].scores[mi],
				  bow_barrel_num_classes(comm_barrel), -1);
	      assert (actual_num_hits == bow_barrel_num_classes(comm_barrel));

	      if (mi == 0)
		scores[hiti].di = di;
	      else
		assert (di == scores[hiti].di);

	      hiti++;
	    }

	  bow_barrel_free (comm_barrel);
	}

      num_unlabeled_docs = hiti;

      /* remap the scores if desired */
      if (active_remap_scores_pr)
	active_remap_scores(doc_barrel, scores,
			    num_unlabeled_docs, active_committee_size);

      /* choose docs to convert to model */
      active_select_docs(doc_barrel, scores, 
			 active_add_per_round, num_unlabeled_docs, 
			 active_committee_size);

   }

  /* turn off perturbing for building final barrel */
  if (bow_em_perturb_starting_point != bow_em_perturb_none)
    {
      bow_em_perturb_starting_point = bow_em_perturb_none;
    }

  /* make a real number of EM rounds if final em run */
  if (active_final_em)
    {
      bow_em_num_em_runs = 7;
    }

  /* Do no EM for stats-reporting */
  if (active_no_final_em)
    {
      bow_em_num_em_runs = 1;
    }
  
  /* turn cross entropy off if just testing docs */
  if (active_selection_method == wkl
      || active_selection_method == dkl)
    {
      em_cross_entropy = 0;
    }

  if (vpc_barrel != NULL)
    bow_free_barrel(vpc_barrel);
  vpc_barrel = 
    (*(secondary_method->vpc_with_weights))(doc_barrel);

  /* free scores */
  for (di = 0; di < orig_num_unlabeled_docs ; di++)
    {
      for (mi = 0; mi < active_committee_size; mi ++)
	{
	  bow_free (scores[di].scores[mi]);
	}
      bow_free(scores[di].scores);
    }
  bow_free(scores);

  return vpc_barrel;
}

void
active_undef_prior (bow_barrel *vpc_barrel,
		    bow_barrel *doc_barrel)
{
  bow_error("Active priors depends on secondary method.");
  return;
}

int
active_undef_score (bow_barrel *barrel, bow_wv *query_wv, 
		    bow_score *bscores, int bscores_len,
		    int loo_class)
{
  bow_error("Active scoring depends on secondary method.");
  return -1;
}



/* Run test trials, outputing results to TEST_FP.  The results are
   indended to be read and processed by the Perl script
   ./rainbow-stats. */
void
active_test (FILE *test_fp, bow_barrel *rainbow_doc_barrel,
		    bow_barrel *rainbow_class_barrel)
{
  bow_dv_heap *test_heap;	/* we'll extract test WV's from here */
  bow_wv *query_wv;
  int di;			/* a document index */
  bow_score *hits = NULL;
  int num_hits_to_retrieve=0;
  int actual_num_hits;
  int hi;			/* hit index */
  bow_cdoc *doc_cdoc;
  bow_cdoc *class_cdoc;

  fprintf (test_fp, "#0\n");

  num_hits_to_retrieve = bow_barrel_num_classes (rainbow_class_barrel);
  hits = alloca (sizeof (bow_score) * num_hits_to_retrieve);

  /* Create the heap from which we'll get WV's. */
  test_heap = bow_test_new_heap (rainbow_doc_barrel);

  /* Initialize QUERY_WV so BOW_TEST_NEXT_WV() knows not to try to free */
  query_wv = NULL;

  /* Loop once for each test document.  NOTE: This will skip documents
     that don't have any words that are in the vocabulary. */

  while ((di = bow_heap_next_wv (test_heap, rainbow_doc_barrel, &query_wv,
				 bow_cdoc_is_test)) != -1)
    {
      doc_cdoc = bow_array_entry_at_index (rainbow_doc_barrel->cdocs, 
					   di);
      
      class_cdoc = bow_array_entry_at_index (rainbow_class_barrel->cdocs, 
					     doc_cdoc->class);
      bow_wv_set_weights (query_wv, rainbow_class_barrel);
      bow_wv_normalize_weights (query_wv, rainbow_class_barrel);
      actual_num_hits = 
	bow_barrel_score (rainbow_class_barrel, 
			  query_wv, hits,
			  num_hits_to_retrieve, -1);
      assert (actual_num_hits == num_hits_to_retrieve);
#if 0
      printf ("%8.6f %d %8.6f %8.6f %d ",
	      class_cdoc->normalizer, 
	      class_cdoc->word_count, 
	      class_cdoc->normalizer / class_cdoc->word_count, 
	      class_cdoc->prior,
	      doc_cdoc->class);
      if (hits[0].di == doc_cdoc->class)
	printf ("1\n");
      else
	printf ("0\n");
#endif
      fprintf (test_fp, "%s %s ", 
	       doc_cdoc->filename, 
	       filename_to_classname(class_cdoc->filename));
      for (hi = 0; hi < actual_num_hits; hi++)
	{
	  class_cdoc = 
	    bow_array_entry_at_index (rainbow_class_barrel->cdocs,
				      hits[hi].di);
	  fprintf (test_fp, "%s:%.*g ", 
		   bow_barrel_classname_at_index
		   (rainbow_class_barrel, hits[hi].di),
		   bow_score_print_precision,
		   hits[hi].weight);
	}
      fprintf (test_fp, "\n");
    }
}



rainbow_method bow_method_active = 
{
  "active",
  NULL, /* bow_leave_weights_alone_since_theyre_really_counts */
  0,				/* no weight scaling function */
  NULL, /* bow_barrel_normalize_weights_by_summing, */
  active_learn,
  active_undef_prior,
  active_undef_score,
  bow_wv_set_weights_to_count,
  NULL,				/* no need for extra weight normalization */
  NULL  
};

void _register_method_active () __attribute__ ((constructor));
void _register_method_active ()
{
  static int done = 0;
  if (done) 
    return;
  bow_method_register_with_name ((bow_method*)&bow_method_active,
				 "active", 
				 sizeof (rainbow_method),
				 &active_argp_child);
  bow_argp_add_child (&active_argp_child);
  done = 1;
}
