/* Weight-setting and scoring implementation for Naive-Bayes classification */

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

#include <bow/libbow.h>
#include <math.h>
#include <argp/argp.h>

/* 

My reading of the SMART documentation and code makes me think that the
various tf weight options - particularly 'l' and 'a' apply only to
words that occur in the document in question - which avoids any taking
the log of 0. 

If you want to check this, look at the table at
http://pi0959.kub.nl:2080/Paai/Onderw/Smart/examp_10.html 
(Linked to by the Advanced actions part of the SMART tutorial at
http://pi0959.kub.nl:2080/Paai/Onderw/Smart/hands-on-tekst.html#advanced)
and also look at the tfwt_log function in the SMART source tree at
src/libconvert/weights_tf.c.

The weighting options implemented here are:
Position 1 - TF. If f == 0 then TF == 0. Otherwise, for f > 0
'n' - none     - f
'b' - binary   - 1
'm' - max-norm - f / (max f in doc)
'a' - aug-norm - 0.5 + 0.5 * (f / (max f in doc))
'l' - log      - 1.0 + ln(f)

Position 2 - IDF.
'n' - none     - 1.0
't' - tfidf    - ln (total docs / docs containing term)

Position 3 - NORM
'n' - none     - 1.0
'c' - cosine   - 1 / sqrt (sum (tf * idf)**2)
*/

/* Command-line options specific to kNN */

/* Default value for option "knn_k", the number of neighbours to look
   at. */
static int knn_k = 30;

/* Default values for the weighting schemes */
static char query_weights[4] = "nnn";
static char doc_weights[4] = "nnn";

/* The integer or single char used to represent this command-line option.
   Make sure it is unique across all libbow and rainbow. */
#define KNN_K_KEY 4001
#define KNN_WEIGHTING_KEY 4002

static struct argp_option knn_options[] =
{
  {0,0,0,0,
   "K-nearest neighbor options, --method=knn:", 40},
  {"knn-k", KNN_K_KEY, "K", 0,
   "Number of neighbours to use for nearest neighbour. Defaults to "
   "30."},
  {"knn-weighting", KNN_WEIGHTING_KEY, "xxx.xxx", 0,
   "Weighting scheme to use, coded like SMART. Defaults to nnn.nnn"
   "The first three chars describe how the model documents are"
   "weighted, the second three describe how the test document is"
   "weighted. The codes for each position are described in knn.c."
   "Classification consists of summing the scores per class for the"
   "k nearest neighbour documents and sorting."
  },
  {0, 0}
};

error_t
knn_parse_opt (int key, char *arg, struct argp_state *state)
{
  switch (key)
    {
    case KNN_K_KEY:
      knn_k = atoi (arg);
      break;
    case KNN_WEIGHTING_KEY:
      /* Arg is a string that we need to split into two bits */
      strncpy(query_weights, arg, 3);
      strncpy(doc_weights, arg + 4, 3);
      break;
    default:
      return ARGP_ERR_UNKNOWN;
    }
  return 0;
}

static const struct argp knn_argp =
{
  knn_options,
  knn_parse_opt
};

static struct argp_child knn_argp_child =
{
  &knn_argp,     		/* This child's argp structure */
  0,				/* flags for child */
  0,				/* optional header in help message */
  0				/* arbitrary group number for ordering */
};

/* End of command-line options specific to kNN */

/* Some useful macros */
#define TF_M(x) ((x)[0] == 'm')
#define TF_B(x) ((x)[0] == 'b')
#define TF_A(x) ((x)[0] == 'a')
#define TF_L(x) ((x)[0] == 'l')
#define TF_N(x) ((x)[0] == 'n')

#define IDF_T(x) ((x)[1] == 't')

#define NORM_C(x) ((x)[2] == 'c')

/* Function to assign tfidf weights to every word in the barrel
   according to the contents of doc_weights */
void
bow_knn_set_weights (bow_barrel *barrel)
{
  int di;
  bow_cdoc *cdoc;
  int wi;			/* a "word index" into WI2DVF */
  int max_wi;			/* the highest "word index" in WI2DVF. */
  bow_dv *dv;			/* the "document vector" at index WI */
  int dvi;			/* an index into the DV */
  int num_docs, total_model_docs;

  /* We assume we are dealing with the full document barrel - no 
     whimpy vector-per-class stuff here. */
    
  assert (!strcmp (barrel->method->name, "knn"));
  max_wi = MIN (barrel->wi2dvf->size, bow_num_words());

  /* Step one calculate the number of documents in the model. We'll
     use this for the idf calculation later on. Also, reset each
     document's word_count - we're going to use this to store the max
     tf in the document which is needed by some of the tf weighting
     methods. */

  total_model_docs = 0;

  for (di = 0; di < barrel->cdocs->length; di++)
    {
      cdoc = bow_cdocs_di2doc(barrel->cdocs, di);
      if (cdoc->type == bow_doc_train) 
	{
	  total_model_docs++;
	  cdoc->word_count = 0;
	}
    }

  /* Step two - we can calculate weights for the b, l and n weighting
     schemes now. For a we can calculate the max tf in each
     document. We can also calculate the idf term and store it. */
  for (wi = 0; wi < max_wi; wi++)
    {
      /* Count the number of model docs this word occurs in */
      num_docs = 0;
	  
      dv = bow_wi2dvf_dv (barrel->wi2dvf, wi);
      if (dv == NULL) 
	continue; 
      for (dvi = 0; dvi < dv->length; dvi++)  
	{ 
	  cdoc = bow_cdocs_di2doc(barrel->cdocs, dv->entry[dvi].di);
	  if (cdoc->type == bow_doc_train)
	    {
	      num_docs++;

	      /* Set some weights */
	      if (TF_B(doc_weights))
		{
		  /* Binary counts */
		  dv->entry[dvi].weight = 1;
		}
	      else if (TF_L(doc_weights))
		{
		  /* 1 + ln(tf) */
		  dv->entry[dvi].weight = 1 + log(dv->entry[dvi].count);
		}
	      else if (TF_N(doc_weights))
		{
		  /* tf */
		  dv->entry[dvi].weight = dv->entry[dvi].count;
		}
	      else
		{
		  /* Update the max tf count */
		  if (cdoc->word_count < dv->entry[dvi].count)
		    {
		      cdoc->word_count = dv->entry[dvi].count;
		    }
		}
	    }
	}
      /* Set up the IDF for this word */
      dv->idf = log((double)total_model_docs / (double)num_docs);
    }

  /* Final Step - calculate weights for methods that use max tf
     stuff. Also multiply in the IDF terms. */ 
  for (wi = 0; wi < max_wi; wi++) 
    { 
      dv = bow_wi2dvf_dv (barrel->wi2dvf, wi); 
      if (dv == NULL)  
	continue;  
      for (dvi = 0; dvi < dv->length; dvi++)   
	{  
	  cdoc = bow_cdocs_di2doc(barrel->cdocs, dv->entry[dvi].di); 
	  if (cdoc->type == bow_doc_train) 
	    {
	      if (TF_A(doc_weights))
		{
		  /* 0.5 + 0.5 * (tf / max_tf_in_doc) */
		  dv->entry[dvi].weight = 0.5 + 0.5 * ((double)dv->entry[dvi].count / (double)cdoc->word_count);
		}
	      else if (TF_M(doc_weights))
		{
		  /* tf / max_tf_in_doc */
		  dv->entry[dvi].weight = (double)dv->entry[dvi].count / (double)cdoc->word_count;
		}

	      /* Do the IDF */
	      if (IDF_T(doc_weights))
		  dv->entry[dvi].weight *= dv->idf;
	    }
	}
    }

  /* Now our barrel has the tf*idf weight for each term in each
     document in our model */
}

void bow_knn_normalise_weights (bow_barrel *barrel)
{
  /* This puts the euclidian doc length in cdoc->normalizer for each
     document in the model. */
  if (NORM_C(doc_weights))
    {
      bow_barrel_normalize_weights_by_vector_length(barrel);
    }
}


bow_barrel *
bow_knn_classification_barrel (bow_barrel *barrel)
{
  /* Just use the doc barrel - set the weights, normalise and return. */

  bow_knn_set_weights(barrel);
  bow_knn_normalise_weights(barrel);

  return barrel;
}

/* Set the weights for the query word vector according to the
   weighting scheme in query_weights */
void bow_knn_query_set_weights(bow_wv *query_wv, bow_barrel *barrel)
{
  bow_dv *dv;
  int wvi, max_tf;

  /* null to statement to avoid compilation warning */
  barrel = barrel;

  /* Pass one - set weights for b,l or n. Figure out the maximum
     word frequency of the document. */
  max_tf = 0;
  for (wvi = 0; wvi < query_wv->num_entries; wvi++)
    {
      if (TF_B(query_weights))
	{
	  query_wv->entry[wvi].weight = 1;
	}
      else if (TF_L(query_weights))
	{
	  query_wv->entry[wvi].weight = 1 + log(query_wv->entry[wvi].count);
	}
      else if (TF_N(query_weights))
	{
	  query_wv->entry[wvi].weight = query_wv->entry[wvi].count;
	}
      else if (max_tf < query_wv->entry[wvi].count)
	{
	  max_tf = query_wv->entry[wvi].count;
	}
    }

  /* Pass two - Get the correct weights for 'a' or 'm'. Do IDF if
     required. */
  for(wvi = 0; wvi < query_wv->num_entries; wvi++) 
    { 
      if (TF_M(query_weights))
	{
	  query_wv->entry[wvi].weight = (double)query_wv->entry[wvi].count / (double) max_tf;
	}
      else if (TF_A(query_weights))
	{
	  query_wv->entry[wvi].weight = 0.5 + 0.5 * ((double)query_wv->entry[wvi].count / (double) max_tf);
	}

      /* Cheat here - we can leverage off the fact that I've so
	 far only implemented  one IDF method. Otherwise I'd have
	 to store raw statistics for the word occurances for each
	 word. */
      if (IDF_T(query_weights))
	  {
	    dv = bow_wi2dvf_dv (barrel->wi2dvf,	query_wv->entry[wvi].wi);
	    query_wv->entry[wvi].weight *= dv->idf;
	  }
    }

  /* Done - the idf term was calculated earlier on */
}

void
bow_knn_normalise_query_weights (bow_wv *query)
{
  if (NORM_C(query_weights))
    {
      bow_wv_normalize_weights_by_vector_length(query);
    }
}

/* Little :) function lifted from tfidf.c and edited to do exactly
   what we need here. Was originally called bow_tfidf_score */
int
bow_knn_get_k_best (bow_barrel *barrel, bow_wv *query_wv,  
		    bow_score *scores, int best)
{
  bow_dv_heap *heap; 
  bow_cdoc *doc; 
  int num_scores = 0;           /* How many elements are in this array */ 
  int current_di, wi, current_index, i; 
  double current_score = 0.0, doc_tfidf; 
  float tmp; 

  /* Create the Heap of vectors of documents */ 
  heap = bow_make_dv_heap_from_wv (barrel->wi2dvf, query_wv); 
 
  /* Keep looking at document/word entries until the heap is emptied */ 
  while (heap->length > 0) 
    { 
      /* Get the index of the document we're currently working on */ 
      current_di = heap->entry[0].current_di; 
 
      /* Get the document structure */ 
      doc = bow_cdocs_di2doc (barrel->cdocs, current_di); 

      /* If it's not a model document, then move on to next one */ 
      if (doc->type != bow_doc_train) 
        { 
          do  
            { 
              bow_dv_heap_update (heap); 
            } 
          while ((current_di == heap->entry[0].current_di) 
                 && (heap->length > 0)); 
         
          /* Try again */ 
          continue; 
        } 
 
      /* Reset the index into the query word vector */ 
      current_index = 0; 
 
      /* Reset the score */ 
      current_score = 0.0; 
 
      /* Loop over all the words this document has in common with our
	 query document, summing up the score. We know the words come
	 out of the heap in index order and we know the words in the
	 query word vector are in index order as well. */
      do 
        { 
          wi = heap->entry[0].wi; 
          doc_tfidf = heap->entry[0].dv->entry[heap->entry[0].index].weight;

	  /* Find the corresponding word in the query word vector */ 
	  /* Note - we know this word is in the query because we built
	     the heap using only the query words. */
          while (wi > (query_wv->entry[current_index].wi)) 
            current_index++; 
          assert (wi == query_wv->entry[current_index].wi); 

	  /* Now we can add something to the score. Normalisation
	     happens outside this loop, we just need to check for the
	     idf factor stuff. The tf weights are just fine. */

	  /* Multiply the tfidf weights */
	  /* printf("%f * %f\n", query_wv->entry[current_index].weight,
		 doc_tfidf); */

	  tmp = query_wv->entry[current_index].weight * doc_tfidf;

	  /* Plop this into the current score */
	  current_score += tmp;

	  /* A test to make sure we haven't got NaN. */ 
          assert (current_score == current_score); 
 
          /* Now we need to update the heap - moving this element on
	     to its 
             new position */ 
          bow_dv_heap_update (heap); 
        } 
      while ((current_di == heap->entry[0].current_di) 
             && (heap->length > 0)); 

      /* Now check for normalisation */
      if(NORM_C(query_weights))
	{
	  current_score *= query_wv->normalizer;
	}
      if(NORM_C(doc_weights))
	{
	  current_score *= doc->normalizer;
	}

      assert (current_score == current_score); /* checking for NaN */ 

      /* We now hopefully have the correct score for doc */

      /* Store the result in the SCORES array */ 
      /* If we haven't filled the list, or we beat the last item in
	 the list */ 
      if ((num_scores < best) 
          || (scores[num_scores - 1].weight < current_score)) 
        { 
          /* We're going to search up the list comparing element i-1 with 
             our current score and moving it down the list if it's worse */ 
          if (num_scores < best) 
            { 
              i = num_scores; 
              num_scores++; 
            } 
          else 
            i = num_scores - 1; 

	  /* Shift down all the bits of the array that need shifting */ 
          for (; (i > 0) && (scores[i - 1].weight < current_score); i--) 
	    {
	      scores[i].di = scores[i-1].di; 
	      scores[i].weight = scores[i-1].weight; 
	      scores[i].name = scores[i-1].name; 
	    }
 
          /* Insert our new score */ 
          scores[i].weight = current_score; 
          scores[i].di = current_di;
	  scores[i].name = doc->filename;
        } 
    } 

  bow_free (heap); 
 
  /* All done - return the number of elements we have */ 
  return num_scores; 
} 


/* Get class scores using closest K neighbors.  */
int
bow_knn_score (bow_barrel *barrel, bow_wv *query_wv, 
	       bow_score *bscores, int bscores_len,
	       int loo_class)
{
  int count;
  int ni,ci;
  double scores_sum = 0.0;
  int num_scores;  
  bow_score *neighbors;         /* Place to hold scores of nearest neighbors */
  double *scores;

  /* This should be initialized in case BSCORES_LEN is larger than the number
   * of classes in the barrel */
  for (ci=0; ci < bscores_len; ci++)
    {
      bscores[ci].weight = 0.0;
      bscores[ci].di = 0;
      bscores[ci].name = "default";
    }

  /* Get scores of neighbors */
  neighbors = alloca (sizeof (bow_score) * knn_k);
  count = bow_knn_get_k_best (barrel, query_wv, neighbors, knn_k);

  /* Allocate space for class scores */
  scores = alloca (bow_barrel_num_classes (barrel) * sizeof (double));
  for (ci=0; ci < bow_barrel_num_classes (barrel); ci++)
    scores[ci] = 0.0;

  /* Put contributing document scores into class scores */
  for (ni=0; ni < count; ni++)
    {
      /* Get the class of this document */
      bow_cdoc *doc = bow_cdocs_di2doc (barrel->cdocs, neighbors[ni].di);
      scores[doc->class] += neighbors[ni].weight;
      scores_sum += neighbors[ni].weight;
    }

  num_scores = 0;

  /* Put SCORES into BSCORES in sorted order */
  /* Each round, find the best remainaing score and put it into bscores */
  for (ci=0; ci < bow_barrel_num_classes (barrel); ci++)
    {
      if (num_scores < bscores_len
	  || bscores[num_scores-1].weight < scores[ci])
	{
	  int dsi;
	  /* We are going to put this score and class index into SCORES
	   * because either 1) there is an empty space in SCORES, or 2)
	   * SCORES[CI] is larger than the smallest score currently there */
	  if (num_scores < bscores_len)
	    num_scores++;
	  dsi = num_scores - 1;
	  /* Shift down all the entries that are smaller than SCORES[CI] */
	  for (; dsi > 0 && bscores[dsi-1].weight < scores[ci]; dsi--)
	    {
	      bscores[dsi].weight = bscores[dsi-1].weight;
	      bscores[dsi].name = bscores[dsi-1].name;
	      bscores[dsi].di = bscores[dsi-1].di;
	    }
	  bscores[dsi].weight = scores[ci];
	  bscores[dsi].di = ci;
	  bscores[dsi].name = "default";
	}
    }
  return num_scores;
}


rainbow_method bow_method_knn = 
{
  "knn",
  bow_knn_set_weights,
  0,				/* no weight scaling function */
  bow_knn_normalise_weights,
  bow_knn_classification_barrel,
  NULL,                         /* We don't do priors */
  bow_knn_score,
  bow_knn_query_set_weights,
  bow_knn_normalise_query_weights,
  bow_barrel_free,
  0
};

void _register_method_knn () __attribute__ ((constructor));
void _register_method_knn ()
{
  bow_method_register_with_name ((bow_method*)&bow_method_knn, "knn",
				 sizeof (rainbow_method),
				 &knn_argp_child);
  bow_argp_add_child (&knn_argp_child);
}
