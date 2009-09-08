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

#ifndef __BOW_NAIVEBAYES_H
#define __BOW_NAIVEBAYES_H

/* Default value for option "naivebayes-m-est-m".  When zero, then use
   size-of-vocabulary instead. */
double naivebayes_argp_m_est_m;

/* icky globals for Dirichlet smoothing */
double *bow_naivebayes_dirichlet_alphas;
double bow_naivebayes_dirichlet_total;

/* the score normally returns P(c|X) - this makes the score return P(X|c) */
extern int  naivebayes_score_returns_doc_pr;
/* leave the scores in sorted order with regards to class indices */
extern int  naivebayes_score_unsorted;
extern double bow_naivebayes_anneal_temperature;

/* load up the alphas */
void bow_naivebayes_load_dirichlet_alphas ();
void bow_naivebayes_initialize_dirichlet_smoothing (bow_barrel *barrel);


void bow_naivebayes_set_weights (bow_barrel *barrel);

int bow_naivebayes_score (bow_barrel *barrel, bow_wv *query_wv, 
			  bow_score *bscores, int bscores_len,
			  int loo_class);
/* Print the top N words by odds ratio for each class. */
void bow_naivebayes_print_odds_ratio_for_all_classes (bow_barrel *barrel, 
						      int n);


/* The method and parameters of NaiveBayes weight settings. */
extern rainbow_method bow_method_naivebayes;
typedef struct _bow_naivebayes_params {
  bow_boolean uniform_priors;
  bow_boolean normalize_scores;
} bow_params_naivebayes;

/* Print the top N words by odds ratio for each class. */
void bow_naivebayes_print_odds_ratio_for_all_classes (bow_barrel *barrel, 
						      int n);

void bow_naivebayes_print_odds_ratio_for_class (bow_barrel *barrel,
						const char *classname);

void bow_naivebayes_print_word_probabilities_for_class (bow_barrel *barrel,
							const char *classname);


/* Get the total number of terms in each class; store this in
   CDOC->WORD_COUNT. */
void bow_naivebayes_set_cdoc_word_count_from_wi2dvf_weights
(bow_barrel *barrel);


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
double bow_naivebayes_pr_wi_ci (bow_barrel *barrel,
				int wi, int ci,
				int loo_class,
				float loo_wi_count, float loo_w_count,
				bow_dv **last_dv, int *last_dvi);


#endif /* __BOW_NAIVEBAYES_H */
