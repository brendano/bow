/* hem.c - Hierarchical Expectation Maximization
   Copyright (C) 1998, 1999 Andrew McCallum

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
#include <argp.h>
#include <bow/crossbow.h>

extern void crossbow_leaf_document_probs_print (int num_to_print);
extern void crossbow_classify_tagged_docs (int tag, int verbose,
					   FILE *out);

#define SHRINK_WITH_UNIFORM_ONLY 0
#define PRINT_WORD_DISTS 0

#define MN 0
#if MN
extern double crossbow_hem_em_one_mn_iteration ();
#endif

static int crossbow_hem_branching_factor = 2;
static double crossbow_hem_temperature = 100;
static double crossbow_hem_temperature_end = 1;
static int crossbow_hem_max_num_iterations = 9999999;
static double crossbow_hem_temperature_decay = 0.9;
static double crossbow_hem_em_acceleration = 1.0;
static double crossbow_hem_split_kl_threshold = 0.4;
static int crossbow_hem_maximum_depth = 6;
static double crossbow_hem_lambdas_from_validation = 0.0;

/* Doing statistical garbage collection? */
static int crossbow_hem_garbage_collection = 0;

/* Doing incremental labeling, ala co-training? */
static int crossbow_hem_incremental_labeling = 0;

/* Are the documents already labeled to belong to one leaf? */
int crossbow_hem_deterministic_horizontal = 0;
int crossbow_hem_restricted_horizontal = 0;

/* Doing "full-EM"?, meaning that vertical word distributions are
   changed by EM.  Note that speech recognitions's traditional 
   "deleted interpolation" only uses EM to set the lambdas.  */
int crossbow_hem_vertical_word_movement = 1;

/* Doing shrinkage */
int crossbow_hem_shrinkage = 1;

/* Using shrinkage, but with fixed weights.  Don't learn them by EM.
   Only active is crossbow_hem_shrinkage = 1 */
int crossbow_hem_fixed_shrinkage = 0;

/* Doing Leave-One-Out */
int crossbow_hem_loo = 1;

/* The class tag is part of the generative model, and should be used
   in the E-step to estimate class membership, and the M-step should
   update the class distribution in each leaf. */
int crossbow_hem_generates_class = 1;

/* If non-zero, then after the initial E-step, change all labeled 
   documents to unlabeled. */
int crossbow_hem_pseudo_labeled = 0;


/* Command-line setting routines */

enum {
  BRANCHING_FACTOR_KEY = 17000,
  TEMPERATURE_START_KEY,
  TEMPERATURE_END_KEY,
  TEMPERATURE_DECAY_KEY,
  EM_ACCELERATION_KEY,
  SPLIT_KL_THRESHOLD_KEY,
  MAXIMUM_DEPTH_KEY,
  NO_VERTICAL_WORD_MOVEMENT_KEY,
  NO_SHRINKAGE_KEY,
  NO_LOO_KEY,
  DETERMINISTIC_HORIZONTAL_KEY,
  RESTRICTED_HORIZONTAL_KEY,
  PSEUDO_LABELED_KEY,
  GARBAGE_COLLECTION_KEY,
  MAX_NUM_ITERATIONS_KEY,
  LAMBDAS_FROM_VALIDATION_KEY,
  INCREMENTAL_LABELING_KEY,
};

static struct argp_option crossbow_hem_options[] =
{
  {0, 0, 0, 0,
   "Hierarchical EM Clustering options:", 101},
  {"hem-branching-factor", BRANCHING_FACTOR_KEY, "NUM", 0,
   "Number of clusters to create.  Default is 2."},
  {"hem-temperature-start", TEMPERATURE_START_KEY, "NUM", 0,
   "The initial value of T."},
  {"hem-temperature-end", TEMPERATURE_END_KEY, "NUM", 0,
   "The final value of T.  Default is 1."},
  {"hem-max-num-iterations", MAX_NUM_ITERATIONS_KEY, "NUM", 0,
   "Do no more iterations of EM than this."},
  {"hem-temperature-decay", TEMPERATURE_DECAY_KEY, "NUM", 0,
   "Temperature decay factor.  Default is 0.9."},
  {"hem-em-acceleration", EM_ACCELERATION_KEY, "NUM", OPTION_HIDDEN,
   "Accelerated EM \eta factor.  1 is plain EM.  Can safely go "
   "as high as 2.0.  1.8 is a good value.  Default is 1."},
  {"hem-split-kl-threshold", SPLIT_KL_THRESHOLD_KEY, "NUM", 0,
   "KL divergence value at which tree leaves will be split. "
   "Default is 0.2"},
  {"hem-maximum-depth", MAXIMUM_DEPTH_KEY, "NUM", 0,
   "The hierarchy depth beyond which it will not split.  Default is 6."},
  {"hem-no-vertical-word-movement", NO_VERTICAL_WORD_MOVEMENT_KEY, 0, 0,
   "Use EM just to set the vertical priors, not to set the vertical "
   "word distribution; i.e. do not to `full-EM'."},
  {"hem-no-shrinkage", NO_SHRINKAGE_KEY, 0, 0,
   "Use only the clusters at the leaves; do not do anything with the "
   "hierarchy."},
  {"hem-no-loo", NO_LOO_KEY, 0, 0,
   "Do not use leave-one-out evaluation during the E-step."},
  {"hem-deterministic-horizontal", DETERMINISTIC_HORIZONTAL_KEY, 0, 0,
   "In the horizontal E-step for a document, set to zero the membership "
   "probabilities of all leaves, except the one matching the document's "
   "filename"},
  {"hem-restricted-horizontal", RESTRICTED_HORIZONTAL_KEY, 0, 0,
   "In the horizontal E-step for a document, set to zero the membership "
   "probabilities of all leaves whose names are not found in the document's "
   "filename"},
  {"hem-pseudo-labeled", PSEUDO_LABELED_KEY, 0, 0,
   "After using the labels to set the starting point for EM, change all "
   "training documents to unlabeled, so that they can have their class "
   "labels re-assigned by EM.  Useful for imperfectly labeled training data."},
  {"hem-garbage-collection", GARBAGE_COLLECTION_KEY, 0, 0,
   "Add extra /Misc/ children to every internal node of the hierarchy, "
   "and keep their local word distributions flat"},
  {"hem-lambdas-from-validation", LAMBDAS_FROM_VALIDATION_KEY, "NUM", 0,
   "Instead of setting the lambdas from the labeled/unlabeled data "
   "(possibly with LOO), instead set the lambdas using held-out "
   "validation data.  0<NUM<1 is the fraction of unlabeled documents "
   "just before EM training of the classifier begins.  Default is 0, "
   "which leaves this option off."},
  {"hem-incremental-labeling", INCREMENTAL_LABELING_KEY, 0, 0,
   "Instead of using all unlabeled documents in the M-step, use only "
   "the labeled documents, and incrementally label those unlabeled documents "
   "that are most confidently classified in the E-step"},

  {0, 0}
};

error_t
crossbow_hem_parse_opt (int key, char *arg, struct argp_state *state)
{
  switch (key)
    {
    case BRANCHING_FACTOR_KEY:
      crossbow_hem_branching_factor = atoi (arg);
      break;
    case TEMPERATURE_START_KEY:
      crossbow_hem_temperature = atof (arg);
      break;
    case TEMPERATURE_END_KEY:
      crossbow_hem_temperature_end = atof (arg);
      break;
    case TEMPERATURE_DECAY_KEY:
      crossbow_hem_temperature_decay = atof (arg);
      break;
    case EM_ACCELERATION_KEY:
      crossbow_hem_em_acceleration = atof (arg);
      break;
    case SPLIT_KL_THRESHOLD_KEY:
      crossbow_hem_split_kl_threshold = atof (arg);
      break;
    case MAXIMUM_DEPTH_KEY:
      crossbow_hem_maximum_depth = atoi (arg);
      break;
    case NO_VERTICAL_WORD_MOVEMENT_KEY:
      crossbow_hem_vertical_word_movement = 0;
      break;
    case NO_SHRINKAGE_KEY:
      crossbow_hem_shrinkage = 0;
      break;
    case NO_LOO_KEY:
      crossbow_hem_loo = 0;
      break;
    case RESTRICTED_HORIZONTAL_KEY:
      crossbow_hem_restricted_horizontal = 1;
      break;
    case DETERMINISTIC_HORIZONTAL_KEY:
      crossbow_hem_deterministic_horizontal = 1;
      break;
    case PSEUDO_LABELED_KEY:
      crossbow_hem_pseudo_labeled = 1;
      break;
    case GARBAGE_COLLECTION_KEY:
      crossbow_hem_garbage_collection = 1;
      break;
    case MAX_NUM_ITERATIONS_KEY:
      crossbow_hem_max_num_iterations = atoi (arg);
      break;
    case LAMBDAS_FROM_VALIDATION_KEY:
      crossbow_hem_lambdas_from_validation = atof (arg);
      break;
    case INCREMENTAL_LABELING_KEY:
      crossbow_hem_incremental_labeling = 1;
      break;
    default:
      return ARGP_ERR_UNKNOWN;
    }
  return 0;
}

static const struct argp crossbow_hem_argp =
{
  crossbow_hem_options,
  crossbow_hem_parse_opt
};

static struct argp_child crossbow_hem_argp_child =
{
  &crossbow_hem_argp,	/* This child's argp structure */
  0,			/* flags for child */
  0,			/* optional header in help message */
  0			/* arbitrary group number for ordering */
};



/* create num_children children for the leaf node tn */
void
crossbow_hem_create_children_for_node (treenode *tn, int num_children)
{
  int ci;
  treenode *child;
  int ai;
  int wi;

  assert (tn->children_count == 0);
  for (ci = 0; ci < num_children; ci++)
    {
      child = bow_treenode_new (tn, num_children, NULL);

      if (!crossbow_hem_shrinkage)
	{
	  /* if no shrinkage, set the lamdas all at the leaf */	  
	  child->new_lambdas[0] = 1.0;
	  for (ai = 1; ai < child->depth + 2; ai++)
	    child->new_lambdas[ai] = 0.0;
	  bow_treenode_set_lambdas_from_new_lambdas (child, 0);
	}
      else
	{
	  /* set the children close to parent, sharing their lambdas */
	  child->new_lambdas[0] = tn->lambdas[0]/2;
	  child->new_lambdas[1] = tn->lambdas[0]/2;
	  for (ai = 2; ai < child->depth + 2; ai++)
	    child->new_lambdas[ai] = 
	      tn->lambdas[ai-1];
	  bow_treenode_set_lambdas_from_new_lambdas (child, 0);
	}
      /* make each word distribution like parent's, but perturbed */
      for (wi = 0; wi < tn->words_capacity; wi++)
	child->words[wi] = tn->words[wi];
      /* xxx But we're going to perturb them again in hem_cluster!!! */
      bow_treenode_set_new_words_from_perturbed_words (child, 0.1);

      /* split the prior of the parent amongst the children */
      child->prior = tn->prior / num_children;
      bow_treenode_set_words_from_new_words (child, 0);
    }

  /* zero out the prior of the parent now that it's not a leaf */
  tn->prior = 0.0;
}

/* Return non-zero if a split happens */
int
crossbow_hem_hypothesize_grandchildren (treenode *tn, int num_children)
{
  int ci;
  double kldiv;
  /* The number of words of training data in the children of TN */

  assert (bow_treenode_is_leaf_parent (tn));

  kldiv = bow_treenode_children_kl_div (tn);
  if (kldiv > crossbow_hem_split_kl_threshold 
      && tn->depth < crossbow_hem_maximum_depth)
    {
      printf ("Splitting children of node %s\n", tn->name);
      
      /* Create and attach grandchildren, and copy perturbed word
         distribution. */
      for (ci = 0; ci < tn->children_count; ci++)
	{
	  crossbow_hem_create_children_for_node (tn->children[ci], 
						 num_children);
	}
      return 1;
    }
  return 0;
}

/* Return the perplexity of the data in documents for which the
   function USE_DOC_P returns non-zero. */
double
crossbow_hem_perplexity (int (*use_doc_p)(bow_doc*))
{
  int di;
  crossbow_doc *doc;
  bow_wv *wv;
  treenode *iterator, *leaf;
  int li;			/* a leaf index */
  int num_leaves;
  double *leaf_membership;
  double *leaf_data_prob;
  double log_prob_of_data = 0;
  int num_data_words = 0;	/* the number of word occurrences */

  num_leaves = bow_treenode_leaf_count (crossbow_root);
  leaf_membership = alloca (num_leaves * sizeof (double));
  leaf_data_prob = alloca (num_leaves * sizeof (double));
  for (di = 0; di < crossbow_docs->length; di++)
    {
      doc = bow_array_entry_at_index (crossbow_docs, di);
      if (! (*use_doc_p)((bow_doc*)doc))
	continue;

      /* E-step estimating leaf membership probability for one
         document, with annealing temperature. */
      wv = crossbow_wv_at_di (di);
      for (iterator = crossbow_root, li = 0;
	   (leaf = bow_treenode_iterate_leaves (&iterator)); 
	   li++)
	{
	  if (crossbow_hem_shrinkage)
	    leaf_data_prob[li] = bow_treenode_log_prob_of_wv (leaf, wv);
	  else
	    leaf_data_prob[li] = bow_treenode_log_local_prob_of_wv (leaf, wv);

	  leaf_membership[li] = (log (leaf->prior)
				 + (leaf_data_prob[li]
				    / crossbow_hem_temperature));
	}
      crossbow_convert_log_probs_to_probs (leaf_membership, num_leaves);

      /* For perplexity calculation */
      for (iterator = crossbow_root, li = 0;
	   (leaf = bow_treenode_iterate_leaves (&iterator)); 
	   li++)
	{
	  /* xxx Should this be with bow_treenode_complete_log_prob_of_wv()? */
	  log_prob_of_data += (leaf_membership[li] * leaf_data_prob[li]);
	  assert (log_prob_of_data == log_prob_of_data);
	}
      num_data_words += bow_wv_word_count (wv);
    }

  /* Return the perlexity */
  if (num_data_words)
    return exp (-log_prob_of_data / num_data_words);
  return 0;
}

/* Return the perplexity of the data (perplexity (without knowledge of
   the class label, P(D|theta)) in documents for which the function
   USE_DOC_P returns non-zero. */
double
crossbow_hem_unlabeled_perplexity (int (*use_doc_p)(bow_doc*))
{
  int di;
  crossbow_doc *doc;
  bow_wv *wv;
  treenode *iterator, *leaf;
  int li;			/* a leaf index */
  int num_leaves;
  double leaf_data_log_prob;
  double leaf_pp;
  double max_leaf_pp;
  double log_prob_of_data = 0;
  int num_data_words = 0;	/* the number of word occurrences */

  num_leaves = bow_treenode_leaf_count (crossbow_root);
  for (di = 0; di < crossbow_docs->length; di++)
    {
      doc = bow_array_entry_at_index (crossbow_docs, di);
      if (! (*use_doc_p)((bow_doc*)doc))
	continue;

      wv = crossbow_wv_at_di (di);
      max_leaf_pp = -FLT_MAX;
      for (iterator = crossbow_root, li = 0;
	   (leaf = bow_treenode_iterate_leaves (&iterator)); 
	   li++)
	{
	  if (crossbow_hem_shrinkage)
	    leaf_data_log_prob = bow_treenode_log_prob_of_wv (leaf, wv);
	  else
	    leaf_data_log_prob = bow_treenode_log_local_prob_of_wv (leaf, wv);
	  leaf_pp = log(leaf->prior) + leaf_data_log_prob;
	  assert (leaf_pp == leaf_pp);
#if 1
	  /* Test for -Inf, and if so, immediately return Inf */
	  if (leaf_pp == -HUGE_VAL)
	    return HUGE_VAL;
#endif
	  if (leaf_pp > max_leaf_pp)
	    max_leaf_pp = leaf_pp;
	}
      assert (max_leaf_pp != -FLT_MAX);
      log_prob_of_data += max_leaf_pp;
      num_data_words += bow_wv_word_count (wv);
    }

  /* Return the perlexity */
  if (num_data_words)
    return exp (-log_prob_of_data / num_data_words);
  return 0;
}

/* Return the perplexity (given knowledge of the class label,
   P(D,C|theta)) of the data in documents for which the function
   USE_DOC_P returns non-zero. */
double
crossbow_hem_labeled_perplexity (int (*use_doc_p)(bow_doc*))
{
  int di;
  crossbow_doc *doc;
  bow_wv *wv;
  treenode *leaf;
  int num_leaves;
  double leaf_data_log_prob;
  double log_prob_of_data = 0;
  int num_data_words = 0;	/* the number of word occurrences */

  num_leaves = bow_treenode_leaf_count (crossbow_root);
  for (di = 0; di < crossbow_docs->length; di++)
    {
      doc = bow_array_entry_at_index (crossbow_docs, di);
      if (! (*use_doc_p)((bow_doc*)doc))
	continue;

      wv = crossbow_wv_at_di (di);
      leaf = bow_treenode_descendant_matching_name (crossbow_root,
						    doc->filename);
      if (crossbow_hem_shrinkage)
	leaf_data_log_prob = bow_treenode_log_prob_of_wv (leaf, wv);
      else
	leaf_data_log_prob = bow_treenode_log_local_prob_of_wv (leaf, wv);
      /* Test for -Inf, and if so, immediately return Inf */
      if (leaf_data_log_prob == -HUGE_VAL)
	return HUGE_VAL;
      log_prob_of_data += (log (leaf->prior) + leaf_data_log_prob);
      assert (log_prob_of_data == log_prob_of_data);
      num_data_words += bow_wv_word_count (wv);
    }

  /* Return the perlexity */
  if (num_data_words)
    return exp (-log_prob_of_data / num_data_words);
  return 0;
}

/* Classify all unlabeled documents and convert the most confidently
   classified to labeled */
void
crossbow_hem_label_most_confident ()
{
  int di, li;
  crossbow_doc *doc;
  //  bow_wv *wv;
  bow_wa *wa;
  int word_count;
  double score;
  int leaf_count = bow_treenode_leaf_count (crossbow_root);
  bow_wa **high_scores_per_class;
  static int unlabeled_count = -1;
  static int num_to_label = 999;
  treenode *iterator, *leaf;

  assert (crossbow_hem_incremental_labeling);

  /* Calculate num_to_label if we are to label all examples in 20
     iterations. */
  if (unlabeled_count == -1)
    {
      unlabeled_count = 0;
      for (di = 0; di < crossbow_docs->length; di++)
	{
	  doc = bow_array_entry_at_index (crossbow_docs, di);
	  if (doc->tag == bow_doc_unlabeled)
	    unlabeled_count++;
	}
      num_to_label = unlabeled_count / 20;
    }

  high_scores_per_class = alloca (leaf_count * sizeof (void*));
  for (li = 0; li < leaf_count; li++)
    high_scores_per_class[li] = bow_wa_new (0);

  for (di = 0; di < crossbow_docs->length; di++)
    {
      bow_wv *wv;
      doc = bow_array_entry_at_index (crossbow_docs, di);
      if (doc->tag != bow_doc_unlabeled)
        continue;

      wv = crossbow_wv_at_di (doc->di);
      word_count = bow_wv_word_count (wv);

      wv = crossbow_wv_at_di (doc->di);
      assert (wv);
      wa = crossbow_classify_doc_new_wa (wv);

      bow_wa_sort (wa);
      score = wa->entry[0].weight;
      score /= ((word_count + 1) / MIN(9,word_count));
      bow_wa_append (high_scores_per_class[wa->entry[0].wi], di, score);
      bow_wa_free (wa);
    }

  for (iterator = crossbow_root, li = 0;
       (leaf = bow_treenode_iterate_leaves (&iterator)); 
       li++)
    {
      int i, num_to_label_this_class = MAX(1,num_to_label * leaf->prior);

      if (high_scores_per_class[li]->length == 0)
	continue;

      bow_wa_sort (high_scores_per_class[li]);
      if (num_to_label_this_class > high_scores_per_class[li]->length)
	{
	  bow_verbosify (bow_quiet,
			 "Not enough unlabeled documents classified as %s\n",
			 leaf->name);
	  num_to_label_this_class = high_scores_per_class[li]->length;
	}
      for (i = 0; i < num_to_label_this_class; i++)
	{
	  char *newname = bow_malloc (128);
	  doc = 
	    bow_array_entry_at_index (crossbow_docs, 
				      high_scores_per_class[li]->entry[i].wi);
	  assert (doc->tag = bow_doc_unlabeled);
	  doc->tag = bow_doc_train;
	  doc->ci = li;
	  /* xxx Yuck!  WhizBang-specific */
	  sprintf (newname, "./data%s%s", leaf->name, 
		   strrchr(doc->filename, '/') + 1);
	  /* xxx Memory leak here.  Free the doc->name first. */
	  doc->filename = newname;
	  bow_verbosify (bow_progress, "Labeling class %10s %35s %g\n",
			 leaf->name, doc->filename,
			 high_scores_per_class[li]->entry[i].weight);
	}
    }


  for (li = 0; li < leaf_count; li++)
    bow_wa_free (high_scores_per_class[li]);
}


#if MN
#include "mn.c"
#endif

/* Return the perplexity */
double
crossbow_hem_em_one_iteration ()
{
  int di;
  crossbow_doc *doc;
  bow_wv *wv;
  treenode *iterator, *leaf, *ancestor;
  int li;			/* a leaf index */
  int wvi;
  int num_leaves;
  double *leaf_membership;
  double *leaf_data_prob;
  double pp, log_prob_of_data = 0;
  int num_data_words = 0;	/* the number of word occurrences */
  double *ancestor_membership;
  double ancestor_membership_total;
  double total_deposit_prob;
  int found_deterministic_leaf;
  int docs_added_count = 0;

#if MN
  return crossbow_hem_em_one_mn_iteration ();
#endif

  num_leaves = bow_treenode_leaf_count (crossbow_root);
  leaf_membership = alloca (num_leaves * sizeof (double));
  leaf_data_prob = alloca (num_leaves * sizeof (double));
  /* xxx Here NUM_LEAVES+10 should be MAX_DEPTH */
  ancestor_membership = alloca ((num_leaves + 10) * sizeof (double));
  for (di = 0; di < crossbow_docs->length; di++)
    {
      total_deposit_prob = 0;

      doc = bow_array_entry_at_index (crossbow_docs, di);
      if (crossbow_hem_incremental_labeling)
	{
	  if (crossbow_hem_lambdas_from_validation)
	    {
	      if (doc->tag != bow_doc_train
		  && doc->tag != bow_doc_validation)
		continue;
	    }
	  else
	    {
	      if (doc->tag != bow_doc_train)
		continue;
	    }
	}
      else if (crossbow_hem_lambdas_from_validation)
	{
	  if (doc->tag != bow_doc_train
	      && doc->tag != bow_doc_unlabeled
	      && doc->tag != bow_doc_validation)
	    continue;
	}
      else
	{
	  if (doc->tag != bow_doc_train && doc->tag != bow_doc_unlabeled)
	    continue;
	}
      /* Temporary fix */
      if (strstr (doc->filename, ".include")
	  || strstr (doc->filename, ".exclude"))
	continue;

      /* E-step estimating leaf membership probability for one
         document, with annealing temperature. */
      wv = crossbow_wv_at_di (di);
      found_deterministic_leaf = 0;
      for (iterator = crossbow_root, li = 0;
	   (leaf = bow_treenode_iterate_leaves (&iterator)); 
	   li++)
	{
	  if (crossbow_hem_shrinkage)
	    {
	      if (crossbow_hem_loo)
		leaf_data_prob[li] = 
		  bow_treenode_log_prob_of_wv_loo (leaf, wv, di);
	      else
		leaf_data_prob[li] = bow_treenode_log_prob_of_wv (leaf, wv);
	    }
	  else
	    {
	      if (crossbow_hem_loo)
		leaf_data_prob[li] = 
		  bow_treenode_log_local_prob_of_wv_loo (leaf, wv, di);
	      else
		leaf_data_prob[li] = 
		  bow_treenode_log_local_prob_of_wv (leaf, wv);
	    }
	  assert (leaf_data_prob[li] > -HUGE_VAL);

	  if (crossbow_hem_deterministic_horizontal
	      && (doc->tag == bow_doc_train
		  || doc->tag == bow_doc_validation))
	    {
	      if (strstr (doc->filename, leaf->name))
		{
		  assert (!found_deterministic_leaf);
		  leaf_membership[li] = 1.0;
		  found_deterministic_leaf = 1;
		}
	      else
		/* The validation document was formerly an unlabeled
                   document.  Set the membership to zero for now; we
                   will set it to the results of the E-step below when
                   we call crossbow_convert_log_probs_to_probs */
		leaf_membership[li] = 0.0;
	      continue;
	    }
	  else if (crossbow_hem_restricted_horizontal
		   && (doc->tag == bow_doc_train
		       || doc->tag == bow_doc_validation))
	    {
	      treenode *label_node =
		bow_treenode_descendant_matching_name (crossbow_root,
						       doc->filename);
	      if (strstr (leaf->name, label_node->name))
		leaf_membership[li] = (log (leaf->prior)
				       + (leaf_data_prob[li]
					  / crossbow_hem_temperature));
	      else
		/* Set it to probability zero, which, in log space is -Inf */
		leaf_membership[li] = -HUGE_VAL;
	    }
	  else
	    {
	      leaf_membership[li] = (log (leaf->prior)
				     + (leaf_data_prob[li]
					/ crossbow_hem_temperature));
	    }
	}
      if (!crossbow_hem_deterministic_horizontal
	  || doc->tag == bow_doc_unlabeled
	  || !found_deterministic_leaf)
	/* Last condition above for unlabeled docs that were changed 
	   to validation docs */
	crossbow_convert_log_probs_to_probs (leaf_membership, num_leaves);
      else
	/* No longer meaningful!? */
	assert (found_deterministic_leaf);


      /* For perplexity calculation */
      for (iterator = crossbow_root, li = 0;
	   (leaf = bow_treenode_iterate_leaves (&iterator)); 
	   li++)
	{
	  /* xxx Should this be with bow_treenode_complete_log_prob_of_wv()? */
	  if (leaf_membership[li])
	    log_prob_of_data += (leaf_membership[li] * leaf_data_prob[li]);
	  assert (log_prob_of_data == log_prob_of_data);
	}
      num_data_words += bow_wv_word_count (wv);

      docs_added_count++;

      /* E-step estimating ancestor membership probability for words
         in one document, and M-step for one document */
      for (iterator = crossbow_root, li = 0;
	   (leaf = bow_treenode_iterate_leaves (&iterator)); 
	   li++)
	{
	  if (leaf_membership[li] == 0)
	    continue;
	  if (strstr (leaf->name, "/Misc/"))
	    continue;
	  for (wvi = 0; wvi < wv->num_entries; wvi++)
	    {
	      if (crossbow_hem_shrinkage)
		{
		  int ai;
		  double word_deposit, lambda_deposit;

		  /* Calculate normalized ancestor membership probs */
		  ancestor_membership_total = 0;
		  for (ancestor = leaf, ai = 0; ancestor; 
		       ancestor = ancestor->parent, ai++)
		    {
		      if (crossbow_hem_loo)
			ancestor_membership[ai] =
			  leaf->lambdas[ai]
			  * bow_treenode_pr_wi_loo_local (ancestor, 
							  wv->entry[wvi].wi,
							  di, wvi);
		      else
			ancestor_membership[ai] = leaf->lambdas[ai] * 
			  ancestor->words[wv->entry[wvi].wi];
		      assert (ancestor_membership[ai] >= 0);
		      ancestor_membership_total += ancestor_membership[ai];
		    }
		  ancestor_membership[ai] =
		    leaf->lambdas[ai] * 1.0 / leaf->words_capacity;
		  ancestor_membership_total += ancestor_membership[ai];
		  assert (ancestor_membership_total);
		  for (ai = 0; ai < leaf->depth + 2; ai++)
		    {
		      assert (ancestor_membership[ai] >= 0);
		      ancestor_membership[ai] /= ancestor_membership_total;
		    }


		  /* The M-step */
		  for (ancestor = leaf, ai = 0; ancestor; 
		       ancestor = ancestor->parent, ai++)
		    {
		      if (crossbow_hem_vertical_word_movement)
			word_deposit = wv->entry[wvi].count
			  * leaf_membership[li] * ancestor_membership[ai];
		      else
			word_deposit = wv->entry[wvi].count
			  * leaf_membership[li];
#define UNLABELED_WEIGHT_REDUCED 0
#if UNLABELED_WEIGHT_REDUCED
		      if (doc->tag == bow_doc_unlabeled)
			word_deposit /= 3;
#endif
		      assert (word_deposit >= 0);
		      if (!crossbow_hem_lambdas_from_validation
			  || doc->tag != bow_doc_validation)
			{
			  if (crossbow_hem_loo)
			    bow_treenode_add_new_loo_for_di_wvi
			      (ancestor, word_deposit, di, wvi,
			       wv->num_entries, crossbow_docs->length);
			  ancestor->new_words[wv->entry[wvi].wi] += 
			    word_deposit;
			}
		      if (ancestor_membership[ai] == 0) 
			continue;
		      lambda_deposit = wv->entry[wvi].count
			* leaf_membership[li] * ancestor_membership[ai];
		      assert (lambda_deposit >= 0);
		      if (!crossbow_hem_lambdas_from_validation
			  || doc->tag == bow_doc_validation)
			leaf->new_lambdas[ai] += lambda_deposit;
		    }
		  /* The uniform distribution */
		  if (!crossbow_hem_lambdas_from_validation
		      || doc->tag == bow_doc_validation)
		    leaf->new_lambdas[ai] += 
		      wv->entry[wvi].count
		      * leaf_membership[li] * ancestor_membership[ai];
		} /* if crossbow_hem_shrinkage */
	      else 
		{
		  /* The M-step without shrinkage, without ancestor
		     membership probabilities. */
		  leaf->new_words[wv->entry[wvi].wi] += 
		    wv->entry[wvi].count * leaf_membership[li];
		  leaf->new_lambdas[0]++;
		}
	      assert (leaf->new_words[wv->entry[wvi].wi] >= 0);
	      assert (leaf->new_words[wv->entry[wvi].wi]
		      == leaf->new_words[wv->entry[wvi].wi]);
	    }
	  leaf->new_prior += leaf_membership[li];
	}
    }

  /* Finish M-step */
  bow_treenode_set_leaf_prior_from_new_prior_all (crossbow_root, 1);
  for (iterator = crossbow_root;
       (leaf = bow_treenode_iterate_all (&iterator));)
    {
      if (crossbow_hem_shrinkage)
	{
	  bow_treenode_set_words_from_new_words (leaf, 0);
	  bow_treenode_set_lambdas_from_new_lambdas (leaf, 1);
	}
      else
	{
	  bow_treenode_set_words_from_new_words (leaf, 1);
	  bow_treenode_set_lambdas_from_new_lambdas (leaf, 0);
	}
    }

  pp = exp (-log_prob_of_data / num_data_words);
  bow_verbosify (bow_progress, "EM incorporated %d documents; pp=%g\n", 
		 docs_added_count, pp);

  /* Return the perlexity */
  return pp;
}

int
crossbow_hem_consider_splitting ()
{
  int grandparents_count;
  treenode *tn, *iterator, **grandparents;
  int ci;
  int num_leaves;
  int did_split = 0;

  /* Make an array of grandparents, then try splitting them.
	 If you just iterate through tree, then iteration gets messed
	 up the creation of new grandchildren. */
  num_leaves = bow_treenode_leaf_count (crossbow_root);
  grandparents = bow_malloc (num_leaves * sizeof (void*));
  grandparents_count = 0;
  for (iterator = crossbow_root; 
       (tn = bow_treenode_iterate_all (&iterator));)
    {
      if (bow_treenode_is_leaf_parent (tn))
	grandparents[grandparents_count++] = tn;
    }
  for (ci = 0; ci < grandparents_count; ci++)
    did_split |=
      crossbow_hem_hypothesize_grandchildren (grandparents[ci], 
	crossbow_hem_branching_factor);

#if 0
  printf (".........................................................\n");
  for (iterator = crossbow_root; 
       (tn = bow_treenode_iterate_all (&iterator));)
    {
      printf ("%s %g\n", tn->name, tn->prior);
      if (tn->children_count == 0)
	{
	  bow_treenode_word_probs_print (tn, 5);
	  printf ("\n");
	  bow_treenode_word_leaf_likelihood_ratios_print (tn, 5);
	  //bow_treenode_word_likelihood_ratios_print (tn, 10);
	}
    }
#endif
  bow_free (grandparents);
  return did_split;
}



void 
crossbow_hem_cluster ()
{
  int di;
  crossbow_doc *doc;
  double pp, old_pp, test_pp;
  treenode *iterator, *tn;
  FILE *classify_fp;
  int iteration;
  char buf[1024];

  bow_random_set_seed();

  bow_treenode_set_lambdas_uniform (crossbow_root);
  
  /* initialize all data to be at the root */
  for (di = 0; di < crossbow_docs->length; di++)
    {
      int wvi;
      bow_wv *wv = crossbow_wv_at_di (di);
      
      doc = bow_array_entry_at_index (crossbow_docs, di);
      if (doc->tag != bow_doc_train && doc->tag != bow_doc_unlabeled)
	continue;
      
      for (wvi = 0; wvi < wv->num_entries; wvi++)
	{ 
	  crossbow_root->new_words[wv->entry[wvi].wi] += 
	    wv->entry[wvi].count;

	  if (crossbow_hem_loo)
	    bow_treenode_add_new_loo_for_di_wvi
	      (crossbow_root, wv->entry[wvi].count, di, wvi,
	       wv->num_entries, crossbow_docs->length);
	}
    }
  crossbow_root->new_prior = 1.0;

  
  //bow_treenode_set_new_words_from_perturbed_words_all (crossbow_root);
  bow_treenode_set_words_from_new_words_all (crossbow_root, 
					 1.0 / crossbow_root->words_capacity);
  bow_treenode_set_leaf_prior_from_new_prior_all (crossbow_root, 1.0);

  /* Initialize children of the root */
  if (crossbow_root->children_count == 0)
    crossbow_hem_create_children_for_node (crossbow_root, 
					   crossbow_hem_branching_factor);
  

  /* CROSSBOW_HEM_TEMPERATURE already set */
  iteration = 0;
  for ( ; crossbow_hem_temperature >= crossbow_hem_temperature_end;
	crossbow_hem_temperature *= crossbow_hem_temperature_decay)
    {
      bow_verbosify (bow_progress, "TEMPERATURE = %g\n",
		     crossbow_hem_temperature);
      printf ("TEMPERATURE = %g\n", crossbow_hem_temperature);

      /* Always Add hypothesis children here. */

      /* Run EM to convergence. */
      old_pp = FLT_MAX;
      pp = old_pp / 2;
      /* Loop until convergence, i.e. perplexity doesn't change */
      while (ABS (old_pp - pp) > 2
	     && iteration < crossbow_hem_max_num_iterations)
	{
	  printf ("--------------------------------------------------"
		  " Iteration %d\n", iteration);
	  old_pp = pp;
	  pp = crossbow_hem_em_one_iteration ();
	  iteration++;
	  test_pp = crossbow_hem_perplexity (bow_doc_is_test);
	  printf ("train-pp=%f test-pp=%f \n", pp, test_pp);
	  for (iterator = crossbow_root; 
	       (tn = bow_treenode_iterate_all (&iterator));)
	    {
	      printf ("%s", tn->name);
	      if (tn->children_count == 0)
		{
		  int ai, ci;
		  printf (" prior=%g lambdas=[ ", tn->prior);
		  for (ai = 0; ai < tn->depth + 2; ai++)
		    printf ("%5.3f ", tn->lambdas[ai]);
		  printf ("]");
		  if (0 && crossbow_classes_count > 1)
		    {
		      printf ("\n  classes=[ ");
		      for (ci = 0; ci < crossbow_classes_count; ci++)
			printf ("%5.3f ", tn->classes[ci]);
		      printf ("]");
		    }
		}
	      else
		printf (" KL %g WKL %g", 
			bow_treenode_children_kl_div (tn),
			bow_treenode_children_weighted_kl_div (tn));
	      printf ("\n");
	      if (1 || tn->children_count == 0)
		{
		  //bow_treenode_word_likelihood_ratios_print (tn, 10);
		  bow_treenode_word_probs_print (tn, 10);
		  printf ("\n");
		  bow_treenode_word_likelihood_ratios_print (tn, 5);
		  //bow_treenode_word_leaf_likelihood_ratios_print (tn, 5);
		  //bow_treenode_word_leaf_odds_ratios_print (tn, 10);
		}
	    }
	  //crossbow_leaf_document_probs_print (3);
	}

      /* Consider making splits. */
      /* xxx This function should delete leaves that didn't become "real". */
      if (crossbow_hem_consider_splitting ())
	{
	  /* xxx But leaves were just perturbed!!! */
	  /* Output document classifications */
	  sprintf (buf, "crossbow-classifications-%d", iteration);
	  classify_fp = bow_fopen (buf, "w");
	  crossbow_classify_tagged_docs (-1, 1, classify_fp);
	  fflush (classify_fp);
	  fclose (classify_fp);

	  /* Output top words */
	  sprintf (buf, "crossbow-words-%d", iteration);
	  classify_fp = bow_fopen (buf, "w");
	  bow_verbosify (bow_progress, "========= keywords ========\n");
	  bow_treenode_keywords_print_all (crossbow_root, classify_fp);
	  fflush (classify_fp);
	  fclose (classify_fp);
	}

      /* Perturb the leaves */
      bow_treenode_set_new_words_from_perturbed_words_all (crossbow_root, 0.1);
      bow_treenode_set_words_from_new_words_all (crossbow_root, 0);
    }

}

/* Put all documents into the NEW_WORDS distributions. */
void
crossbow_hem_place_labeled_data ()
{
  int di, wvi;
  crossbow_doc *doc;
  treenode *leaf;
  bow_wv *wv;

  /* Clear all previous information. */
  bow_treenode_set_new_words_to_zero_all (crossbow_root);
  bow_treenode_free_loo_all (crossbow_root, crossbow_docs->length);

  /* Initialize the word distributions and LOO entries with the data
     and initialize lambdas to uniform */
  for (di = 0; di < crossbow_docs->length; di++)
    {
      doc = bow_array_entry_at_index (crossbow_docs, di);
      wv = crossbow_wv_at_di (di);
      if (doc->tag != bow_doc_train)
	continue;
      /* Temporary fix */
      if (strstr (doc->filename, ".include")
	  || strstr (doc->filename, ".exclude"))
	continue;
      leaf = bow_treenode_descendant_matching_name (crossbow_root, 
						    doc->filename);
      //assert (leaf->children_count == 0);
      leaf->new_prior++;
      while (leaf)
	{
	  for (wvi = 0; wvi < wv->num_entries; wvi++)
	    {
	      leaf->new_words[wv->entry[wvi].wi] += wv->entry[wvi].count;
	      bow_treenode_add_new_loo_for_di_wvi
		(leaf, wv->entry[wvi].count, di, wvi,
		 wv->num_entries, crossbow_docs->length);
	    }
	  leaf = leaf->parent;
	}
    }
}

/* Do full EM, without determinisitic annealing, or leaf splitting. */
void
crossbow_hem_full_em ()
{
  double pp, old_pp;
  double test_labeled_pp, test_unlabeled_pp;
  double train_labeled_pp, train_unlabeled_pp;
  treenode *iterator, *tn;
  int iteration = 0;
  int old_hem_shrinkage;
#if PRINT_WORD_DISTS
  char prefix[BOW_MAX_WORD_LENGTH];
#endif

  //assert (crossbow_hem_shrinkage);
  //assert (crossbow_hem_loo);

  if (crossbow_hem_garbage_collection)
    {
      /* Add "Misc" children to each parent in the tree */
      bow_treenode_add_misc_child_all (crossbow_root);
    }

  /* If CROSSBOW_HEM_LAMBDAS_FROM_VALIDATION is non-zero, then change
     X percent of the train and unlabeled documents to validation. */
  if (crossbow_hem_lambdas_from_validation)
    {
      int di;
      crossbow_doc *doc;
      int validation_count = 0;
      for (di = 0; di < crossbow_docs->length; di++)
	{
	  doc = bow_array_entry_at_index (crossbow_docs, di);
	  if ((/*doc->tag == bow_doc_train ||*/ doc->tag == bow_doc_unlabeled)
	      && (bow_random_double (0.0, 1.0)
		  < crossbow_hem_lambdas_from_validation))
	    {
	      doc->tag = bow_doc_validation;
	      validation_count++;
	    }
	}
      bow_verbosify (bow_progress, "Placed %d document in validation set\n",
		     validation_count);
    }

  /* Initialize the word distributions and LOO entries with the data
     and initialize lambdas to uniform */
  crossbow_hem_place_labeled_data ();
  bow_treenode_set_words_from_new_words_all (crossbow_root, 1);
  bow_treenode_set_leaf_prior_from_new_prior_all (crossbow_root, 1);
  bow_treenode_set_lambdas_leaf_only_all (crossbow_root);

  printf ("No Shrinkage\n");
  old_hem_shrinkage = crossbow_hem_shrinkage;
  crossbow_hem_shrinkage = 0;
  crossbow_classify_tagged_docs (bow_doc_test, 0, stdout);
  crossbow_hem_shrinkage = old_hem_shrinkage;
  
  train_labeled_pp = crossbow_hem_labeled_perplexity (bow_doc_is_train);
  train_unlabeled_pp=crossbow_hem_unlabeled_perplexity (bow_doc_is_train);
  test_labeled_pp = crossbow_hem_labeled_perplexity (bow_doc_is_test);
  test_unlabeled_pp = crossbow_hem_unlabeled_perplexity (bow_doc_is_test);
  printf ("train-unlabeled-pp=%f train-labeled-pp=%f\n"
	  " test-unlabeled-pp=%f  test-labeled-pp=%f\n", 
	  train_unlabeled_pp, train_labeled_pp,
	  test_unlabeled_pp, test_labeled_pp);
  if (crossbow_hem_vertical_word_movement)
    bow_treenode_word_probs_print_all (crossbow_root, 5);

  crossbow_hem_place_labeled_data ();
  if (crossbow_hem_shrinkage)
    bow_treenode_set_words_from_new_words_all (crossbow_root, 0);
  else
    bow_treenode_set_words_from_new_words_all (crossbow_root, 1);

  bow_treenode_set_leaf_prior_from_new_prior_all (crossbow_root, 1);

  /* Initialize the lambdas */
#if SHRINK_WITH_UNIFORM_ONLY
  /* Set the lambdas to use the uniform and the leaf, and nothing else */
     for (iterator = crossbow_root; 
       (tn = bow_treenode_iterate_leaves (&iterator));)
    {
      int li;
      for (li = 0; li < tn->depth + 2; li++)
	{
	  if (li == 0 || li == tn->depth+1)
	    tn->lambdas[li] = 0.5;
	  else
	    tn->lambdas[li] = 0;
	}
    }
#elif 1
     if (crossbow_hem_shrinkage)
       bow_treenode_set_lambdas_uniform_all (crossbow_root);
     else
       bow_treenode_set_lambdas_leaf_only_all (crossbow_root);
     
#else
  /* Just for fun see what happens when we initialize more data in leaves */
  for (iterator = crossbow_root; 
       (tn = bow_treenode_iterate_leaves (&iterator));)
    {
      int li;
      for (li = 0; li < tn->depth + 2; li++)
	{
	  if (li == 0)
	    tn->lambdas[li] = 0.5;
	  else
	    tn->lambdas[li] = 0.5 / (tn->depth + 1);
	}
    }
#endif
  //bow_treenode_word_probs_print_all (crossbow_root, 5);

  if (crossbow_hem_pseudo_labeled)
    bow_tag_change_tags (crossbow_docs, bow_doc_train, bow_doc_unlabeled);

  /* Run EM to convergence. */
  old_pp = FLT_MAX;
  pp = -1;
  crossbow_hem_temperature = 1;
  /* Loop until convergence, i.e. perplexity doesn't change */
  while (/* ABS (old_pp - pp) > 0.1 && */
	 iteration < crossbow_hem_max_num_iterations)
    {
      printf ("--------------------------------------------------"
	      " Iteration %d\n", iteration);

      /* Output the percent correct, and various perplexities. */
      crossbow_classify_tagged_docs (bow_doc_test, 0, stdout);
      train_labeled_pp = 
	crossbow_hem_labeled_perplexity (bow_doc_is_train);
      train_unlabeled_pp =
	crossbow_hem_unlabeled_perplexity (bow_doc_is_train);
      test_labeled_pp = 
	crossbow_hem_labeled_perplexity (bow_doc_is_test);
      test_unlabeled_pp = 
	crossbow_hem_unlabeled_perplexity (bow_doc_is_test);
      printf ("train-unlabeled-pp=%f train-labeled-pp=%f\n"
	      " test-unlabeled-pp=%f  test-labeled-pp=%f\n", 
	      train_unlabeled_pp, train_labeled_pp,
	      test_unlabeled_pp, test_labeled_pp);

#if PRINT_WORD_DISTS
      sprintf (prefix, "word-dists/em%d-%d", iteration, bow_random_seed);
      bow_treenode_print_all_word_probabilities_all (prefix, 1);
#endif

      for (iterator = crossbow_root; 
	   (tn = bow_treenode_iterate_all (&iterator));)
	{
	  printf ("%s", tn->name);
	  if (tn->children_count == 0)
	    {
	      int ai;
	      printf ("\n lambdas=[ ");
	      for (ai = 0; ai < tn->depth + 2; ai++)
		printf ("%5.3f ", tn->lambdas[ai]);
	      printf ("]");
	    }
	  printf ("\n");
	  if (1 || tn->children_count == 0)
	    {
	      printf ("prior=%g\n", tn->prior);
	      //bow_treenode_word_likelihood_ratios_print (tn, 10);
	      //printf ("\n");
	      if (crossbow_hem_vertical_word_movement)
		bow_treenode_word_probs_print (tn, 5);
	      //bow_treenode_word_likelihood_ratios_print (tn, 5);
	      //bow_treenode_word_leaf_likelihood_ratios_print (tn, 5);
	      //bow_treenode_word_leaf_odds_ratios_print (tn, 10);
	    }
	}

      old_pp = pp;
      pp = crossbow_hem_em_one_iteration ();
      if (iteration % 2 == 0 && crossbow_hem_incremental_labeling)
	crossbow_hem_label_most_confident ();
      iteration++;
    }
}


/* If we replace the loss function
   L= sum_i (tilde{p}_i - p_i)^2
with
   LL = sum_i (tilde{p}_i - p_i)^2/ (p_i (1-p_i) )
then we get a loss function which is still tractable but is more
sensitive to errors for small probabilities.

If I repeat the calucations I get that lambda should be:
   lambda = (t/n) / (   (t/n) + B)
where
   B = sum_i  (u_i -p_i)^2 /( p_i (1-p_i) )
(the sum is over the vocabulary).  Here, t is the vocabulary size.  */

#define LOG_LOSS 1

void
crossbow_hem_fienberg_treenode (treenode *tn)
{
  double u;
  double numerator;
  double wi_err;
  double sq_err;
  double n;
  double lambda;
  treenode *ancestor, *node;
  int wi, i;
  double b;
  double t;

  /* Sample size = Total number of word occurrences. */
  n = tn->new_words_normalizer;  
  t = tn->words_capacity;
  numerator = sq_err = b = 0;

#if SHRINK_WITH_UNIFORM_ONLY
  if (tn->children_count != 0)
    {
      for (i = 0; i < tn->depth + 2; i++)
	tn->lambdas[i] = 0;
      goto do_children;
    }
#endif

  if (SHRINK_WITH_UNIFORM_ONLY || tn->parent == NULL)
    {
      /* Calculating lambda for the root */
      for (wi = 0; wi < tn->words_capacity; wi++)
	{
	  /* Parent word distribution is the uniform distribution */
	  u = 1.0 / tn->words_capacity;
	  numerator += tn->words[wi] * (1.0 - tn->words[wi]);
	  wi_err = u - tn->words[wi];
	  sq_err += wi_err * wi_err;
	  b += ((wi_err * wi_err) / (tn->words[wi] * (1.0 - tn->words[wi])));
	}
      printf ("  n = %d  sum p*(1-p) = %f   squared error = %f  b = %f\n", 
	      (int)n, numerator, sq_err, b);
#if LOG_LOSS
      lambda = (t/n) / ((t/n) + b);
#else
      lambda = (1.0/n) * (numerator / (sq_err + (1.0/n) * numerator));
#endif
#if SHRINK_WITH_UNIFORM_ONLY
      tn->lambdas[0] = 1.0 - lambda;
      for (i = 1; i < tn->depth + 1; i++)
	tn->lambdas[i] = 0;
      tn->lambdas[tn->depth+1] = lambda;
#else
      tn->lambdas[1] = lambda;
      tn->lambdas[0] = 1.0 - lambda;
#endif
    }
  else
    {
      /* Calculating lambda for an interior node or leaf */
      for (wi = 0; wi < tn->words_capacity; wi++)
	{
	  /* Calculate parent word distribution as a mixture */
	  u = 0;
	  node = tn->parent;
	  for (ancestor = node, i = 0; 
	       ancestor; ancestor = ancestor->parent, i++)
	    u += node->lambdas[i] * ancestor->words[wi];
	  /* Add in the uniform distribution */
	  u += node->lambdas[i] / node->words_capacity;

	  numerator += tn->words[wi] * (1.0 - tn->words[wi]);
	  wi_err = u - tn->words[wi];
	  sq_err += wi_err * wi_err;
	  b += ((wi_err * wi_err) / (tn->words[wi] * (1.0 - tn->words[wi])));
	  if (0 && wi % 1000 == 0)
	    printf ("n %f s %f\n", numerator, sq_err);
	}
      printf ("  n = %d  sum p*(1-p) = %f   squared error = %f  b = %f\n", 
	      (int)n, numerator, sq_err, b);
#if LOG_LOSS
      lambda = (t/n) / ((t/n) + b);
#else
      lambda = (1.0/n) * (numerator / (sq_err + (1.0/n) * numerator));
#endif
      tn->lambdas[0] = 1.0 - lambda;
      for (i = 1; i < tn->depth + 2; i++)
	tn->lambdas[i] = lambda * tn->parent->lambdas[i-1];
    }
  
  bow_verbosify (bow_progress, "%20s\n  local_lambda=%f parent_lambda=%f\n",
		 tn->name, 1.0 - lambda, lambda);

#if SHRINK_WITH_UNIFORM_ONLY
 do_children:
#endif
  for (i = 0; i < tn->children_count; i++)
    crossbow_hem_fienberg_treenode (tn->children[i]);
}

void
crossbow_hem_fienberg ()
{
  treenode *iterator, *tn;
  double test_labeled_pp, test_unlabeled_pp;
  double train_labeled_pp, train_unlabeled_pp;
#if PRINT_WORD_DISTS
  char prefix[BOW_MAX_WORD_LENGTH];
#endif
  double lambda;

#if 0
  /* Print the word distribution of all the data, then exit. */
  bow_set_all_docs_untagged (crossbow_docs);
  bow_set_doc_types_of_remaining (crossbow_docs, bow_doc_train);
  crossbow_hem_place_labeled_data ();
  bow_treenode_set_words_from_new_words_all (crossbow_root, 0);
  bow_treenode_set_leaf_prior_from_new_prior_all (crossbow_root, 0);
  bow_treenode_set_lambdas_uniform_all (crossbow_root);
  sprintf (prefix, "word-dists/all-mle");
  bow_treenode_print_all_word_probabilities_all (prefix, 0);
  sprintf (prefix, "word-dists/all-uniform");
  bow_treenode_print_all_word_probabilities_all (prefix, 1);
  exit (0);
#endif

#if 0
  /* Initialize the word distributions and LOO entries with the data
     and initialize lambdas to use local estimates only */
  crossbow_hem_place_labeled_data ();
  bow_treenode_set_words_from_new_words_all (crossbow_root, 1);
  bow_treenode_set_leaf_prior_from_new_prior_all (crossbow_root, 1);
  bow_treenode_set_lambdas_leaf_only_all (crossbow_root);

  printf ("\n\nNo Shrinkage\n");
  crossbow_classify_tagged_docs (bow_doc_test, 0, 0, stdout);
#endif

  crossbow_hem_place_labeled_data ();
  bow_treenode_set_words_from_new_words_all (crossbow_root, 1);
  bow_treenode_set_leaf_prior_from_new_prior_all (crossbow_root, 1);

  crossbow_hem_fienberg_treenode (crossbow_root);
  

  /* Print the tree */
  for (iterator = crossbow_root; 
       (tn = bow_treenode_iterate_all (&iterator));)
    {
      int ai;
      printf ("%s", tn->name);
      printf (" prior=%g lambdas=[ ", tn->prior);
      for (ai = 0; ai < tn->depth + 2; ai++)
	printf ("%5.3f ", tn->lambdas[ai]);
      printf ("]\n");
    }

  printf ("\n\nFienberg\n");

#if PRINT_WORD_DISTS
  sprintf (prefix, "word-dists/fienberg-%d", bow_random_seed);
  bow_treenode_print_all_word_probabilities_all (prefix, 1);
  sprintf (prefix, "word-dists/map-%d", bow_random_seed);
  bow_treenode_print_all_word_probabilities_all (prefix, 0);
#endif

  crossbow_classify_tagged_docs (bow_doc_test, 0, stdout);
  train_labeled_pp = crossbow_hem_labeled_perplexity (bow_doc_is_train);
  train_unlabeled_pp=crossbow_hem_unlabeled_perplexity (bow_doc_is_train);
  test_labeled_pp = crossbow_hem_labeled_perplexity (bow_doc_is_test);
  test_unlabeled_pp = crossbow_hem_unlabeled_perplexity (bow_doc_is_test);
  printf ("train-unlabeled-pp=%f train-labeled-pp=%f\n"
	  "test-unlabeled-pp=%f test-labeled-pp=%f\n", 
	  train_unlabeled_pp, train_labeled_pp,
	  test_unlabeled_pp, test_labeled_pp);

#if 1
  /* Set lambdas several different constants and test */
  crossbow_hem_place_labeled_data ();
  bow_treenode_set_words_from_new_words_all (crossbow_root, 0);
  bow_treenode_set_leaf_prior_from_new_prior_all (crossbow_root, 0);

  for (lambda = 0.0; lambda < 1.01; lambda += 0.05)
    {
      printf ("\nFixed local_lambda=%f uniform_lambda=%f\n", 
	      1.0 - lambda, lambda);
      for (iterator = crossbow_root; 
	   (tn = bow_treenode_iterate_all (&iterator));)
	{
	  int ai;
	  for (ai = 0; ai < tn->depth + 2; ai++)
	    {
	      if (ai == 0)
		tn->lambdas[ai] = 1.0 - lambda;
	      else if (ai == tn->depth + 1)
		tn->lambdas[ai] = lambda;
	      else
		tn->lambdas[ai] = 0;
	    }
	}

      crossbow_classify_tagged_docs (bow_doc_test, 0, stdout);
      train_labeled_pp = 
	crossbow_hem_labeled_perplexity (bow_doc_is_train);
      train_unlabeled_pp =
	crossbow_hem_unlabeled_perplexity (bow_doc_is_train);
      test_labeled_pp = 
	crossbow_hem_labeled_perplexity (bow_doc_is_test);
      test_unlabeled_pp = 
	crossbow_hem_unlabeled_perplexity (bow_doc_is_test);
      printf ("train-unlabeled-pp=%f train-labeled-pp=%f\n"
	      "test-unlabeled-pp=%f test-labeled-pp=%f\n", 
	      train_unlabeled_pp, train_labeled_pp,
	      test_unlabeled_pp, test_labeled_pp);
    }
#endif
}

extern int crossbow_classify_doc (crossbow_doc *doc, int verbose, FILE *out);

crossbow_method hem_cluster_method =
{
  "hem-cluster",
  NULL,
  NULL,
  crossbow_hem_cluster,
  crossbow_classify_doc,
};

crossbow_method hem_classify_method =
{
  "hem-classify",
  NULL,
  crossbow_hem_full_em,
  NULL,
  crossbow_classify_doc,
};

crossbow_method hem_fienberg_method =
{
  "fienberg-classify",
  NULL,
  crossbow_hem_fienberg,
  NULL,
  crossbow_classify_doc,
};


void _register_method_hem () __attribute__ ((constructor));
void _register_method_hem ()
{
  bow_method_register_with_name ((bow_method*)&hem_cluster_method,
				 "hem-cluster", 
				 sizeof (crossbow_method),
				 NULL);
  bow_method_register_with_name ((bow_method*)&hem_classify_method,
				 "hem-classify", 
				 sizeof (crossbow_method),
				 NULL);
  bow_method_register_with_name ((bow_method*)&hem_fienberg_method,
				 "fienberg-classify", 
				 sizeof (crossbow_method),
				 NULL);

  bow_argp_add_child (&crossbow_hem_argp_child);
}
