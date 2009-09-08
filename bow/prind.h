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

#ifndef __BOW_PRIND_H
#define __BOW_PRIND_H

/* The method and parameters of Prind weight settings. */
extern rainbow_method bow_method_prind;
typedef struct _bow_prind_params {
  /* If this is non-zero, use uniform class priors. */
  bow_boolean uniform_priors;
  /* If this is zero, do not normalize the PrInd classification scores. */
  bow_boolean normalize_scores;
} bow_params_prind;

#endif /* __BOW_PRIND_H */
