/* Copyright (C) 1999 Greg Schohn - gcs@jprc.com */

/* ********************* svm_trans.c **********************
 * Code for transductive SVM's */
 
#include <bow/svm.h>

/* anything > 1 will be verbose... */
//#define DEBUG 1

int transduce_svm(bow_wv **docs, int *yvect, int *trans_yvect_store, 
		  double *weights, double *tvals, double *a_b, 
		  double **W, int ndocs, int ntrans, int *up_nsv) {
  double   cs_pos, cs_neg;
  float   *cvect;
  int      nlabeled;
  int      nsolns;
  int      nswitches;
  int      nsv;
  int     *old_yvect;
  double   tb;
  double   thresh;
  double  *tmpdv;
  float   *trans_cvect;
  bow_wv **trans_docs; 
  int      trans_npos;   /* the # of docs that will get a + label */
  double  *trans_scores; /* copy of the scores after the first run */
  int     *trans_yvect;

  struct svm_smo_model smo_model; /* needed if smo is used... */
  int i,j,k;

#ifdef DEBUG  
  double *oldw;
#endif
#if DEBUG>2
  int foo;
#endif

  nsv = *up_nsv;
  nlabeled = ndocs-ntrans;

  trans_docs = &(docs[nlabeled]);
  trans_yvect = &(yvect[nlabeled]);
  /* don't want to crush existing labels? - so copy them */
  old_yvect = (int *) malloc(sizeof(int)*ntrans);

  for (i=0; i<ntrans; i++) {
    old_yvect[i] = trans_yvect[i];
  }

  cvect = (float *) malloc(sizeof(float)*ndocs);
  trans_cvect = &(cvect[nlabeled]);

  for (i=0; i<nlabeled; i++) {
    cvect[i] = (float) svm_C;
  }

  /* this is sortof necessary.  Because of the transduction algorithm, 
   * previous weights won't help since the bounds are iteratively 
   * increased at each step (ie. the inflow will be totally different
   * than the outflow).  */
  nsv = 0;
  /* get the values for the unlabeled docs later */
  for (i=0; i<nlabeled; i++) {
    weights[i] = 0.0;
    tvals[i] = 0.0;
  }

  /* this is done to simplify some of the higher level fns. */
  if (*W) {
    free(*W);
  }
  *W = NULL;

  /* create the initial model */
  solve_svm(docs, yvect, weights, &tb, W, nlabeled, tvals, cvect, &nsv);

  tmpdv = (double *) malloc(sizeof(double)*ntrans);
  trans_scores = (double *) malloc(sizeof(double)*ntrans);

  trans_npos = 0;
  /* classify all of the unlabeled docs */
  for (i=0; i<ntrans; i++) {
    if (svm_kernel_type == 0) {
      tmpdv[i] = evaluate_model_hyperplane(*W, tb, trans_docs[i]);
    } else {
      tmpdv[i] = evaluate_model_cache(docs, weights, yvect, tb, trans_docs[i], nsv);
    }
    trans_scores[i] = tmpdv[i];

    if (svm_trans_nobias) {
      trans_yvect[i] = (tmpdv[i] > 0.0) ? 1 : -1;
      if (tmpdv[i] > 0.0) {
	trans_npos ++;
      }
    }
  }

  if (!svm_trans_nobias) {
    qsort(tmpdv, ntrans, sizeof(double), d_cmp);

    if (svm_trans_npos) {
      /* sort happens in ascending order */
      thresh = tmpdv[ntrans-svm_trans_npos];
      trans_npos = svm_trans_npos;
    } else {
      /* k = npos in training set */
      for (i=k=0; i<nlabeled; i++) {
	if (yvect[i] > 0) {
	  k++;
	}
      }
      trans_npos = (int) ((k*ntrans)/nlabeled);
      thresh = tmpdv[ntrans-trans_npos];
    }
  } else {
    thresh = 0.0;
  }

#define EPSILON_CSTAR 1e-2
  cs_neg = EPSILON_CSTAR;
  cs_pos = EPSILON_CSTAR * (ntrans - trans_npos + 1)/trans_npos;

  for (i=k=0; i<ntrans; i++) {
    if (trans_scores[i] >= thresh && k < trans_npos) {
      trans_yvect[i] = 1;
      cvect[i+nlabeled] = cs_pos;
      k++;
    } else {
      trans_yvect[i] = -1;
      cvect[i+nlabeled] = cs_neg;
    }
  }

  free(trans_scores);

#if DEBUG > 1
  printf("trans_yvect: %d: ", foo++);
  for (i=0; i<ndocs-nlabeled; i++) {
    printf("%d ", trans_yvect[i]);
  }
  printf("\n");

  printf("trans cycle %d: ", foo++);
  for (i=0; i<ndocs; i++) {
    printf("%f ",weights[i]);
  }
  printf("\n");
#endif

  /* initialize what the tvect should look like for the unlabeled docs */
  if (svm_use_smo) {
    /* nothing needs to be done to the tvals since they are only valid for sv's */
    for (i=nlabeled; i<ndocs; i++) {
      weights[i] = 0.0;
    }
    
    /* setup model for future use */
    /* only smo_evaluate_error uses this so only that info needs to be setup */
    smo_model.docs = docs;
    smo_model.yvect = yvect;
    smo_model.ndocs = ndocs;
    smo_model.weights = weights;
  } else {
    for (i=nlabeled; i<ndocs; i++) {
      tvals[i] = 0.0;
      for (j=k=0; j<nsv; k++) {
	if (weights[k] > 0.0) {
	  tvals[i] += weights[k]*yvect[k]*svm_kernel_cache(docs[k],docs[i]);
	  j++;
	}
      }
    }
  }

  nsolns = 1;
  nswitches = 0;

  /* this will need to be done more intelligently since this fn can be in
   * an inner loop (active learning) */
  while ((cs_pos < svm_trans_cstar) || (cs_neg < svm_trans_cstar)) {
    int maxp=0, maxn=0;
    double maxpv, maxnv;

    /* switch loop */
    do {
      if (!svm_trans_smart_vals) {
	nsv = 0;
	/* get the values for the unlabeled docs later */
	for (i=0; i<ndocs; i++) {
	  weights[i] = 0.0;
	  tvals[i] = 0.0;
	}
	free(*W);
	*W = NULL;
      } else {
	/* if we don't need this - get rid of it */
	if (svm_kernel_type == 0 && 
	    (!svm_use_smo || !(nsolns % svm_trans_hyp_refresh))) {
	  free(*W);
	  *W = NULL;
	}
      }

      nsolns ++;
      solve_svm(docs, yvect, weights, &tb, W, ndocs, tvals, cvect, &nsv);

      /* this block should be replaced by fns to convert tmp values
       * into error values */
      for (i=0; i<ntrans; i++) {
	if (svm_kernel_type == 0) {
	  tmpdv[i] = evaluate_model_hyperplane(*W, tb, trans_docs[i]);
	} else {
	  tmpdv[i] = evaluate_model_cache(docs, weights, yvect, tb, trans_docs[i], nsv);
	}
	if (trans_yvect[i] == 1 && tmpdv[i] < 1.0) {
	  tmpdv[i] = 1.0 - tmpdv[i];
	} else if (trans_yvect[i] == -1 && tmpdv[i] > -1.0) {
	  tmpdv[i] = tmpdv[i] + 1.0;
	} else {
	  tmpdv[i] = 0.0;
	}
      }

      for (i=0, maxnv=maxpv=0.0; i<ntrans; i++) {
	if (trans_yvect[i] > 0) {
	  if (tmpdv[i] > maxpv) {
	    maxpv = tmpdv[i];
	    maxp = i;
	  }
	} else {
	  if (tmpdv[i] > maxnv) {
	    maxnv = tmpdv[i];
	    maxn = i;
	  }
	}
      }

      /* switch the largest 2 */
      if (maxpv > 0.0 && maxnv > 0.0 && maxpv + maxnv > 2.0) {
	nswitches++;
	//printf("switching %d & %d\n",maxp,maxn);
	if (bow_verbosity_level > 2) {
	  fprintf(stderr,"  switching %d & %d\n",maxp,maxn);
	} else {
	  fprintf(stderr,"\r\t\t\t\t\t\t\t\t\t\t\t\t\t\tswitch #%d",nswitches);
	}
	trans_yvect[maxp] *= -1;
	trans_yvect[maxn] *= -1;

	/* need to also fix the hyperplane */
	if (svm_kernel_type == 0 && (nsolns % svm_trans_hyp_refresh)) {
	  for (j=maxp,k=0; k<2; j=maxn,k++) {
	    for (i=0; i<trans_docs[j]->num_entries; i++) {
	      (*W)[trans_docs[j]->entry[i].wi] += 
		2 * trans_yvect[j] * weights[j+nlabeled] * trans_docs[j]->entry[i].weight;
	    }
	  }
	}

	if (svm_trans_smart_vals) {
	  if (svm_use_smo) {
	    double wi, wj;
	    int yi, yj;
	    yi = trans_yvect[maxn];
	    yj = trans_yvect[maxp];
	    wi = weights[nlabeled+maxn];
	    wj = weights[nlabeled+maxp];

	    /* set tmp vals */
	    for (k=0; k<ndocs; k++) {
	      if ((weights[k] < cvect[k] - svm_epsilon_a) && (weights[k] > svm_epsilon_a)) {
		tvals[k] += 2*(wi*yi*svm_kernel_cache(docs[k],trans_docs[maxn]) + 
			       wj*yj*svm_kernel_cache(docs[k],trans_docs[maxp]));
	      }
	    }

#ifdef DEBUG
	    /* sanity check */
	    smo_model.W = *W;
	    for (i=0; i<ndocs; i++) {
	      if ((weights[i] < cvect[i] - svm_epsilon_a) && (weights[i] > svm_epsilon_a)) {
		int tmp = tvals[i] - smo_evaluate_error(&smo_model, i);
		if (tmp < 0) tmp *= -1;
		if (tmp > 0.1) {
		  double herror, verror;
		  herror = smo_evaluate_error(&smo_model, i);
		  svm_kernel_type = 1;
		  verror = smo_evaluate_error(&smo_model, i);
		  fprintf(stderr, "bad temporary (i=%d, tv[i]=%f, actual_H=%f, actual_V=%f)\n",i,tvals[i],
			  herror, verror);
		  svm_kernel_type = 0;
		  fflush(stderr);
		  kill(getpid(),SIGSTOP);
		}
	      }
	    }
#endif
	  } else {
	    double wn, wp;
	    wn = weights[maxn+nlabeled] * trans_yvect[maxn];
	    wp = weights[maxp+nlabeled] * trans_yvect[maxp];
	  
	    for (k=0; k<ndocs; k++) {
	      tvals[k] += 2*(wn*svm_kernel_cache(docs[k],trans_docs[maxn]) + 
			     wp*svm_kernel_cache(docs[k],trans_docs[maxp]));
	    }
	  }
	}
      } else {
	break;
      }
    } while (1);

#if DEBUG > 1
    printf("trans cycle %d: ", foo++);
    for (i=0; i<ndocs; i++) {
      printf("%f ",weights[i]);
    }
    printf("\n");
#endif
    
    /* set the proper tvals for the unlabeled docs 
     * (since they will no longer be bound) */
    if (svm_use_smo) {
      smo_model.W = *W;
      for (i=nlabeled; i<ndocs; i++) {
	if ((weights[i] > cvect[i] - svm_epsilon_a) && 
	    (weights[i] < svm_trans_cstar)) {
	  tvals[i] = smo_evaluate_error(&smo_model, i);
	}
      }
    }

    cs_pos = MIN(cs_pos*1.5,svm_trans_cstar);
    cs_neg = MIN(cs_neg*1.5,svm_trans_cstar);

    fprintf(stderr,"\r\t\t\t\b\b\bc=(%f,%f)    ",cs_pos,cs_neg);
    
    for (i=0; i<ntrans; i++) {
      if (trans_yvect[i] == 1) 
	trans_cvect[i] = cs_pos;
      else
	trans_cvect[i] = cs_neg;
    }
  }

#ifdef DEBUG
  /* debugging - sensitivity check */
  oldw = *W;
  *W = (double *) malloc(sizeof(double) * bow_num_words());
  for (i=0; i<bow_num_words(); i++) {
    (*W)[i] = 0;
  }

  for (i=0; i<ndocs; i++) {
    for (j=0; j<docs[i]->num_entries; j++) {
      (*W)[docs[i]->entry[j].wi] += yvect[i] * weights[i] * docs[i]->entry[j].weight;
    }
  }

  for (i=0; i<bow_num_words(); i++) {
    oldw[i] -= (*W)[i];
    if (oldw[i] > 0 ) oldw[i] *= -1;
    if (oldw[i] > 0.01) {
      fprintf(stderr, "bad hyperplane (j=%d)\n",j);
      fflush(stderr);
      kill(getpid(),SIGSTOP);
    }
  }

#endif

  free(cvect);
  free(tmpdv);

#if DEBUG > 1
  fprintf(stderr, "trans_yvect1 ");
  for (i=0; i<ndocs;i++) {
    fprintf(stderr," %d",yvect[i]);
  }
  fprintf(stderr,"\n");
#endif

  if (trans_yvect_store) {
    for (i=0; i<ntrans; i++) {
      trans_yvect_store[i] = trans_yvect[i];
    }
  }

#if DEBUG > 1
  fprintf(stderr, "trans_yvect2 ");
  for (i=0; i<ntrans;i++) {
    fprintf(stderr," %d",trans_yvect_store[i]);
  }
  fprintf(stderr,"\n");
#endif

  for (i=0; i<ntrans; i++) {
    trans_yvect[i] = old_yvect[i];
  }

  free(old_yvect);

  *up_nsv = nsv;

  /* ?bug?  Need to add code to signal whether or not the weights have changed
   * its hard to believe that the weights don't change at least a 
   * little bit - right */
  return 1;
}

