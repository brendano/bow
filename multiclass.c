#include <bow/libbow.h>
#include <argp.h>
#include <bow/crossbow.h>

/* Thursday am - changed total_num_mixtures_possible calculation
                 changed palpha from 1.0 to 0.01
                 changed malpha from 0 to 1
		 changed pruning class set size from 4 to 3 */

extern bow_int4str *crossbow_classnames;

static double multiclass_uniform_prior;
static double multiclass_uniform_new_prior;
static double multiclass_mixture_prior_alpha;
static double multiclass_mixture_prior_normalizer;

static double **cached_mixture = NULL;
void multiclass_mixture_clear_cache ();

int compare_ints (const void *x, const void *y)
{
  if (*(int*)x > *(int*)y)
    return 1;
  else if (*(int*)x == *(int*)y)
    return 0;
  else
    return -1;
}


#define CCC crossbow_classes_count

#define DOING_COMBO 1


/* Plus 1 for no class */
#define MAX_NUM_CLASSES (100 + 1)
/* Plus one for root, and plus one for uniform */
#define MAX_NUM_MIXTURE_CLASSES 20
#define MAX_NUM_MIXTURES (MAX_NUM_MIXTURE_CLASSES + 1 + 1)
typedef struct _mcombo {
  double prior;
  double new_prior;
  int doc_count;
  /* The class indicies for this multi-label combination */
  /* Indexed by cisi, up to cis_size-1; + root + uniform */
  int cis[MAX_NUM_MIXTURE_CLASSES];
  int cis_size;
  /* Indexed by cisi, up to cis_size-1; + root + uniform */
  /* The mixture weights for this multi-label combination */
  double m[MAX_NUM_MIXTURES];
  double new_m[MAX_NUM_MIXTURES];
  /* each dimension indexed by CI+1 */
} cmixture;

/* Info on all class mixtures */
static cmixture *cm = NULL;

/* The number of entries in the above */
static int cm_length = 0;

typedef struct _multiclass_score {
    double score;
    int c[MAX_NUM_MIXTURE_CLASSES];
} multiclass_score;

static int
compare_multiclass_scores (const void *x, const void *y)
{
  if (((multiclass_score*)x)->score > ((multiclass_score*)y)->score)
    return -1;
  else if (((multiclass_score*)x)->score == ((multiclass_score*)y)->score)
    return 0;
  else
    return 1;
}


/* Return a pointer to the cmixture structure for the specific class
   set specified by CIS.  If CREATE_NEW is non-zero, then create a
   cmixture entry if one doesn't already exist.  The actual number of 
   classes in the mixture is returned in ACTUAL_SIZE */
cmixture *
cmixture_for_cis (const int *cis, int cis_size, int create_new, 
		  int *actual_size)
{
  static bow_int4str *cmi_map = NULL;
  static int cm_size = 0;
  int cmi, cisi, num_chars, real_size;
  static const int cis_name_size = 512;
  char cis_name[cis_name_size], *cis_name_p;

  assert (cis_size <= MAX_NUM_MIXTURE_CLASSES);
  cis_name_p = cis_name;
  real_size = 0;
  for (cisi = 0; cisi < cis_size && cis[cisi] >= 0; cisi++)
    {
      num_chars = sprintf (cis_name_p, "%d,", cis[cisi]);
      cis_name_p += num_chars;
      assert (cis_name_p - cis_name <= cis_name_size);
      real_size++;
    }
  if (actual_size)
    *actual_size = real_size;
  if (!cmi_map)
    cmi_map = bow_int4str_new (0);
  if (create_new)
    cmi = bow_str2int (cmi_map, cis_name);
  else
    cmi = bow_str2int_no_add (cmi_map, cis_name);
  if (cmi < 0)
    return NULL;
  if (cmi >= cm_length)
    {
      /* Add a new entry for this class mixture combination */
      cm_length++;
      if (cm == NULL)
	{
	  cm_size = 128;
	  cm = bow_malloc (cm_size * sizeof (cmixture));
	}
      if (cmi >= cm_size)
	{
	  cm_size *= 2;
	  cm = bow_realloc (cm, cm_size * sizeof (cmixture));
	}

      bow_verbosify (bow_verbose, "New entry for ");
      for (cisi = 0; cisi < real_size; cisi++)
	bow_verbosify (bow_verbose, "%s,", 
		       bow_int2str (crossbow_classnames, cis[cisi]));
      bow_verbosify (bow_verbose, "\n");

      /* Initialize the new CM entry */
      cm[cmi].prior = 0;
      cm[cmi].new_prior = 0;
      cm[cmi].doc_count = 0;
      cm[cmi].cis_size = real_size;
      for (cisi = 0; cisi < real_size; cisi++)
	{
	  cm[cmi].cis[cisi] = cis[cisi];
	  cm[cmi].m[cisi] = 1.0 / real_size;
	  cm[cmi].new_m[cisi] = 0;
	}
      for (cisi = real_size; cisi < MAX_NUM_MIXTURE_CLASSES; cisi++)
	  cm[cmi].cis[cisi] = -1;
      for (cisi = real_size; cisi < MAX_NUM_MIXTURES; cisi++)
	{
	  cm[cmi].m[cisi] = 0;
	  cm[cmi].new_m[cisi] = 0;
	}
    }
  return &(cm[cmi]);
}

void
cmixture_set_from_new (int set_p_flag, double p_alpha, double m_alpha)
{
  double p_sum;
  double m_sum;
  int cmi, l, total_num_mixtures_possible;
  cmixture *m;

  /* Get normalization constants */
  assert (MAX_NUM_CLASSES > crossbow_classes_count);
  p_sum = 0;

  for (cmi = 0; cmi < cm_length; cmi++)
    {
      m = &(cm[cmi]);
      p_sum += m->new_prior + p_alpha;
      /* Don't touch the mixtures cached at test time. */
      if (m->doc_count <= 0)
	continue;
      m_sum = 0;
      assert (m->cis_size+2 <= MAX_NUM_MIXTURES);
      for (l = 0; l < m->cis_size+2; l++)
	m_sum += m->new_m[l] + m_alpha;
      assert (m_sum);
      for (l = 0; l < m->cis_size+2; l++)
	{
	  m->m[l] = (m->new_m[l] + m_alpha) / m_sum;
	  assert (m->m[l] > 0);
	  m->new_m[l] = 0;
	  assert (m->m[l] <= 1.0 && m->m[l] >= 0.0);
	}
    }
  /* xxx This number possible is an over-estimate? */
  total_num_mixtures_possible = 1;
  for (l = crossbow_classes_count; 
       (l > (crossbow_classes_count - MAX_NUM_MIXTURE_CLASSES)
	&& l >= 1);
       l--)
    total_num_mixtures_possible *= l;

  p_sum += (total_num_mixtures_possible - cm_length) * p_alpha;
  assert (p_sum > 0);
  multiclass_mixture_prior_alpha = p_alpha;
  multiclass_mixture_prior_normalizer = p_sum;

  /* Set p and m's from normalized new data, and zero the new data */
  for (cmi = 0; cmi < cm_length; cmi++)
    {
      m = &(cm[cmi]);
      if (set_p_flag)
	{
	  m->prior = (m->new_prior + p_alpha) / p_sum;
	  assert (m->prior <= 1.0 && m->prior >= 0.0);
	}
      m->new_prior = 0;
    }

  /* Clear the mixture cache so it will get reset */
  multiclass_mixture_clear_cache ();
}

void
cmixture_print_diagnostics (FILE *out)
{
  int i, l, cmi;
  cmixture *m;

  for (cmi = 0; cmi < cm_length; cmi++)
    {
      m = &(cm[cmi]);
      /* Skip over class mixtures that have no training data */
      if (m->doc_count <= 0)
	continue;
      /* Print the list of classes */
      for (i = 0; i < MAX_NUM_MIXTURE_CLASSES; i++)
	if (m->cis[i] >= 0)
	  fprintf (out, "%s,", bow_int2str (crossbow_classnames, m->cis[i]));
      fprintf (out, " prior=%g ", m->prior);
      for (l = 0; l < m->cis_size+2; l++)
	fprintf (out, "%g,", m->m[l]);
      fprintf (out, "\n");
    }
}



void
multiclass_place_labeled_data ()
{
  int di, wvi;
  crossbow_doc *doc;
  treenode *node;
  bow_wv *wv;
  int cmi, cisi;
  cmixture *m;
  int l, cis_size;

  /* Clear all previous information. */
  bow_treenode_set_new_words_to_zero_all (crossbow_root);
  bow_treenode_free_loo_and_new_loo_all (crossbow_root, crossbow_docs->length);
  bow_treenode_set_prior_from_new_prior_all (crossbow_root, 0);
  multiclass_uniform_new_prior = 0;

  /* Clear MC */
  for (cmi = 0; cmi < cm_length; cmi++)
    {
      cm[cmi].doc_count = 0;
      cm[cmi].prior = 0;
      cm[cmi].new_prior = 0;
      for (l = 0; l < MAX_NUM_MIXTURES; l++)
	{
	  cm[cmi].m[l] = 0;
	  cm[cmi].new_m[l] = 0;
	}
    }

  /* Initialize the word distributions and LOO entries with the data
     and initialize lambdas to uniform */
  for (di = 0; di < crossbow_docs->length; di++)
    {
      doc = bow_array_entry_at_index (crossbow_docs, di);
      
      /* Make sure that the CIS are in sorted order */
      if (doc->cis_size > 1)
	qsort (doc->cis, doc->cis_size, sizeof (int), compare_ints);

      /* If space for this document's mixture hasn't already been allocated,
	 do that now. */
      if (doc->cis_mixture == NULL)
	doc->cis_mixture = bow_malloc ((doc->cis_size + 2) * sizeof (double));

      wv = crossbow_wv_at_di (di);
      if (doc->tag != bow_doc_train)
	continue;
      /* Temporary fix */
      if (strstr (doc->filename, ".include")
	  || strstr (doc->filename, ".exclude"))
	continue;

      /* Put the data in each of the leaf classes to which the 
	 document belongs, and lastly the root. */
      for (cisi = 0; cisi <= doc->cis_size; cisi++)
	{
	  if (cisi == doc->cis_size)
	    node = crossbow_root;
	  else
	    {
	      assert (crossbow_root->children_count > doc->cis[cisi]);
	      node = crossbow_root->children[doc->cis[cisi]];
	    }
	  node->new_prior++;
	  multiclass_uniform_new_prior++;
	  for (wvi = 0; wvi < wv->num_entries; wvi++)
	    {
	      node->new_words[wv->entry[wvi].wi] += wv->entry[wvi].count;
	      bow_treenode_add_new_loo_for_di_wvi
		(node, wv->entry[wvi].count, di, wvi,
		 wv->num_entries, crossbow_docs->length);
	    }
	}

      /* Put data into MC */
      m = cmixture_for_cis (doc->cis, doc->cis_size, 1, &cis_size);
      assert (cis_size == doc->cis_size);
      m->doc_count++;
      m->new_prior += 1.0;
      for (cisi = 0; cisi < doc->cis_size+2; cisi++)
	m->new_m[cisi] += 1.0;
    }

  bow_treenode_set_prior_and_extra_from_new_prior_all
    (crossbow_root, &multiclass_uniform_new_prior, 
     &multiclass_uniform_prior, 0);
  bow_treenode_set_words_from_new_words_all (crossbow_root, 0);
  cmixture_set_from_new (1, 0.01, 1);
}


double
multiclass_cis_overlap (int *cis1, int cis1_size, int *cis2, int cis2_size)
{
  int cisi1, cisi2;
  double overlap = 0;

#if 0
  if (cis1_size == cis2_size)
    overlap++;
#endif
  for (cisi1 = cisi2 = 0; cisi2 < cis2_size; cisi2++)
    {
      while (cisi1 < cis1_size && cis1[cisi1] < cis2[cisi2])
	cisi1++;
      if (cis1[cisi1] == cis2[cisi2])
	overlap++;
    }
  return 2 * overlap / (cis1_size + cis2_size);
}


/* Erase the cached information used by MULTICLASS_MIXTURE_GIVEN_CIS(),
   forcing it to be re-calculated. */
void
multiclass_mixture_clear_cache ()
{
  int cisi, cmi;

  if (cached_mixture)
    {
      for (cisi = 0; cisi < MAX_NUM_MIXTURE_CLASSES; cisi++)
	if (cached_mixture[cisi])
	  bow_free (cached_mixture[cisi]);
      bow_free (cached_mixture);
      cached_mixture = NULL;
    }

  /* Clear the CMIXTURE cache by changing the special "has cached
     average mixture" flag of -1 back to the "simply has no data, no
     cached mixture" of 0. */
  for (cmi = 0; cmi < cm_length; cmi++)
    {
      if (cm[cmi].doc_count == -1)
	cm[cmi].doc_count = 0;
    }
}

/* Place into MIXTURE the mixture weights for the class set specified by 
   CIS.  When this class set appeared in the training data, this is simply
   a matter of copying the mixtures from the global CM structure.  When it
   didn't, various forms of backoff are used.  This function caches its
   backoff calculations.  The above function clears the cache, which should 
   happen any time mixtures in CM are changed. */
void
multiclass_mixture_given_cis (int *cis, int cis_size, double *mixture)
{
  cmixture *m;
  int cisi;

  assert (cis_size <= MAX_NUM_MIXTURE_CLASSES);
  if (cached_mixture == NULL)
    {
      cached_mixture = bow_malloc((MAX_NUM_MIXTURE_CLASSES+1)*sizeof(double*));
      /* Entry 0 never gets used. */
      for (cisi = 0; cisi < MAX_NUM_MIXTURE_CLASSES+1; cisi++)
	cached_mixture[cisi] = NULL;
    }

  m = cmixture_for_cis (cis, cis_size, 0, 0);
  if (m && !m->doc_count == 0)
    {
      /* This set of classes exists in the training data, use the 
	 MAP-calculated mixture weights. */
      for (cisi = 0; cisi < cis_size + 2; cisi++)
	mixture[cisi] = m->m[cisi];
    }
  else
    {
      /* This set of classes appeared nowhere in the training data,
	 backoff to an average of related mixtures, and cache the
	 results in a (possibly) new CMIXTURE extry. */
      int cmi, cisimb;
      double bmixture_sum, *mixture_count, similarity;
      cmixture *mb;

      /* Make sure that there is at least one training document
	 with this label. */
      for (cisi = 0; cisi < cis_size; cisi++)
	assert (crossbow_root->children[cis[cisi]]->prior);

      /* Get a (possibly new) CMIXTURE extry; We will set
         DOC_COUNT==-1 to indicate that it has a mixture cached from
         the following calculation. */
      m = cmixture_for_cis (cis, cis_size, 1, 0);
      assert (m->doc_count == 0);
      m->doc_count = -1;

      mixture_count = alloca (MAX_NUM_MIXTURES * sizeof (double));
      for (cisi = 0; cisi < cis_size+2; cisi++)
	{
	  m->m[cisi] = 0;
	  mixture_count[cisi] = 0;
	}
      /* Go through all mixtures for which there is training data */
      for (cmi = 0; cmi < cm_length; cmi++)
	{
	  mb = &(cm[cmi]);
	  if (mb->doc_count <= 0)
	    continue;
	  similarity = multiclass_cis_overlap (mb->cis, mb->cis_size,
					       cis, cis_size);
	  if (similarity == 0)
	    continue;
	  for (cisimb = cisi = 0; cisimb < mb->cis_size; cisimb++)
	    {
	      while (cisi < cis_size && cis[cisi] < mb->cis[cisimb])
		cisi++;
	      if (mb->cis[cisimb] == cis[cisi])
		{
		  m->m[cisi] += mb->m[cisimb] * similarity;
		  assert (m->m[cisi] == m->m[cisi]);
		  mixture_count[cisi] += similarity;
		}
	    }
	  /* Likewise for the root and uniform mixtures */
	  m->m[cis_size] += mb->m[mb->cis_size] * similarity;
	  mixture_count[cis_size] += similarity;
	  m->m[cis_size+1] += mb->m[mb->cis_size+1] * similarity;
	  mixture_count[cis_size+1] += similarity;
	}

      /* Take the average of each column */
      for (cisi = 0; cisi < cis_size+2; cisi++)
	{
	  assert (mixture_count[cisi]);
	  m->m[cisi] /= mixture_count[cisi];
	  assert (m->m[cisi] == m->m[cisi]);
	}
      /* Normalize the mixture to sum to one */
      bmixture_sum = 0;
      for (cisi = 0; cisi < cis_size+2; cisi++)
	bmixture_sum += m->m[cisi];
      assert (bmixture_sum > 0);
      /* Normalize and put into MIXTURE for return */
      for (cisi = 0; cisi < cis_size+2; cisi++)
	{
	  m->m[cisi] /= bmixture_sum;
	  mixture[cisi] = m->m[cisi];
	}
    }

#if 0
  double normalizer = 0;
  int cisi2;
  /* Another (unused) estimate based on adding all mixtures */
  /* This mixture did not occur in the training data, used a smoothed
     estimate. */
  for (cisi = 0; cisi < cis_size + 2; cisi++)
    mixture[cisi] = 1.0;
  normalizer = (cis_size + 2) * 1.0;
  for (cmi = 0; cmi < cm_length; cmi++)
    {
      for (cisi = cisi2 = 0; cisi2 < cm[cmi]->cis_size; cisi2++)
	{
	  while (cisi < cisi2)
	    cisi++;
	  if (cm[cmi]->cis[cisi2] == cis[cisi])
	    {
	      mixture[cisi] += cm[cmi]->cis[cisi2];
	      normalizer += mixture[cisi];
	    }
	}
    }
  for (cisi = 0; cisi < cis_size; cisi++)
    mixture[cisi] /= normalizer;
  return;
#endif

#if 0
  /* Another (unused) option is to use a completely factored 
     representation */
  /* Calculcate normalized mixture weights just from the treenode priors,
     i.e., not using the CMIXTURE.  These may not actually get used.  */
  /* Plus one for the root, plus one for the uniform */
  double mixture_prior_sum;
  mixture_weights = alloca ((cis_size + 1 + 1) * sizeof (double));
  mixture_prior_sum = 0;
  for (cisi = 0; cisi < cis_size; cisi++)
    {
      assert (cis[cisi] >= 0);
      mixture_prior_sum += crossbow_root->children[cis[cisi]]->prior;
    }
  mixture_prior_sum += crossbow_root->prior + multiclass_uniform_prior;
  for (cisi = 0; cisi < cis_size; cisi++)
    if (cis[cisi] >= 0)
      mixture_weights[cisi] = 
	crossbow_root->children[cis[cisi]]->prior / mixture_prior_sum;
  mixture_weights[cis_size] = crossbow_root->prior / mixture_prior_sum;
  mixture_weights[cis_size+1] = multiclass_uniform_prior / mixture_prior_sum;
#endif
}

/* MIXTURE must be as large as CIS_SIZE+2 */
void
multiclass_mixture_given_doc_and_cis (crossbow_doc *doc, 
				      int *cis, int cis_size,
				      double *mixture)
{
  bow_wv *wv;
  double *cis_mixture;
  double mixture_sum;
  treenode *node;
  int cisi, wvi;
  int num_nodes;
  double *node_data_prob;
  double node_data_prob_sum;
  double *node_membership;

  wv = crossbow_wv_at_di (doc->di);
  cis_mixture = alloca (sizeof (double) * (cis_size + 2));
  multiclass_mixture_given_cis (cis, cis_size, cis_mixture);

  num_nodes = crossbow_root->children_count + 1 + 1;
  node_membership = alloca (num_nodes * sizeof (double));
  node_data_prob = alloca (num_nodes * sizeof (double));

  for (cisi = 0; cisi <= cis_size+1; cisi++)
    mixture[cisi] = 0;
  mixture_sum = 0;

  for (wvi = 0; wvi < wv->num_entries; wvi++)
    {
      /* Analagous to the per-word E-step */
      node_data_prob_sum = 0;
      for (cisi = 0; cisi <= cis_size; cisi++)
	{
	  if (cisi == cis_size)
	    node = crossbow_root;
	  else
	    node = crossbow_root->children[cis[cisi]];
	  node_data_prob[cisi] = cis_mixture[cisi] *
	    bow_treenode_pr_wi_loo_local(node,wv->entry[wvi].wi,doc->di,wvi);
	  assert (node_data_prob[cisi] >= 0);
	  node_data_prob_sum += node_data_prob[cisi];
	}
      /* For the uniform distribution */
      node_data_prob[cis_size+1] = cis_mixture[cis_size+1] *
	(1.0 / bow_num_words ());
      assert (node_data_prob[cis_size+1] >= 0);
      node_data_prob_sum += node_data_prob[cis_size+1];
      assert (node_data_prob_sum != 0);

      /* Normalize the node data probs, so they are membership
	 probabilities. */
      for (cisi = 0; cisi <= cis_size+1; cisi++)
	node_membership[cisi] = 
	  node_data_prob[cisi] / node_data_prob_sum;

      /* Analagous to the per-word M-step */
      for (cisi = 0; cisi <= cis_size+1; cisi++)
	{
	  mixture[cisi] += wv->entry[wvi].count * node_membership[cisi];
	  mixture_sum += mixture[cisi];
	}
    }

  /* Normalize the mixture to be returned */
  for (cisi = 0; cisi <= cis_size+1; cisi++)
    mixture[cisi] /= mixture_sum;
}

/* MIXTURE must be as large as CIS_SIZE+2 */
void
multiclass_iterated_mixture_given_doc_and_cis (crossbow_doc *doc, 
					       int *cis, int cis_size,
					       double *mixture)
{
  bow_wv *wv;
  double *cis_mixture;
  double mixture_sum;
  treenode *node;
  int cisi, wvi;
  int num_nodes;
  double *node_data_prob;
  double node_data_prob_sum;
  double *node_membership;
  //double pp, old_pp;

  wv = crossbow_wv_at_di (doc->di);
  cis_mixture = alloca (sizeof (double) * (cis_size + 2));
  multiclass_mixture_given_cis (cis, cis_size, cis_mixture);

  num_nodes = crossbow_root->children_count + 1 + 1;
  node_membership = alloca (num_nodes * sizeof (double));
  node_data_prob = alloca (num_nodes * sizeof (double));

  for (cisi = 0; cisi <= cis_size+1; cisi++)
    mixture[cisi] = 0;
  mixture_sum = 0;

  for (wvi = 0; wvi < wv->num_entries; wvi++)
    {
      /* Analagous to the per-word E-step */
      node_data_prob_sum = 0;
      for (cisi = 0; cisi <= cis_size; cisi++)
	{
	  if (cisi == cis_size)
	    node = crossbow_root;
	  else
	    node = crossbow_root->children[cis[cisi]];
	  node_data_prob[cisi] = cis_mixture[cisi] *
	    bow_treenode_pr_wi_loo_local(node,wv->entry[wvi].wi,doc->di,wvi);
	  assert (node_data_prob[cisi] >= 0);
	  node_data_prob_sum += node_data_prob[cisi];
	}
      /* For the uniform distribution */
      node_data_prob[cis_size+1] = cis_mixture[cis_size+1] *
	(1.0 / bow_num_words ());
      assert (node_data_prob[cis_size+1] >= 0);
      node_data_prob_sum += node_data_prob[cis_size+1];
      assert (node_data_prob_sum != 0);

      /* Normalize the node data probs, so they are membership
	 probabilities. */
      for (cisi = 0; cisi <= cis_size+1; cisi++)
	node_membership[cisi] = 
	  node_data_prob[cisi] / node_data_prob_sum;

      /* Analagous to the per-word M-step */
      for (cisi = 0; cisi <= cis_size+1; cisi++)
	{
	  mixture[cisi] += wv->entry[wvi].count * node_membership[cisi];
	  mixture_sum += mixture[cisi];
	}
    }

  /* Normalize the mixture to be returned */
  for (cisi = 0; cisi <= cis_size+1; cisi++)
    mixture[cisi] /= mixture_sum;
}


/* MIXTURE must be as large as CROSSBOW_ROOT->CHILDREN_COUNT+2 */
void
multiclass_mixture_given_doc (crossbow_doc *doc, 
			      double *mixture)
{
  int mixture_count = crossbow_root->children_count + 2;
  bow_wv *wv;
  double mixture_sum;
  treenode *node;
  int mi, wvi;
  double node_membership_sum;
  double *node_membership;

  wv = crossbow_wv_at_di (doc->di);
  node_membership = alloca (mixture_count * sizeof (double));

  for (mi = 0; mi < mixture_count; mi++)
    mixture[mi] = 0;
  mixture_sum = 0;

  for (wvi = 0; wvi < wv->num_entries; wvi++)
    {
      /* Analagous to the per-word E-step */
      node_membership_sum = 0;
      for (mi = 0; mi <= mixture_count-2; mi++)
	{
	  if (mi == mixture_count-2)
	    node = crossbow_root;
	  else
	    node = crossbow_root->children[mi];
	  if (doc->tag == bow_doc_train || doc->tag == bow_doc_unlabeled)
	    node_membership[mi] = 
	      bow_treenode_pr_wi_loo_local (node,wv->entry[wvi].wi,
					    doc->di,wvi);
	  else
	    node_membership[mi] = node->words[wv->entry[wvi].wi];
	  assert (node_membership[mi] >= 0);
	  node_membership_sum += node_membership[mi];
	}
      /* For the uniform distribution */
      node_membership[mixture_count-1] = 1.0 / bow_num_words ();
      node_membership_sum += node_membership[mixture_count-1];
      assert (node_membership_sum != 0);

      /* Normalize the node data probs, so they are membership
	 probabilities. */
      for (mi = 0; mi < mixture_count; mi++)
	node_membership[mi] = node_membership[mi] / node_membership_sum;

      /* Analagous to the per-word M-step */
      for (mi = 0; mi < mixture_count; mi++)
	{
	  mixture[mi] += wv->entry[wvi].count * node_membership[mi];
	  mixture_sum += mixture[mi];
	}
    }

  /* Normalize the mixture to be returned */
  assert (mixture_sum);
  for (mi = 0; mi < mixture_count; mi++)
    {
      mixture[mi] /= mixture_sum;
      //assert (mixture[mi] > 0);
    }
}

/* Return the most likely mixture over mixture components, assuming
   that we are already committed to including the classes in CIS, and
   that we probabilistically remove the words that they account for.
   MIXTURE must be as large as CROSSBOW_ROOT->CHILDREN_COUNT+2 */
void
multiclass_mixture_given_doc_and_partial_cis (crossbow_doc *doc, 
					      const int *cis, int cis_size,
					      const int *exclude_cis, 
					      int exclude_cis_size,
					      double *mixture)
{
  int mixture_count = crossbow_root->children_count + 2;
  bow_wv *wv;
  double mixture_sum;
  treenode *node;
  int mi, wvi, cisi;
  double node_membership_sum;
  double *node_membership;
  double *node_word_prob;
  double average_word_prob_cis, incr;

  wv = crossbow_wv_at_di (doc->di);
  node_membership = alloca (mixture_count * sizeof (double));
  node_word_prob = alloca (mixture_count * sizeof (double));

  for (mi = 0; mi < mixture_count; mi++)
    mixture[mi] = 0;
  mixture_sum = 0;

  for (wvi = 0; wvi < wv->num_entries; wvi++)
    {
      /* Analagous to the per-word E-step */
      node_membership_sum = 0;
      for (mi = 0; mi <= mixture_count-2; mi++)
	{
	  if (mi == mixture_count-2)
	    node = crossbow_root;
	  else
	    node = crossbow_root->children[mi];
	  node_word_prob[mi] = 
	    bow_treenode_pr_wi_loo_local (node,wv->entry[wvi].wi,doc->di,wvi);
	  node_membership[mi] = node_word_prob[mi];
	  assert (node_membership[mi] >= 0);
	}
      /* For the uniform distribution */
      node_membership[mixture_count-1] = 1.0 / bow_num_words ();

      /* Calculate the average word probability of the classes
	 explicitly included with CIS, and the always-included root
	 and uniform distribution.  Zero the mixture probabilities for
	 those mixtures. */
      average_word_prob_cis = 0;
      for (cisi = 0; cisi < cis_size; cisi++)
	{
	  average_word_prob_cis += node_membership[cis[cisi]];
	  node_membership[cis[cisi]] = 0;
	}
      average_word_prob_cis += node_membership[mixture_count-2];
      node_membership[mixture_count-2] = 0;
      average_word_prob_cis += node_membership[mixture_count-1];
      node_membership[mixture_count-1] = 0;
      average_word_prob_cis /= cis_size + 2;

      /* Zero the probabilities of the classes explicitly excluded */
      for (cisi = 0; cisi < exclude_cis_size; cisi++)
	node_membership[exclude_cis[cisi]] = 0;

      /* Subtract the average  */
      for (mi = 0; mi < mixture_count; mi++)
	{
	  node_membership[mi] -= average_word_prob_cis;
	  if (node_membership[mi] < 0) 
	    node_membership[mi] = 0;
	  node_membership_sum += node_membership[mi];
	}

#if 1
      /* If any of the NODE_MEMBERSHIP's are non-zero, normalize the
	 node data probs, so they are membership probabilities. */
      if (node_membership_sum != 0)
	for (mi = 0; mi < mixture_count; mi++)
	  node_membership[mi] = node_membership[mi] / node_membership_sum;
#endif

      /* Analagous to the per-word M-step */
      for (mi = 0; mi < mixture_count; mi++)
	{
	  if (node_membership[mi] == 0)
	    continue;
	  incr= (wv->entry[wvi].count * node_membership[mi]
		 * log (node_word_prob[mi]/average_word_prob_cis));
	  assert (incr >= 0);
	  mixture[mi] += incr;
	  mixture_sum += mixture[mi];
	}
    }

  /* Normalize the mixture to be returned */
  for (mi = 0; mi < mixture_count; mi++)
    mixture[mi] /= mixture_sum;
}


/* Return the perplexity */
double
multiclass_em_one_iteration ()
{
  int di;
  crossbow_doc *doc;
  bow_wv *wv;
  treenode *node;
  int cisi, wvi;
  int num_nodes;
  double *node_word_prob, log_prob_of_data2;
  double node_membership_sum, word_prob, log_prob_of_data, deposit;
  int num_data_words = 0;	/* the number of word occurrences */
  double *node_membership;
  cmixture *m;
  int cis_size;
  double *mixture_all;

  /* One node for each topic, plus one for all-english, plus one for uniform */
  num_nodes = crossbow_root->children_count + 1 + 1;
  node_membership = alloca (num_nodes * sizeof (double));
  node_word_prob = alloca (num_nodes * sizeof (double));
  mixture_all = alloca ((crossbow_root->children_count+2) * sizeof(double));

  log_prob_of_data = log_prob_of_data2 = 0;
  for (di = 0; di < crossbow_docs->length; di++)
    {
      doc = bow_array_entry_at_index (crossbow_docs, di);
      if (doc->tag != bow_doc_train && doc->tag != bow_doc_unlabeled)
	continue;
      /* Temporary fix */
      if (strstr (doc->filename, ".include")
	  || strstr (doc->filename, ".exclude"))
	continue;

      multiclass_mixture_given_doc (doc, mixture_all);
      bow_verbosify (bow_verbose, "%s ", doc->filename);
      for (cisi = 0; cisi < crossbow_root->children_count+2; cisi++)
	{
	  bow_verbosify (bow_verbose, "%s=%g,",
			 (cisi < crossbow_root->children_count
			  ? bow_int2str (crossbow_classnames, cisi)
			  : (cisi == crossbow_root->children_count
			     ? "root"
			     : "uniform")),
			 mixture_all[cisi]);
	}
      bow_verbosify (bow_verbose, "\n");


      /* Get the word vector for this document, and for each word,
         estimate its membership probability in each of its classes
         (and the root class), and then gather stats for the M-step */
      wv = crossbow_wv_at_di (di);
      m = cmixture_for_cis (doc->cis, doc->cis_size, 0, &cis_size);
      assert (m);
      assert (m->doc_count > 0);
      /* Zero the document-specific mixture in preparation for incrementing */
      for (cisi = 0; cisi < cis_size + 2; cisi++)
	doc->cis_mixture[cisi] = 0;
      for (wvi = 0; wvi < wv->num_entries; wvi++)
	{
	  num_data_words += wv->entry[wvi].count;

	  /* Per-word E-step */
	  node_membership_sum = 0;
	  for (cisi = 0; cisi <= doc->cis_size; cisi++)
	    {
	      if (cisi == doc->cis_size)
		node = crossbow_root;
	      else
		node = crossbow_root->children[doc->cis[cisi]];
	      node_word_prob[cisi] = 
		bow_treenode_pr_wi_loo_local (node, wv->entry[wvi].wi,
					      di, wvi);
	      node_membership[cisi] = m->m[cisi] * node_word_prob[cisi];
	      assert (node_word_prob[cisi] >= 0);
	      node_membership_sum += node_membership[cisi];
	    }
	  /* For the uniform distribution */
	  node_word_prob[doc->cis_size+1] = (1.0 / bow_num_words ());
	  node_membership[doc->cis_size+1] = m->m[doc->cis_size+1] *
	    node_word_prob[doc->cis_size+1];
	  node_membership_sum += node_membership[doc->cis_size+1];
	  assert (node_membership_sum != 0);

	  /* Normalize the node membership probs.  Also increment
             perplexity */
	  word_prob = 0;
	  for (cisi = 0; cisi <= doc->cis_size+1; cisi++)
	    {
	      node_membership[cisi] /= node_membership_sum;
	      word_prob += node_membership[cisi] * node_word_prob[cisi];
	      if (node_membership[cisi])
		log_prob_of_data2 += (node_membership[cisi]
				      * wv->entry[wvi].count
				      * log (node_word_prob[cisi]));
	    }
	  log_prob_of_data += wv->entry[wvi].count * log (word_prob);

	  /* Per-word M-step */
	  for (cisi = 0; cisi <= doc->cis_size; cisi++)
	    {
	      if (cisi == doc->cis_size)
		node = crossbow_root;
	      else
		node = crossbow_root->children[doc->cis[cisi]];
	      deposit = wv->entry[wvi].count * node_membership[cisi];
	      node->new_words[wv->entry[wvi].wi] += deposit;
	      bow_treenode_add_new_loo_for_di_wvi
		(node, deposit, di, wvi, 
		 wv->num_entries, crossbow_docs->length);

	      /* For non-combo version */
	      node->new_prior += deposit;
	      /* For combo version */
	      m->new_m[cisi] += deposit;
	      doc->cis_mixture[cisi] += deposit;
	    }
	  /* For the uniform distribution */
	  deposit = wv->entry[wvi].count * node_membership[doc->cis_size+1];
	  multiclass_uniform_new_prior += deposit;
	  m->new_m[doc->cis_size+1] += deposit;
	  doc->cis_mixture[cis_size+1] += deposit;
	}

      /* Normalize the document-specific CIS_MIXTURE, (and print it out) */
      {
	double max = -FLT_MAX;
	double cis_mixture_sum;
	for (cisi = 0; cisi < cis_size+2; cisi++)
	  if (doc->cis_mixture[cisi] > max)
	    max = doc->cis_mixture[cisi];
	cis_mixture_sum = 0;
	for (cisi = 0; cisi < cis_size+2; cisi++)
	  {
	    //doc->cis_mixture[cisi] = exp (doc->cis_mixture[cisi] - max);
	    cis_mixture_sum += doc->cis_mixture[cisi];
	  }
	bow_verbosify (bow_verbose, "%s ", doc->filename);
	for (cisi = 0; cisi < cis_size+2; cisi++)
	  {
	    doc->cis_mixture[cisi] /= cis_mixture_sum;
	    bow_verbosify (bow_verbose, "%s=%g,",
			   (cisi < cis_size
			    ? bow_int2str (crossbow_classnames, doc->cis[cisi])
			    : (cisi == cis_size
			       ? "root"
			       : "uniform")),
			   doc->cis_mixture[cisi]);
	  }
	bow_verbosify (bow_verbose, "\n");
      }
    }
  /* Normalize all per-word M-step results */
  bow_treenode_set_words_from_new_words_all (crossbow_root, 0.0);
  bow_treenode_set_prior_and_extra_from_new_prior_all
    (crossbow_root, &multiclass_uniform_new_prior,
     &multiclass_uniform_prior, 0.0);
  /* xxx Increase this M? */
  cmixture_set_from_new (0, 0.01, 1);

  bow_verbosify (bow_progress, "PP2=%g\n", 
		 -log_prob_of_data2 / num_data_words);
  return exp (-log_prob_of_data / num_data_words);
}

void
multiclass_train ()
{
  int iteration, max_num_iterations = 999999;
  double pp, old_pp;
  void print_diagnostics ()
    {
      treenode *iterator, *tn;
      printf ("uniform prior=%g\n", multiclass_uniform_prior);
      for (iterator = crossbow_root; 
	   (tn = bow_treenode_iterate_all (&iterator));)
	{
	  printf ("%s prior=%g\n", tn->name, tn->prior);
	  bow_treenode_word_probs_print (tn, 10);
	  printf ("\n");
	  //bow_treenode_word_likelihood_ratios_print (tn, 5);
	}
      cmixture_print_diagnostics (stdout);
    }


  bow_treenode_set_lambdas_leaf_only_all (crossbow_root);
  multiclass_place_labeled_data ();
  print_diagnostics ();

  for (iteration = 0, old_pp = 3000, pp = 2000;
       iteration < max_num_iterations && (old_pp - pp) > 0.0001;
       iteration++)
    {
      old_pp = pp;
      pp = multiclass_em_one_iteration ();
      printf ("PP=%g\n", pp);
      if (old_pp < pp)
	bow_verbosify (bow_progress, "Perplexity rose!\n");
      print_diagnostics ();
    }
}

void
bow_sort_scores (bow_score *scores, int count)
{
  static int score_compare (const void *x, const void *y)
    {
      if (((bow_score *)x)->weight > ((bow_score *)y)->weight)
	return -1;
      else if (((bow_score *)x)->weight == ((bow_score *)y)->weight)
	return 0;
      else
	return 1;
    }
  qsort (scores, count, sizeof (bow_score), score_compare);
}

double
multiclass_log_prob_of_classes_given_doc (int *cis, int cis_size,
					  crossbow_doc *doc)
{
  int cisi, wvi, actual_cis_size;
  double *mixture;
  double log_prob_of_classes;
  bow_wv *wv;
  int wv_word_count;
  //int num_mixtures = cis_size + 2;
  double pr_w;
  cmixture *m;
  static int verbose = 0;
  static int factored_prior = 0;

  /* Get the mcombo entry for this set of classes */
  m = cmixture_for_cis (cis, cis_size, 0, &actual_cis_size);
  assert (actual_cis_size == cis_size);

  /* Allocate space for word-specific mixture weights */
  mixture = alloca ((cis_size + 1 + 1) * sizeof (double));
  multiclass_mixture_given_cis (cis, cis_size, mixture);

  /* Incoporate the prior of this class combination */
  if (factored_prior)
    {
      log_prob_of_classes = 0;
      for (cisi = 0; cisi < cis_size; cisi++)
	if (cis[cisi] >= 0)
	  log_prob_of_classes += 
	    log (crossbow_root->children[cis[cisi]]->prior);
      log_prob_of_classes += log (crossbow_root->prior);
      log_prob_of_classes += log (multiclass_uniform_prior);
    }
  else
    {
      /* If the CIS mixture includes a class that has no training
	 data, then reject by returning an impossibly low score. */
      for (cisi = 0; cisi < cis_size; cisi++)
	if (crossbow_root->children[cis[cisi]]->prior == 0)
	  return -FLT_MAX;
      if (m)
	log_prob_of_classes = log (m->prior);
      else
	log_prob_of_classes = log (multiclass_mixture_prior_alpha
				   / multiclass_mixture_prior_normalizer);
    }

  wv = crossbow_wv_at_di (doc->di);
  wv_word_count = bow_wv_word_count (wv);
  for (wvi = 0; wvi < wv->num_entries; wvi++)
    {
#if 0
      /* Get "complete"-knowledge mixture weights specific to this word */
      static int complete = 0;
      double *mixture_weights;
      double mixture_sum;
      if (complete)
	{
	  mixture_sum = 0;
	  for (cisi = 0; cisi < cis_size; cisi++)
	    if (cis[cisi] >= 0)
	      {
		mixture[cisi] = mixture_weights[cisi] * 
		  crossbow_root->children[cis[cisi]]->words[wv->entry[wvi].wi];
		mixture_sum += mixture[cisi];
	      }
	  mixture[cis_size] = mixture_weights[cis_size] *
	    crossbow_root->words[wv->entry[wvi].wi];
	  mixture_sum += mixture[cis_size];
	  mixture[cis_size+1] = mixture_weights[cis_size+1] *
	    1.0 / bow_num_words ();
	  mixture_sum += mixture[cis_size+1];
	  /* Normalize them */
	  for (cisi = 0; cisi < cis_size+2; cisi++)
	    if (cis[cisi] >= 0)
	      mixture[cisi] /= mixture_sum;
	}
#endif

      pr_w = 0;
      for (cisi = 0; cisi < cis_size; cisi++)
	{
	  if (cis[cisi] >= 0)
	    pr_w += mixture[cisi] * 
	      crossbow_root->children[cis[cisi]]->words[wv->entry[wvi].wi];
	}
      pr_w += mixture[cis_size] * crossbow_root->words[wv->entry[wvi].wi];
      pr_w += mixture[cis_size+1] * 1.0 / bow_num_words ();

      assert (pr_w > 0);
      log_prob_of_classes += wv->entry[wvi].count * log (pr_w);
      
      if (verbose)
	{
	  fprintf (stdout, "%04d %06d %-16s %12.3f %12g ", 
		   doc->di, wv->entry[wvi].wi,
		   bow_int2word (wv->entry[wvi].wi),
		   -log_prob_of_classes, pr_w);
	  for (cisi = 0; cisi < cis_size; cisi++)
	    if (cis[cisi] >= 0)
	      fprintf (stdout, "%s,", 
		       bow_int2str (crossbow_classnames, cis[cisi]));
	  fprintf (stdout, "\n");
	}
    }
#define USE_BIC 0
#if USE_BIC
  return log_prob_of_classes - (num_mixtures-1) / 2 * log(wv_word_count);
#else
  return log_prob_of_classes;
#endif
}

#if 0
static void
multiclass_classify_doc_into_single_class (crossbow_doc *doc,
					   bow_score *scores, int scores_count)
{
  int ci;
  int cis[3];

  assert (scores_count >= crossbow_root->children_count);
  cis[1] = cis[2] = -1;
  for (ci = 0; ci < crossbow_root->children_count; ci++)
    {
      cis[0] = ci;
      scores[ci].di = ci;
      scores[ci].weight = 
	multiclass_log_prob_of_classes_given_doc (cis, 1, doc);
    }
  bow_sort_scores (scores, crossbow_root->children_count);
}
#endif

static int
multiclass_cis_is_in_top (int *cis, multiclass_score *scores, int top_count)
{
  int si;
  int ci;

  /* Temporarily always say yes. */
  return 1;

  for (si = 0; si < top_count; si++)
    {
      for (ci = 0; ci < 3; ci++)
	if (scores[si].c[ci] != cis[ci])
	  break;
      if (ci == 3)
	/* All CIS were matched, return `yes'. */
	return 1;
    }
  /* CIS not found in the TOP_COUNT entries of SCORES.  Return `no'. */
  return 0;
}



/* Good below */


/* Returns 0 when there is no next */
int
multiclass_next_cis (int *cis, int cis_size)
{
  int cisi = cis_size - 1;
  bow_error ("Not implemented");
  while (cis[cisi] < crossbow_root->children_count)
    {
      cis[cis_size]++;
      return 1;
    }
  return 0;
}

int
multiclass_cis_scores_index (int *cis, int cis_size,
			     multiclass_score *scores, int scores_count)
{
  int si, cisi;

  assert (cis_size <= MAX_NUM_MIXTURE_CLASSES);
  for (si = 0; si < scores_count; si++)
    {
      for (cisi = 0; cisi < cis_size; cisi++)
	{
	  if (scores[si].c[cisi] != cis[cisi])
	    goto next_si;
	  else if (cis[cisi] == -1)
	    break;
	}
      return si;
    next_si:
    }
  return -1;
}

/* Allow CIS's that have size 3 or less, or are already in the
   training data. */
int
multiclass_artificially_prune_cis (int *cis, int cis_size)
{
  return (cis_size > 3 && !cmixture_for_cis (cis, cis_size, 0, NULL));
}

/* Greedily add classes to CIS by P(c|d,\vec{c}). */
int
multiclass_explore_cis_greedy0 (crossbow_doc *doc, 
				multiclass_score *scores, int *scores_count,
				int scores_capacity,
				const int *cis, int cis_size, int cis_capacity,
				const int *exclude_cis, int exclude_cis_size,
				int exclude_cis_capacity)
{
  int nc = crossbow_root->children_count;
  int *local_cis, local_cis_size, *local_exclude_cis, local_exclude_cis_size;
  int cisi, ci, ci2, si, max_si = -1;
  int max_ci, max_ci2;
  double max_score;

  local_cis_size =  cis_size + 1;
  if (local_cis_size > MAX_NUM_MIXTURE_CLASSES)
    return 0;

  local_exclude_cis = alloca (exclude_cis_capacity * sizeof(int));
  local_cis = alloca (cis_capacity * sizeof(int));

  local_exclude_cis_size = exclude_cis_size;
  for (cisi = 0; cisi < exclude_cis_capacity; cisi++)
    local_exclude_cis[cisi] = exclude_cis[cisi];
  for (cisi = 0; cisi < cis_capacity; cisi++)
    local_cis[cisi] = cis[cisi];

  max_score = -FLT_MAX;
  max_ci = -1;
  for (ci = 0; ci < nc; ci++)
    {
      if (crossbow_root->children[ci]->prior == 0)
	goto next_class1;
      for (cisi = 0; cisi < exclude_cis_size; cisi++)
	if (exclude_cis[cisi] == ci)
	  goto next_class1;
      for (cisi = 0; cisi < cis_size; cisi++)
	if (cis[cisi] == ci)
	  goto next_class1;

      /* Copy the old CIS into LOCAL_CIS, plus the new class */
      for (cisi = 0; cisi < cis_size; cisi++)
	local_cis[cisi] = cis[cisi];
      local_cis[cis_size] = ci;
      qsort (local_cis, local_cis_size, sizeof (int), compare_ints);
      
      if ((si = multiclass_cis_scores_index (local_cis, local_cis_size, 
					     scores, *scores_count)) == -1
	  && !multiclass_artificially_prune_cis (local_cis, local_cis_size))
	{
	  for (cisi = 0; cisi < MAX_NUM_MIXTURE_CLASSES; cisi++)
	    scores[*scores_count].c[cisi] = local_cis[cisi];
	  scores[*scores_count].score = 
	    multiclass_log_prob_of_classes_given_doc (local_cis, 
						      local_cis_size, doc);
	  if (scores[*scores_count].score > max_score)
	    {
	      max_score = scores[*scores_count].score;
	      max_si = *scores_count;
	      max_ci = ci;
	    }
	  (*scores_count)++;
	  assert (*scores_count < scores_capacity);
	}
      else if (si != -1 && scores[si].score > max_score)
	{
	  max_score = scores[si].score;
	  max_si = si;
	  max_ci = ci;
	}
    next_class1:
    }

  if (local_exclude_cis_size + 1 < exclude_cis_capacity/2
      && local_exclude_cis_size < 5
      && max_ci >= 0)
    {
      /* Do some exploration by making a recursive call that excludes
         the winner */
      local_exclude_cis[local_exclude_cis_size++] = max_ci;
      assert (local_exclude_cis_size < exclude_cis_capacity);
      multiclass_explore_cis_greedy0 (doc, scores, scores_count, 
				      scores_capacity,
				      cis, cis_size, cis_capacity,
				      local_exclude_cis, 
				      local_exclude_cis_size,
				      exclude_cis_capacity);
      local_exclude_cis_size--;
      local_exclude_cis[local_exclude_cis_size] = -1;
    }

  local_cis_size = cis_size + 2;  
  if (local_cis_size > MAX_NUM_MIXTURE_CLASSES)
    return 0;

  max_ci = max_ci2 = -1;
  for (ci = 0; ci < nc; ci++)
    {
      if (crossbow_root->children[ci]->prior == 0)
	goto next_class2;
      for (cisi = 0; cisi < exclude_cis_size; cisi++)
	if (exclude_cis[cisi] == ci)
	  goto next_class2;
      for (cisi = 0; cisi < cis_size; cisi++)
	if (cis[cisi] == ci)
	  goto next_class2;

      for (ci2 = ci+1; ci2 < nc; ci2++)
	{
	  if (crossbow_root->children[ci2]->prior == 0)
	    goto next_class22;
	  for (cisi = 0; cisi < exclude_cis_size; cisi++)
	    if (exclude_cis[cisi] == ci2)
	      goto next_class22;
	  for (cisi = 0; cisi < cis_size; cisi++)
	    if (cis[cisi] == ci2)
	      goto next_class22;

	  /* Copy the old CIS into LOCAL_CIS, plus the new classes */
	  for (cisi = 0; cisi < cis_size; cisi++)
	    local_cis[cisi] = cis[cisi];
	  local_cis[cis_size] = ci;
	  local_cis[cis_size+1] = ci2;
	  qsort (local_cis, local_cis_size, sizeof (int), compare_ints);
      
	  if ((si = multiclass_cis_scores_index
	       (local_cis, local_cis_size, scores, *scores_count)) == -1
	      && !multiclass_artificially_prune_cis (local_cis,local_cis_size))
	    {
	      for (cisi = 0; cisi < MAX_NUM_MIXTURE_CLASSES; cisi++)
		scores[*scores_count].c[cisi] = local_cis[cisi];
	      scores[*scores_count].score = 
		multiclass_log_prob_of_classes_given_doc (local_cis, 
							  local_cis_size, doc);
	      if (scores[*scores_count].score > max_score)
		{
		  max_score = scores[*scores_count].score;
		  max_si = *scores_count;
		  max_ci = ci;
		  max_ci2 = ci2;
		}
	      (*scores_count)++;
	      assert (*scores_count < scores_capacity);
	    }
	  else if (si != -1 && scores[si].score > max_score)
	    {
	      max_score = scores[si].score;
	      max_si = si;
	      max_ci = ci;
	      max_ci2 = ci2;
	    }
	next_class22:
	}
    next_class2:
    }
  assert (max_si >= 0);

  if (local_exclude_cis_size + 2 < exclude_cis_capacity/2
      && local_exclude_cis_size < 5
      && max_ci >= 0 && max_ci2 >= 0)
    {
      /* Do some exploration by making a recursive call that excludes
         the winner */
      local_exclude_cis[local_exclude_cis_size++] = max_ci;
      local_exclude_cis[local_exclude_cis_size++] = max_ci2;
      assert (local_exclude_cis_size < exclude_cis_capacity);
      multiclass_explore_cis_greedy0 (doc, scores, scores_count, 
				      scores_capacity,
				      cis, cis_size, cis_capacity,
				      local_exclude_cis, 
				      local_exclude_cis_size,
				      exclude_cis_capacity);
      local_exclude_cis_size--;
      local_exclude_cis[local_exclude_cis_size] = -1;
      local_exclude_cis_size--;
      local_exclude_cis[local_exclude_cis_size] = -1;
    }


  /* Make a recursive call */
  if (cis_size + 1 < MAX_NUM_MIXTURE_CLASSES)
    {
      /* Copy the current highest scorer into LOCAL_CIS */
      local_cis_size = MAX_NUM_MIXTURE_CLASSES;
      for (cisi = 0; cisi < MAX_NUM_MIXTURE_CLASSES; cisi++)
	{
	  local_cis[cisi] = scores[max_si].c[cisi];
	  if (local_cis_size == 0 && scores[max_si].c[cisi] == -1)
	    local_cis_size = cisi;
	}
      assert (local_cis_size > cis_size);
      if (local_cis_size < MAX_NUM_MIXTURE_CLASSES)
	multiclass_explore_cis_greedy0 (doc, scores, scores_count,
					scores_capacity,
					local_cis, local_cis_size, 
					cis_capacity,
					exclude_cis, exclude_cis_size,
					exclude_cis_capacity);
    }

  return 0;
}

/* Greedily add classes to CIS in the order of their mixture weight. */
int
multiclass_explore_cis_greedy1 (crossbow_doc *doc, 
				multiclass_score *scores, int *scores_count,
				int scores_capacity,
				const int *exclude_cis, int exclude_cis_size,
				int exclude_cis_capacity)
{
  int nc = crossbow_root->children_count;
  int nm = nc + 2;
  double *mixture;
  int *cis, cis_size, *local_exclude_cis, local_exclude_cis_size;
  int cisi, mi;
  double max_mixture;
  int max_mi = -1;
  int top_mi = -1;

  mixture = alloca ((nm) * sizeof(double));
  cis = alloca ((nc+2) * sizeof(int));
  local_exclude_cis = alloca (exclude_cis_capacity * sizeof(int));

  for (cisi = 0; cisi < nc; cisi++)
    cis[cisi] = -1;
  cis_size =  0;
  local_exclude_cis_size = exclude_cis_size;
  for (cisi = 0; cisi < exclude_cis_size; cisi++)
    local_exclude_cis[cisi] = exclude_cis[cisi];

  multiclass_mixture_given_doc (doc, mixture);
  for (cisi = 0; cisi < exclude_cis_size; cisi++)
    mixture[exclude_cis[cisi]] = -1;
  /* Exclude the root and uniform mixtures */
  mixture[nc] = mixture[nc+1] = -1;
  while (cis_size < MAX_NUM_MIXTURE_CLASSES && cis_size < nc)
    {
      max_mixture = -1;
      max_mi = -1;
      for (mi = 0; mi < nc; mi++)
	/* Make sure we don't pick a class that has no training data */
	if (mixture[mi] > max_mixture && crossbow_root->children[mi]->prior)
	  {
	    max_mixture = mixture[mi];
	    max_mi = mi;
	  }
      /* If MAX_MI is still -1, then we didn't find any positive 
	 mixture weight; they were all excluded or zero from 
	 multiclass_mixture_given_doc().  Nothing more to be done;
	 pop out of this loop. */
      if (max_mi < 0)
	break;
      for (cisi = 0; cisi < cis_size; cisi++)
	assert (cis[cisi] != max_mi);
      if (cis_size == 0)
	top_mi = max_mi;
      assert (mixture[max_mi] > 0);
      /* Drive this mixture low so that it will never be max_mi again */
      mixture[max_mi] = -1;
      assert (max_mi < crossbow_root->children_count);
      cis[cis_size++] = max_mi;
      qsort (cis, cis_size, sizeof (int), compare_ints);

      if (multiclass_cis_scores_index (cis, cis_size, 
				       scores, *scores_count) == -1
	  && !multiclass_artificially_prune_cis (cis, cis_size))
	{
	  for (cisi = 0; cisi < MAX_NUM_MIXTURE_CLASSES; cisi++)
	    scores[*scores_count].c[cisi] = cis[cisi];
	  scores[*scores_count].score = 
	    multiclass_log_prob_of_classes_given_doc (cis, cis_size, doc);
	  (*scores_count)++;
	  assert (*scores_count < scores_capacity);
	}
    }
  if (exclude_cis_size < 5 && exclude_cis_size < exclude_cis_capacity)
    {
      assert (top_mi >= 0);
      local_exclude_cis[local_exclude_cis_size++] = top_mi;
      multiclass_explore_cis_greedy1 (doc, scores, scores_count,
				      scores_capacity,
				      local_exclude_cis, 
				      local_exclude_cis_size,
				      exclude_cis_capacity);
    }
  return 0;
}

int
multiclass_explore_cis_greedy2 (crossbow_doc *doc, 
				multiclass_score *scores, int *scores_count,
				int scores_capacity,
				const int *exclude_cis, int exclude_cis_size,
				int exclude_cis_capacity)
{
  int nc = crossbow_root->children_count;
  int nm = nc + 2;
  double *mixture;
  int *cis, cis_size, *local_exclude_cis;
  int cisi, mi;
  double max_mixture;
  double max_mi;

  mixture = alloca ((nm) * sizeof(double));
  cis = alloca ((nc+2) * sizeof(int));
  local_exclude_cis = alloca ((nc+2) * sizeof(int));

  for (cisi = 0; cisi < nc; cisi++)
    cis[cisi] = -1;
  cis_size = 0;

  /* Greedily add classes to the mixture one at a time. */
  while (cis_size < MAX_NUM_MIXTURE_CLASSES)
    {
      multiclass_mixture_given_doc_and_partial_cis
	(doc, cis, cis_size, exclude_cis, exclude_cis_size, mixture);
      max_mixture = -FLT_MAX;
      max_mi = -1;
      for (mi = 0; mi < nm; mi++)
	if (mixture[mi] > max_mixture && mi < nc)
	  {
	    max_mixture = mixture[mi];
	    max_mi = mi;
	  }
      assert (max_mi >= 0);
      cis[cis_size++] = max_mi;
      qsort (cis, cis_size, sizeof (int), compare_ints);

      if (multiclass_cis_scores_index (cis, cis_size, 
				       scores, *scores_count) == -1)
	{
	  for (cisi = 0; cisi < MAX_NUM_MIXTURE_CLASSES; cisi++)
	    scores[*scores_count].c[cisi] = cis[cisi];
	  scores[*scores_count].score = 
	    multiclass_log_prob_of_classes_given_doc (cis, cis_size, doc);
	  (*scores_count)++;
	  assert (*scores_count < scores_capacity);
	}
    }
  return 0;
}

int
multiclass_classify_doc (crossbow_doc *doc, int verbose, FILE *out)
{
  int nc = crossbow_root->children_count;
  int *exclude_cis, exclude_cis_size, exclude_cis_capacity;
  int *cis, cis_size, cis_capacity, cisi, si, scores_count;
  int scores_capacity = nc * nc * nc/2;
  multiclass_score *scores, top_score;

  exclude_cis_capacity = nc+2;
  exclude_cis = alloca (exclude_cis_capacity * sizeof(int));
  scores = bow_malloc (scores_capacity * sizeof (multiclass_score));
  cis_capacity = nc;
  cis = alloca (nc * sizeof(int));

  si = 0;
  for (cisi = 0; cisi < nc; cisi++)
    cis[cisi] = exclude_cis[cisi] = -1;
  exclude_cis_size = 0;
  cis_size = 0;

  scores_count = 0;
  multiclass_explore_cis_greedy0 (doc, scores, &scores_count, scores_capacity,
				  cis, cis_size, cis_capacity,
				  exclude_cis, exclude_cis_size,
				  exclude_cis_capacity);
  multiclass_explore_cis_greedy1 (doc, scores, &scores_count, scores_capacity,
				  exclude_cis, exclude_cis_size,
				  exclude_cis_capacity);
  
  assert (scores_count < scores_capacity);

  qsort (scores, scores_count, sizeof (multiclass_score), 
	 compare_multiclass_scores);

  if (verbose)
    {
      double *mixture = alloca (MAX_NUM_MIXTURES * sizeof (double));
      fprintf (out, "%s ", doc->filename);
      if (doc->cis_size >= 0)
	{
	  for (cisi = 0; cisi < doc->cis_size-1; cisi++)
	    fprintf (out, "%s,", bow_int2str (crossbow_classnames, 
					      doc->cis[cisi]));
	  fprintf (out, "%s ", 
		   bow_int2str (crossbow_classnames, doc->cis[cisi]));
	}
      else
	fprintf (out, "<unknown> ");
      /* Artificially print only the top scores */
      for (si = 0; si < 30; si++)
	{
	  fprintf (out,"%s",bow_int2str(crossbow_classnames,scores[si].c[0]));
	  for (cisi = 1; 
	       cisi < MAX_NUM_MIXTURE_CLASSES && scores[si].c[cisi] >= 0;
	       cisi++)
	    fprintf (out, ",%s", 
		     bow_int2str (crossbow_classnames, scores[si].c[cisi]));
	  fprintf (out, ":%g ", scores[si].score);
	  if (verbose < 2)
	    break;
	}
      fprintf (out, "\n");

      /* Print the mixture of the winner */
      for (cis_size = 0; cis_size < MAX_NUM_MIXTURE_CLASSES; cis_size++)
	if (scores[0].c[cis_size] == -1)
	  break;
      multiclass_mixture_given_cis (scores[0].c, cis_size, mixture);
      for (cisi = 0; cisi < cis_size+2; cisi++)
	fprintf (out, "%g ", mixture[cisi]);
      fprintf (out, "\n");

      /* Print a message if the correct mixture was never even tested. */
      if (multiclass_cis_scores_index (doc->cis, doc->cis_size,
				       scores, scores_count) == -1)
	fprintf (out, "Correct class set was not explored in %d sets.\n",
		 scores_count);
    }

  /* Return 1 iff the classification is completely correct. */
  top_score = scores[0];
  bow_free (scores);
  for (cisi = 0; cisi < crossbow_root->children_count; cisi++)
    {
      if (top_score.c[cisi] == -1 && doc->cis[cisi] == -1)
	return 1;
      if (top_score.c[cisi] != doc->cis[cisi])
	return 0;
    }
  return 1;
}

int
old2_multiclass_classify_doc (crossbow_doc *doc, int verbose, FILE *out)
{
  int nc = crossbow_root->children_count;
  int c1, c2, c3, c4, c5, si, scores_count, cisi, correct;
  int this_cis[MAX_NUM_MIXTURE_CLASSES];
  int scores_capacity = nc * nc * nc;
  multiclass_score *scores, top_score;

  /* xxx More than enough space */
  scores = bow_malloc (scores_capacity * sizeof (multiclass_score));
  si = 0;
  for (c1 = 0; c1 < nc; c1++)
    {
      this_cis[0] = scores[si].c[0] = c1;
      this_cis[1] = scores[si].c[1] = -1;
      this_cis[2] = scores[si].c[2] = -1;
      this_cis[3] = scores[si].c[3] = -1;
      this_cis[4] = scores[si].c[4] = -1;
      scores[si].score = multiclass_log_prob_of_classes_given_doc
	(this_cis, 1, doc);
      si++;
    }

  /* Sort the single-class results, and use these rankings to prune the 
     number of class combinations we evaluate. */
  qsort (scores, si, sizeof (multiclass_score), compare_multiclass_scores);

  for (c1 = 0; c1 < nc; c1++)
    for (c2 = c1+1; c2 < nc; c2++)
      {
	this_cis[0] = scores[si].c[0] = c1;
	this_cis[1] = scores[si].c[1] = c2;
	this_cis[2] = scores[si].c[2] = -1;
	this_cis[3] = scores[si].c[3] = -1;
	this_cis[4] = scores[si].c[4] = -1;
	if (multiclass_cis_is_in_top (this_cis, scores, MAX(nc/3,10)))
	  scores[si].score = multiclass_log_prob_of_classes_given_doc
	    (this_cis, 2, doc);
	else
	  scores[si].score = -FLT_MAX;
	si++;
      }
  for (c1 = 0; c1 < nc; c1++)
    for (c2 = c1+1; c2 < nc; c2++)
      for (c3 = c2+1; c3 < nc; c3++)
	{
	  this_cis[0] = scores[si].c[0] = c1;
	  this_cis[1] = scores[si].c[1] = c2;
	  this_cis[2] = scores[si].c[2] = c3;
	  this_cis[3] = scores[si].c[3] = -1;
	  this_cis[4] = scores[si].c[4] = -1;
	  if (multiclass_cis_is_in_top (this_cis, scores, MAX(nc/3,10)))
	    scores[si].score = multiclass_log_prob_of_classes_given_doc
	      (this_cis, 3, doc);
	  else
	    scores[si].score = -FLT_MAX;
	  si++;
	}
  if (MAX_NUM_MIXTURE_CLASSES < 4)
    goto done_scoring;
  for (c1 = 0; c1 < nc; c1++)
    for (c2 = c1+1; c2 < nc; c2++)
      for (c3 = c2+1; c3 < nc; c3++)
	for (c4 = c3+1; c4 < nc; c4++)
	  {
	    this_cis[0] = scores[si].c[0] = c1;
	    this_cis[1] = scores[si].c[1] = c2;
	    this_cis[2] = scores[si].c[2] = c3;
	    this_cis[3] = scores[si].c[3] = c4;
	    this_cis[4] = scores[si].c[4] = -1;
	    if (multiclass_cis_is_in_top (this_cis, scores, MAX(nc/3,10)))
	      scores[si].score = multiclass_log_prob_of_classes_given_doc
		(this_cis, 4, doc);
	    else
	      scores[si].score = -FLT_MAX;
	    si++;
	  }

  if (MAX_NUM_MIXTURE_CLASSES < 5)
    goto done_scoring;
  for (c1 = 0; c1 < nc; c1++)
    for (c2 = c1+1; c2 < nc; c2++)
      for (c3 = c2+1; c3 < nc; c3++)
	for (c4 = c3+1; c4 < nc; c4++)
	  for (c5 = c4+1; c5 < nc; c5++)
	    {
	      this_cis[0] = scores[si].c[0] = c1;
	      this_cis[1] = scores[si].c[1] = c2;
	      this_cis[2] = scores[si].c[2] = c3;
	      this_cis[3] = scores[si].c[3] = c4;
	      this_cis[4] = scores[si].c[4] = c5;
	      if (multiclass_cis_is_in_top (this_cis, scores, MAX(nc/3,10)))
		scores[si].score = multiclass_log_prob_of_classes_given_doc
		  (this_cis, 5, doc);
	      else
		scores[si].score = -FLT_MAX;
	      si++;
	    }

 done_scoring:
  scores_count = si;
  assert (scores_count < scores_capacity);

  qsort (scores, scores_count, sizeof (multiclass_score), 
	 compare_multiclass_scores);

  if (verbose)
    {
      fprintf (out, "%s ", doc->filename);
      if (doc->cis_size >= 0)
	{
	  for (cisi = 0; cisi < doc->cis_size-1; cisi++)
	    fprintf (out, "%s,", bow_int2str (crossbow_classnames, 
					      doc->cis[cisi]));
	  fprintf (out, "%s ", 
		   bow_int2str (crossbow_classnames, doc->cis[cisi]));
	}
      else
	fprintf (out, "<unknown> ");
      /* Artificially print only the top 20 scores */
      scores_count = 20;
      for (si = 0; si < scores_count; si++)
	{
	  fprintf (out, "%s", 
		   bow_int2str (crossbow_classnames, scores[si].c[0]));
	  if (scores[si].c[1] >= 0)
	    fprintf (out, ",%s", 
		     bow_int2str (crossbow_classnames, scores[si].c[1]));
	  if (scores[si].c[2] >= 0)
	    fprintf (out, ",%s", 
		     bow_int2str (crossbow_classnames, scores[si].c[2]));
	  fprintf (out, ":%g ", scores[si].score);
	  if (verbose < 2)
	    break;
	}
      fprintf (out, "\n");
    }

  top_score = scores[0];
  bow_free (scores);
  correct = 1;
  assert (doc->cis_size <= 3);
  if (top_score.c[0] != doc->cis[0])
    return 0;
  if (doc->cis_size == 1)
    return 1;
  if (top_score.c[1] != doc->cis[1])
    return 0;
  if (doc->cis_size == 2)
    return 1;
  if (top_score.c[2] != doc->cis[2])
    return 0;
  return 1;
}

int
multiclass_old_classify_doc (crossbow_doc *doc, int verbose, FILE *out)
{
  bow_wv *wv;
  int cisi, ni;
  double node_data_prob[crossbow_root->children_count];
  double node_data_prob_sum;
  double node_membership[crossbow_root->children_count];
  bow_score scores[crossbow_root->children_count];
  double score_sum;
  int wvi;
  treenode *node;

  score_sum = 0;
  for (ni = 0; ni < crossbow_root->children_count; ni++)
    scores[ni].weight = 0;

  wv = crossbow_wv_at_di (doc->di);
  for (wvi = 0; wvi < wv->num_entries; wvi++)
    {
      node_data_prob_sum = 0;
      for (ni = 0; ni < crossbow_root->children_count; ni++)
	{
	  node = crossbow_root->children[ni];
	  node_data_prob[ni] = 
	    /* node->prior * */ node->words[wv->entry[wvi].wi];
	  node_data_prob_sum += node_data_prob[ni];
	}

      /* Skip over words with zero probability everywhere */
      if (node_data_prob_sum == 0)
	continue;

      /* Normalize the node data probs, so they are membership probabilities,
	 and increment the classification score of each node */
      for (ni = 0; ni < crossbow_root->children_count; ni++)
	{
	  node_membership[ni] = node_data_prob[ni] / node_data_prob_sum;
	  scores[ni].weight += node_membership[ni] * wv->entry[wvi].count;
	  score_sum += scores[ni].weight;
	}
    }

  /* Normalize the class scores, and fill in DI's */
  for (ni = 0; ni < crossbow_root->children_count; ni++)
    {
      scores[ni].weight /= score_sum;
      scores[ni].di = ni;
    }
  bow_sort_scores (scores, crossbow_root->children_count);

  fprintf (out, "%s ", doc->filename);
  for (cisi = 0; cisi < doc->cis_size-1; cisi++)
    fprintf (out, "%s,", bow_int2str (crossbow_classnames, 
				      doc->cis[cisi]));
  fprintf (out, "%s ", bow_int2str (crossbow_classnames, doc->cis[cisi]));
  for (ni = 0; ni < crossbow_root->children_count; ni++)
    {
      if (scores[ni].weight <= 0)
	break;
      fprintf (out, "%s:%g ", 
	       bow_int2str (crossbow_classnames, scores[ni].di),
	       scores[ni].weight);
    }
  fprintf (out, "\n");

  /* Return non-zero if the top ranked class is among the CIS of this doc */
  for (cisi = 0; cisi < doc->cis_size; cisi++)
    if (doc->cis[cisi] == scores[0].di)
      return 1;
  return 0;
}

crossbow_method multiclass_method =
{
  "multiclass",
  NULL,
  multiclass_train,
  NULL,
  multiclass_classify_doc
};

void _register_method_multiclass () __attribute__ ((constructor));
void _register_method_multiclass ()
{
  bow_method_register_with_name ((bow_method*)&multiclass_method,
				 "multiclass", 
				 sizeof (crossbow_method),
				 NULL);
}
