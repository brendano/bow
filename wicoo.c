#include <bow/libbow.h>

bow_wi2dvf *
bow_wicoo_from_barrel  (bow_barrel *barrel)
{
  bow_wi2dvf *wicoo;
  bow_dv_heap *heap;
  float num_words_in_wv;
  int wvi1, wvi2;
  bow_wv *wv;
  int di;
  bow_dv *dv;

  wicoo = bow_wi2dvf_new (0);

  /* Add statistics for all word co-occurrences. */
  /* And prepare to set IDF to Pr(w) */
  heap = bow_test_new_heap (barrel);
  wv = NULL;
  bow_verbosify (bow_progress,
		 "Calculating word co-occurrences          ");
  while ((di = bow_nontest_next_wv (heap, barrel, &wv))
	 != -1)
    {
      if (di % 10 == 0)
	bow_verbosify (bow_progress, "\b\b\b\b\b\b\b%7d", di);

      /* Calculate the total number of words in WV */
      num_words_in_wv = 0;
      for (wvi1 = 0; wvi1 < wv->num_entries; wvi1++)
	{
#if 0
	  /* Only count those words that are part of the vocabulary. */
	  if (bow_str2int_no_add (rainbowh_arg_state.vocab_map,
				  bow_int2word (wv->entry[wvi1].wi))
	      != -1)
#endif
	    num_words_in_wv += wv->entry[wvi1].count;
	}

      for (wvi1 = 0; wvi1 < wv->num_entries; wvi1++)
	{
	  for (wvi2 = 0; wvi2 < wv->num_entries; wvi2++)
	    {
	      /* Set COUNT to co-occurrence count.
		 Set WEIGHT to probabilistic sampling of document,
		 then word. */
	      bow_wi2dvf_add_wi_di_count_weight
		(&wicoo, wv->entry[wvi1].wi, wv->entry[wvi2].wi,
		 wv->entry[wvi2].count,
		 wv->entry[wvi2].count / num_words_in_wv);
	    }
	  dv = bow_wi2dvf_dv (wicoo, wv->entry[wvi1].wi);
	  /* This relies on IDF being initialized to zero in bow_dv_new() */
	  dv->idf += wv->entry[wvi1].count / num_words_in_wv;
	}
    }

  /* Normalize the IDF's so they are equal to Pr(w) in the corpus. */
  {
    int wi;
    double idf_total = 0;
    for (wi = 0; wi < wicoo->size; wi++)
      {
	dv = bow_wi2dvf_dv (wicoo, wi);
	if (dv)
	  idf_total += dv->idf;
      }
    for (wi = 0; wi < wicoo->size; wi++)
      {
	dv = bow_wi2dvf_dv (wicoo, wi);
	if (dv)
	  dv->idf /= idf_total;
      }
  }

  bow_verbosify (bow_progress, "\n");

  return wicoo;
}

void
bow_wicoo_pr_w_w (bow_wi2dvf *wicoo, int wi1, int wi2)
{
}

void
bow_wicoo_print_word_entropy (bow_wi2dvf *wicoo, int wi)
{
  bow_dv *coov;
  float total_num_coo_words;
  float pr_w_w;
  float total_pr_w_w;
  int coovi;
  float entropy;
  int wi2, max_wi;
  int m_est_m;
  float m_est_p;
  bow_dv *dv2;

  coov = bow_wi2dvf_dv (wicoo, wi);
  if (!coov)
    return;

  total_num_coo_words = 0;
  for (coovi = 0; coovi < coov->length; coovi++)
    total_num_coo_words += coov->entry[coovi].weight;

  entropy = 0;
  max_wi = bow_num_words ();
  m_est_m = wicoo->num_words / 100;
  total_pr_w_w = 0;
  for (wi2 = 0, coovi = 0; wi2 < max_wi; wi2++)
    {
      dv2 = bow_wi2dvf_dv (wicoo, wi2);
      if (!dv2)
	continue;
      m_est_p = dv2->idf;

      while (coov->entry[coovi].di < wi2 && coovi < coov->length)
	coovi++;
      if (coov->entry[coovi].di == wi2)
	{
	  /* Found word WI2 in vector. */
	  pr_w_w = (((float)coov->entry[coovi].weight + m_est_m * m_est_p)
		    / (total_num_coo_words + m_est_m));
	}
      else
	{
	  /* Word WI2 does not co-occur with WI. */
	  pr_w_w = ((m_est_m * m_est_p)
		    / (total_num_coo_words + m_est_m));
	}
#if 1
      printf ("%-30s %12.7f %s\n",
	      bow_int2word (wi), pr_w_w, bow_int2word (wi2));
#endif
      /* pr_w_w = (float)coov->entry[coovi].weight / total_num_coo_words; */
      total_pr_w_w += pr_w_w;
      entropy -= pr_w_w * log (pr_w_w);
    }
  assert (total_pr_w_w > 0.99 && total_pr_w_w < 1.01);
  printf ("%-15.7f %s\n", entropy, bow_int2word (wi));
}

/* Shrink the weights of WV toward documents in BARREL, according to
   their distance to WV. */
void
bow_barrel_shrink_wv (bow_barrel *barrel, bow_wv *wv)
{
  return;
}


