/* Copyright (C) 1999 Greg Schohn - gcs@jprc.com */

/* ******************** svm_al.c *******************
 * Active learning add-ons for SVM's. */

#include <bow/svm.h>

#define NDIM_INSPECTED 14
int dim_map(int i) {
  static int map[] = {1,2,3,4,6,8,12,16,24,32,48,64,128,256};
  return (map[i]);
}

/* compute the prec. recall breakeven point shifting the value of b */
double prec_recall_breakeven(double *test_evals, int *test_yvect, int n, int total_pos) {
  struct di *ey;
  double max;
  int npos;
  int i;

  ey = (struct di *) malloc(sizeof(struct di)*n);

  for (i=0; i<n; i++) {
    ey[i].d = -1*test_evals[i]; /* -1 is to force the sort the way i want it */
    ey[i].i = test_yvect[i];
  }

  qsort(ey,n,sizeof(struct di),di_cmp);

  max = -1.0;
  for (i=npos=0; i<n; i++) {
    double min;
    if (ey[i].i > 0) {
      npos ++;
    }
    min = MIN(((double)npos)/(i+1), ((double)npos)/total_pos);
    if (min > max) {
      max = min;
    }
  }
  free(ey);
  return max;
}

struct al_test_data {
  int ntest;
  int ndim_sat;

  int *docs_added;
  int *test_yvect, *apvect, *anvect;
  int *train_apvect, *train_anvect, *query_apvect, *query_anvect;
  int *nsv_vect, *nbsv_vect, *time_vect, *nkce_vect;
  int *npos_added, *nneg_added;

  double *prb, *scores_added;
  int **sv_dim_sat_vect, **train_dim_sat_vect;

  double **test_scores;
  bow_wv **test_docs;
};


/* the data coming in should have train data in the first (ndocs-ntrans)
 * slots & the permanently unlabel-able data in the next ntrans slots */
/* no unlabeled data will be used unless svm_al_do_trans is set - if it
 * is then ALL unlabeled data will be used. */

/* this fn. does active learning by selecting which docs to pass into 
 * the svm solver */
int al_svm_guts(bow_wv **train_docs, int *train_yvect, double *weights, 
		double *b, double **W, int nperm_unlabeled, int ndocs, 
		struct al_test_data *astd, int do_random_learning) {
  int          changed;
  int         *cur_hyp_yvect;
  int          dec;
  int         *hyp_yvect; /* for transduction */
  int          last_subndocs;
  int          nplabeled; /* # of potentially labeled */
  int          nleft;
  int          nsv;
  int          n_trans_correct;
  int          num_words;
  int         *old_svbitmap;
  int          qsize;    /* query size, size of chunks to grow training set by */
  struct di   *train_scores, *train_cscores;
  int         *train_sat_vect;
  int         *sv_sat_vect; /* shows how many  */
  int          sub_ndocs;
  double       tb;
  int         *tdocs;    /* translation table */
  double      *tvals;

  int i,j,k,n,nloop;

  sub_ndocs = MIN(ndocs,svm_init_al_tset);

  train_scores = (struct di *) malloc(sizeof(struct di)*ndocs);
  tvals = (double *) malloc(sizeof(double)*ndocs);
  tdocs = (int *) malloc(sizeof(int)*ndocs);

  for (i=0; i<ndocs; i++) {
    tdocs[i] = i;
  }

  /* hyp_yvect is a hack - it holds the hypotheses, but also the
   * correct labels for the queried docs (so a proper vect can be
   * passed to eval) */
  cur_hyp_yvect = hyp_yvect = (int *) malloc(sizeof(int)*ndocs);

  nplabeled = ndocs - nperm_unlabeled;
  num_words = bow_num_words();


  /* BEGIN LOGGING CODE */

  if (astd->sv_dim_sat_vect) {
    train_sat_vect = (int *) malloc(sizeof(int)*num_words);
  } else {
    train_sat_vect = NULL;
  }

  if (astd->train_dim_sat_vect) {
    old_svbitmap = (int *) malloc((ndocs+7)/8);
    sv_sat_vect = (int *) malloc(sizeof(int)*num_words);
  } else {
    old_svbitmap = NULL;//(int *) malloc((ndocs+7)/8);
    sv_sat_vect = NULL;//(int *) malloc(sizeof(int)*num_words);
  }

  /* initialize accounting stuff */
  if (sv_sat_vect) {
    memset(old_svbitmap, 0, (ndocs+7)/8);
    for (i=0; i<num_words; i++) {
      sv_sat_vect[i] = 0.0;
    }
  }
  if (train_sat_vect) {
    for (i=0; i<num_words; i++) {
      train_sat_vect[i] = 0.0;
    }
  }

  /* END LOGGING CODE */


  /* initialize... */
  nsv = 0;
  for (i=0; i<ndocs; i++) {
    weights[i] = 0.0;
    tvals[i] = 0.0;
  }

  qsize = svm_al_qsize;

  /* select an initial set of things to classify */
  /* the following is equivalent to asking the user to classify 1/2 the
   * documents as positive & the other half as negative */
  for (k=-1, i=0, n=sub_ndocs/2; k<2; n=sub_ndocs,k=k+2) {
    for (j=0; i<n && j<nplabeled; j++) {
      if (train_yvect[j] != k) {
	continue;
      }
      {
	int t;
	bow_wv *twv;

	t = tdocs[j];
	tdocs[j] = tdocs[i];
	tdocs[i] = t;

	t = train_yvect[j];
	train_yvect[j] = train_yvect[i];
	train_yvect[i] = t;

	twv = train_docs[j];
	train_docs[j] = train_docs[i];
	train_docs[i] = twv;
      }
      i++;
    }
  }
  sub_ndocs = i;
  last_subndocs = 0;

  /* copy the initial yvect into hyp_yvect (for evaluate) */
  for (i=0; i<sub_ndocs; i++) {
    hyp_yvect[i] = train_yvect[i];
  }
  cur_hyp_yvect = &(hyp_yvect[sub_ndocs]);

  for (i=train_yvect[0],j=1; j<sub_ndocs; j++) {
    if (j != i) break;
  }
  if (j == i) {
    bow_error("Can't active learn when all examples are from the same class!");
  }

  train_cscores = NULL;
  dec = 0;

  nleft = ndocs - sub_ndocs;
  changed = 1; /* for code where transduction is done */
  n_trans_correct = 0;

  for (nloop=0; ;nloop++) {
    struct tms t1, t2;

    /* BEGIN LOGGING CODE */

    /* this is done at the beginning of the loop so that the base case
       (ie. after initial set) works too... */
    if (astd->npos_added && astd->nneg_added) {
      astd->npos_added[nloop] = 0;
      astd->nneg_added[nloop] = 0;
      for (i=last_subndocs; i<sub_ndocs; i++) {
	if (train_yvect[i] == 1) {
	  astd->npos_added[nloop] ++;
	} else {
	  astd->nneg_added[nloop] ++;
	}
      }
    }

    /* add the document indices */
    if (astd->docs_added) {
      for (i=last_subndocs; i<sub_ndocs; i++) {
	astd->docs_added[i] = tdocs[i];
      }
    }

    if (train_sat_vect) {
      for (i=last_subndocs; i<sub_ndocs; i++) {
	for (j=0; j<train_docs[i]->num_entries; j++) {
	  train_sat_vect[train_docs[i]->entry[j].wi] ++;
	}
      }
    }

    svm_nkc_calls = 0;
    /* END LOGGING CODE */


    fprintf(stderr,"\r%dth AL iteration",nloop);

    times(&t1);
    if (nloop==2) {
      //exit(1);
    }
    /* the changed flag shows whether or not the theorized y's are different than
     * the actual ones (if they are, retraining is done, otherwise it isn't). */
    if (svm_al_do_trans) {
      if (changed) {
	changed = svm_trans_or_chunk(train_docs, train_yvect, cur_hyp_yvect, weights,
				     tvals, &tb, W, nleft+nperm_unlabeled, ndocs, &nsv);
      }
    } else {
      changed = svm_trans_or_chunk(train_docs, train_yvect, cur_hyp_yvect, weights,
				   tvals, &tb, W, 0, sub_ndocs, &nsv);
    }
    times(&t2);


    /* BEGIN LOGGING CODE */

    /* a couple of accounting things that are independent of a test/validation set */
    if (astd->time_vect)
      astd->time_vect[nloop] = (int) (t2.tms_utime - t1.tms_utime + t2.tms_stime - t1.tms_stime);
    if (astd->nsv_vect)
      astd->nsv_vect[nloop] = nsv;
    if (astd->nbsv_vect) {
      astd->nbsv_vect[nloop] = 0;
      for (j=0; j<sub_ndocs; j++) {
	/* note - use svm_C because the label IS known */
	if (weights[j] >= svm_C - svm_epsilon_a)
	  astd->nbsv_vect[nloop] ++;
      }
    }
    if (astd->nkce_vect) 
      astd->nkce_vect[nloop] = svm_nkc_calls;

    /* END LOGGING CODE */

    /* find the next example that is closest to the hyperplane that we just found */
    /* the scores need to be recalculated if any of the weights changed... */
    if (changed) {
      train_cscores = train_scores;
      if (svm_kernel_type == 0) {
	for (j=sub_ndocs,nleft=0; j<nplabeled; j++) {
	  train_scores[nleft].d = fabs(evaluate_model_hyperplane(*W, tb, train_docs[j]));
	  train_scores[nleft].i = j;
	  nleft ++;
	}
      } else {
	for (j=sub_ndocs,nleft=0; j<nplabeled; j++) {
	  train_scores[nleft].d = fabs(evaluate_model_cache(train_docs, weights, hyp_yvect, tb, train_docs[j], nsv));
	  train_scores[nleft].i = j;
	  nleft ++;
	}
      }

      /* BEGIN LOGGING CODE */      

      if (astd->train_anvect && astd->train_apvect) {
	double out;
	astd->train_anvect[nloop] = astd->train_apvect[nloop] = 0;
	for (i=0; i<sub_ndocs; i++) {
	  if (svm_kernel_type == 0) {
	    out = evaluate_model_hyperplane(*W,tb,train_docs[i]);
	  } else {
	    out = evaluate_model_cache(train_docs, weights, hyp_yvect, tb, train_docs[i], nsv);
	  }
	  if (train_yvect[i]*out > 0) {
	    if (train_yvect[i] > 0) {
	      astd->train_apvect[nloop] ++;
	    } else {
	      astd->train_anvect[nloop] ++;
	    }
	  }
	}
      }

      /* lets figure out the change in fdim saturation... */
      if (sv_sat_vect) {
	for (i=0; i<sub_ndocs; i++) {
	  if ((weights[i] == 0.0) && (GETVALID(old_svbitmap,i))) {
	    SETINVALID(old_svbitmap,i);
	    for (j=0; j<train_docs[i]->num_entries; j++) {
	      sv_sat_vect[train_docs[i]->entry[j].wi] --;
	    }
	  } else if ((weights[i] != 0.0) && (!GETVALID(old_svbitmap,i))) {
	    SETVALID(old_svbitmap,i);
	    for (j=0; j<train_docs[i]->num_entries; j++) {
	      sv_sat_vect[train_docs[i]->entry[j].wi] ++;
	    }
	  }
	}      
	
	/* this could be smarter - but it would involve more arrays... */
	/* update the history vector... */
	for (i=0; i<astd->ndim_sat; i++) {
	  astd->sv_dim_sat_vect[i][nloop] = 0;
	}
	
	for (j=0; j<num_words; j++) {
	  for (i=0; sv_sat_vect[j]>=dim_map(i) && i<astd->ndim_sat; i++) {
	    astd->sv_dim_sat_vect[i][nloop] ++;
	  }
	}
      }
      
      if (astd->train_dim_sat_vect) {
	for (i=0; i<astd->ndim_sat; i++) {
	  astd->train_dim_sat_vect[i][nloop] = 0;
	}

	for (j=0; j<num_words; j++) {
	  for (i=0; train_sat_vect[j]>=dim_map(i) && i<astd->ndim_sat; i++) {
	    astd->train_dim_sat_vect[i][nloop] ++;
	  }
	}
      }

      /* now lets find the accuracy... */
      if (astd->prb) {
	int npos;
	double *test_evals = (double *) malloc(sizeof(double)*astd->ntest);
	
	astd->anvect[nloop] = astd->apvect[nloop] = 0;
	if (svm_kernel_type == 0) {
	  for (j=0; j<astd->ntest; j++) {
	    test_evals[j] = evaluate_model_hyperplane(*W, tb, astd->test_docs[j]);
	  }
	} else {
	  for (j=0; j<astd->ntest; j++) {
	    test_evals[j] = evaluate_model(train_docs, weights, hyp_yvect, 
					   tb, astd->test_docs[j], nsv);
	  }
	}

	if (astd->test_scores) {
	  for (j=0; j<astd->ntest; j++) {
	    astd->test_scores[nloop][j] = test_evals[j];
	  }
	}

	for (j=npos=0; j<astd->ntest; j++) {
	  if (astd->test_yvect[j] * test_evals[j] > 0.0) {
	    if (astd->test_yvect[j] == -1) 
	      astd->anvect[nloop] ++;
	    else
	      astd->apvect[nloop] ++;
	  }
	  if (astd->test_yvect[j] > 0) {
	    npos ++;
	  }
	}

	if (svm_al_do_trans && (astd->apvect[nloop] == 0 || astd->anvect[nloop] == 0)) {
	  fprintf(stderr,"Unlikely occurence that all test (%d) examples have the same \n"
		  "label when classified with the current model of %d support vectors.\n"
		  "Unless this is expected, there is probably a bug in the program.\n"
		  "Please send the author (gcs@cmu.edu) email (note the cmd line arguments).\n"
		  "The function has stopped the program so that it may be debugged, terminated"
		  "or continued\n", astd->ntest, nsv);
	  for (j=0; j<astd->ntest; j++) {
	    if (test_evals[j] * evaluate_model(train_docs, weights, hyp_yvect, 
					       tb, astd->test_docs[j], nsv) < 0.0) {
	      fprintf(stderr, "bad hyperplane (j=%d, te[j]=%f, actual_sv=%f, actual_hyp=%f)\n",j,
		      evaluate_model(train_docs, weights, hyp_yvect, tb, astd->test_docs[j], nsv),
		      test_evals[j],evaluate_model_hyperplane(*W, tb, astd->test_docs[j]));
	      fflush(stderr);
	      kill(getpid(),SIGSTOP);
	    }
	  }

#ifdef GCSJPRC
	  system("echo \"rainbow did a boo-boo - stopping!\" | /usr/sbin/sendmail gcs@jules.res.cmu.edu");
#endif
	  /* if it didn't get stopped before */
	  if (j == astd->ntest) {
	    fflush(stderr);
	    kill(getpid(),SIGSTOP);
	  }
	}

	/* precision recall breakevens too */
	astd->prb[nloop] = prec_recall_breakeven(test_evals, astd->test_yvect, 
						 astd->ntest, npos);
	free(test_evals);
      }

      /* END LOGGING CODE */

    } else {
      /* BEGIN LOGGING CODE */

      /* we can use the scores that we got last time (they'll still be the same) - just
       * remove the previous ones from the scores array... */
      /* since nothing changed, we don't need to recalculate the test accuracy */
      if (astd->prb) {
	astd->apvect[nloop] = astd->apvect[nloop-1];
	astd->anvect[nloop] = astd->anvect[nloop-1];
	astd->prb[nloop] = astd->prb[nloop-1];
      }

      /* quite nasty (because of the dependency on nneg_added & npos_added) */
      if (astd->train_anvect && astd->train_apvect && astd->nneg_added && astd->npos_added) {
	astd->train_anvect[nloop] = astd->train_anvect[nloop-1] + astd->nneg_added[nloop];
	astd->train_apvect[nloop] = astd->train_apvect[nloop-1] + astd->npos_added[nloop];
      }
	
      if (astd->train_dim_sat_vect) {
	for (i=0; i<astd->ndim_sat; i++) {
	  astd->train_dim_sat_vect[i][nloop] = astd->train_dim_sat_vect[i][nloop-1];
	}
      } 

      if (astd->sv_dim_sat_vect) {
	for (i=0; i<astd->ndim_sat; i++) {
	  astd->sv_dim_sat_vect[i][nloop] = astd->sv_dim_sat_vect[i][nloop-1];
	}
      }

      if (astd->test_scores) {
	for (i=0; i<astd->ntest; i++) {
	  astd->test_scores[nloop][i] = astd->test_scores[nloop-1][i];
	}
      }

      /* END LOGGING CODE */

      /* this code doesn't get touched till after stuff was added */
      nleft -= dec;
      cur_hyp_yvect = &(cur_hyp_yvect[dec]);
      train_cscores = &(train_cscores[dec]);
    }

    /* see if there are any indices < sub_ndocs in the score array */
    for (i=0; i<nplabeled-sub_ndocs; i++) {
      assert (train_cscores[i].i >= sub_ndocs); 
    }

    if (sub_ndocs == nplabeled) {
      break;
    }

    /* now use the scores (& possibly other things) to chose the next examples to learn */
    if (nleft < qsize) {
      dec = nleft;
    } else {
      dec = qsize;
    }

    /* do this even if nleft<qsize to find the min... */
    if (!do_random_learning) {
      get_top_n(train_cscores, nleft, dec);
    }

    /* this is where the termination criteria goes - right now its pretty dumb... */
    /* (it would be a fn, but since bookkeeping & setting up need to go on in here
     * anyway, i'm just computing it */
    if ((train_cscores[0].d > 1) && (0)) {
      break;
    }
    
    /* this only matters when transduction is being used (otherwise its harmless) */
    changed = 0;

    /* BEGIN LOGGING CODE */
    if (astd->query_anvect && astd->query_apvect) {
      astd->query_anvect[nloop] = astd->query_apvect[nloop] = 0;
    }
    /* END LOGGING CODE */

    /* query "oracle" */
    for (j=0; j<dec; j++) {
      int t,tj;
      bow_wv *twv;

      tj = train_cscores[j].i;

      t = tdocs[sub_ndocs+j];
      tdocs[sub_ndocs+j] = tdocs[tj];
      tdocs[tj] = t;

      twv = train_docs[sub_ndocs+j];
      train_docs[sub_ndocs+j] = train_docs[tj];
      train_docs[tj] = twv;

      t = train_yvect[sub_ndocs+j];
      train_yvect[sub_ndocs+j] = train_yvect[tj];
      train_yvect[tj] = t;

      if (svm_al_do_trans) {
	if ((train_yvect[sub_ndocs+j] != cur_hyp_yvect[j]) || 
	    (weights[sub_ndocs+j] >= svm_trans_cstar - svm_epsilon_a)) {
	  changed = 1;
	}
      } 

      /* BEGIN LOGGING CODE */
      if (astd->query_anvect && astd->query_apvect) {
	double out;
	if (svm_kernel_type == 0) {
	  out = evaluate_model_hyperplane(*W,tb,train_docs[i]);
	} else if (svm_al_do_trans) {
	  out = evaluate_model_cache(train_docs,weights,hyp_yvect,tb,train_docs[i],nsv);
	}
	if (train_yvect[sub_ndocs+j]*out > 0) {
	  if (train_yvect[sub_ndocs+j] > 0) {
	    astd->query_apvect[nloop] ++;
	  } else {
	    astd->query_anvect[nloop] ++;
	  }
	}
      }
      /* END LOGGING CODE */

      /* also need to swap the scores - since they will be used if the output doesn't change */
      for (i=0; ; i++) {
	if (train_cscores[i].i == sub_ndocs+j) {
	  train_cscores[i].i = tj;
	  break;
	}
      }

      train_cscores[j].i = sub_ndocs+j;

      if (astd->scores_added) 
	astd->scores_added[sub_ndocs+j] = train_cscores[j].d;
    }

    for (j=0; j<dec; j++) {
      hyp_yvect[sub_ndocs+j] = train_yvect[sub_ndocs+j];
    }

    if (!changed) {
      n_trans_correct ++;
    }

    last_subndocs = sub_ndocs;

    /* calculate tvals that are necessary */
    if (svm_use_smo) {
      for (j=sub_ndocs; j<dec; j++) {
	weights[j] = 0.0;
	//tvals[j] doesn't matter
      }

      sub_ndocs += dec;
    } else {
      int n;
      for (n=0; n<dec; n++) {
	for (j=k=0; k<nsv; j++) {
	  if (weights[j] != 0.0) {
	    tvals[sub_ndocs] += weights[j] * train_yvect[j] * 
	      svm_kernel_cache(train_docs[sub_ndocs],train_docs[j]);
	    k++;
	  }
	}
	sub_ndocs++;
      }
    }

    /* if we no longer need W, lets ditch it (note - the loop never exits here so a 
     * valid W is still in place for the calling fn. */
    if (!svm_use_smo && svm_kernel_type == 0) {
      free(*W);
      *W = NULL;
    }
  }

  if (svm_al_do_trans) {
    printf("Queried for a total of %d labels.\nSkipped %d loops w/ transduction.\n",
	   sub_ndocs, n_trans_correct);
  }

  free(hyp_yvect);
  
  free(train_scores);
  free(tvals);

  if (sv_sat_vect) {
    free(sv_sat_vect);
    free(old_svbitmap);
  }
  if (train_sat_vect) {
    free(train_sat_vect);
  }

  /* fill everything back in - depermute everything */
  for (i=0; i<sub_ndocs; ) {
    int t,j;
    double td;
    bow_wv *twv;

    j = tdocs[i];

    if (j == i) {
      i++;
      continue;
    }

    twv = train_docs[j];
    train_docs[j] = train_docs[i];
    train_docs[i] = twv;

    t = train_yvect[j];
    train_yvect[j] = train_yvect[i];
    train_yvect[i] = t;

    td = weights[j];
    weights[j] = weights[i];
    weights[i] = td;

    tdocs[i] = tdocs[j];
    tdocs[j] = j;
  }
  free(tdocs);

  *b = tb;
  return nsv;
}

/* this cuts up the training set into training & validation */
/* the data coming in has already been permutated */
/* the first docs become the test docs 
 * (to prevent us from having to move everything) */
int al_svm_test_wrapper(bow_wv **docs, int *yvect, double *weights, double *b, 
			double **W, int ntrans, int ndocs, int do_ts, 
			int do_random_learning, int *permute_table) {
  struct al_test_data altd;
  int      max_iter;
  int      nlabeled;
  int      ntrain;
  int      nsv;
  int      ntest;
  int      tp, tn;
  bow_wv **train_docs;
  int     *train_y;
  int  i,j,k;

  ntrain = altd.ntest = 0;

  nlabeled = ndocs - ntrans;
  ntrain = nlabeled/2;
  ntest = nlabeled - ntrain;
  altd.ntest = ntest;

  train_docs = &(docs[ntest]);
  train_y = &(yvect[ntest]);

  altd.test_docs = docs;
  altd.test_yvect = yvect;

  max_iter = ((ntrain+svm_al_qsize-1) / svm_al_qsize) + 1;
  
  altd.apvect = (int *) malloc(sizeof(int)*max_iter);
  altd.anvect = (int *) malloc(sizeof(int)*max_iter);
  altd.nsv_vect = (int *) malloc(sizeof(int)*max_iter);
  altd.nbsv_vect = (int *) malloc(sizeof(int)*ntrain);
  altd.prb = (double *) malloc(sizeof(double)*max_iter);
  altd.nkce_vect = (int *) malloc(sizeof(int)*max_iter);
  altd.time_vect = (int *) malloc(sizeof(int)*max_iter);
  
  altd.query_anvect = (int *) malloc(sizeof(int)*max_iter);
  altd.query_apvect = (int *) malloc(sizeof(int)*max_iter);
  altd.train_anvect = (int *) malloc(sizeof(int)*max_iter);
  altd.train_apvect = (int *) malloc(sizeof(int)*max_iter);

  if (do_ts) {
    altd.test_scores = (double **) malloc(sizeof(double *)*max_iter);
    
    for (i=0; i<max_iter; i++) {
      altd.test_scores[i] = (double *) malloc(sizeof(double)*altd.ntest);
    }
  } else {
    altd.test_scores = NULL;
  }

  altd.npos_added = (int *) malloc(sizeof(int)*max_iter+1);
  altd.nneg_added = (int *) malloc(sizeof(int)*max_iter+1);
  altd.docs_added = (int *) malloc(sizeof(int)*ntrain);
  altd.scores_added = (double *) malloc(sizeof(double)*ntrain);

  for (i=0; i<ntrain; i++) {
    altd.scores_added[i] = 0.0;
  }

  memset(altd.apvect, -1, max_iter*sizeof(int));
  memset(altd.anvect, -1, max_iter*sizeof(int));
  
  altd.ndim_sat = NDIM_INSPECTED;
  altd.sv_dim_sat_vect = (int **) malloc(NDIM_INSPECTED*sizeof(int *));
  altd.train_dim_sat_vect = (int **) malloc(NDIM_INSPECTED*sizeof(int *));
  for(i=0; i<NDIM_INSPECTED; i++) {
    altd.sv_dim_sat_vect[i] = (int *) malloc(sizeof(int)*max_iter);
    altd.train_dim_sat_vect[i] = (int *) malloc(sizeof(int)*max_iter);
  }

  nsv = al_svm_guts(train_docs, train_y, weights, b, W, ntrans, ntrain,
		    &altd, do_random_learning);

  for (i=tp=tn=0; i<altd.ntest; i++) {
    if (altd.test_yvect[i] == 1) {
      tp ++;
    } else {
      tn ++;
    }
  }

  printf("%d positive test documents, %d negative test documents.\npositive accuracy vector: ",tp,tn);
  for (i=0; (altd.apvect[i]>=0) && i < max_iter; i++) {
    printf("  %d", altd.apvect[i]);
  }
  printf("\nnegative accuracy vector: ");
  for (j=0; j<i; j++) {
    printf("  %d", altd.anvect[j]);
  }
  printf("\nprecision/recall breakeven vector: ");
  for (j=0; j<i; j++) {
    printf("  %f", altd.prb[j]);
  }
  printf("\nquery positive accuracy vector: ");
  for (j=0; j<i-1; j++) {
    printf("  %d",altd.query_apvect[j]);
  }
  printf("\nquery negative accuracy vector: ");
  for (j=0; j<i-1; j++) {
    printf("  %d",altd.query_anvect[j]);
  }
  printf("\ntrain positive accuracy vector: ");
  for (j=0; j<i; j++) {
    printf("  %d",altd.train_apvect[j]);
  }
  printf("\ntrain negative accuracy vector: ");
  for (j=0; j<i; j++) {
    printf("  %d",altd.train_anvect[j]);
  }
  printf("\nnumber of positive documents inspected: ");
  for (j=0; j<i; j++) {
    printf(" %d", altd.npos_added[j]);
  }
  printf("\nnumber of negative documents inspected: ");
  for (j=0; j<i; j++) {
    printf(" %d", altd.nneg_added[j]);
  }
  printf("\nnumber of support vectors: ");
  for (j=0; j<i; j++) {
    printf("  %d", altd.nsv_vect[j]);
  }
  printf("\nnumber of bounded support vectors: ");
  for (j=0; j<i; j++) {
    printf("  %d", altd.nbsv_vect[j]);
  }

  {
    int k;
    int start_index= MIN(ntrain, svm_init_al_tset);
    printf("\n\"Real\" document indices when added: ");

    printf("0(%d",permute_table[altd.docs_added[0]]);
    for (k=1; k<start_index; k++) {
      printf(",%d",permute_table[altd.docs_added[k]]);
    }
    printf(") ");

    for (j=0; j<i-1; j++) {
      printf("%d(%d",j+1,permute_table[altd.docs_added[j*svm_al_qsize+start_index]]);
      for (k=1; k<svm_al_qsize && k+j*svm_al_qsize+start_index<ntrain; k++) {
	printf(",%d",permute_table[altd.docs_added[j*svm_al_qsize+start_index+k]]);
      }
      printf(") ");
    }
    printf("\nminimum scores of documents when added: ");
    for (j=0; j<i-1; j++) {
      printf("  %f", altd.scores_added[j*svm_al_qsize+svm_init_al_tset]);
    }
    printf("\naverage scores of documents when added: ");
    for (j=0; j<i-1; j++) {
      double avg = 0.0;
      for (k=0; k<svm_al_qsize && k+j*svm_al_qsize+svm_init_al_tset<ntrain; k++) {
	avg += altd.scores_added[j*svm_al_qsize+k+svm_init_al_tset];
      }
      printf("  %f", avg/k);
    }
  }
  printf("\nrunning times: ");
  for (j=0; j<i; j++) {
    printf("  %d", altd.time_vect[j]);
  }
  printf("\nkernel_cache calls: ");
  for (j=0; j<i; j++) {
    printf(" %d", altd.nkce_vect[j]);
  }
  for (k=0; k<NDIM_INSPECTED; k++) {
    /* following is only good if the 0'th # of dimensions == 1 */
    int num_words = altd.train_dim_sat_vect[0][i-1];
    printf("\nnumber of SV dimensions with more than %d elements (%d total dimensions): ", dim_map(k), num_words);
    for (j=0; j<i; j++) {
      printf("  %d", altd.sv_dim_sat_vect[k][j]);
    }
  }
  for (k=0; k<NDIM_INSPECTED; k++) {
    int num_words = altd.train_dim_sat_vect[0][i-1];
    printf("\nnumber of train dimensions with more than %d elements (%d total dimensions): ", dim_map(k), num_words);
    for (j=0; j<i; j++) {
      printf("  %d", altd.train_dim_sat_vect[k][j]);
    }
  }
  if (do_ts) {
    printf("\nbegin score matrix:");
    for (j=0; j<i; j++) {
      int k;
      printf("\n");
      for (k=0; k<altd.ntest; k++) {
	printf(" %.3f", altd.test_scores[j][k]);
      }
    }
    printf("\nend score matrix\n");
    for (i=0; i<max_iter; i++) {
      free(altd.test_scores[i]);
    }
    free(altd.test_scores);
  } else {
    printf("\n");
  }

  for(i=0; i<NDIM_INSPECTED; i++) {
    free(altd.sv_dim_sat_vect[i]);
    free(altd.train_dim_sat_vect[i]);
  }
  free(altd.docs_added);
  free(altd.scores_added);
  free(altd.apvect);
  free(altd.anvect);
  free(altd.prb);
  free(altd.nsv_vect);
  free(altd.nbsv_vect);
  free(altd.time_vect);
  free(altd.sv_dim_sat_vect);
  free(altd.train_dim_sat_vect);
  free(altd.nkce_vect);
  free(altd.npos_added);
  free(altd.nneg_added);
  free(altd.query_anvect);
  free(altd.query_apvect);
  free(altd.train_anvect);
  free(altd.train_apvect);

  return nsv;
}

int al_svm(bow_wv **docs, int *yvect, double *weights, double *b, double **W, 
	   int ntrans, int ndocs, int do_rlearn) {
  struct al_test_data altd;

  bzero(&altd,sizeof(struct al_test_data));

  return (al_svm_guts(docs, yvect, weights, b, W, ntrans, ndocs, &altd, do_rlearn));
}
