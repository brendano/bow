/* treenode.h - public declartions for hierarchical word distributions.
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

#ifndef __treenode_h_INCLUDE
#define __treenode_h_INCLUDE

#include <bow/libbow.h>

typedef struct _treenode {
  const char *name;
  struct _treenode *parent;
  struct _treenode **children;
  int children_count;
  int children_capacity;
  int words_capacity;		/* length of WORDS and NEW_WORDS array */
  double *words;		/* multinomial distribution over words */
  double *new_words;		/* M-step-in-progress dist over words */
  double new_words_normalizer;	/* was used to set WORDS from NEW_WORDS */
  double prior;			/* the prior probability of this node */
  double new_prior;		/* M-step-in-progress prior probability */
  int depth;			/* depth of this node in the tree */
  double *lambdas;		/* mixture weights up the tree */
  double *new_lambdas;		/* mixture weights up the tree */
  int ci_in_parent;		/* child index in parent */
  double *di_loo;		/* total NEW_WORDS prob mass from each doc */
  double **di_wvi_loo;		/* NEW_WORDS prob mass from each doc/word */
  double *new_di_loo;		/* total NEW_WORDS prob mass from each doc */
  double **new_di_wvi_loo;	/* NEW_WORDS prob mass from each doc/word */
#if 0
  FILE *loo_fp;			/* File for LOO info instead of memory */
  FILE *new_loo_fp;		/* File for new LOO info instead of memory */
#endif
  /* Variables for classification */
  int classes_capacity;
  double *classes;		/* multinomial distribution over class labels*/
  double *new_classes;		/* M-step-in-progress dist over classes */
} treenode;



/* Creation, freeing, attachment, detachment */

/* Create and return a new treenode. */
treenode *bow_treenode_new (treenode *parent, 
			int children_capacity,
			const char *name);

/* Free the memory allocate by TN and its children */
void bow_treenode_free (treenode *tn);

/* Create and return a new treenode with the proper settings to be the
   root treenode */
treenode *bow_treenode_new_root (int children_count);

/* Free the memory allocate by TN and its children */
void bow_treenode_free (treenode *tn);

/* Reallocate memory for the WORDS and NEW_WORDS arrays, big enough 
   for the vocabulary of size bow_num_words().  This is useful when the 
   tree has been created before all the documents have been indexed. */
void bow_treenode_realloc_words_all (treenode *root);

/* Add to parent a CHILD that was previously created with a NULL parent. */
void bow_treenode_add_child (treenode *parent, treenode *child);

/* Detach CHILD from PARENT, shifting remaining children, and updating
   the remaining children's CI_IN_PARENT. */
void bow_treenode_remove_child (treenode *parent, treenode *child);

/* To this node and all its children, add a child named "Misc" if not
   there already. */
void
bow_treenode_add_misc_child_all (treenode *root);


/* Iteration over tree nodes. */

/* Return the "next" treenode in a traversal of the tree.  CONTEXT
   should be initialized to the root of the tree, otherwise strange
   results will ensue: nodes on the path from the initial CONTEXT node
   to the root will be skipped by the iteration.  When the traversal
   is finished, NULL will be returned. */
treenode *bow_treenode_iterate_all (treenode **context);

/* Same as above, but only return the leaf nodes. */
treenode *bow_treenode_iterate_leaves (treenode **context);

/* Return the deepest descendant with a ->NAME that is contained in NAME */
treenode *bow_treenode_descendant_matching_name (treenode *root, 
						 const char *name);


/* Archiving */

/* Write a treenode (and all its children) to FP. */
void bow_treenode_write (treenode *tn, FILE *fp);

/* Read and return a new treenode (and all its children) from FP. */
treenode *bow_treenode_new_from_fp (FILE *fp);


/* Word distributions and prior distributions */

/* Set all of TN's ancestor mixture weights, LAMBDAS, to equal values. */
void bow_treenode_set_lambdas_uniform (treenode *tn);

/* Same as above, but for all leaves in the tree. */
void bow_treenode_set_lambdas_uniform_all (treenode *tn);

/* Set TN's mixture weights, LAMBDAS, to use only the estimates. */
void bow_treenode_set_lambdas_leaf_only (treenode *tn);

/* Same as above, but for all leaves in the tree. */
void bow_treenode_set_lambdas_leaf_only_all (treenode *tn);

/* Add WEIGHT to treenode TN's record of how much probability mass
   document DI contributed to TN's NEW_WORDS for the word at DI's
   WVI'th word.  This mass can later be subtracted to do leave-one-out
   calculations.  DI_WV_NUM_ENTRIES-1 is the maximum WVI that can be
   expected for DI; DI_COUNT-1 is the maximum DI that can be expected;
   both are used to know how much space to allocate. */
void bow_treenode_add_new_loo_for_di_wvi (treenode *tn, 
				      double weight, int di, int wvi,
				      int di_wv_num_entries, int di_count);

/* Clear all LOO info for treenode TN */
void bow_treenode_free_loo (treenode *tn, int di_count);

/* Same as above, over all nodes of the tree. */
void bow_treenode_free_loo_all (treenode *root, int di_count);

/* Clear all LOO and NEW_LOO info for treenode TN */
void bow_treenode_free_loo_and_new_loo (treenode *tn, int di_count);

/* Same as above, over all nodes of the tree. */
void bow_treenode_free_loo_and_new_loo_all (treenode *root, int di_count);

/* Normalize the NEW_WORDS distribution, move it into the WORDS array
   and zero the NEW_WORDS array.  ALPHA is the parameter for the
   Dirichlet prior. */
void bow_treenode_set_words_from_new_words (treenode *tn, double alpha);

/* Save as above, over all nodes in the tree. */
void bow_treenode_set_words_from_new_words_all (treenode *root, double alpha);

/* Set NEW_WORDS counts to zero. */
void bow_treenode_set_new_words_to_zero (treenode *tn);

/* Same as above, over all nodes of the tree. */
void bow_treenode_set_new_words_to_zero_all (treenode *root);

/* Set the NEW_WORDS distribution from the addition of the WORDS
   distribution and some random noise.  NOISE_WEIGHT 0.5 gives equal
   weight to the data and the noise. */
void bow_treenode_set_new_words_from_perturbed_words (treenode *tn, 
						  double noise_weight);

/* Same as above, over all nodes of the tree. */
void bow_treenode_set_new_words_from_perturbed_words_all (treenode *root,
						      double noise_weight);

/* Over all nodes of the tree, set the PRIOR by the results of
   smoothing and normalizing the NEW_PRIOR distribution.  ALPHA is the
   parameter for the Dirichlet prior. */
void bow_treenode_set_leaf_prior_from_new_prior_all (treenode *root, 
						     double alpha);

/* Over all nodes (including interior and root) of the tree, set the
   PRIOR by the results of smoothing and normalizing the NEW_PRIOR
   distribution.  ALPHA is the parameter for the Dirichlet prior. */
void bow_treenode_set_prior_from_new_prior_all (treenode *root, double alpha);

/* Over all nodes (including interior and root) of the tree, plus one
   "extra" quantity (intended for the prior probability of the uniform
   distribution), set the PRIOR by the results of smoothing and
   normalizing the NEW_PRIOR distribution, and set EXTRA as part of
   the normalization.  ALPHA is the parameter for the Dirichlet
   prior. */
void bow_treenode_set_prior_and_extra_from_new_prior_all (treenode *root, 
							  double *new_extra, 
							  double *extra, 
							  double alpha);

/* Normalize the NEW_LAMBDAS distribution, move it into the LAMBDAS array
   and zero the NEW_LAMBDAS array.  ALPHA is the parameter for the
   Dirichlet prior. */
void bow_treenode_set_lambdas_from_new_lambdas (treenode *tn, double alpha);

/* Set the CLASSES distribution to uniform, allocating space for it if
   necessary */
void bow_treenode_set_classes_uniform (treenode *tn, int classes_capacity);

/* Normalize the NEW_CLASSES distribution, move it into the CLASSES array
   and zero the NEW_CLASSES array.  ALPHA is the parameter for the
   Dirichlet prior. */
void bow_treenode_set_classes_from_new_classes (treenode *tn, double alpha);


/* Data probabilities */

/* Return the probability of word WI in LEAF, using the hierarchical
   mixture */
double bow_treenode_pr_wi (treenode *leaf, int wi);

/* Return the probability of word WI in node TN, but with the
   probability mass of document LOO_DI removed. */
double bow_treenode_pr_wi_loo_local (treenode *tn, int wi, 
				 int loo_di, int loo_wvi);

/* Return the probability of word WI in LEAF, using the hierarchical
   mixture, but with the probability mass of document LOO_DI removed. */
double bow_treenode_pr_wi_loo (treenode *tn, int wi,
			   int loo_di, int loo_wvi);

/* Return the prior probability of TN being in a path selected for
   generating a document. */
double bow_treenode_prior (treenode *tn);

/* Return the log-probability of node TN's WORD distribution having
   produced the word vector WV. */
double bow_treenode_log_prob_of_wv (treenode *tn, bow_wv *wv);

/* Return the log-probability of node TN's WORD distribution having
   produced the word vector WV. */
double bow_treenode_log_local_prob_of_wv (treenode *tn, bow_wv *wv);

/* Return the log-probability of node TN's WORD distribution having
   produced the word vector WV, but with document DI removed from TN's
   WORD distribution. */
double bow_treenode_log_prob_of_wv_loo (treenode *tn, bow_wv *wv, int loo_di);

/* Same as above, but return a probability instead of a log-probability */
double bow_treenode_prob_of_wv (treenode *tn, bow_wv *wv);

/* Return the log-probability of node TN's WORD distribution having
   produced the word vector WV. */
double bow_treenode_log_local_prob_of_wv (treenode *tn, bow_wv *wv);

/* Return the local log-probability of node TN's WORD distribution
   having produced the word vector WV, but with document DI removed
   from TN's WORD distribution. */
double bow_treenode_log_local_prob_of_wv_loo (treenode *tn, 
					  bow_wv *wv, int loo_di);

/* Return the expected complete log likelihood of node TN's word
   distribution having produced the word vector WV. */
double bow_treenode_complete_log_prob_of_wv (treenode *tn, bow_wv *wv);


/* Tree statistics and diagnostics. */

/* Return the number of leaves under (and including) TN */
int bow_treenode_leaf_count (treenode *tn);

/* Return the number of tree nodes under (and including) TN */
int bow_treenode_node_count (treenode *tn);

/* Return an array of words with their associated likelihood ratios,
   calculated relative to its siblings. */
bow_wa *bow_treenode_word_likelihood_ratio (treenode *tn);

/* Return an array of words with their associated likelihood ratios,
   calculated relative to all the leaves. */
bow_wa *bow_treenode_word_leaf_likelihood_ratios (treenode *tn);

/* Print the NUM_TO_PRINT words with highest likelihood ratios,
   calculated relative to its siblings. */
void bow_treenode_word_likelihood_ratio_print (treenode *tn, int num_to_print);

/* Print the NUM_TO_PRINT words with highest likelihood ratios,
   calculated relative to all the leaves. */
void bow_treenode_word_leaf_likelihood_ratios_print (treenode *tn, 
						 int num_to_print);

/* Print the NUM_TO_PRINT words with highest odds ratios,
   calculated relative to all the leaves. */
void bow_treenode_word_leaf_odds_ratios_print (treenode *tn, int num_to_print);

/* Print the NUM_TO_PRINT words with highest likelihood ratios,
   calculated relative to its siblings. */
void bow_treenode_word_likelihood_ratios_print (treenode *tn, 
						int num_to_print);

/* Same as above, for all nodes in the tree. */
void bow_treenode_word_likelihood_ratios_print_all (treenode *tn,
						    int num_to_print);

/* Return a bow_wa array of words with their associated probabilities */
bow_wa *bow_treenode_word_probs (treenode *tn);

/* Print the NUM_TO_PRINT words with highest probability */
void bow_treenode_word_probs_print (treenode *tn, int num_to_print);

/* Same as above, for all nodes in the tree. */
void bow_treenode_word_probs_print_all (treenode *tn, int num_to_print);

/* Print most probable words in one line, and only if parent's
   WKL is high enough */
void bow_treenode_keywords_print (treenode *tn, FILE *fp);

/* Same as above, but for TN and all treenodes under TN */
void bow_treenode_keywords_print_all (treenode *tn, FILE *fp);

/* Print the word distribution for each leaf to a separate file, each
   file having prefix FILENAME_PREFIX.  Use vertical mixture if
   SHRINKAGE is non-zero. */
void bow_treenode_print_all_word_probabilities_all
(const char *filename_prefix, int shrinkage);

/* Return the "KL Divergence to the Mean" among the children of TN */
double bow_treenode_children_kl_div (treenode *tn);

/* Return the weighted "KL Divergence to the mean among the children
   of TN" multiplied by the number of words of training data in the
   children. */
double bow_treenode_children_weighted_kl_div (treenode *tn);

/* Return the "KL Divergence to the mean" between TN1 and TN2. */
double bow_treenode_pair_kl_div (treenode *tn1, treenode *tn2);

/* Same as above, but multiply by the number of words in TN1 and TN2. */
double bow_treenode_pair_weighted_kl_div (treenode *tn1, treenode *tn2);

/* Return non-zero if any of TN's children are leaves */
int bow_treenode_is_leaf_parent (treenode *tn);

#endif /* __treenode_h_INCLUDE */
