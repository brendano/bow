/* Copyright (C) 1999 Greg Schohn - gcs@jprc.com */

/* ******************** svm_fisher.c ******************* 
 * An implementation of the naive bayes fisher kernel.
 * This is still a work very much in progress, ridden with
 * numerical precision problems.
 */

#include <bow/svm.h>

static bow_barrel *rainbow_nb_barrel;
static double fisher_norm0;

static int total_num_words_occurences;
static double *dIi;  /* approximate diagonal inverse information matrix for classes */
static double *dIij; /* approximate diagonal inverse information matrix for class-words */

typedef struct _NPair {
  int N;
  int index;
} NPair;

void svm_set_fisher_barrel_weights(bow_wv **docs, int ndocs) {
  int i,j;
  total_num_words_occurences = 0;
  for (i=0; i<ndocs; i++) {
    docs[i]->normalizer = 1.0;
    for (j=0; j<docs[i]->num_entries; j++) {
      docs[i]->entry[j].weight = (float) docs[i]->entry[j].count;
      total_num_words_occurences += docs[i]->entry[j].count;
    }
  }
}

double svm_kernel_fisher(bow_wv *wv1, bow_wv *wv2) {
  bow_cdoc  *cd;
  int        max_entries;   /* max number of elements that can be in both */
  int        nclasses;
  int        nwords;
  NPair     *Nvector;
  double     rval;
  double     tmp;
  bow_we    *v1, *v2;

  double t2, pci;
  int i, j, k;

  nwords = total_num_words_occurences;
  nclasses = bow_barrel_num_classes(rainbow_nb_barrel);
  max_entries = MIN(wv1->num_entries, wv2->num_entries);

  Nvector = (NPair *) alloca(max_entries*sizeof(NPair));

  v1 = wv1->entry;
  v2 = wv2->entry;

  /* compute the N(wi,X1)*N(wi,X2) vector */
  for (i=j=k=0; (i<max_entries) && (j<max_entries); ) {
    if(v1[i].wi > v2[j].wi) {
      j++;
    }
    else if (v1[i].wi < v2[j].wi) {
      i++;
    }
    else {
      Nvector[k].index = v1[i].wi;
      Nvector[k].N = (v1[i].count)*(v2[j].count);
      k++;
      i++;
      j++;
    }
  }

  max_entries = k;
  rval = 0.0;

  /* now we have all of the P(X*|C*) terms - in ascending order with
   * regards to class index */
  for (i=0; i<nclasses; i++) {
    for (j=0, tmp=0; j<max_entries; j++) {
      t2 = bow_naivebayes_pr_wi_ci(rainbow_nb_barrel, Nvector[j].index, i, -1, 0, 0, NULL, NULL);
      t2 = t2*t2;
      tmp += Nvector[j].N / (dIij[i*nclasses + Nvector[j].index] * t2);
      assert(finite(tmp));
    }

    /* compute P(x[12]|ci)/P(x[12]) */
    {
      double p_w;
      bow_we *v;
      bow_wv *w;
      int k,h,n;
      
      t2 = fisher_norm0;

      for (w=wv1, n=0; n<2; n++, w=wv2) {
	v = w->entry;
	for (h=0; h<w->num_entries; h++) {
	  double sum, t;
	  bow_dv *dv = bow_wi2dvf_dv(rainbow_nb_barrel->wi2dvf, v[h].wi);
	  assert(dv);
	  /* sum up the number of words that appeared in all of the classes */
	  for (k=0, sum=0.0; k<dv->length; k++) {
	    sum += dv->entry[k].weight;
	  }

	  p_w = log(sum/nwords)*v[h].weight;
	  t = (double) bow_naivebayes_pr_wi_ci (rainbow_nb_barrel, v[h].wi, i, -1,
						       0.0, 0.0, NULL, NULL);
	  t = log(t) * v[h].weight;
	  t2 += t - p_w;
	  assert(finite(t2));
	  //printf("P(w%d|c%d)^%f, p_w^%f\n", v[h].wi, k, t, p_w);
	}
      }
    }

    cd = GET_CDOC_ARRAY_EL(rainbow_nb_barrel,i);
    pci = cd->prior;
    rval += exp(t2 + log(dIi[i] + (tmp*pci*pci)));
    assert(finite(rval));
  }

  //rval = exp(rval);

  printf("kernel=%f\n",rval);
  return rval;
}

void svm_setup_fisher(bow_barrel *old_barrel, bow_wv **docs, int nclasses, int ndocs) {
  double *PXk, PX;
  int     i,j,k;

  rainbow_method *tmp = old_barrel->method;

  old_barrel->method = &bow_method_naivebayes;
  rainbow_nb_barrel = bow_barrel_new_vpc_merge_then_weight (old_barrel);
  old_barrel->method = tmp;

    /* set some global variables that naivebayes.c uses */
  naivebayes_score_returns_doc_pr = 1;
  naivebayes_score_unsorted = 1;

  fprintf(stderr, "Finding maximum kernel value for normalizing\n");

  i = bow_num_words()*nclasses;
  dIi = (double *) malloc(sizeof(double)*nclasses);
  PXk = (double *) malloc(sizeof(double)*nclasses);
  dIij = (double *) malloc(sizeof(double)*i);

  for (j=0; j<i; j++) {
    dIij[j] = 0.0;
  }

  for (j=0; j<nclasses; j++) {
    dIi[j] = 0.0;
  }

  for (i=0; i<ndocs; i++) {
    double max_lpr;
    bow_score *scores = malloc(sizeof(bow_score)*nclasses);

    /* compute the P(X|class) * P(class) terms (since they're used so often) */
    PX = 0.0;
    /* NOTE: with the ...returns_doc_pr variable set, the scores are not probabilities,
     * but instead log probabilities */
    bow_naivebayes_score(rainbow_nb_barrel, docs[i], scores, nclasses, -1);

    max_lpr = scores[0].weight;
    for (k=1; k<nclasses; k++) {
      if (scores[k].weight > max_lpr) 
	max_lpr = scores[k].weight;
    }

    for (k=0; k<nclasses; k++) {
      bow_cdoc *cd = GET_CDOC_ARRAY_EL(rainbow_nb_barrel,k);
      /* the max lpr over everything is the same as multiplying both the
       * denominator & the numerator by some large constant */
      PXk[k] = cd->prior * exp(scores[k].weight - max_lpr);
      /* hacky-hacky-hacky-smoothing */
#define THRESH 1e-1
      if (PXk[k] < THRESH) {
	PXk[k] = THRESH;
	printf("underflow on P(X%d|C%d) - setting to small val\n",i,k);
	fflush(stdout);
      }
      PX += PXk[k];
      assert(finite(PXk[k]) && PXk[k] != 0.0);
    }
    free(scores);


    /* compute term for Iij - d/d-theta_ij * log(P(X|theta)) */
    for (j=0; j<docs[i]->num_entries; j++) {
      for (k=0; k<nclasses; k++) {
	double tmp;

	tmp = bow_naivebayes_pr_wi_ci (rainbow_nb_barrel, docs[i]->entry[j].wi,
				       k, -1, 0.0, 0.0, NULL, NULL);

	dIij[k*nclasses + docs[i]->entry[j].wi] += 
	  ((((docs[i]->entry[j].count * PXk[k]) /tmp) /PX) * 
	   (((docs[i]->entry[j].count * PXk[k]) /tmp) /PX));
      }
    }

    /* compute term for Ii - d/d-theta_i * log(P(X|theta)) */
    for (k=0; k<nclasses; k++) {
      /* M is in both of these terms, so we don't need to worry about it... */
      dIi[k] += (PXk[k]/ PX)*(PXk[k]/ PX);
      assert(finite(dIi[k]));
    }

    if (!(i % 100)) {
      fprintf(stderr, "\r%f%%", (float) ((double)i)/((double)ndocs)*100.0);
    }
  }
  free(PXk);

    /* now invert the values class values */
  for (i=0; i<nclasses; i++) {
    dIi[i] = 1/dIi[i];
  }

  fprintf(stderr,"%f%%\n",(float)100.0);

    /* set "fisher normalizer" */
  {
    double max = -1;     
    int from=0;
    fisher_norm0 = 0;  /* keep in mind, this is log(scalar) */
    for (i=0; i<ndocs; i++) {
      double tmp = svm_kernel_fisher(docs[i],docs[i]);
      if (max < tmp) {
	max = tmp;
	from = i;
      }
    }
    fisher_norm0 = log(1/max);
  }
}
