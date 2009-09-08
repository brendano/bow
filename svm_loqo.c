/* Copyright (C) 1999 Greg Schohn - gcs@jprc.com */

/* ********************* svm_thorsten.c **********************
 * Based on Thorsten Joachim's "Making large-Scale SVM 
 * Learning Practical" 
 * (http://www-ai.cs.uni-dortmund.de/DOKUMENTE/joachims_99a.ps.gz)
 * 
 * This version does not do shrinking.  This also is dependent
 * upon Alex Smola's pr_loqo solver (see README_SVM). */

#include <bow/svm.h>

#define INIT_SIGDIGIT 15    /* precision that pr_loqo will start with */
#define LOOSE2LIVE    1000  /* # of iterations that pr_loqo should 
			     * spin with a loose precision */

/* should use the selection algorithm described on pg 44 of joachim's ch. 11 */
/* returns the # of items placed into the ws vector, the elements are returned
 * in sorted order */
/* s is the s(t) from 11.36, the gradient of a(t) */
/* n must be multiple of 4 */
int get_ws(int *ws, int *y, double *a, double *s, float *cvect, 
	   int total, int old_n, int n, struct di *scratch) {
  int npicked;
  int nws;
  int  *oldws;
  char *picked;
  double tmp;
  int i,j,k;

  npicked = 0;
  oldws = alloca(sizeof(int)*n);
  picked = alloca(sizeof(char)*total);
  bzero(picked, sizeof(char)*total);

  for (i=0; i<n; i++) {
    oldws[i] = ws[i];
  }

  /* this fills in half - the half with the old ones... */
  for (j=1; j>(-2); j-=2) { /* go thru twice, each time filling up n/4 elements */
    for(i=0, nws=0; i<old_n; i++) {
      /* only add those elements which satisfy 11.21 & 22 */
      tmp = j*y[oldws[i]];
      /* follow DIRECTLY from the logic in 11.3 of adv. kernel methods */
      /* the d_i = y_i case happens first (where the elements with the LARGEST
       * V(_) are chosen, then the d_i = -y_i are chosen next, where the SMALLEST
       * are chosen */
      if (((a[oldws[i]]>svm_epsilon_a) && (a[oldws[i]]<cvect[oldws[i]]-svm_epsilon_a)) 
	  || ((a[oldws[i]]<=svm_epsilon_a) && (tmp>0))
	  || ((a[oldws[i]]>=cvect[oldws[i]]-svm_epsilon_a) && (tmp<0))) {
	/* use tmp instead of y[i] so that we can still pull things off of
	 * the front of the list (like choosing a different sort fn) */
	scratch[nws].d = tmp*(-1+y[oldws[i]]*s[oldws[i]]); /* look familiar? (g(a)) */
	scratch[nws].i = oldws[i];
	nws++;
      }
    }

    /* this used  to be qsort, but nws can be extremely large */
    get_top_n(scratch, nws, n);

    /* k counts the number of things added */
    for (i=k=0; (k<n/4) && (i<nws); i++) {
      if (!picked[scratch[i].i]) {
	ws[npicked] = scratch[i].i;
	picked[scratch[i].i] = 1;
	npicked++;
	k++;
      }
    }
  }

  for (j=1; j>(-2); j-=2) { /* go thru twice, each time filling up n/4 elements */
    for(i=0, nws=0; i<total; i++) {
      /* only add those elements which satisfy 11.21 & 22 */
      tmp = j*y[i];
      /* follow DIRECTLY from the logic in 11.3 of adv. kernel methods */
      /* the d_i = y_i case happens first (where the elements with the LARGEST
       * V(_) are chosen, then the d_i = -y_i are chosen next, where the SMALLEST
       * are chosen */
      if (((a[i]>svm_epsilon_a) && (a[i]<cvect[i]-svm_epsilon_a)) 
	  || ((a[i]<=svm_epsilon_a) && (tmp>0))
	  || ((a[i]>=cvect[i]-svm_epsilon_a) && (tmp<0))) {
	/* use tmp instead of y[i] so that we can still pull things off of
	 * the front of the list (like choosing a different sort fn) */
	scratch[nws].d = tmp*(-1+y[i]*s[i]); /* look familiar? (g(a)) */
	scratch[nws].i = i;
	nws++;
      }
    }

    /* this used  to be qsort, but nws can be extremely large */
    get_top_n(scratch, nws, n);

    /* k counts the number of things added */
    for (i=k=0; (k<n/4) && (i<nws); i++) {
      if (!picked[scratch[i].i]) {
	ws[npicked] = scratch[i].i;
	picked[scratch[i].i] = 1;
	npicked++;
	k++;
      }
    }
  }

  if (npicked < n) {
    n = npicked;
  }

  qsort(ws, n, sizeof(int), i_cmp);

  if (svm_verbosity > 1) { 
    int ii; 
    fprintf(stderr,"working set: "); 
    for (ii=0; ii<n;ii++) { 
      fprintf(stderr,"%d ",ws[ii]);
    } 
    fprintf(stderr,"\n"); 

    fprintf(stderr,"s[ws[*]]: "); 
    for (ii=0; ii<n;ii++) { 
      fprintf(stderr,"%f ",s[ws[ii]]);
    } 
    fprintf(stderr,"\n"); 
  }

  return n;
}

static double calculate_obj(struct svm_qp *q, double *a, int n) {
  double obj;
  int i, j;

  obj = 0.0;
  for (i=0; i<n; i++) {
    /*      "linear part"   "quadratic" part across the diagonal */
    obj += (q->g0[i]*a[i]) + (.5*a[i]*a[i]*q->g[i*n+i]);

    /* since its sym. only go thru once for each ind. & mult by 2 (the .5 goes to 1) */
    for (j=0; j<i; j++) { 
      obj += a[i]*a[j]*q->g[j*n+i];
    }
  }
  return obj;
}

static int npr_loqo_failures=0;  /* counts the number of times the objective has increased */
/* calls pr_loqo & does the best error checking that it can (ie. the check's
 * that svmlight does... */
int solve_qp(struct svm_qp *q, int n) {
  double dist;
  double epsilon_loqo;
  int    iter;
  double margin;
  int    result;
  double obj0, obj1;

  int i, j;

  result = !OPTIMAL_SOLUTION;

  /* calculate the objective value before loqo has a go at it */
  obj0 = calculate_obj(q, q->init_a, n);

  /* still don't understand the margin stuff - just copied from svmlight */
  for (iter=q->init_iter, margin=q->margin; (margin<=.9999999) && (result != OPTIMAL_SOLUTION); ) {
    /* note how m always == 1 & restart is always false */
    result = pr_loqo(n, 1, q->g0, q->g, q->ce, q->ce0, q->lbv, q->ubv, q->primal, q->dual,
		     svm_verbosity-4, (double) q->digits, iter, q->margin, q->bound, 0);

    if (isnan(q->dual[0])) {
      if (q->margin < .8) {
	q->margin = (margin*4+1.0)/5.0;
      }
      margin = (margin+1)/2.0;
      q->digits--;
      //printf("invalid dual, Reducing precision of solver (digits = %d).\n", q->digits);
    } else if (result != OPTIMAL_SOLUTION) { /* if there is some other problem */
      iter += 2000; /* yaslh */
      q->init_iter += 10;
      q->digits--;
      //printf(" (digits = %d).\n", q->digits);
    }
  }

  /* svmlight does this & it doesn't seem like a bad idea */
  epsilon_loqo=1E-10;
  for(i=0; i<n; i++) {
    dist=-q->dual[0]*q->ce[i];
    dist+=(q->g0[i]+1.0);
    for(j=0; j<i; j++) {
      dist += (q->primal[j]*q->g[j*n+i]);
    }
    for(j=i; j<n; j++) {
      dist += (q->primal[j]*q->g[i*n+j]);
    }
    if((q->primal[i]<(q->ubv[i]-epsilon_loqo)) && (dist < (1.0-svm_epsilon_crit))) {
      fprintf(stderr, "relaxing epsilon_loqo (%f,%f)\n", q->primal[i],dist);
      epsilon_loqo=(q->ubv[i]-q->primal[i])*2.0;
    } else if((q->primal[i]>epsilon_loqo) && (dist > (1.0+svm_epsilon_crit))) {
      fprintf(stderr, "relaxing epsilon_loqo (%f,%f)\n", q->primal[i],dist);
      epsilon_loqo = q->primal[i]*2.0;
    }
  }

  for(i=0; i<n; i++) {  /* clip alphas to bounds */
    if(q->primal[i]<=epsilon_loqo) {
      //fprintf(stderr,"primal[i]=%f,eps=%f",q->primal[i],epsilon_loqo);
      q->primal[i] = 0;
    } else if(q->primal[i]>=q->ubv[i]-epsilon_loqo) {
      //fprintf(stderr,"primal[i]=%f,eps=%f",q->primal[i],epsilon_loqo);
      q->primal[i] = q->ubv[i];
    }
  }

  obj1 = calculate_obj(q, q->primal, n);

  if (obj1 >= obj0) {
    q->digits += 2;
    fprintf(stderr,"objective function increased (from %f to %f)! Increasing precision (digits = %d)\n",obj0,obj1,q->digits);
    if (svm_verbosity > 0) {
      printV("Before: ", q->init_a, n, "\n");
      printV("After:  ", q->primal, n, "\n");
    }

    npr_loqo_failures++;
    if (npr_loqo_failures > 200) {
      npr_loqo_failures=0;
      svm_epsilon_crit = svm_epsilon_crit * 1.5; /* give up at this prec., make cond. easier... */
      fprintf(stderr,"Over 200 increases of the objective - increasing KKT slack to %f\n",svm_epsilon_crit);
      printf("Over 200 increases of the objective - increasing KKT slack to %f\n",svm_epsilon_crit);
    }
  } else if (svm_verbosity >2) {
    fprintf(stderr,"objective: %f --> %f\n", obj0, obj1);
    printV("After:  ", q->primal, n, "\n");
  }

  /* make sure to round results within epsilon of the bounds */
  if (result == OPTIMAL_SOLUTION) {
    return SUCCESS;
  } else {
    fprintf(stderr,"optimal solution not found by pr_loqo");
    return ERROR;
  }
}

void setup_solve_sub_qp(int *ws, int *y, double *a, bow_wv **docs, struct svm_qp *qd, int n, int *nsv) {
  int di;
  double qbn;

  int i,j,h,k;

  qd->ce0[0] = 0.0;

  /* compute the constant Sum{i of N}{A_i*y_i} in the constraint */
  /* since this is an equality constraint that sums to 0, the sum of
   * the terms in the working set before optimization must be equal to 
   * that after...  therefore, simply summing over the working set is
   * just as good as explicitly summing over the bound set... */
  for (i=0; i<n; i++) {
    if (a[ws[i]] > svm_epsilon_a) {
      qd->ce0[0] += y[ws[i]]*a[ws[i]];
    }
  }

  /* compute things in B */
  for (i=0; i<n; i++) {
    /* setup equality constraint (a_i*y_i) vector */
    di = ws[i];
    qd->ce[i] = y[di];

    qbn = 0.0;

    for (h=j=k=0, qbn=0.0; h<(*nsv); j++) {
      /* if this is an sv */
      if (a[j] > svm_epsilon_a) {
	/* remember we're ONLY adding those things in N, not b U n */
	if (k < n) {
	  if (ws[k] == j) {
	    h++;
	    k++;
	    continue;
	  } else {
	    if (ws[k] < j) {  /* same as above */
	      k++;
	      j--;
	      continue;
	    }
	  }
	}
	qbn += a[j]*y[j]*svm_kernel_cache(docs[j], docs[di]);
	h++;
      }
    }

    /* multiply that sum by the label of its cross-reference - this is Qbn
     * since the term -a_b also gets summed up - add them to qbn */
    qd->g0[i] = -1 + y[di]*qbn;

    /* put together the "quadratic" terms - the BxB part */
    for (j=i; j<n; j++) {
      qd->g[i*n + j] = y[di]*y[ws[j]]*svm_kernel_cache(docs[ws[j]], docs[di]);
    }
  }

  kcache_age();

  /* init_a is kept in qd so that the B alphas that correspond to 
   * the alphas in the primal are readily & easily available */
  for(i=0; i<n; i++) {
    qd->init_a[i] = a[ws[i]];
  }

  /* IMPORTANT - this is the only place that the number of support vectors
   * can change & they'll only change (arrive or leave) in the working set
   * (since those alpa in N cannot be modified) */

  if (svm_verbosity > 3) {
    printf("calling solver with these variables...\nce0=%f\n",qd->ce0[0]);
    printV("init_a: ", qd->init_a, n, "\n");
    printV("ce:     ", qd->ce, n, "\n");
    printV("g0:     ", qd->g0, n, "\n");
  
    printf("hessian:\n");
    for (i=0; i<n; i++) {
      printV("     ", &(qd->g[i*n]), n, "\n");
    }
  }
  
  /* this is a function so that other functions for other solvers may be written */
  if (SUCCESS == solve_qp(qd, n)) {
    /* copy primal (the solution for the alphas to our alpha) */
    /* data has already been clipped/rounded by solve_qp (things within epsilon
     * are rounded, see above) */
    for (i=0; i<n; i++) {
      /* round those alpha's whose values are close to the boundaries */
      if (qd->primal[i] <= svm_epsilon_a) {
	if (a[ws[i]] > svm_epsilon_a) {
	  (*nsv)--;
	}
	a[ws[i]] = 0.0;
      } else {
	if (a[ws[i]] <= svm_epsilon_a) {
	  (*nsv)++;
	}
	if (qd->primal[i] >= qd->ubv[i]-svm_epsilon_a) {
	  a[ws[i]] = qd->ubv[i];
	} else {
	  a[ws[i]] = qd->primal[i];
	}
      }
    }
  }
}

void recompute_gradient(double *s, bow_wv **docs, int *yvect, double *weights, 
			double *old_weights, int *ws, int wss, int total) {
  int i,j;

  fprintf(stderr,"differences:");

  for (i=0; i<total; i++) {
    double tmp = 0.0;
    for (j=0; j<total; j++) {
      tmp += weights[j]*yvect[j]*svm_kernel_cache_lookup(docs[i],docs[j]);
    }
    if (s[i] - tmp > svm_epsilon_a) { 
      fprintf(stderr, "%d diff = %f", i, tmp-s[i]);
    }
    s[i] = tmp;
  }
  fprintf(stderr,"\n");
}

void update_gradient(double *s, bow_wv **docs, int *yvect, double *weights, 
		     double *old_weights, int *ws, int wss, int total) {
  int i,j,k;
  double *wdy;
  int    *wds;  /* those wdy's that are non-zero */

  wdy = (double *) alloca(sizeof(double)*wss);
  wds = (int *) alloca(sizeof(int)*wss);
  
  /* store all of the results early on, so that a potential
   * enormous s can be cycled thru in a cache friendly manner */
  for (k=i=0; i<wss; i++) {
    j = ws[i];
    if (weights[j] != old_weights[j]) {
      wdy[k] = (weights[j] - old_weights[j]) * yvect[j];
      wds[k] = j;
      k++;
    }
  }

  for (i=0; i<total; i++) {
    for (j=0; j<k; j++) {
      s[i] += wdy[j]*svm_kernel_cache(docs[i],docs[wds[j]]);
    }
  }

  kcache_age();
}

double calculate_b(double *s, int *yvect, double *a, float *cvect, int ndocs) {
  int i,j;
  double b, maxgrad, mingrad;

  mingrad = MAXDOUBLE;
  maxgrad = -1*MAXDOUBLE;

  b = 0;
  for (j=i=0; i<ndocs; i++) {
    if (a[i] > svm_epsilon_a) {
      if (a[i] < cvect[i]-svm_epsilon_a) {
	b += s[i] - yvect[i];
	j++;
      } else if (!j) {
	if ((yvect[i] == 1) && (maxgrad<s[i])) {
	  maxgrad = s[i];
	} else if ((yvect[i] == -1) && (mingrad>s[i])) {
	  mingrad = s[i];
	}
      }
    }
  }

  if (j) {
    return (b/j);
  } else {
    assert(maxgrad != MAXDOUBLE);
    return ((maxgrad+mingrad)/2);
  }
}

int check_optimality(double *s, double *a, int *y, float *cvect, double b, int n) {
  double dist, adist, max_dist;

  int i;

  max_dist = 0;

  /* sanity check 
  dist = 0.0;
  for (i=0; i<n; i++) {
    dist += y[i]*a[i];
  }
  if ((dist > svm_epsilon_crit) || (dist < -1*svm_epsilon_crit)) {
    printf("\ndist == %f\n",dist);
    abort();
    }*/

  for(i=0; i<n; i++) {
    dist = (s[i]-b)*y[i];   /* distance from hyperplane*/
    adist = fabs(dist-1.0); /* how far is it from where it should be */

    if(adist > max_dist) {
      if((a[i] < cvect[i]-svm_epsilon_a) && (dist < 1)) {
	//printf("max_dist=%f, (%f-%f)*%d\n", adist, s[i], b, y[i]);
	max_dist = adist;
      }
      if((a[i]>svm_epsilon_a) && (dist > 1)) {
	//printf("max_dist=%f, (%f-%f)*%d\n", adist, s[i], b, y[i]);
	max_dist = adist;
      }
    }
  }

  if (max_dist > svm_epsilon_crit) {  /* termination criterion */
    return (0);
  } else {
    return (1);
  }
}

int build_svm_guts(bow_wv **docs, int *yvect, double *weights, double *b, 
		   double **W, int ndocs, double *s, float *cvect, int *nsv) {
  double       tb;
  int          cwss;        /* current working set size */
  int          n2inc_prec;  /* # of iterations before we try to increase 
			     * the prec. of the solver  */
  double       original_eps_crit; /* global epsilon_crit gets altered, this 
				   * is to set it back */
  double      *original_weights;  /* address of the vector passed in */
  double      *old_weights; /* lagrange multipliers */
  int         *old_ws; /* just for debugging... */
  struct svm_qp qdata;       
  int          qp_cnt;
  struct di   *scratch;     /* scratch area for 2*bsize doubles */
  int         *ws;          /* bsize of these - the current working set */

#ifdef GCSJPRC
  int old_digits=-1;
#endif

  int i,j;


  //recompute_gradient(s, docs, yvect, weights, old_weights, ws, cwss, ndocs);
  
  npr_loqo_failures=0;

  original_eps_crit = svm_epsilon_crit;

  scratch = (struct di *) alloca(sizeof(struct di)*ndocs);
  old_weights = (double *) alloca(sizeof(double)*ndocs);
  ws = (int *) alloca(sizeof(int)*svm_bsize);
  old_ws = (int *) alloca(sizeof(int)*svm_bsize);

  qdata.init_a = (double *) alloca(sizeof(double)*svm_bsize);
  qdata.ce = (double *) alloca(sizeof(double)*svm_bsize);
  qdata.ce0 = (double *) alloca(sizeof(double)); /* only 1 constant in 1 constraint */
  qdata.g = (double *) alloca(sizeof(double)*svm_bsize*svm_bsize); /* hessian */
  qdata.g0 = (double *) alloca(sizeof(double)*svm_bsize);      /* qbn */

  qdata.primal = (double *) alloca(sizeof(double)*svm_bsize*3);
  qdata.dual = (double *) alloca(sizeof(double)*(svm_bsize*2+1));
  qdata.ubv = (double *) alloca(sizeof(double)*svm_bsize/* should be m */);
  qdata.lbv = (double *) alloca(sizeof(double)*svm_bsize);
  
  /* initialize lbv to non-restricting values */
  /* also hit the bottom triangle of the hessian */
  for (i=0; i<svm_bsize; i++) {
    for (j=i;j<svm_bsize;j++) {
      qdata.g[i*svm_bsize+j] = 0.0;
    }
    qdata.lbv[i] = 0.0;
  }

  /* this is what svmlight does, i'm not sure what the bound is used for */
  qdata.bound = svm_C/4.0;
  qdata.digits = INIT_SIGDIGIT;
  qdata.margin = 0.15;
  qdata.init_iter = 500;
  
  for (i=0; i<ndocs; i++) {
    old_weights[i] = weights[i];
  }

  if (svm_weight_style == WEIGHTS_PER_MODEL) {
    kcache_init(ndocs);
  }

  n2inc_prec = LOOSE2LIVE;
  original_weights = NULL;
  qp_cnt = 0;
  cwss = 0;

  while (1) {
    /* the optimality check is first so that when active learning is happening,
     * it becomes a lot quicker - since a update_gradient may not need to be
     * called for a good number of iterations. */
    /* update b */

    tb = calculate_b(s, yvect, weights, cvect, ndocs);

    /* check optimality */
    if (check_optimality(s, weights, yvect, cvect, tb, ndocs)) {
      break;
    }

    qp_cnt++;
    if (svm_verbosity > 1) {
      fprintf(stderr,"%dth iteration of solve_qp\n", qp_cnt);
    } else {
      if (!(qp_cnt % 200)) {
	fprintf(stderr,"\r\t\t\t\t\t\t%dth iteration", qp_cnt);
	fflush(stdout);
      }
    }

    /* put a working set together */
    for (i=0; i<cwss; i++) {
      old_ws[i] = ws[i];
    }
    cwss = get_ws(ws, yvect, weights, s, cvect, ndocs, cwss, svm_bsize, scratch);    

    for (i=j=0; i<cwss; i++) {
      if (old_weights[ws[i]] == weights[ws[i]] && ws[i] == old_ws[i]) {
	j++;
      }
      old_weights[ws[i]] = weights[ws[i]];
      qdata.ubv[i] = cvect[ws[i]];
    }

    /* this detects infinite loops - which shouldn't happen - but... */
#if 0
    if (j == cwss && qdata.digits == old_digits) {
      fprintf(stderr, "Uh-oh - old weights identical to new weights");
      system("echo \"rainbow did a boo-boo - stopping!\" | /usr/sbin/sendmail gcs@jules.res.cmu.edu");
      svm_verbosity = 4;
      fflush(stderr);
      kill(getpid(),SIGSTOP);
    }
#endif

#ifdef GCSJPRC
    old_digits = qdata.digits;
#endif

    /* using the working set, solve the subproblem */
    setup_solve_sub_qp(ws, yvect, weights, docs, &qdata, cwss, nsv);

    /* update s(t) */
    update_gradient(s, docs, yvect, weights, old_weights, ws, cwss, ndocs);

    if (qdata.digits < INIT_SIGDIGIT) {
      if (n2inc_prec) {
	n2inc_prec --;
      } else {
	n2inc_prec = LOOSE2LIVE;
	qdata.digits = INIT_SIGDIGIT;
	/* fprintf(stderr, "LOOSE2LIVE reached... Increasing precision\n"); */
      }
    } else {
      n2inc_prec = LOOSE2LIVE;
    }
  }

  /* make a hyperplane if we can, since they're so fast :) */
  if (svm_kernel_type == 0) {
    int num_words = bow_num_words();
    int i,j,k;

    *W = (double *) malloc(sizeof(double)*num_words);
    for (i=j=0; i<num_words; i++) {
      (*W)[i] = 0.0;
    }

    for (i=j=0; j<*nsv; i++) {
      if (weights[i] != 0.0) {
	for (k=0; k<docs[i]->num_entries; k++) {
	  (*W)[docs[i]->entry[k].wi] += weights[i]*yvect[i]*docs[i]->entry[k].weight;
	}
	j++;
      }
    }
  }

  if (svm_weight_style == WEIGHTS_PER_MODEL) {
    kcache_clear();
  }

  svm_epsilon_crit = original_eps_crit;

  *b = tb;

  return qp_cnt;
}

/* if this gets called, the weight must have been bound */
inline double svm_loqo_tval_to_err(double si, double b, int y) {
  double ytest = si-b;
  if (y == 1) {
    if (ytest < y) {
      return(y - ytest);
    }
  } else {
    if (ytest > y) {
      return(ytest - y);
    }
  }
  return (0.0);
}
