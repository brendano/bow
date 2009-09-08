/* Copyright (C) 1999 Greg Schohn - gcs@jprc.com */

/* ********************* svm_smo.c **********************
 * John Platt's Sequential Minimal Optimization algorithm 
 * (http://www.research.microsoft.com/~jplatt/smo-book.pdf).
 * This version is based on the modifications proposed by 
 * Keerthi, Shevade, Bhattacharyya & Murthy 
 * (http://guppy.mpe.nus.edu.sg/~mpessk/smo_mod.shtml) 
 */
 
#include <bow/svm.h>
/* the debugging information is out of date since it uses svm_C as its bound
 * instead of values in cvect */
//#define DEBUG

inline int make_set(int bmap_size, int max_list_size, struct set *s) {
  int i;
  s->items = (int *) malloc(sizeof(int)*max_list_size);
  s->loc_map = (int *) malloc(sizeof(int)*bmap_size);

  if (!(s->items && s->loc_map)) {
    return 0;
  }

  for (i=0; i<bmap_size; i++) {
    s->loc_map[i] = -1;
  }

  s->ilength = 0;
  return 1;
}

void free_set(struct set *s) {
  free(s->items);
  free(s->loc_map);
}

inline int set_insert(int a, struct set *s) {
  if (s->loc_map[a] != -1) {
    return 0;
  }
  s->loc_map[a] = s->ilength;
  s->items[s->ilength] = a;
  s->ilength ++;
  return 1;
}

inline int set_delete(int a, struct set *s) {
  if (s->loc_map[a] == -1) {
    return 0;
  }
  if (!(s->ilength == s->loc_map[a] + 1)) {
    /* we need to swap */
    s->items[s->loc_map[a]] = s->items[s->ilength-1];
    s->loc_map[s->items[s->ilength-1]] = s->loc_map[a];
  }
  s->loc_map[a] = -1;
  s->ilength --;
  return 1;
}

inline int set_lookup(int a, struct set *s) {
  return (s->loc_map[a] != -1);
}

#ifdef DEBUG
int c0(double t, int y) { if (t < svm_epsilon_a || t > svm_C - svm_epsilon_a) return 1; else return 0; }
int c1(double t, int y) { if (t < svm_epsilon_a && y == 1) return 0; else return 1; }
int c2(double t, int y) { if (t > svm_C - svm_epsilon_a && y == -1) return 0; else return 1; }
int c3(double t, int y) { if (t > svm_C - svm_epsilon_a && y == 1) return 0; else return 1; }
int c4(double t, int y) { if (t < svm_epsilon_a  && y == -1) return 0; else return 1; }

void check_s(struct set *s, int(*check_weight)(double,int), struct svm_smo_model *m) {
  int i;
  for (i=0; i<s->ilength; i++) {
    if (s->loc_map[s->items[i]] != i) {
      printf("loc_map inconsistent with item list!\n");
      abort();
    }
    if (check_weight(m->weights[s->items[i]],m->yvect[s->items[i]])) {
      printf("wrong set!\n");
      abort();
    }
  }
  return;
}

void check_inv(struct svm_smo_model *m,int ndocs) {
  check_s(&(m->I0), c0, m);
  check_s(&(m->I1), c1, m);
  check_s(&(m->I2), c2, m);
  check_s(&(m->I3), c3, m);
  check_s(&(m->I4), c4, m);

  if (m->I0.ilength + m->I1.ilength + m->I2.ilength + 
      m->I3.ilength + m->I4.ilength != ndocs) {
    abort();
  }
  return;
}
#endif

static int m1=0,m2=0,m3=0,m4=0;

#define PRINT_SMO_PROGRESS(f,ms) (fprintf((f),                          \
       "\r\t\t\t\t\t\tmajor: %d   opt_single: %d/%d   opt_pair: %d/%d     ",\
       (ms)->n_outer, (ms)->n_single_suc, (ms)->n_single_tot,           \
       (ms)->n_pair_suc, (ms)->n_pair_tot))


/* does NOT call kcache_age */
double smo_evaluate_error(struct svm_smo_model *model, int ex) {
  /* do the hyperplane calculation... */
  if (svm_kernel_type == 0) {
    return (evaluate_model_hyperplane(model->W, 0.0, model->docs[ex]) - model->yvect[ex]);
  } else {
    bow_wv **docs;
    int      ndocs;
    double   sum;
    double  *weights;
    int     *yvect;
    int i;

    docs = model->docs;
    ndocs = model->ndocs;
    weights = model->weights;
    yvect = model->yvect;
 
    for (i=0, sum=0.0; i<ndocs; i++) {
      if (weights[i] != 0.0) {
	sum += yvect[i]*weights[i]*svm_kernel_cache(docs[i], docs[ex]);
      }
    }
    return (sum - yvect[ex]);
  }
}

/* in this case we need to compute the obj. function when a2 is at the endpoints */
double calc_eta_hi(int ex1, int ex2, double L, double H, double k11, double k12, 
		   double k22, struct svm_smo_model *ms) {
  bow_wv **docs;
  int ndocs;
  double *weights;
  int *yvect;

  double Lf, Hf,s, gamma;
  double tmp;
  int i;

  docs = ms->docs;
  ndocs = ms->ndocs;
  weights = ms->weights;
  yvect = ms->yvect;

  /* this need not be horribly efficient, since it is only called,
   * for every time eta is non-negative (which is alledgedly rare) */
  {
    double v1,v2;
    double a1, a2;
    int y1, y2;

    for(i=0,v1=v2=0.0; i<ndocs; i++) {
      if (i==ex1 || i==ex2) {
	continue;
      }
      if (weights[i] == 0.0) {
	continue;
      }
      tmp = yvect[i]*weights[i];
      v1 += tmp*svm_kernel_cache(docs[ex1],docs[i]);
      v2 += tmp*svm_kernel_cache(docs[ex2],docs[i]);
    }

    a1 = weights[ex1];
    a2 = weights[ex2];

    y1 = yvect[ex1];
    y2 = yvect[ex2];

#define CALC_W(gamma,s,a2) ((tmp=gamma-s*a2) + a2 - .5*(k11*(tmp*tmp) - k22*a2*a2) \
			    - s*k12*tmp*a2 - y1*tmp*v1 - y2*a2*v2)

    s = y1*y2;
    gamma = a1-s*a2;
    Lf = CALC_W(gamma,s,L);
    Hf = CALC_W(gamma,s,H);
  }

  if (Lf > Hf + svm_epsilon_crit) {
    return L;
  } else if (Lf < Hf - svm_epsilon_crit) {
    return H;
  } else {
    return MAXDOUBLE;
  }
}

/* "tries" to jointly optimize a pair of lagrange weights ...
 * can't always succeed - in those cases, 0 is returned, 1 on success */

/* INVARIANTS: both error[ex1] & error[ex2] must be valid, though 
 * it doesn't matter what set they belong to 

 * all of the weights are feasible & obey the lin. equality
 * constraint when they come in & only this fn plays with the weights */
int opt_pair(int ex1, int ex2, struct svm_smo_model *ms) {
  double   a1, a2;
  double   ao1, ao2;
  float    C1, C2, C_min;
  bow_wv **docs;
  double   diff1, diff2;
  double   e1, e2;
  double   eta;    /* the value of the second deriv. of the obj */
  double   k11, k12, k22;
  int      ndocs;
  double   L, H;
  double  *weights;
  int     *yvect;
  int      y1, y2;

  int i;

  //printf("opt_pair(%d, %d)\n",ex1,ex2);
  //printV("",ms->error,ms->ndocs,"\n");

  if (ex1 == ex2) {
    m1 ++;
    return 0;
  }

  ms->n_pair_tot ++;

  weights = ms->weights;
  yvect = ms->yvect;

  C1 = ms->cvect[ex1];
  C2 = ms->cvect[ex2];
  C_min = MIN(C1, C2);

  y1 = yvect[ex1];
  y2 = yvect[ex2];
  a1 = weights[ex1];
  a2 = weights[ex2];

  if (y1 == y2) {
    H = a1 + a2;
    L = H - C1;
    L = (0 > L) ? 0 : L;
    H = (C2 < H) ? C2 : H;
  } else {
    L = a2 - a1;
    H = L + C1;
    L = (0 > L) ? 0 : L;
    H = (C2 < H) ? C2 : H;
  }

  if (L >= H) {
    m2++;
    return 0;
  }

  docs = ms->docs;
  ndocs = ms->ndocs;

  k12 = svm_kernel_cache(docs[ex1],docs[ex2]);
  k11 = svm_kernel_cache(docs[ex1],docs[ex1]);
  k22 = svm_kernel_cache(docs[ex2],docs[ex2]);

  eta = 2*k12 - k11 - k22;

  //printf("k11,k12,k22,eta:(%f,%f,%f,%f)\n",k11,k12,k22,eta);

  e1 = ms->error[ex1];
  e2 = ms->error[ex2];

  ao2 = a2;

  if (eta < 0) {
    /* a2 still holds weights[j] */
    a2 = a2 - y2*(e1-e2)/eta;
    if (a2 < L) a2 = L;
    else if (a2 > H) a2 = H;

    if (a2 < svm_epsilon_a) {
      a2 = 0;
    } else if (a2 > C2 - svm_epsilon_a) {
      a2 = C2;
    }
  } else {
    a2 = calc_eta_hi(ex1, ex2, L, H, k11, k12, k22, ms);
    if (a2 == MAXDOUBLE)
      return 0;
  }

  if (fabs(a2 - ao2) < svm_epsilon_a) { //*(a2 + ao2 + svm_epsilon_crit)) {
    m4 ++;
    return 0;
  }

  ao1 = weights[ex1];
  a1 = ao1 + y1*y2*(ao2 - a2);

  /* we know that a2 can't be out of the feasible range since we expilicitly
   * tested for this (by clipping) - however, due to prec. problems - a1
   * could be out of range - if it is, we need to make it feasible (to the
   * alpha constraints), since the number is bogus anyway & was caused by
   * precision problems - there's no reason to alter a2 */
  if (a1 < svm_epsilon_a) {
    a1 = 0.0;
  } else if (a1 > C1 - svm_epsilon_a) {
    a1 = C1;
  }

  weights[ex1] = a1;
  weights[ex2] = a2;

  diff1 = y1*(a1 - ao1);
  diff2 = y2*(a2 - ao2);

  /* update the hyperplane */
  if (svm_kernel_type == 0) {
    double *W = ms->W;

    for (i=0; i<docs[ex1]->num_entries; i++) {
      W[docs[ex1]->entry[i].wi] += diff1 * docs[ex1]->entry[i].weight;
    }
    
    for (i=0; i<docs[ex2]->num_entries; i++) {
      W[docs[ex2]->entry[i].wi] += diff2 * docs[ex2]->entry[i].weight;
    }
  }

  /* update the sets (& start to re-evaluate bup & blow) */
  { 
    int j, i, y;
    double a, aold, C, e;
    struct set *s;

    ms->bup = MAXDOUBLE;
    ms->blow = -1*MAXDOUBLE;

    for (j=0, i=ex1, a=a1, aold=ao1, y=y1, C=C1, e=e1; 
	 j<2; 
	 j++, i=ex2, a=a2, aold=ao2, y=y2, C=C2, e=e2) {
      /* the following block also sets bup & blow to preliminary values.
       * this is so that we don't need to repeat these checks when we're 
       * trying to figure out whether or not some */
      if (a < svm_epsilon_a) {
	if (y == 1)  {
	  s = &(ms->I1);
	  if (ms->bup > e) { ms->bup = e; ms->iup = i; }
	} else {
	  s = &(ms->I4);
	  if (ms->blow < e) { ms->blow = e; ms->ilow = i; }
	}
      } else if (a > C - svm_epsilon_a) {
	if (y == 1)  {
	  s = &(ms->I3);
	  if (ms->blow < e) { ms->blow = e; ms->ilow = i; }
	} else {
	  s = &(ms->I2);
	  if (ms->bup > e) { ms->bup = e; ms->iup = i; }
	}
      } else {
	s = &(ms->I0);
	if (ms->blow < e) { ms->blow = e; ms->ilow = i; }
	if (ms->bup > e) { ms->bup = e; ms->iup = i; }	
      }

      if (set_insert(i, s)) { /* if this was actually inserted, 
				 the state of sets has changed, something needs deleted */
	int deleted=0;
	if (aold < svm_epsilon_a) {
	  ms->nsv ++;
	} else if (a < svm_epsilon_a) { /* if this a changed & its zero now, it used to be an SV */
	  ms->nsv --;
	}

	/* there's 12 different possible ways for the sets to change, 
	 * I believe this to be a pretty simple & efficient way to do it... */
	if (y == 1) {
	  if (s != &(ms->I1))
	    deleted = set_delete(i,&(ms->I1));
	  if (!deleted && s != &(ms->I3))
	    deleted = set_delete(i,&(ms->I3));
	} else if (y == -1) {
	  if (s != &(ms->I2)) 
	    deleted = set_delete(i,&(ms->I2));
	  if (!deleted && s != &(ms->I4))
	    deleted = set_delete(i,&(ms->I4));
	}
	if (!deleted) {
	  set_delete(i,&(ms->I0));
	}
      }
    }
  }

  ms->n_pair_suc ++;

  /* much like the build_svm algorithm's s(t) vector, error needs 
   * to be updated every time we set some new alphas */
  /* also finish update bup & blow */
  {
    double *error = ms->error;
    int    *items;
    int     nitems;

    items = ms->I0.items;
    nitems = ms->I0.ilength;

    for (i=0; i<nitems; i++) {
      double a, b;
      a = svm_kernel_cache(docs[ex1],docs[items[i]]);
      b = svm_kernel_cache(docs[ex2],docs[items[i]]);
      error[items[i]]  +=  diff1*a + diff2*b;
    }

    {
      int efrom;
      double e;

      /* compute the new bup & blow */
      for (i=0, e=ms->bup; i<nitems; i++) {
	if (e > error[items[i]]) {
	  e = error[items[i]];
	  efrom = items[i];
	}
      }
      if (e != ms->bup) {
	ms->bup = e;
	ms->iup = efrom;
      }

      for (i=0, e=ms->blow; i<nitems; i++) {
	if (e < error[items[i]]) {
	  e = error[items[i]];
	  efrom = items[i];
	}
      }
      if (ms->blow != e) {
	ms->blow = e;
	ms->ilow = efrom;
      }
    }
  }

  kcache_age();
  //printf("blow = %f(%d), bup = %f(%d)\n",ms->blow, ms->ilow, ms->bup, ms->iup);

  return 1;
}

/* this function is only called when all examples are being queried (ie.
 * the examine_all phase). */
int opt_single(int ex2, struct svm_smo_model *ms) {
  double  *error;
  int      ndocs;
  double  *weights;
  int     *yvect;

  double a2;
  double e2;
  int    y2;

  ms->n_single_tot ++;

  error = ms->error;
  ndocs = ms->ndocs;
  weights = ms->weights;
  yvect   = ms->yvect;

  y2 = ms->yvect[ex2];
  a2 = weights[ex2];

  if (set_lookup(ex2, &(ms->I0))) {
    e2 = error[ex2];
  } else {
    e2 = error[ex2] = smo_evaluate_error(ms, ex2);

    if (set_lookup(ex2, &(ms->I1)) || set_lookup(ex2, &(ms->I2))) {
      if (e2 < ms->bup) {
	ms->iup = ex2;
	ms->bup = e2;
      }
    } else if (!set_lookup(ex2, &(ms->I0))) {  /* must be in I3 orI4 */
      if (e2 > ms->blow) {
	ms->ilow = ex2;
	ms->blow = e2;
      }     
    }
  }

  {
    int opt=1;
    int ex1;

    if (set_lookup(ex2, &(ms->I0)) || set_lookup(ex2, &(ms->I1)) 
	|| set_lookup(ex2, &(ms->I2))) {
      if (ms->blow-e2 > 2*svm_epsilon_crit) {
	opt = 0;
	ex1 = ms->ilow;
      }
    }

    if (set_lookup(ex2, &(ms->I0)) || set_lookup(ex2, &(ms->I3))
	|| set_lookup(ex2, &(ms->I4))) {
      if (e2-ms->bup > 2*svm_epsilon_crit) {
	opt = 0;
	ex1 = ms->iup;
      }
    }

    if (opt == 1) {
      kcache_age();
      return 0;
    }

    /* if we get here, then opt was == 1 & ex1 was valid */

    if (set_lookup(ex2, &(ms->I0))) {
      if (ms->blow > 2*e2 - ms->bup) {
	ex1 = ms->ilow;
      } else {
	ex1 = ms->iup;
      }
    }

    if (!set_lookup(ex1, &(ms->I0))) { /* not in the cache & it needs to be */
      error[ex1] = smo_evaluate_error(ms, ex1);
    }

    kcache_age();
    if (opt_pair(ex1, ex2, ms)) {
      ms->n_single_suc ++;
      return 1;
    } else {
      return 0;
    }
  }
}

int smo(bow_wv **docs, int *yvect, double *weights, double *a_b, double **W, 
	int ndocs, double *error, float *cvect, int *nsv) {
  int          changed;
  int          inspect_all;
  struct svm_smo_model model;
  int          nchanged;
  int          num_words;
  double      *original_weights;

  int i,j,k,n;

  num_words = bow_num_words();

  m1 = m2 = m3 = m4 = 0;

  model.n_pair_suc = model.n_pair_tot = model.n_single_suc = 
    model.n_single_tot = model.n_outer = 0;
  model.nsv = *nsv;
  model.docs = docs;
  model.error = error;
  model.ndocs = ndocs;
  model.cvect = cvect;
  original_weights = NULL;
  if (svm_kernel_type == 0 && !(*W)) {
    *W = model.W = (double *) malloc(sizeof(double)*num_words);
  } else {
    model.W = NULL;
  }
  model.weights = weights;
  model.yvect = yvect;

  /* figure out the # of positives */
  for (i=j=k=n=0; i<ndocs; i++) {
    if (yvect[i] == 1) {
      k = i;
      j++;
    } else {
      n = i;
    }
  }
  /* k is set to the last positive example found, n is the last negative */

  make_set(ndocs,ndocs,&(model.I0));
  make_set(ndocs,j,&(model.I1));
  make_set(ndocs,ndocs-j,&(model.I2));
  make_set(ndocs,j,&(model.I3));
  make_set(ndocs,ndocs-j,&(model.I4));

  /* this is the code which initializes the sets according to the weights values */
  for (i=0; i<ndocs; i++) {
    struct set *s;
    if (weights[i] > svm_epsilon_a && weights[i] < cvect[i] - svm_epsilon_a) {
      s = &(model.I0);
    } else if (yvect[i] == 1) {
      if (weights[i] < svm_epsilon_a)   s = &(model.I1);
      else                          s = &(model.I3);
    } else {
      if (weights[i] < svm_epsilon_a)   s = &(model.I4);
      else                          s = &(model.I2);
    }
    set_insert(i, s);
  }

  if (model.W) {
    for (i=0; i<num_words; i++) {
      model.W[i] = 0.0;
    }
  }

  if (model.I0.ilength == 0) {
    model.blow = 1;
    model.bup  = -1;
    model.iup  = k;
    model.ilow = n;
    error[k] = -1;
    error[n] = 1;
  } else { /* compute bup & blow */
    int    efrom, nitems;
    int   *items;
    double e;

    nitems = model.I0.ilength;
    items = model.I0.items;

    for (i=0, e=-1*MAXDOUBLE; i<nitems; i++) {
      if (e < error[items[i]]) {
	e = error[items[i]];
	efrom = items[i];
      }
    }
    model.blow = e;
    model.ilow = efrom;
    
    for (i=0, e=MAXDOUBLE; i<nitems; i++) {
      if (e > error[items[i]]) {
	e = error[items[i]];
	efrom = items[i];
      }
    }
    model.bup = e;
    model.iup = efrom;

    if (model.W) {
      for (i=0; i<nitems; i++) {
	for (j=0; j<docs[items[i]]->num_entries; j++) {
	  model.W[docs[items[i]]->entry[j].wi] += 
	    yvect[items[i]] * weights[items[i]] * docs[items[i]]->entry[j].weight;
	}
      }
      
      /* also need to include bound sv's (I2 & I3) */
      for (k=0, nitems=model.I2.ilength, items=model.I2.items; 
	   k<2; 
	   k++, nitems=model.I3.ilength, items=model.I3.items) {
	
	for (i=0; i<nitems; i++) {
	  for (j=0; j<docs[items[i]]->num_entries; j++) {
	    model.W[docs[items[i]]->entry[j].wi] += 
	      yvect[items[i]] * weights[items[i]] * docs[items[i]]->entry[j].weight;
	  }
	}
      }
    }
  }

  if (!model.W) {
    model.W = *W;
  }

  if (svm_weight_style == WEIGHTS_PER_MODEL) {
    kcache_init(ndocs);
  }

  inspect_all = 1;
  nchanged = 0;
  changed = 0;
  while (nchanged || inspect_all) {
    nchanged = 0;
    
#ifdef DEBUG
    check_inv(&model,ndocs);
#endif

    model.n_outer ++;
    PRINT_SMO_PROGRESS(stderr, &model);
    fflush(stderr);

    if (1 && inspect_all) {
      int ub = ndocs;
      i=j=random() % ndocs;
      for (k=0; k<2; k++,ub=j,i=0) {
	for (; i<ub; i++) {
	  nchanged += opt_single(i, &model);

#ifdef DEBUG
	  check_inv(&model,ndocs);
#endif
	}
      }
      inspect_all = 0;
    } else {
      /* greg's modification to keerthi, et al's modification 2 */
      /* loop of optimizing all pairwise in a row with all elements
       * in I0 (just like above, but only those in I0) */
      do {
	nchanged = 0;

	/* here's the continuous iup/ilow loop */
	while (1) {
	  if (!set_lookup(model.iup, &(model.I0))) {
	    error[model.iup] = smo_evaluate_error(&model,model.iup);
	  }
	  if (!set_lookup(model.ilow, &(model.I0))) {
	    error[model.ilow] = smo_evaluate_error(&model,model.ilow);
	  }
	  if (opt_pair(model.iup, model.ilow, &model)) {
#ifdef DEBUG
	    check_inv(&model,ndocs);
#endif
	    
	    nchanged ++;
	  } else {
	    break;
	  }
	  if (model.bup > model.blow - 2*svm_epsilon_crit)
	    break;
	}
	
	if (nchanged) {
	  changed = 1;
	}
	nchanged = 0;
	  
	/* now inspect all of the elements in I0 */
	{
	  int ub = ndocs;
	  i=j=random() % ndocs;
	  for (k=0; k<2; k++,ub=j,i=0) {
	    for (; i<ub; i++) {
	      if (set_lookup(i, &(model.I0))) {
		nchanged += opt_single(i, &model);
#ifdef DEBUG
		check_inv(&model,ndocs);
#endif
	      }
	    }
	  }
	}
      } while (nchanged);
      /* of of the loop */

      if (nchanged) {
	changed = 1;
      } 
      inspect_all = 1;
    }

    /* note: both of the above blocks no when they are done so they flip inspect_all */
    if (nchanged) {
      changed = 1;
    } 
  }

  free_set(&model.I0);
  free_set(&model.I1);
  free_set(&model.I2);
  free_set(&model.I3);
  free_set(&model.I4);

  if (svm_weight_style == WEIGHTS_PER_MODEL) {
    kcache_clear();
  }
  if (svm_verbosity > 3) 
    fprintf(stderr,"\n");

  //printf("bup=%f, blow=%f\n",model.bup,model.blow);

  *a_b = (model.bup + model.blow) / 2;

  if (svm_kernel_type == 0) {
    for (i=j=0; i<num_words; i++) {
      if (model.W[i] != 0.0) 
	j++;
    }
  }

  //printf("m1: %d, m2: %d, m3: %d, m4: %d", m1,m2,m3,m4);
  *nsv = model.nsv;

  return (changed);
}

/* defunct fn. */
inline double svm_smo_tval_to_err(double si, double b, int y) {
  return 0.0;
}
