/* Barrel weight-scaling */

/* Copyright (C) 1997 Andrew McCallum

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

/* Multiply each weight by the information gain of that word. */
void
bow_barrel_scale_weights_by_given_infogain (bow_barrel *barrel, 
					    float *wi2ig, int wi2ig_size)
{
  int wi, max_wi, j;
  bow_dv *dv;

  bow_verbosify (bow_progress, 
		 "Scaling weights by information gain over words: "
		 "           ");

  max_wi = wi2ig_size;
  for (wi = 0; wi < max_wi; wi++) 
    {
      /* Get this document vector */
      dv = bow_wi2dvf_dv (barrel->wi2dvf, wi);
      if (dv == NULL)
	continue;

      /* Scale all the elements using this gain. */
      for (j = 0; j < dv->length; j++)
	{
	  dv->entry[j].weight *= wi2ig[wi];
	}

      /* Done */
      if (wi % 100 == 0)
	bow_verbosify (bow_progress, "\b\b\b\b\b\b\b\b\b%9d", 
		       max_wi - wi - 1); 
    }
  bow_verbosify (bow_progress, "\n");
}


/* Multiply each weight by the Foil-gain of that word/class
   combination.  BARREL should be the `class_barrel' */
void
bow_barrel_scale_weights_by_given_foilgain (bow_barrel *barrel, 
					    float **fig_per_wi_ci,
					    int num_wi, int num_ci)
{
  int wi, dvi;
  bow_dv *dv;
  bow_cdoc *cdoc;

  bow_verbosify (bow_progress, 
		 "Scaling weights by Foil-gain over words: "
		 "           ");
  
  for (wi = 0; wi < num_wi; wi++) 
    {
      /* Get this document vector */
      dv = bow_wi2dvf_dv (barrel->wi2dvf, wi);
      if (dv == NULL)
	continue;

      /* Scale all the elements using this gain. */
      for (dvi = 0; dvi < dv->length; dvi++)
	{
	  cdoc = bow_array_entry_at_index (barrel->cdocs, dv->entry[dvi].di);
	  dv->entry[dvi].weight *= fig_per_wi_ci[wi][cdoc->class];
	  /* Catch cases in which it's NaN */
	  assert (dv->entry[dvi].weight == dv->entry[dvi].weight);
	}

      /* Done */
      if (wi % 10 == 0)
	bow_verbosify (bow_progress, "\b\b\b\b\b\b\b\b\b%9d", 
		       num_wi - wi - 1); 
    }
  bow_verbosify (bow_progress, "\n");
}


/* Multiply each weight by the Quinlan `Foilgain' of that word. */
void
bow_barrel_scale_weights_by_foilgain (bow_barrel *vpc_barrel,
				      bow_barrel *doc_barrel)
{
  float **fig_per_wi_ci;
  int fig_num_wi;
      
  fig_per_wi_ci = 
    bow_foilgain_per_wi_ci_new (doc_barrel,
				vpc_barrel->cdocs->length,
				&fig_num_wi);
  bow_barrel_scale_weights_by_given_foilgain (vpc_barrel, 
					      fig_per_wi_ci, 
					      fig_num_wi,
					      vpc_barrel->cdocs->length);
  bow_foilgain_free (fig_per_wi_ci, fig_num_wi);
}

/* Multiply each weight by the information gain of that word. */
void
bow_barrel_scale_weights_by_infogain (bow_barrel *vpc_barrel,
				      bow_barrel *doc_barrel)
{
  float *wi2ig;
  int wi2ig_size;
      
  wi2ig = bow_infogain_per_wi_new (doc_barrel,
				   vpc_barrel->cdocs->length,
				   &wi2ig_size);
  bow_barrel_scale_weights_by_given_infogain (vpc_barrel, 
					      wi2ig, wi2ig_size);
  bow_free (wi2ig);
}
