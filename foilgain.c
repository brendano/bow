/* Calculating Quinlan's Foil-gain */

#include <bow/libbow.h>

#if !HAVE_LOG2F
#define log2f log
#endif

/* Return a malloc()'ed array containing an Foil-gain score for
   each word-index for class CI.  BARREL must be a `doc_barrel' */
float *
bow_foilgain_ci_per_wi_new (bow_barrel *barrel, int ci,
			    int num_classes, int *num_wi)
{
  int max_wi = MIN (barrel->wi2dvf->size, bow_num_words());
  float *fig_per_wi;		/* The return value */
  int pos_per_ci;		/* This is p_0 in Tom's book */
  int neg_per_ci;		/* This is n_0 in Tom's book */
  int *pos_per_wi;		/* This is p_1 in Tom's book */
  int *neg_per_wi;		/* This is n_1 in Tom's book */
  int di;			/* a document index */
  int wi;			/* a word index */
  int dvi;
  bow_cdoc *cdoc;
  bow_dv *dv;

  /* Use malloc() instead of alloca() because otherwise (I think) we
     were running out of stack space. */
  pos_per_wi = bow_malloc (max_wi * sizeof (int));
  neg_per_wi = bow_malloc (max_wi * sizeof (int));
  
  *num_wi = max_wi;
  fig_per_wi = bow_malloc (max_wi * sizeof (float));

  /* Initialize all the counts to zero. */
  pos_per_ci = 0;
  neg_per_ci = 0;
  for (wi = 0; wi < max_wi; wi++)
    {
      pos_per_wi[wi] = 0;
      neg_per_wi[wi] = 0;
    }

  /* Loop over all documents, incrementing {pos,neg}_per_ci counts. */
  for (di = 0; di < barrel->cdocs->length; di++)
    {
      cdoc = bow_array_entry_at_index (barrel->cdocs, di);
      /* This document is a positive instance of class CDOC->CLASS,
	 and a negative instance of all other classes. */
      if (cdoc->class == ci)
	pos_per_ci++;
      else
	neg_per_ci++;
    }

  /* Loop over all words and document vectors, incremeting the
     {pos,neg}_per_wi_ci counts. */
  for (wi = 0; wi < max_wi; wi++)
    {
      dv = bow_wi2dvf_dv (barrel->wi2dvf, wi);

      /* Deal with empty document vectors. */
      if (!dv)
	{
	  /* There are no documents with word WI.  Increment
	     our negative count for this WI/CI combination by the
	     total number of documents. */
	  neg_per_wi[wi] += barrel->cdocs->length;
	  continue;
	}

      /* There are some documents containing this word, increment our
	 negative count for this WI/CI combination by the number that
	 are missing from the document vector DV. */
      assert (barrel->cdocs->length - dv->length >= 0);
      neg_per_wi[wi] += barrel->cdocs->length - dv->length;
      
      /* Look at the classes of each of the documents in the document
	 vector.  Increment the positive count for this WI/CI pair;
	 also increment the WI negative count for the classes that
	 each document is not. */
      for (dvi = 0; dvi < dv->length; dvi++)
	{
	  cdoc = bow_array_entry_at_index (barrel->cdocs, dv->entry[dvi].di);
	  /* This document is a positive instance of class CDOC->CLASS,
	     and a negative instance of all other classes. */
	  if (cdoc->class == ci)
	    pos_per_wi[wi]++;
	  else
	    neg_per_wi[wi]++;
	}
    }

  /* Fill in FIG_PER_WI_CI for each word/class combination. */
  {
    float bits_pre_split =
      - (log2f (((float)pos_per_ci)
		/ (pos_per_ci + neg_per_ci)));
    assert (pos_per_ci + neg_per_ci > 0);
    assert (bits_pre_split == bits_pre_split);
    assert (bits_pre_split >= 0);
    for (wi = 0; wi < max_wi; wi++)
      {
	if (pos_per_wi[wi] == 0)
	  {
	    fig_per_wi[wi] = 0;
	  }
	else
	  {
	    int gdb_pos = pos_per_wi[wi];
	    int gdb_neg = neg_per_wi[wi];
	    float bits_post_split = 
	      - (log2f (((float)pos_per_wi[wi])
			/ (pos_per_wi[wi] + neg_per_wi[wi])));

	    fig_per_wi[wi] = 
	      (pos_per_wi[wi] 
	       * (bits_post_split - bits_pre_split));
		 
	    /* Catch cases in which it's NaN */
	    assert (fig_per_wi[wi] == fig_per_wi[wi]);
	    assert (gdb_pos + gdb_neg > 0);
	  }
      }
  }

  bow_free (pos_per_wi);
  bow_free (neg_per_wi);

  return fig_per_wi;
}


/* Return a malloc()'ed array containing an Foil-gain score for
   each ``word-index / class pair''.  BARREL must be a `doc_barrel' */
float **
bow_foilgain_per_wi_ci_new (bow_barrel *barrel, int num_classes, int *num_wi)
{
  int max_wi = MIN (barrel->wi2dvf->size, bow_num_words());
  float **fig_per_wi_ci;		/* The return value */
  int pos_per_ci[num_classes];	/* This is p_0 in Tom's book */
  int neg_per_ci[num_classes];	/* This is n_0 in Tom's book */
  int **pos_per_wi_ci;		/* This is p_1 in Tom's book */
  int **neg_per_wi_ci;		/* This is n_1 in Tom's book */
  int ci;			/* a class index */
  int di;			/* a document index */
  int wi;			/* a word index */
  int dvi;
  bow_cdoc *cdoc;
  bow_dv *dv;

  /* Use malloc() instead of alloca() because otherwise (I think) we
     were running out of stack space. */
  pos_per_wi_ci = bow_malloc (max_wi * sizeof (int*));
  neg_per_wi_ci = bow_malloc (max_wi * sizeof (int*));
  for (wi = 0; wi < max_wi; wi++)
    {
      pos_per_wi_ci[wi] = bow_malloc (num_classes * sizeof (int));
      neg_per_wi_ci[wi] = bow_malloc (num_classes * sizeof (int));
    }
  
  *num_wi = max_wi;
  fig_per_wi_ci = bow_malloc (max_wi * sizeof (float*));
  for (wi = 0; wi < max_wi; wi++)
    fig_per_wi_ci[wi] = malloc (num_classes * sizeof (float));

  /* Initialize all the counts to zero. */
  for (ci = 0; ci < num_classes; ci++)
    {
      pos_per_ci[ci] = 0;
      neg_per_ci[ci] = 0;
      for (wi = 0; wi < max_wi; wi++)
	{
	  pos_per_wi_ci[wi][ci] = 0;
	  neg_per_wi_ci[wi][ci] = 0;
	}
    }

  /* Loop over all documents, incrementing {pos,neg}_per_ci counts. */
  for (di = 0; di < barrel->cdocs->length; di++)
    {
      cdoc = bow_array_entry_at_index (barrel->cdocs, di);
      /* This document is a positive instance of class CDOC->CLASS,
	 and a negative instance of all other classes. */
      for (ci = 0; ci < num_classes; ci++)
	{
	  if (cdoc->class == ci)
	    pos_per_ci[ci]++;
	  else
	    neg_per_ci[ci]++;
	}
    }

  /* Loop over all words and document vectors, incremeting the
     {pos,neg}_per_wi_ci counts. */
  for (wi = 0; wi < max_wi; wi++)
    {
      dv = bow_wi2dvf_dv (barrel->wi2dvf, wi);

      /* Deal with empty document vectors. */
      if (!dv)
	{
	  /* There are no documents with word WI.  Increment
	     our negative count for this WI/CI combination by the
	     total number of documents. */
	  for (ci = 0; ci < num_classes; ci++)
	    neg_per_wi_ci[wi][ci] += barrel->cdocs->length;
	  continue;
	}

      /* There are some documents containing this word, increment our
	 negative count for this WI/CI combination by the number that
	 are missing from the document vector DV. */
      for (ci = 0; ci < num_classes; ci++)
	{
	  assert (barrel->cdocs->length - dv->length >= 0);
	  neg_per_wi_ci[wi][ci] += barrel->cdocs->length - dv->length;
	}
      
      /* Look at the classes of each of the documents in the document
	 vector.  Increment the positive count for this WI/CI pair;
	 also increment the WI negative count for the classes that
	 each document is not. */
      for (dvi = 0; dvi < dv->length; dvi++)
	{
	  cdoc = bow_array_entry_at_index (barrel->cdocs, dv->entry[dvi].di);
	  /* This document is a positive instance of class CDOC->CLASS,
	     and a negative instance of all other classes. */
	  for (ci = 0; ci < num_classes; ci++)
	    {
	      if (cdoc->class == ci)
		pos_per_wi_ci[wi][ci]++;
	      else
		neg_per_wi_ci[wi][ci]++;
	    }
      }
    }

  /* Fill in FIG_PER_WI_CI for each word/class combination. */
  for (ci = 0; ci < num_classes; ci++)
    {
      float bits_pre_split =
	- (log2f (((float)pos_per_ci[ci])
		  / (pos_per_ci[ci] + neg_per_ci[ci])));
      assert (pos_per_ci[ci] + neg_per_ci[ci] > 0);
      assert (bits_pre_split == bits_pre_split);
      assert (bits_pre_split >= 0);
      for (wi = 0; wi < max_wi; wi++)
	{
	  if (pos_per_wi_ci[wi][ci] == 0)
	    {
	      fig_per_wi_ci[wi][ci] = 0;
	    }
	  else
	    {
	      int gdb_pos = pos_per_wi_ci[wi][ci];
	      int gdb_neg = neg_per_wi_ci[wi][ci];
	      float bits_post_split = 
		- (log2f (((float)pos_per_wi_ci[wi][ci])
			  / (pos_per_wi_ci[wi][ci] + neg_per_wi_ci[wi][ci])));

	      fig_per_wi_ci[wi][ci] = 
		(pos_per_wi_ci[wi][ci] 
		 * (bits_post_split - bits_pre_split));
		 
	      /* Catch cases in which it's NaN */
	      assert (fig_per_wi_ci[wi][ci] == fig_per_wi_ci[wi][ci]);
	      assert (gdb_pos + gdb_neg > 0);
	    }
	}
    }
  for (wi = 0; wi < max_wi; wi++)
    {
      bow_free (pos_per_wi_ci[wi]);
      bow_free (neg_per_wi_ci[wi]);
    }
  bow_free (pos_per_wi_ci);
  bow_free (neg_per_wi_ci);

  return fig_per_wi_ci;
}

/* Free the memory allocated in the return value of the function
   bow_foilgain_per_wi_ci_new() */
void
bow_foilgain_free (float **fig_per_wi_ci, int num_wi)
{
  int wi;

  for (wi = 0; wi < num_wi; wi++)
    bow_free (fig_per_wi_ci[wi]);
  bow_free (fig_per_wi_ci);
}

