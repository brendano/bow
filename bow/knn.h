/* Copyright (C) 1997, 1999 Andrew McCallum

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

#ifndef __BOW_KNN_H
#define __BOW_KNN_H

/* The method and parameters of k-nearest neighbour weight settings. */
extern rainbow_method bow_method_knn;
typedef struct _bow_knn_params {
  /* Number of neighbours to consider */
  int k;
  /* Both of the following get set to a 3 character string denoting the 
     weighting to be used. The codes are taken from SMART and denote
     
     term_freq x idf x normalisation

     term_freq:
     b - binary
     a - 0.5 + 0.5 * tf/max_tf_in_doc
     l - 1 + ln(tf)
     n - tf
     m - tf/max_tf_in_doc

     idf:
     t - ln(N/n) where N is total docs and n is no docs where term occurs
     n - 1

     normalisation
     c - cosine
     n - none
  */

  /* Weight settings for query doc */
  char query_weights[3];
  /* Weight settings for corpus doc */
  char doc_weights[3];
} bow_params_knn;

#endif /* __BOW_KNN_H */
