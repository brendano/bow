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

#ifndef __BOW_NBSIMPLE_H
#define __BOW_NBSIMPLE_H

/* Get the total number of terms in each class; store this in
   CDOC->WORD_COUNT. */
void bow_nbsimple_set_cdoc_word_count_from_wi2dvf_weights (bow_barrel *barrel);

int bow_nbsimple_score (bow_barrel *barrel, bow_wv *query_wv, 
			bow_score *bscores, int bscores_len,
			int loo_class);

#endif /* __BOW_NNSIMPLE_H */
