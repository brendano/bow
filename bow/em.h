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

#ifndef __BOW_EM_H
#define __BOW_EM_H

/* The method and parameters of EM weight settings. */
extern rainbow_method bow_method_em;
typedef enum {
  bow_em_perturb_none = 0,
  bow_em_perturb_with_gaussian,
  bow_em_perturb_with_dirichlet
} bow_em_perturb_method;

extern bow_em_perturb_method bow_em_perturb_starting_point;
extern int bow_em_num_em_runs;
extern int em_cross_entropy;

/* hack for scoring for perplexity calculation */
extern int bow_em_calculating_perplexity;


typedef struct _bow_em_pr_struct {
  double score;
  int correct;
} bow_em_pr_struct;


void
bow_em_perturb_weights (bow_barrel *doc_barrel, bow_barrel *vpc_barrel);

int
bow_em_pr_struct_compare (const void *x, const void *y);

void
bow_em_compare_to_nb (bow_barrel *doc_barrel);

void
bow_em_print_log_odds_ratio (bow_barrel *barrel, int num_to_print);

/* Set the class prior probabilities by counting the number of
   documents of each class. note this counts all train and unlabeled
   docs.  Note that we're doing an m-estimate thing-y by starting
   out as one doc each per class. */
void bow_em_set_priors_using_class_probs (bow_barrel *vpc_barrel,
					  bow_barrel *doc_barrel);

#endif /* __BOW_EM_H */
