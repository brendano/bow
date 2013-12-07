/* Copyright (C) 1997, 1998 Andrew McCallum

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

#ifndef __BOW_SVM_H
#define __BOW_SVM_H

#include <math.h>
#include <string.h>
#include <float.h>
#include <time.h>
#include <signal.h>
#include <sys/times.h>
#include <bow/libbow.h>
#include <argp/argp.h>
#include <bow/naivebayes.h> /* for fisher kernel */
#ifdef HAVE_LOQO
#include <bow/pr_loqo.h> /* see alex smola's page for the source */
#endif
#ifdef HAVE_FPSETMASK
#include <floatingpoint.h>
#endif

#if !HAVE_LOG2F
#define log2f log
#endif

#ifndef MAXFLOAT
#ifdef FLT_MAX
#define MAXFLOAT FLT_MAX
#else
#define MAXFLOAT 3.40282347e+38F
#endif
#endif


/* this macro returns a float, which should be enough since b is only used in
 * 1 addition & the range of differences is not large */
#define GET_CDOC_ARRAY_EL(barrel,i) \
      ((bow_cdoc *)(bow_array_entry_at_index((barrel->cdocs),i)))

#define printV(s1,v,n,s3) 			\
{ int ii; 					\
  fprintf(stderr,s1); 				\
  for (ii=0; ii<n;ii++) { 			\
    fprintf(stderr,"%2.6f ",(v)[ii]);		\
  } 						\
  fprintf(stderr,s3); 				\
}

/* random macros for bitmaps... */
#define GETVALID(valid,i)   (((valid)[(i)/(sizeof(int)*8)]>>((i)%(sizeof(int)*8))) & 1)
#define SETVALID(valid,i)   ((valid)[(i)/(sizeof(int)*8)] |= (1 << ((i)%(sizeof(int)*8))))
#define SETINVALID(valid,i) ((valid)[(i)/(sizeof(int)*8)] &= (~(1 << ((i)%(sizeof(int)*8)))))

#define SUCCESS       0
#define ERROR         1

#define RAW       0
#define LOG       1
#define SQRT      2
#define TFIDF     1
#define INFOGAIN  2

#define FISHER    4

#define NO_WEIGHTS         0
#define WEIGHTS_PER_BARREL 1
#define WEIGHTS_PER_MODEL  2

#define INIT_KKT      0.001 /* initial val for epsilon_crit */
#define EPSILON_CSTAR 1E-2

struct di {
  double d;
  int i;
};

struct svm_qp {
  /* this is only for calculating before & after objective fn
   * values to check for progress */
  double *init_a;
  /* ce = equality constraint, g = minimization fn*/
  /* 0 is for constants & linear things (constraint & min fns respectively) */
  double *ce, *ce0, *g, *g0;
  /* these aren't altered by me, they're just scratch spaces for pr_logo */
  double *primal, *dual, *lbv, *ubv;
  /* other random stuff... */
  /* digits is the precision... */
  double margin, bound;
  int    digits, init_iter;
};

/* some constant-time anything set's -> + constant time
 *                                      - big memory footprint */
struct set {
  int  ilength;
  int *loc_map; /* non-sparse map */
  int *items;
};

struct svm_smo_model {
  bow_wv    **docs;
  /* see the tech rept. for the meaning of these poorly named variables */
  struct set  I0, I1, I2, I3, I4;
  double     *weights;
  double     *W;
  double     *error;
  float      *cvect;
  int        *yvect;
  double      bup,blow;
  int         iup,ilow;
  int         ndocs;
  int         nsv;
  int         n_pair_suc, n_pair_tot, n_single_suc, n_single_tot, n_outer;
};

extern double svm_epsilon_a;    /* for alpha's & there bounds */
extern double svm_epsilon_crit; /* for critical KT points */
extern double svm_C;

extern int svm_bsize;
extern int svm_kernel_type;
extern int svm_remove_misclassified;
extern int svm_weight_style;
/* this is included here so that the kcache call count can be reset  */
extern int svm_nkc_calls;

extern int svm_init_al_tset;
extern int svm_al_qsize;
extern int svm_al_do_trans;

extern int svm_use_smo;
extern int svm_verbosity;
extern int svm_random_seed;

/* for transduction */
extern double svm_trans_cstar;
extern int svm_trans_nobias;
extern int svm_trans_npos;
extern int svm_trans_hyp_refresh;
extern int svm_trans_smart_vals;

/* comparison functions for qsort */
int di_cmp(const void *v1, const void *v2);
int i_cmp(const void *v1, const void *v2);
int d_cmp(const void *v1, const void *v2);
int s_cmp(const void *v1, const void *v2);

/* utility fns */
void svm_permute_data(int *permute_table, bow_wv **docs, int *yvect, int ndocs);
void svm_unpermute_data(int *permute_table, bow_wv **docs, int *yvect, int ndocs);
bow_wv *svm_darray_to_wv(double *W);

/* util fn when qsort is not necessary */
void get_top_n(struct di *arr, int len, int n);

/* increment the kernel cache's lru counter */
void kcache_init(int nwide);
void kcache_clear();
void kcache_age();
double svm_kernel_cache(bow_wv *wv1, bow_wv *wv2);
double svm_kernel_cache_lookup(bow_wv *wv1, bow_wv *wv2);

int build_svm_guts(bow_wv **docs, int *yvect, double *weights, double *b, 
		   double **W, int ndocs, double *s, float *cvect, int *nsv);
int smo(bow_wv **docs, int *yvect, double *weights, double *a_b, double **W, 
	int ndocs, double *error, float *cvect, int *nsv);


inline int solve_svm(bow_wv **docs, int *yvect, double *weights, double *tb,
		     double **W, int nlabeled, double *tvals, float *cvect, int *nsv);
int svm_trans_or_chunk(bow_wv **docs, int *yvect, int *trans_yvect, 
		       double *weights, double *tvals, double *ab, double **W,
		       int ntrans, int ndocs, int *nsv);


inline double evaluate_model_hyperplane(double *W, double b, bow_wv *query_wv);
inline double evaluate_model_cache(bow_wv **docs, double *weights, int *yvect, double b, 
				   bow_wv *query_wv, int nsv);
inline double evaluate_model(bow_wv **docs, double *weights, int *yvect, double b, 
			     bow_wv *query_wv, int nsv);
double smo_evaluate_error(struct svm_smo_model *model, int ex);

inline double svm_loqo_tval_to_err(double si, double b, int y);
inline double svm_smo_tval_to_err(double si, double b, int y);
double svm_tval_to_err(double si, double b, int y);


int al_svm(bow_wv **docs, int *yvect, double *weights, double *b, double **W, 
	   int ntrans, int ndocs, int do_rlearn);
int al_svm_test_wrapper(bow_wv **docs, int *yvect, double *weights, 
			double *b, double **W, int ntrans, int ndocs, 
			int do_ts, int do_rlearn, int *pt);

int transduce_svm(bow_wv **docs, int *yvect, int *trans_yvect, 
		  double *weights, double *tvals, double *a_b, 
		  double **W, int ndocs, int ntrans, int *up_nsv);
int svm_remove_bound_examples(bow_wv **docs, int *yvect, double *weights,
			   double *b, double **W, int ndocs, double *tvals,
			   float *cvect, int *nsv);


void svm_set_fisher_barrel_weights(bow_wv **docs, int ndocs);
void svm_setup_fisher(bow_barrel *old_barrel, bow_wv **docs, int nclasses, int ndocs);
double svm_kernel_fisher(bow_wv *wv1, bow_wv *wv2);

#endif /* __BOW_SVM_H */
