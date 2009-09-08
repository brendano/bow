/* Copyright (C) 1999 Greg Schohn - gcs@jprc.com */

/* "main" file for all of the svm related code - any svm stuff should
 * pass through some function here */
#include <bow/svm.h>

#if !HAVE_SQRTF
#define sqrtf sqrt
#endif


#define BARREL_GET_MAX_NSV(barrel) (*((int *) &((GET_CDOC_ARRAY_EL(barrel,0))->normalizer)))
#define BARREL_GET_NCLASSES(barrel) (*((int *) &((GET_CDOC_ARRAY_EL(barrel,0))->prior)))
#define BARREL_GET_NMETA_DOCS(barrel) (*((int *) &((GET_CDOC_ARRAY_EL(barrel,1))->normalizer)))

#define KERNEL_TYPE                    14001
#define WEIGHT_TYPE                    14002
#define COST_TYPE                      14003
#define EA_TYPE                        14004
#define BSIZE_TYPE                     14005
#define VOTE_TYPE                      14006
#define CACHE_SIZE_ARG                 14007
#define QUICK_SCORE                    14008
#define DF_COUNTS_ARG                  14009
#define REMOVE_MISCLASS_TYPE           14010
#define TF_TRANSFORM_TYPE              14011
#define USE_SMO_ARG                    14012
#define CNAME_ARG                      14013
#define LNAME_ARG                      14014
#define DO_ACTIVE_LEARNING             14015
#define ACTIVE_LEARNING_CHUNK_SIZE_ARG 14016
#define AL_TEST_IN_TRAIN_ARG           14017
#define AL_BASELINE                    14018
#define START_AT_ARG                   14019
#define RANDOM_SEED_ARG                14020
#define SUPPRESS_SCORE_MAT_ARG         14021
#define INITIAL_AL_TSET_ARG            14022
#define TRANSDUCE_CLASS_ARG            14023
#define TRANS_CSTAR_ARG                14024
#define TRANS_NPOS_ARG                 14025
#define SVM_BASENAME_ARG               14026
#define AL_WITH_TRANS_ARG              14027
#define TRANS_IGNORE_BIAS_ARG          14028
#define TRANS_HYP_REFRESH_ARG          14029
#define TRANS_SMART_VALS_ARG           14030

#define AGAINST_ALL 0
#define PAIRWISE    1

#define REMOVE_BOUND 1
#define REMOVE_WRONG 2

static int weight_type=RAW;   /* 0=raw_freq, 1=tfidf, 2=infogain */
static int tf_transform_type=RAW;  /* 0=raw, 1=log, 2?... */
static int vote_type=0;
static int cache_size=4000037;
static int quick_scoring=1;
static int do_active_learning=0;
static int test_in_train=0;
static int suppress_score_mat=0;
static int al_pick_random=0;
static int model_starting_no=0;
/* here's a C hack - it uses the actual of the enum to do the shift
 * make sure when passing arguments, you know what the actuals are */
static int transduce_class=(1 << bow_doc_unlabeled);
static int transduce_class_overriding=0; /* gets set to 1 when args are 
					  * passed to override */
static char *svml_basename=NULL;
FILE *svml_test_file=NULL;

#ifdef HAVE_LOQO
int svm_use_smo=0;
#else
int svm_use_smo=1;
#endif


double svm_epsilon_a=1E-12;       /* for alpha's & there bounds */
double svm_epsilon_crit=INIT_KKT; /* for critical KT points */
double svm_C=1000.0;              /* maximum cost */

int svm_bsize=4;               /* sizeof working set */
int svm_kernel_type=0;          /* 0=linear */
int svm_remove_misclassified=0;
int svm_weight_style;
int svm_nkc_calls;

int svm_trans_npos;
int svm_trans_nobias=0;
int svm_trans_hyp_refresh=40;
int svm_trans_smart_vals=1;
double svm_trans_cstar=200;

int svm_init_al_tset=8;
int svm_al_qsize;
int svm_al_do_trans=0;

int svm_random_seed=0; /* for al - gets filled in with time */
int svm_verbosity=0;

/* for tfidf scoring - they could (should?) be made into options... */
static int df_transform=LOG;
static int df_counts=bow_tfidf_occurrences;

/* these are dangerous optimizations for svm_score... - but they save a lot of time... */
/* dangerous because they waste a lot of memory (about the size of the original barrel)
 * & if the vpc barrel gets played with, then its all wrong & there's no totally
 * error proof way to do that without checking all of the barrel, which i don't do. */
struct model_bucket {
  bow_wv    **docs;
  float     **oweights;  /* original weights (after norm & tf scaling) 
			    note - this only matters when tf_transform is set &
			    some weight_per_model scheme is used */
  /* note - these are regular vectors instead of wv's to save time 
   * (O(# qwv features) instead of O((# qwv features) + (# of features)) */
  union {
    float **sub_model; /* weights for submodels */
    float  *barrel;     /* weights for the whole barrel */
  } word_weights;
  double     *bvect;
  int       **indices;
  int        *sizes;    /* length of each array */
  double    **weights;
  double    **W;
  int       **yvect;
  bow_barrel *barrel;
  int         ndocs;
  int         nmodels;
};

static struct model_bucket model_cache = {NULL, NULL, {NULL}, NULL, NULL, NULL, 
					  NULL, NULL, NULL, 0, 0};

double dprod(bow_wv *wv1, bow_wv *wv2);
double kernel_poly(bow_wv *wv1, bow_wv *wv2);
double kernel_rbf(bow_wv *wv1, bow_wv *wv2);
double kernel_sig(bow_wv *wv1, bow_wv *wv2);

/* by default use the dot product as the kernel */
static double (*kernel)(bow_wv *, bow_wv *) = dprod;

/* Command-line options specific to SVMs */
static struct argp_option svm_options[] = {
  {0,0,0,0,
   "Support Vector Machine options, --method=svm:", 50},
  {"svm-active-learning-baseline", AL_BASELINE, "", 0,
   "Incrementally add documents to the training set at random."},
  {"svm-test-in-train", AL_TEST_IN_TRAIN_ARG, 0, 0,
   "do active learning testing inside of the training...  a hack "
   "around making code 10 times more complicated."},
  {"svm-al-transduce", AL_WITH_TRANS_ARG, 0, 0,
   "do transduction over the unlabeled data during active learning."},
  {"svm-bsize", BSIZE_TYPE, "", 0,
   "maximum size to construct the subproblems."},
  {"svm-cache-size", CACHE_SIZE_ARG, "", 0,
   "Number of kernel evaluations to cache."},
  {"svm-cost", COST_TYPE, "", 0,
   "cost to bound the lagrange multipliers by (default 1000)."},
  {"svm-df-counts", DF_COUNTS_ARG, "", 0,
   "Set df_counts (0=occurrences, 1=words)."},
  {"svm-active-learning", DO_ACTIVE_LEARNING, "", 0,
   "Use active learning to query the labels & incrementally (by arg_size) build the barrels."},
  {"svm-epsilon_a", EA_TYPE, "", 0,
   "tolerance for the bounds of the lagrange multipliers (default 0.0001)."},
  {"svm-kernel", KERNEL_TYPE, "", 0,
   "type of kernel to use (0=linear, 1=polynomial, 2=gassian, 3=sigmoid, 4=fisher kernel)."},
  {"svm-al_init_tsetsize", INITIAL_AL_TSET_ARG, "", 0,
   "Number of random documents to start with in active learning."},
  {"svm-quick-scoring", QUICK_SCORE, 0, 0,
   "Turn quick scoring on."},
  {"svm-rseed", RANDOM_SEED_ARG, "", 0,
   "what random seed should be used in the test-in-train splits"},
  {"svm-remove-misclassified", REMOVE_MISCLASS_TYPE, "", 0,
   "Remove all of the misclassified examples and retrain (default none (0), 1=bound, 2=wrong."},
  {"svm-start-at", START_AT_ARG, "", 0,
   "which model should be the first generated."},
  {"svm-suppress-score-matrix", SUPPRESS_SCORE_MAT_ARG, 0, 0,
   "Do not print the scores of each test document at each AL iteration."},
  {"svml-basename", SVM_BASENAME_ARG, "", OPTION_HIDDEN, ""},
  {"svm-tf-transform", TF_TRANSFORM_TYPE, "", 0,
   "0=raw, 1=log..."},
  {"svm-transduce-class", TRANSDUCE_CLASS_ARG, "", 0,
   "override default class(es) (int) to do transduction with "
   "(default bow_doc_unlabeled)."},
  {"svm-trans-cost", TRANS_CSTAR_ARG, "", 0,
   "value to assign to C* (default 200)."},
  {"svm-trans-hyp-refresh", TRANS_HYP_REFRESH_ARG, "", 0,
   "how often the hyperplane should be recomputed during transduction.  "
   "Only applies to SMO.  (default 40)"},
  {"svm-trans-nobias", TRANS_IGNORE_BIAS_ARG, 0, 0,
   "Do not use a bias when marking unlabeled documents.  Use a "
   "threshold of 0 to determine labels instead of some threshold to"
   "mark a certain number of documents for each class."},
  {"svm-trans-npos", TRANS_NPOS_ARG, "", 0,
   "number of unlabeled documents to label as positive "
   "(default: proportional to number of labeled positive docs)."},
  {"svm-trans-smart-vals", TRANS_SMART_VALS_ARG, "", 0,
   "use previous problem's as a starting point for the next. (default true)"},
  {"svm-use-smo", USE_SMO_ARG, "", 0,
#ifdef HAVE_LOQO
   "default 0 (don't use SMO)"
#else 
   "default 1 (use SMO) - PR_LOQO not compiled"
#endif
  },
  {"svm-vote", VOTE_TYPE, "", 0,
   "Type of voting to use (0=singular, 1=pairwise; default 0)."},
  {"svm-weight", WEIGHT_TYPE, "", 0,
   "type of function to use to set the weights of the documents' words "
   "(0=raw_frequency, 1=tfidf, 2=infogain."},
  {0, 0}
};

union kern_param {
  struct {
    double const_co;
    double lin_co;
    double degree;
  } poly ;
  struct {
    double gamma;
  } rbf;
  struct {
    double const_co;
    double lin_co;
  } sig;
};

union kern_param kparm;

error_t svm_parse_opt (int key, char *arg, struct argp_state *state) {
  switch (key) {
  case START_AT_ARG:
    model_starting_no = atoi(arg);
    break;
  case KERNEL_TYPE:
    svm_kernel_type = atoi (arg);
    if (svm_kernel_type > 4) {
      fprintf(stderr, "Invalid value for -k, value must be between 0, 1, 2, 3, or 4.\n");
      return ARGP_ERR_UNKNOWN;
    }
    switch (svm_kernel_type) {
    case 0:
      kernel = dprod;
      break;
    case 1:
      kparm.poly.const_co = 1.0;
      kparm.poly.lin_co = 1.0;
      kparm.poly.degree = 4.0;
      kernel = kernel_poly;
      break;
    case 2:
      kparm.rbf.gamma = 1.0;
      kernel = kernel_rbf;
      break;
    case 3:
      kparm.sig.lin_co = 1.0;
      kparm.sig.const_co = 0.0;
      kernel = kernel_sig;
      break;
    case 4:
      kernel = svm_kernel_fisher;
      break;
    default:
    }
    break;
  case AL_TEST_IN_TRAIN_ARG:
    test_in_train = 1;
    break;
  case AL_WITH_TRANS_ARG:
    svm_al_do_trans = 1;
    break;
  case BSIZE_TYPE:
    svm_bsize = atoi(arg);
    if (svm_bsize < 2) {
      fprintf(stderr, "Invalid value for -b, value must be at least 2.\n");
      return ARGP_ERR_UNKNOWN;
    }
    svm_bsize = ((svm_bsize+3)/4)*4;
    break;
  case CACHE_SIZE_ARG:
    cache_size = atoi(arg);
    if (cache_size < 2) {
      fprintf(stderr, "Invalid value for --cache_size, value must be greater than 1\n");
      return ARGP_ERR_UNKNOWN;
    }
    break;
  case COST_TYPE:
    svm_C = atof(arg);
    break;
  case DF_COUNTS_ARG:
    key = atoi(arg);
    if (key == 0) {
      df_counts = bow_tfidf_occurrences;
    } else if (key == 1) {
      df_counts = bow_tfidf_words;
    } else {
      return ARGP_ERR_UNKNOWN;
    }
    break;
  case EA_TYPE:
    svm_epsilon_a = atof(arg);
    break;
  case AL_BASELINE:
    test_in_train = 1;
    al_pick_random = 1;
  case DO_ACTIVE_LEARNING:
    do_active_learning = 1;
    svm_al_qsize = atoi(arg);
    if (svm_al_qsize < 0) {
      fprintf(stderr, "Bogus AL-query size\n");
      return ARGP_ERR_UNKNOWN;
    }
    break;
  case INITIAL_AL_TSET_ARG:
    svm_init_al_tset = atoi(arg);
    break;
  case REMOVE_MISCLASS_TYPE:
    svm_remove_misclassified = atoi(arg);
    break;
  case RANDOM_SEED_ARG:
    svm_random_seed = atoi(arg);
    break;
  case QUICK_SCORE:
    quick_scoring = 1;
    break;
  case SUPPRESS_SCORE_MAT_ARG:
    suppress_score_mat = 1;
    break;
  case SVM_BASENAME_ARG:
    svml_basename = arg;
    break;
  case TF_TRANSFORM_TYPE:
    tf_transform_type = atoi(arg);
    if ((tf_transform_type < 0) || (tf_transform_type > 1)) {
      fprintf(stderr, "Invalid value for tf_transform_type, value must be 0 or 1\n");
      return ARGP_ERR_UNKNOWN;
    }
    break;
  case TRANSDUCE_CLASS_ARG:
    { 
      int a;
      a = atoi(arg);
      if (a == bow_doc_train) {
	fprintf(stderr,"Cannot do transduction on training set, ignoring \"%s\" option\n",arg);
      } else {
	if (!transduce_class) {
	  transduce_class_overriding = 1;
	  transduce_class = 0;
	}
	/* < 0 turns transduction off */
	if (a > 0) {
	  transduce_class |= (1 << a);
	}
      }
    }
    break;
  case TRANS_HYP_REFRESH_ARG:
    svm_trans_hyp_refresh = atoi(arg);
    if (svm_trans_hyp_refresh < 1) {
      fprintf(stderr, "svm_trans_hyp_refresh (hyperplane refresh rate)"
	      " must be greater than 0\n");
    }
    break;
  case TRANS_IGNORE_BIAS_ARG:
    svm_trans_nobias = 1;
    break;
  case TRANS_NPOS_ARG:
    svm_trans_npos = atoi(arg);
    if (svm_trans_npos < 1) {
      fprintf(stderr, "svm_trans_npos should be greater than 0.\n");
      return ARGP_ERR_UNKNOWN;
    }
    break;
  case TRANS_CSTAR_ARG:
    svm_trans_cstar = atof(arg);
    break;
  case TRANS_SMART_VALS_ARG:
    svm_trans_smart_vals = atoi(arg);
    break;
  case USE_SMO_ARG:
    svm_use_smo = atoi(arg);
    /* the epsilon is used is 2x as big as it would be in the loqo method */
    if (svm_use_smo == 1) {
      svm_epsilon_crit /= 2;
    }
#ifndef HAVE_LOQO
    if (svm_use_smo != 1) {
      fprintf(stderr,"Cannot switch from SMO, no other solvers were built,\n"
	      "rebuild libbow with pr_loqo to use another algorithm.\n");
    }
#endif
    break;
  case VOTE_TYPE:
    vote_type = atoi(arg);
    if ((vote_type < 0) || (vote_type > 1)) {
      fprintf(stderr, "Invalid value for --vote, value must be 0 for linear or 1 for pairwise.\n");
      return ARGP_ERR_UNKNOWN;
    }
    break;
  case WEIGHT_TYPE:
    weight_type = atoi(arg);
    if ((weight_type < 0) || (weight_type > 3)) {
      fprintf(stderr, "Invalid value for -w, value must be 0, 1, 2, or 3.\n");
      return ARGP_ERR_UNKNOWN;
    }
    break;
  default:
    return ARGP_ERR_UNKNOWN;
  }
  return 0;
}

static const struct argp svm_argp = { svm_options, svm_parse_opt };

static struct argp_child svm_argp_child = {
  &svm_argp,		/* This child's argp structure */
  0,		       	/* flags for child */
  0,		       	/* optional header in help message */
  0		       	/* arbitrary group number for ordering */
};


void svm_permute_data(int *permute_table, bow_wv **docs, int *yvect, int ndocs) {
  int i, j;
  for (i=0; i<ndocs; i++) {
    permute_table[i] = i;
  }

  for (i=0; i<ndocs; i++) {
    bow_wv *d;
    int y;

    j = random() % ndocs;

    d = docs[j];
    docs[j] = docs[i];
    docs[i] = d;

    y = yvect[j];
    yvect[j] = yvect[i];
    yvect[i] = y;

    y = permute_table[j];
    permute_table[j] = permute_table[i];
    permute_table[i] = y;
  }
}

void svm_unpermute_data(int *permute_table, bow_wv **docs, int *yvect, int ndocs) {
  int i, j;
  for (i=0; i<ndocs; ) {
    bow_wv *d;
    int     y;

    j = permute_table[i];

    if (j == i) {
      i++;
      continue;
    }

    d = docs[j];
    docs[j] = docs[i];
    docs[i] = d;

    y = yvect[j];
    yvect[j] = yvect[i];
    yvect[i] = y;

    y = permute_table[j];
    permute_table[j] = permute_table[i];
    permute_table[i] = y;
  }
}

/* Right now, the vectors it looks at are the raw freq vectors */
double dprod(bow_wv *wv1, bow_wv *wv2) {
  double sum;
  bow_we *v1, *v2;
  int i1, i2;

  i1 = i2 = 0;
  sum = 0.0;
  v1 = wv1->entry;
  v2 = wv2->entry;

  while ((i1 < wv1->num_entries) && (i2 < wv2->num_entries)) {
    if(v1[i1].wi > v2[i2].wi) {
      i2++;
    }
    else if (v1[i1].wi < v2[i2].wi) {
      i1++;
    }
    else {
      sum += (v1[i1].weight) * (v2[i2].weight);
      i1++;
      i2++;
    }
  }
  return(sum);
}

/* dot product between a sparce & non-sparse vector */
double dprod_sd(bow_wv *wv, double *W) {
  double sum;
  bow_we *v;
  int i;

  i = 0;
  sum = 0.0;
  v = wv->entry;

  while (i < wv->num_entries) {
    sum += v[i].weight * W[v[i].wi];
    i++;
  }
  return(sum);
}

/* this is a whole different function just because the kernel is the biggest bottleneck */
double ddprod(bow_wv *wv1, bow_wv *wv2) {
  double tmp;
  double sum;
  bow_we *v1, *v2;
  int i1, i2;

  i1 = i2 = 0;
  sum = 0.0;
  v1 = wv1->entry;
  v2 = wv2->entry;

  while ((i1 < wv1->num_entries) && (i2 < wv2->num_entries)) {
    if(v1[i1].wi > v2[i2].wi) {
      i2++;
    }
    else if (v1[i1].wi < v2[i2].wi) {
      i1++;
    }
    else {
      tmp = (v1[i1].weight) - (v2[i2].weight);
      sum += tmp*tmp;
      i1++;
      i2++;
    }
  }
  return(sum);
}

/* End of command-line options specific to SVMs */
double kernel_poly(bow_wv *wv1, bow_wv *wv2) {
  return (pow(kparm.poly.lin_co * dprod(wv1,wv2) + 
	      kparm.poly.const_co, kparm.poly.degree));
}

double kernel_rbf(bow_wv *wv1, bow_wv *wv2) {
  return (exp(-1*kparm.rbf.gamma * (ddprod(wv1,wv2))));
}

double kernel_sig(bow_wv *wv1, bow_wv *wv2) {
  return(tanh(kparm.sig.lin_co * dprod(wv1,wv2)+kparm.sig.const_co));
}


static int rlength;
typedef struct _kc_el {
  bow_wv *i, *j;
  double val;
  unsigned int age;
} kc_el;

static kc_el *harray;
static unsigned int max_age;

void kcache_init(int nwide) {
  int i;
  max_age = 1;
  svm_nkc_calls = 0;
  rlength = nwide;
  if ((harray = (kc_el *) malloc(sizeof(kc_el)*cache_size)) == NULL) {
    cache_size = cache_size/2;
    fprintf(stderr, "Could not allocate space for the kernel cache.\n"
	    "Shrinking size to %d and trying again.\n", cache_size);
    return (kcache_init(nwide));
  }

  for (i=0; i<cache_size; i++) {
    harray[i].i = (bow_wv *) ~0;
    harray[i].age = 0;
  }
}

void kcache_clear() {
  free(harray);
}

void kcache_age() {
  max_age++;
}

#define NHASHES   3
static int sub_nkcc=0; /* this makes nkc_calls = actual calls / 100 */
double svm_kernel_cache(bow_wv *wv1, bow_wv *wv2) {
  int h_index;
  int k;
  unsigned int min_age, min_from;
  double d;

  if (!((sub_nkcc++) % 100)) {
    svm_nkc_calls ++;
  }

  min_age = ~((unsigned long) 0);

  /* all of the kernels are symetric */
  if (wv1>wv2) {
    bow_wv *tmp;
    tmp = wv2;
    wv2 = wv1;
    wv1 = tmp;
  }

  for (k=h_index=0; k<NHASHES; k++) {
    h_index = ((((unsigned int)wv1)*rlength+((unsigned int)wv2))+h_index+19) % cache_size;
    
    if ((harray[h_index].i == wv1) && (harray[h_index].j == wv2)) {
      harray[h_index].age = max_age;
      return (harray[h_index].val);
    } else {
      if (harray[h_index].age > 0) {
	if (min_age > harray[h_index].age) {
	  min_age = harray[h_index].age;
	  min_from = h_index;
	}
	continue;
      } else {
	min_from = h_index;
	break;
      }
    }
  }

  d = kernel(wv1,wv2);
  harray[min_from].i = wv1;
  harray[min_from].j = wv2;
  harray[min_from].val = d;
  harray[min_from].age = max_age;
  return (d);
}

/* don't add the evaluation (useful if the items are getting deleted from a set) */
double svm_kernel_cache_lookup(bow_wv *wv1, bow_wv *wv2) {
  int h_index;
  int k;

  /* all of the kernels are symetric */
  if (wv1>wv2) {
    bow_wv *tmp;
    tmp = wv2;
    wv2 = wv1;
    wv1 = tmp;
  }

  for (k=h_index=0; k<NHASHES; k++) {
    h_index = ((((unsigned int)wv1)*rlength+((unsigned int)wv2))+h_index+19) % cache_size;
    
    if ((harray[h_index].i == wv1) && (harray[h_index].j == wv2)) {
      return (harray[h_index].val);
    }
  }

  return (kernel(wv1,wv2));
}

/* random qsort helpers */
int di_cmp(const void *v1, const void *v2) {
  double d1, d2;
  d1 = ((struct di *) v1)->d;
  d2 = ((struct di *) v2)->d;

  if (d1 < d2) {
    return (-1);
  } else if (d1 > d2) {
    return (1);
  } else {
    return 0;
  }
}

int i_cmp(const void *v1, const void *v2) {
  int d1, d2;
  d1 = *((int *) v1);
  d2 = *((int *) v2);

  if (d1 < d2) {
    return (-1);
  } else if (d1 > d2) {
    return (1);
  } else {
    return 0;
  }
}

int d_cmp(const void *v1, const void *v2) {
  double d1, d2;
  d1 = *((double *) v1);
  d2 = *((double *) v2);

  if (d1 < d2) {
    return (-1);
  } else if (d1 > d2) {
    return (1);
  } else {
    return 0;
  }
}

int s_cmp(const void *v1, const void *v2) {
  bow_score *s1, *s2;
  s1 = ((bow_score *) v1);
  s2 = ((bow_score *) v2);

  if (s1->weight < s2->weight) {
    return (1);
  } else if (s1->weight > s2->weight) {
    return (-1);
  } else {
    if (s1->di < s2->di) {
      return (-1);
    } else if (s1->di > s2->di) {
      return (1);
    } else {
      return 0;
    }
  }
}

/* useful alternative to qsort or radix sort */
/* stick the top n values in the first n slots of arr */
void get_top_n(struct di *arr, int len, int n) {
  double mind, tmpd;
  int    minfrom, tmpi;

  int i,j;

  if (len < n) {
    return;
  }

  for (i=0; i<n && i<len; i++) {
    mind = arr[i].d;
    minfrom = i;
    
    for (j=i+1; j<len; j++) {
      if (arr[j].d < mind) {
	mind = arr[j].d;
	minfrom = j;
      }
    }

    tmpi = arr[minfrom].i;
    tmpd = arr[minfrom].d;
    
    arr[minfrom].d = arr[i].d;
    arr[minfrom].i = arr[i].i;
    
    arr[i].d = tmpd;
    arr[i].i = tmpi;
  }

  return;
}

/* takes in docs, creates an idf vector & then weights the document */
/* sets doc weights by using counts & normalizer */
static float *tfidf(bow_wv **docs, int ntrain) {
  float    idf_sum;            /* sum of all the idf values */
  int      max_wi;	       /* the highest "word index" */
  float   *new_idf_vect;

  int i, j;

  bow_verbosify (bow_progress, "Setting weights over words:          ");
  max_wi = bow_num_words();

  new_idf_vect = (float *) malloc(sizeof(float)*max_wi);

  for (i=0; i<max_wi; i++) {
    new_idf_vect[i] = 0.0;
  }

  idf_sum = 0.0;

  /* First calculate document frequencies. */
  for (i=0; i<ntrain; i++)  {
    for (j=0; j<docs[i]->num_entries; j++) {
      if (df_counts == bow_tfidf_occurrences) {
	/* Make DV be the number of documents in which word WI occurs 
	   at least once.  (We can't just set it to DV->LENGTH because
	   we have to check to make sure each document is part of the
	   model. */
	new_idf_vect[docs[i]->entry[j].wi] ++;
      } else if (df_counts == bow_tfidf_words) {
	/* Make DV be the total number of times word WI appears
	   in any document. */
	new_idf_vect[docs[i]->entry[j].wi] += docs[i]->entry[j].count;
      } else {
	bow_error ("Bad TFIDF parameter df_counts.");
      }
    }
  }

  for (i=0; i<max_wi; i++)  {
    /* Set IDF from DF. */
    /* following what Thorsten alledgedly does */
    if (new_idf_vect[i] >= 3.0) {
      if (df_transform == LOG)
	new_idf_vect[i] = log2f (ntrain / new_idf_vect[i]);
      else if (df_transform == SQRT)
	new_idf_vect[i] = sqrtf (ntrain / new_idf_vect[i]);
      else if (df_transform == RAW)
	new_idf_vect[i] = ntrain / new_idf_vect[i];
      else {
	new_idf_vect[i] = 0;		/* to avoid gcc warning */
	bow_error ("Bad TFIDF parameter df_transform.");
      }
      idf_sum += new_idf_vect[i];
    } else {
      new_idf_vect[i] = 0.0;
    }
  }

  /* "normalize" the idf values */
  for (i=0; i<max_wi; i++)  {
    /* Get the document vector for this word WI */
    new_idf_vect[i] = max_wi*new_idf_vect[i]/idf_sum;
  }

  bow_verbosify (bow_progress, "\n");
  return new_idf_vect;
}

/* next 2 fn's stolen from info-gain.c */
/* Return the entropy given counts for each type of element. */
static double entropy(float e1, float e2) {
  double total = 0;		/* How many elements we have in total */
  double entropy = 0.0;
  double fraction;

  total = e1 + e2;

  /* If we have no elements, then the entropy is zero. */
  if (total == 0) {
    return 0.0;
  }

  entropy = 0.0;

  /* Now calculate the entropy */
  fraction = e1 / total;
  if (fraction != 0.0) {
    entropy  = -1 * fraction * log2f (fraction);
  }

  fraction = e2 / total;
  if (fraction != 0.0) {
    entropy -= fraction * log2f (fraction);
  }

  return entropy;
}

/* Return a malloc()'ed array containing an infomation-gain score for
   each word index. */
float *infogain(bow_wv **docs, int *yvect, int ndocs) {
  int grand_totals[2];  /* Totals for each class. */

  double total_entropy;             /* The entropy of the total collection. */
  double with_word_entropy;         /* The entropy of the set of docs with
				       the word in question. */
  double without_word_entropy;      /* The entropy of the set of docs without
				       the word in question. */

  float grand_total = 0; 
  float with_word_total = 0;
  float without_word_total = 0;
  int i, j;
  float *ret;
  double sum;

  int *fc[2];  /* tallies for all the words in class 1 & 2 */
  int num_words;

  bow_verbosify (bow_progress, "Calculating info gain... words ::          ");

  num_words = bow_num_words();
  ret = bow_malloc (num_words*sizeof (float));
  fc[0] = (int *) malloc(num_words*sizeof(double));
  fc[1] = (int *) malloc(num_words*sizeof(double));

  memset(fc[0], 0, num_words*sizeof(int));
  memset(fc[1], 0, num_words*sizeof(int));

  /* First set all the arrays to zero */
  for(i = 0; i < 2; i++) {
    grand_totals[i] = 0;
  }

  /* Now set up the grand totals. */
  for (i = 0; i<ndocs; i++) {
    if (yvect[i]) { /* if it is unlabeled, ignore it */
      grand_totals[(yvect[i]+1)/2] ++;

      /* this is only done incase some type of occurrence cnt should ever happen */
      grand_total ++;
    }
  }

  /* Calculate the total entropy */
  total_entropy = entropy (grand_totals[0], grand_totals[1]);
  sum = 0.0;

  /* the fc[...] are like the with_word totals */
  for (i=0; i<ndocs; i++) {
    if (yvect[i]) {
      int y = (yvect[i]+1)/2;
      for (j=0; j<docs[i]->num_entries; j++) {
	fc[y][docs[i]->entry[j].wi] ++;
      }
    }
  }

  for (i=0; i<num_words; i++) {
    with_word_total = fc[0][i] + fc[1][i];
    without_word_total = grand_total - with_word_total;

    with_word_entropy = entropy((float)fc[0][i],(float)fc[1][i]);
    without_word_entropy = entropy((float)(grand_totals[0] - fc[0][i]),
				       (float)(grand_totals[1] - fc[1][i]));

    ret[i]=(float) (total_entropy - 
	    (((double)with_word_total/(double)grand_total)*with_word_entropy) -
	    (((double)without_word_total/(double)grand_total)*without_word_entropy));

    assert (ret[i] >= -1e-7);
    sum += ret[i];
  }

  free(fc[0]);
  free(fc[1]);

  /* "normalize" in similar fashion to tfidf */
  for (i=0; i<num_words; i++)  {
    /* Get the document vector for this word WI */
    ret[i] = num_words*ret[i]/sum;
  }
  

  bow_verbosify (bow_progress, "\n");
  return ret;
}

/* this sets the already transformed weights THEN does the normalizing... */
static void svm_set_barrel_weights(bow_wv **docs, int *yvect, int ndocs, float **weight_vect) {
  int i,j;
  
  /* the weights have yet to be set & since that's what we're using... */
  if (svm_kernel_type == FISHER) {
    svm_set_fisher_barrel_weights(docs, ndocs);
    return;
  } else if (weight_type == RAW) {
    for (i=0; i<ndocs; i++) {
      for (j=0; j<docs[i]->num_entries; j++) {
	docs[i]->entry[j].weight *= docs[i]->normalizer;
      }
    }
    return;
  } else if (weight_type == TFIDF) {
    *weight_vect = tfidf(docs, ndocs);
  } else if (weight_type == INFOGAIN) {
    *weight_vect = infogain(docs, yvect, ndocs);
  }

  /* Now loop through all the documents, setting their weights */
  for (i=0; i<ndocs; i++) {
    double sum = 0.0;
    for (j=0; j<docs[i]->num_entries; j++) {
      docs[i]->entry[j].weight *= 
	docs[i]->normalizer * (*weight_vect)[docs[i]->entry[j].wi];
      sum += docs[i]->entry[j].weight;
    }
    if (sum >0.0) {
      bow_wv_normalize_weights_by_summing(docs[i]);
      for (j=0; j<docs[i]->num_entries; j++) {
	docs[i]->entry[j].weight *= docs[i]->normalizer;
      }
    }
  }
}

/* similar to barrel weights above, but this only works on 1 wv at a time */
/* will set weights from an already transformed oweights vector (if it was transformed), 
 * then normalize the weights */
static void svm_set_wv_weights(bow_wv *qwv, float *oweights, float *weight_vect) {
  double sum;
  int i;

  sum = 0.0;
  if (weight_type == TFIDF || weight_type == INFOGAIN) {
    if (tf_transform_type) {
      for (i=0; i<qwv->num_entries; i++) {
	qwv->entry[i].weight = 
	  weight_vect[qwv->entry[i].wi] * oweights[i];
	sum += qwv->entry[i].weight;
      }
    } else {
      for (i=0; i<qwv->num_entries; i++) {
	/* since no transform was used - just use the raw count*/
	qwv->entry[i].weight = 
	  weight_vect[qwv->entry[i].wi] * ((float) qwv->entry[i].count);

	sum += qwv->entry[i].weight;
      }
    }
  } else {
    for (i=0; i<qwv->num_entries && sum == 0.0; i++) {
      sum += qwv->entry[i].weight;
    }
  }

  if (sum > 0.0) {
    bow_wv_normalize_weights_by_summing(qwv);
    for (i=0; i<qwv->num_entries; i++) {
      qwv->entry[i].weight *= qwv->normalizer;
    }
  }
}

/* the below comment is correct - but there are instances (& in some
 * cases a substantial proportion) where some data may create an 
 * excellent starting point for the algorithms, even though so much has changed 
 * --- therefore, this should be changed to be more intelligent */

/* since removing bound support vectors is hard
 * (since each bound support vector removed drastically
 *  changes the constraints) I don't bother to do it 
 * intuitively for each algorithm (that was tried & 
 * performance did not improve (see above)) - this 
 * function is nice because its modular & independent
 * of any implementation. */
/* tvals is ignored, but the values filled in by the
 * algorithm are not changed. */
int svm_remove_bound_examples(bow_wv **docs, int *yvect, double *weights,
			   double *b, double **W, int ndocs, double *tvals,
			   float *cvect, int *nsv) {
  int      nbound=0;
  int     *tdocs;     /* trans table */
  float   *sub_cvect;
  bow_wv **sub_docs;
  int      sub_ndocs=0;
  int     *sub_yvect;
  int i,j,x;

  sub_docs = (bow_wv **) alloca(sizeof(bow_wv *)*ndocs); 
  sub_yvect = (int *) alloca(sizeof(int)*ndocs);
  tdocs = (int *) alloca(sizeof(int)*ndocs);
  sub_cvect = (float *) alloca(sizeof(float)*ndocs);

  if (svm_remove_misclassified==REMOVE_BOUND) {
    for (i=nbound=sub_ndocs=0; i<ndocs; i++) {
      if (weights[i] > cvect[i] - svm_epsilon_a) {
	nbound ++;
      } else {
	sub_docs[sub_ndocs] = docs[i];
	sub_yvect[sub_ndocs] = yvect[i];
	tdocs[sub_ndocs] = i;
	sub_ndocs++;
      }
    }
  } else if (svm_remove_misclassified==REMOVE_WRONG) {
    if (svm_kernel_type == 0) {
      for (i=nbound=sub_ndocs=0; i<ndocs; i++) {
	if (yvect[i]*evaluate_model_hyperplane(*W, *b, docs[i]) < 0.0) {
	  nbound ++;
	} else {
	  sub_docs[sub_ndocs] = docs[i];
	  sub_yvect[sub_ndocs] = yvect[i];
	  tdocs[sub_ndocs] = i;
	  sub_ndocs++;
	}
      }
    } else {
      for (i=nbound=sub_ndocs=0; i<ndocs; i++) {
	if (yvect[i]*evaluate_model_cache(docs, weights, yvect, *b, docs[i], *nsv) < 0.0) {
	  nbound ++;
	} else {
	  sub_docs[sub_ndocs] = docs[i];
	  sub_yvect[sub_ndocs] = yvect[i];
	  tdocs[sub_ndocs] = i;
	  sub_ndocs++;
	}
      }
    }
  }

  if (nbound) {
    fprintf(stderr, "Removing %d bound examples\n",nbound);
    fprintf(stdout, "Removing %d bound examples\n",nbound);
  } else {
    return 0;
  }
  /* prb not worthwile to resize arrays */
  
  /* "unbound" everything & set weights & tvals... */
  for (i=0; i<sub_ndocs; i++) {
    tvals[i] = 0.0;
    weights[i] = 0.0;
    sub_cvect[i] = MAXFLOAT;
  }

  *nsv = 0;

  if (svm_use_smo) {
    x = smo(sub_docs, sub_yvect, weights, b, W, sub_ndocs, tvals, sub_cvect, nsv);
  } else {
#ifdef HAVE_LOQO
    x = build_svm_guts(sub_docs, sub_yvect, weights, b, W, sub_ndocs, tvals, 
		       sub_cvect, nsv);
#else
    bow_error("Must build rainbow with pr_loqo to use this solver!\n");
#endif
  }

  /* place the weights in the proper slots */
  for (i=ndocs-1, j=sub_ndocs-1; i>0; i--) {
    if (tdocs[j] == i) {
      weights[i] = weights[j];
      tvals[i] = tvals[j];
      j--;
    } else {
      weights[i] = 0.0;
      tvals[i] = 0.0;
    }
  }

  return x;
}

/* returns whether or not x has changed */
inline int solve_svm(bow_wv **docs, int *yvect, double *weights, double *ab, 
		     double **W, int ndocs, double *tvals, float *cvect, 
		     int *nsv) {
  int x;

  if (svm_use_smo) {
    x = smo(docs, yvect, weights, ab, W, ndocs, tvals, cvect, nsv);
  } else {
#ifdef HAVE_LOQO
    x = build_svm_guts(docs, yvect, weights, ab, W, ndocs, tvals, cvect, nsv);
#else
    bow_error("Must build rainbow with pr_loqo to use this solver!\n");
#endif
  }

  if (svm_remove_misclassified) {
    x |= svm_remove_bound_examples(docs,yvect,weights,ab,W,ndocs,tvals,
				  cvect,nsv);
  }

  return x;
}

/* returns if the weights have changed */
int svm_trans_or_chunk(bow_wv **docs, int *yvect, int *trans_yvect, 
		       double *weights, double *tvals, double *ab, 
		       double **W, int ntrans, int ndocs, int *nsv) {
  if (ntrans) {
    return (transduce_svm(docs, yvect, trans_yvect, weights, tvals, ab, 
			  W, ndocs, ntrans, nsv));
  } else {
    int i;
    float *cvect = (float *) alloca(sizeof(float)*ndocs);
    for (i=0; i<ndocs; i++) {
      cvect[i] = svm_C;
    }
    return(solve_svm(docs, yvect, weights, ab, W, ndocs, tvals, cvect, nsv));
  }
}

/* cover for all the functions */
/* this function does a small amount of pre & post-processing for the
 * algorithm independent stuff (like randomly permuting everything &
 * outputting a hyperplane if possible) */
int tlf_svm(bow_wv **docs, int *yvect, double *weights, double *ab, 
	    bow_wv **W_wv, int ntrans, int ndocs) {
  int          nlabeled;
  int          misclass;
  int          nsv;
  int         *permute_table;
  double      *tvals;
  double      *W=NULL;

  int i,j;

  struct tms t1, t2;

  if (svm_random_seed) {
    srandom(svm_random_seed);
  } else {
    svm_random_seed = (int) time(NULL);
    srandom(svm_random_seed);
    printf("random seed to chop test/train split: %d\n",svm_random_seed);
    fprintf(stderr,"random seed to chop test/train split: %d\n",svm_random_seed);
  }

  permute_table = (int *) malloc(sizeof(int)*ndocs);

  nlabeled = ndocs - ntrans;

  /* permute each part, but don't mudge them together, because the 
   * solvers are going to expect all unlabeled data (data with a 
   * different C* to be in the latter half) */
  svm_permute_data(permute_table, docs, yvect, nlabeled);
  svm_permute_data(&(permute_table[nlabeled]), &(docs[nlabeled]), &(yvect[nlabeled]), ntrans);

  /* lets try to reduce determinism... */
  srandom((int) time(NULL));

  times(&t1);
      
  if (do_active_learning) {
    if (test_in_train) {
      nsv = al_svm_test_wrapper(docs, yvect, weights, ab, &W, ntrans, ndocs,
				(suppress_score_mat ? 0 : 1),
				al_pick_random, permute_table);
    } else {
      nsv = al_svm(docs, yvect, weights, ab, &W, ntrans, ndocs, al_pick_random);
    }
  } else {
    /* initialize... */
    tvals = (double *) alloca(sizeof(double)*ndocs);
    nsv = 0;
    for (i=0; i<ndocs; i++) {
      weights[i] = 0.0;
      tvals[i] = 0.0;
    }

    svm_trans_or_chunk(docs, yvect, NULL, weights, tvals, ab, &W, ntrans, ndocs, &nsv);
  }

  times(&t2);
  fprintf(stderr,"user: %d, system:%d, kernel_calls:%d\n", (int)(t2.tms_utime-t1.tms_utime),
	  (int) (t2.tms_stime - t1.tms_stime), svm_nkc_calls);
  printf("user: %d, system:%d, kernel_calls:%d\n", (int)(t2.tms_utime-t1.tms_utime),
	  (int) (t2.tms_stime - t1.tms_stime), svm_nkc_calls);

  
  /* unpermute data */
  svm_unpermute_data(permute_table, docs, yvect, nlabeled);
  svm_unpermute_data(&(permute_table[nlabeled]), &(docs[nlabeled]), &(yvect[nlabeled]), ntrans);

  free(permute_table);
  
  if (svm_kernel_type == 0) {
    *W_wv = svm_darray_to_wv(W);
    free(W);
  }

  printf("support vectors: ");
  for (i=j=0; j<nsv; i++) {
    if (weights[i] > svm_epsilon_a) {
      printf("%d(%f) ",i,weights[i]);
      j++;
    }
  }
  misclass = 0;
  if (!svm_remove_misclassified) {
    for (i=misclass=0; i<nlabeled; i++) {
      if (weights[i] > svm_C-svm_epsilon_a) {
	misclass++;
      }
    }
    for (i=0; i<ntrans; i++) {
      if (weights[nlabeled+i] > svm_trans_cstar-svm_epsilon_a) {
	misclass++;
      }
    }
  }
  printf("\n%d support vectors (%d bounded)\n", nsv, misclass);

  return nsv;
}

bow_wv *svm_darray_to_wv(double *W) {
  bow_wv *W_wv;
  int     num_words, i, j;
  
  num_words = bow_num_words();

  for (i=j=0; i<num_words; i++) {
    if (W[i] != 0.0) 
      j++;
  }
  
  W_wv = bow_wv_new(j);
  for (i=j=0; j<W_wv->num_entries; i++) {
    if (W[i] != 0.0) {
      W_wv->entry[j].wi = i;
      W_wv->entry[j].count = 1; /* just so that an assertion doesn't throw up later */
      W_wv->entry[j].weight = W[i];
      j++;
    }
  }

  return (W_wv);
}

/* note - these 2 fn's are not MEANT to be inverses of each
 * other - they don't need to be & shouldn't be!  */

/* given a 'focus' value, this transforms x into some int
 * this must be a BINARY function, outputting ONLY 1 & -1
 * because that's what the SVM use for y. */
int map_class_to_y(int focus, int x) {
  if (focus == x) {
    return 1;
  } else {
    return (-1);
  }
}

/* each pass over these things take up 2 labels... */
/* 1->1, -1->0 */
int map_y_to_class(int focus, int x) {
  return ((focus*2)+((x+1)/2));
}

/* helper to do whatever transform on a wv & then normalize it... */
static void tf_transform(bow_wv *doc) {
  int j;

  for (j=0; j<doc->num_entries; j++) {
    if (tf_transform_type == LOG) {
      doc->entry[j].weight = log2f((float) (doc->entry[j].count + 1));
    } else { 
      doc->entry[j].weight = (float) doc->entry[j].count;
    }
  }
}

/* sets counts & the normalizer too */
/* pulls from the barrel those docs that satisfy dec_fn & turns them into a doc array */
int make_doc_array(bow_barrel *barrel, bow_wv **docs, int *tdocs, int(*dec_fn)(bow_cdoc *)) {
  bow_dv_heap  *heap;
  int ndocs;
  bow_wv       *wv_tmp1;
  bow_wv       *wv_tmp;
  int j;

  /* Create the Heap of vectors of all documents */
  heap = bow_make_dv_heap_from_wi2dvf(barrel->wi2dvf); 
  for (ndocs=0; ; ndocs++) {
    int t = bow_heap_next_wv(heap, barrel, &wv_tmp1, dec_fn);
    if (t == -1) {
      break;
    } else {
      tdocs[ndocs] = t;
    }

    wv_tmp = bow_wv_new(wv_tmp1->num_entries);
    for (j=0; j<wv_tmp->num_entries; j++) {
      wv_tmp->entry[j].wi = wv_tmp1->entry[j].wi;
      wv_tmp->entry[j].count = wv_tmp1->entry[j].count;
    }

    tf_transform(wv_tmp);
    bow_wv_normalize_weights_by_summing(wv_tmp);

    docs[ndocs] = wv_tmp;
  }
  
  return ndocs;
}

/* C sucks - this is just a fn to pass to bow_heap_next_wv */
static int silly_currying_global_v1, silly_currying_global_v2;
int use_train_and_submodel(bow_cdoc *cdoc) {
  return ((cdoc->type == bow_doc_train && 
	   (silly_currying_global_v1 == cdoc->class ||  
	    silly_currying_global_v2 == cdoc->class)) ? 
	  1 : 0);
}

int use_transduction_docs(bow_cdoc *cdoc) {
  return (((1 << cdoc->type) & transduce_class) ? 1 : 0);
}

/* helper fn for adding the data for a training example to the barrel */
int add_sv_barrel(bow_barrel *new_barrel,double *weights, int *yvect, int *tdocs, 
		  double b, int model_no, int nsv) {
  bow_cdoc  cdoc_pos, cdoc_neg;
  bow_wv   *dummy_wv_neg;
  bow_wv   *dummy_wv_pos;
  int       n_meta_docs=0;

  int ni, pi, i, j, num_words;

  num_words = bow_num_words();

  dummy_wv_pos = bow_wv_new(num_words);
  dummy_wv_neg = bow_wv_new(num_words);

  dummy_wv_pos->num_entries = dummy_wv_neg->num_entries = 0;

  cdoc_pos.type = bow_doc_ignore;
  cdoc_neg.normalizer = cdoc_pos.prior = 0.0;
  cdoc_pos.filename = NULL;
  cdoc_pos.class_probs = NULL;
  cdoc_pos.class = 0;
  
  cdoc_neg.type = bow_doc_ignore;
  cdoc_neg.normalizer = cdoc_neg.prior = 0.0;
  cdoc_neg.filename = NULL;
  cdoc_neg.class_probs = NULL;
  cdoc_neg.class = 0;

  if (model_no == 0) {
    /* insert an two empty docs into the barrel so that the 
     * ancillary data has a place to live */
    cdoc_neg.word_count = 0;
    bow_barrel_add_document(new_barrel, &cdoc_neg, dummy_wv_pos);
    bow_barrel_add_document(new_barrel, &cdoc_neg, dummy_wv_pos);
    n_meta_docs = 2;
  }

  cdoc_pos.normalizer = b;
  cdoc_pos.class = map_y_to_class(model_no,(int) 1);

  cdoc_neg.class = map_y_to_class(model_no,(int) -1);   

  ni = pi = 0;
  for (i=j=0; j<nsv; i++) {
    if (weights[i] > svm_epsilon_a) {
      if (yvect[i] > 0) {
	if (pi > num_words) {
	  dummy_wv_pos->num_entries = pi;
	  cdoc_pos.word_count = pi;
	  bow_barrel_add_document(new_barrel, &cdoc_pos, dummy_wv_pos);
	  pi = 0;
	  n_meta_docs++;
	}
	dummy_wv_pos->entry[pi].weight = (float) weights[i];
	dummy_wv_pos->entry[pi].count = tdocs[i] + 1;
	dummy_wv_pos->entry[pi].wi = pi;
	pi++;
      } else {
	if (ni > num_words) {
	  dummy_wv_neg->num_entries = pi;
	  cdoc_neg.word_count = ni;
	  bow_barrel_add_document(new_barrel, &cdoc_neg, dummy_wv_pos);
	  ni = 0;
	  n_meta_docs++;
	}
	dummy_wv_neg->entry[ni].weight = (float) weights[i];
	dummy_wv_neg->entry[ni].count = tdocs[i] + 1;
	dummy_wv_neg->entry[ni].wi = ni;
	ni++;
      }
      j++;
    }
  }
    
  cdoc_pos.word_count = pi;
  dummy_wv_pos->num_entries = pi;
  bow_barrel_add_document(new_barrel, &cdoc_pos, dummy_wv_pos); 

  cdoc_neg.word_count = ni;
  dummy_wv_neg->num_entries = ni;
  bow_barrel_add_document(new_barrel, &cdoc_neg, dummy_wv_neg);

  bow_wv_free(dummy_wv_pos);
  bow_wv_free(dummy_wv_neg);

  return (n_meta_docs+2);
}

bow_barrel *svm_vpc_merge(bow_barrel *src_barrel) {
  double        b;
  int           cto;         /* for pairwise - works with npass */
  bow_wv      **docs;        /* a doc major matrix */
  int           max_nsv;     /* highest # of nsv's in a submodel */
  int           mdocs;       /* the number of docs in the current submodel */
  bow_wv      **model_weights;
  int           n_meta_docs; /* # of documents that will go into the class barrel
			      * before the weight vectors will */
  int           nclasses;
  int           ndocs;        /* total # of documents to be trained & transduced */
  int           ntrain;       /* # of documents to be trained upon */
  int           ntrans;       /* # of "unlabeled" docs to use in transduction */
  bow_barrel   *class_barrel;
  int           nloops;      /* # of the current submodel being built */
  int           npass;       /* tmp for making submodels from the src_barrel */
  int           nsv;         /* # of support vectors for the current model */
  int           num_words;
  bow_wv      **sub_docs;
  int          *tdocs;       /* trans table of indices in docs to indices 
			      * in the original barrel */
  int           total_docs;  /* total # of docs (some not for training) */
  int          *utdocs;      /* trans table of the docs in our training set
			      * to those in the actually used in the models */
  float        *weight_vect;
  double       *weights;     /* lagrange multipliers */
  bow_wv      **W;           /* hyperplane for lin. folding */
  int          *yvect;

  int i,j;

#ifndef HAVE_LOQO
  if (svm_use_smo != 1) {
    fprintf(stderr,"Can only use SMO, no other solvers were built,\n"
	    "rebuild libbow with pr_loqo to use another algorithm.\n");
  }
#endif

#ifdef HAVE_FPSETMASK
  fpsetmask(~(FP_X_INV | FP_X_DNML | FP_X_DZ | FP_X_OFL | FP_X_UFL | FP_X_IMP));
#endif

  total_docs = src_barrel->cdocs->length;
  nclasses = bow_barrel_num_classes(src_barrel);
  weight_vect = NULL;
  model_weights = NULL;
  W = NULL;
  yvect = NULL;

  /* note - this OVER allocates - uses ALL, instead of just those for training */
  docs = (bow_wv **) alloca(sizeof(bow_wv *)*total_docs);
  tdocs = (int *) alloca(sizeof(int)*(total_docs+1));
  mdocs = 0; /* to shut gcc up */
  nsv = 0;

  if (nclasses == 1) {
    fprintf(stderr, "Cannot build SVM with only 1 class.\n");
    fflush(stderr);
    return NULL;
  } else if (nclasses == 2) {
    if (svm_kernel_type != FISHER) {
      vote_type = PAIRWISE;
    }
  }

  if (weight_type && svm_kernel_type == FISHER) {
    weight_type = 0;
    tf_transform_type = RAW;
  }

  if ((weight_type && vote_type == PAIRWISE) || weight_type == INFOGAIN) {
    svm_weight_style = WEIGHTS_PER_MODEL;
  } else if (weight_type) {
    svm_weight_style = WEIGHTS_PER_BARREL;
  } else {
    svm_weight_style = NO_WEIGHTS;
  }

  if (svm_weight_style != WEIGHTS_PER_MODEL) {
    ntrain = make_doc_array(src_barrel, docs, tdocs, bow_cdoc_is_train);

    if (ntrain < 2) {
      if (ntrain)
	bow_wv_free(docs[0]);
      fprintf(stderr, "Cannot build svm with less than 2 documents\n");
      fflush(stderr);
      return NULL;
    }
    
    /* append these trans docs to the arrays that were filled in above */
    ntrans = make_doc_array(src_barrel, &(docs[ntrain]), &(tdocs[ntrain]),
			    use_transduction_docs);

    ndocs = ntrain + ntrans;

    utdocs = (int *) alloca(sizeof(int)*ndocs);
    for (i=0; i<ndocs; i++) {
      utdocs[i] = i;
    }
    sub_docs = docs;
    mdocs = ndocs;

    svm_set_barrel_weights(docs, NULL, ndocs, &weight_vect);

    kcache_init(ndocs);
  } else {
    /* the ndocs value is the number of training documents that will
     * actually be used - this is done now JUST to fill up the tdocs array. */
    ntrain = make_doc_array(src_barrel, docs, tdocs, bow_cdoc_is_train);
   
    if (ntrain < 2) {
      if (ntrain)
	bow_wv_free(docs[0]);
      fprintf(stderr, "Cannot build svm with less than 2 documents\n");
      fflush(stderr);
      return NULL;
    }    

    /* figure out the # of ntrans */
    ntrans = make_doc_array(src_barrel, &(docs[ntrain]), &(tdocs[ntrain]),
			    use_transduction_docs);
    
    ndocs = ntrain + ntrans;

    /* since we don't need the docs for a while, free them */
    for (i=0; i<ndocs; i++) {
      bow_wv_free(docs[i]);
    }

    model_weights = (bow_wv **) malloc(sizeof(bow_wv *)*nclasses);

    utdocs = (int *) alloca(sizeof(int)*ndocs);
    /* the sub_docs vector will be rewritten with wv's to be used each iteration */
    sub_docs = alloca(sizeof(bow_wv *)*ndocs);
  }
  
  /* build the naive bayes model for the kernel... */
  if (svm_kernel_type == FISHER) {
    /* this isn't too bad since the cache REALLY should be large enough
     * to hold everything anyway (the cache doesn't get flushed) */
    if (vote_type == PAIRWISE) {
      fprintf(stderr, "Fisher kernel not implemented for pairwise models yet.\n");
      return NULL;
    }

    svm_setup_fisher(src_barrel,docs,nclasses,ndocs);
    weight_type = 0;
  }

  weights = (double *) alloca(sizeof(double)*ndocs);
  yvect = (int *) alloca(sizeof(int)*ndocs);

  /* put together the resultant barrel */
  class_barrel = bow_barrel_new(src_barrel->wi2dvf->size, 2, sizeof(bow_cdoc), 
				src_barrel->cdocs->free_func);

  class_barrel->method = src_barrel->method;
  class_barrel->is_vpc = 1;
    
  /* make a temp word array big enough to fill a whole strip of the wi2dvf table */
  num_words = bow_num_words();
  n_meta_docs = 0;

  /* this is the beginning of the for loop */
  max_nsv = -1;
  nloops = 0;
  npass = 0;

  if (svm_kernel_type == 0) {
    if (vote_type == PAIRWISE) {
      W = (bow_wv **) malloc(sizeof(bow_wv *)*(nclasses-1)*nclasses/2);
    } else {
      W = (bow_wv **) malloc(sizeof(bow_wv *)*nclasses);
    }
  }

  for (npass=0, cto=1; 1; ) {
    /* initialize & pull together the classes for the npass'th model... */
    if (vote_type == PAIRWISE) {
      if (cto == nclasses) {
	npass ++;
	if (npass == nclasses-1) {
	  break;
	}
	cto = npass+1;
      }

      if (svm_weight_style == WEIGHTS_PER_MODEL) {
	silly_currying_global_v1 = npass;
	silly_currying_global_v2 = cto;
	/* this gets called here since the doctype labels are in the barrel */
	/* utdocs is filled with actual indices, not indices of the train set */
	mdocs = make_doc_array(src_barrel, sub_docs, utdocs, use_train_and_submodel);
		
	/* put the labels in for the labeled docs. */
	for (i=0; i<mdocs; i++) {
	  bow_cdoc *cdoc = (GET_CDOC_ARRAY_EL(src_barrel,utdocs[i]));
	  yvect[i] = map_class_to_y(npass, cdoc->class);
	}

	/* even though this set of docs is always the same (since all of the 
	 * unlabeled data is used for each pairwise document [this is not 
	 * suggested to with a barrel w/ more than 2 classes] is used) we 
	 * still grab it, since the starting position for the unlabeled data
	 * isn't known beforehand (its a slight hack) */
	ntrans = make_doc_array(src_barrel, &(sub_docs[mdocs]), 
				&(utdocs[mdocs]), use_transduction_docs);

	/* this says that it is unlabelled */
	for (i=0; i<ntrans; i++) {
	  yvect[i+mdocs] = 0;
	}

	mdocs = mdocs + ntrans;

	/* utdocs holds the barrel indices we're interested in the sub-model
	 * indices - so we need to remap utdocs */
	for (i=j=0; j<mdocs; i++) {
	  if (tdocs[i] < utdocs[j]) {
	    continue;
	  } else {
	    utdocs[j] = i;
	    j++;
	  }
	}

      } else {
	for (i=j=0; i<ntrain; i++) {
	  bow_cdoc *cdoc = (GET_CDOC_ARRAY_EL(src_barrel,tdocs[i]));
	  if ((cdoc->class == npass) || (cdoc->class == cto)) {
	    sub_docs[j] = docs[i];
	    yvect[j] = map_class_to_y(npass, cdoc->class);
	    utdocs[j] = i;
	    j++;
	  }
	}
	for (i=0; i<ntrans; j++,i++) {
	    sub_docs[j] = docs[i+ntrain];
	    utdocs[j] = i+ntrain;
	    yvect[j] = 0;
	}

	mdocs = j;
      }

    } else {
      if (npass == nclasses) {
	break;
      }
      /* all docs should be included - the yvect will do the proper mapping */
      for (i=0; i<ntrain; i++) {
	bow_cdoc *cdoc = (GET_CDOC_ARRAY_EL(src_barrel,tdocs[i]));
	/* this map will be extended to make the barrel handle more than 2 classes */
	yvect[i] = map_class_to_y(npass, cdoc->class);
      }

      for (i=0; i<ntrans; i++) {
	yvect[i+ntrain] = 0;
      }
      
      if (svm_weight_style == WEIGHTS_PER_MODEL) {
	for (i=0; i<mdocs; i++) {
	  /* the weight values are not correct - they include the last values */
	  /* make_doc_array does this for pairwise voting */
	  tf_transform(docs[i]);
	}
      }
    }

    if (svm_weight_style == WEIGHTS_PER_MODEL) {
      svm_set_barrel_weights(sub_docs, yvect, mdocs, &weight_vect);
      model_weights[nloops] = bow_wv_new(num_words);
      for (i=j=0; i<num_words; i++) {
	if (weight_vect[i] != 0.0) {
	  model_weights[nloops]->entry[j].wi = i;
	  model_weights[nloops]->entry[j].count = 1;
	  model_weights[nloops]->entry[j].weight = weight_vect[i];
	  j++;
	}
      }
      free(weight_vect);
      model_weights[nloops]->num_entries = j;
    }

    if (mdocs < 2) {
      bow_error("Cannot create SVM with only 1 document!\n");
    }

    fprintf(stderr,"Learning %dth model\n",nloops);


    if (svml_basename) {
      char *tmp;
      FILE *f = stdout;
      tmp = malloc(sizeof(char)*(20+strlen(svml_basename)));
      sprintf(tmp,"train_%d_%s",nloops,svml_basename);
      f = fopen (tmp, "w");
      for (i=0; i<mdocs; i++) {
	fprintf(f,"%d ", yvect[i]);
	for (j=0; j<sub_docs[i]->num_entries; j++) {
	  fprintf (f,"%d:%f ",1+sub_docs[i]->entry[j].wi, sub_docs[i]->entry[j].weight);
	}
	fprintf(f,"\n");
      }
      fclose(f);

      /* set up the test output file */
      sprintf(tmp,"test_%s",svml_basename);
      svml_test_file = fopen (tmp, "w");

      free(tmp);

      nsv = 0;
      W[nloops] = bow_wv_new(0);
    } else {
      /* only useful with test-in-train - ONLY build models after a certain point
       * (like when the previously acquired data runs out) */
      if ((!test_in_train) || ((test_in_train) && (nloops >= model_starting_no))) {
	nsv = tlf_svm(sub_docs,yvect,weights,&b,&(W[nloops]),ntrans,mdocs);
      }
    }

    if (vote_type == PAIRWISE && weight_type) {
      for (i=0; i<mdocs; i++) {
	bow_wv_free(sub_docs[i]);
      }
    }

    if (max_nsv < nsv) {
      max_nsv = nsv;
    }

    /* now we need to drop the significant classes into the barrel */
    if (!test_in_train) {
      n_meta_docs += add_sv_barrel(class_barrel, weights, yvect, utdocs, b, nloops, nsv);
    }
    
    if (vote_type == PAIRWISE) {
      cto++;
    } else {
      npass ++;
    }
    nloops++;
  }

  if (test_in_train) {
    exit(0);
  }

  if (svm_kernel_type == 0) {
    bow_cdoc cdoc;
    cdoc.filename = NULL;
    cdoc.class_probs = NULL;
    cdoc.type = bow_doc_ignore;
    cdoc.class = 1;

    for (i=0; i<nloops; i++) {
      cdoc.word_count = W[i]->num_entries;
      bow_barrel_add_document(class_barrel, &cdoc, W[i]);
      bow_wv_free(W[i]);
    }
    free(W);
  }

  /* if it was per model, the cache would need to be alloc-ed & de-alloced locally */
  if (svm_weight_style != WEIGHTS_PER_MODEL) {
    kcache_clear();
  }

  /* place the model weights into the barrel */
  if (svm_weight_style == WEIGHTS_PER_MODEL) {
    bow_cdoc cdoc;
    cdoc.filename = NULL;
    cdoc.class_probs = NULL;
    cdoc.type = bow_doc_ignore;
    cdoc.class = 1;  /* this is fine since all of the docs are class 0 & we
		      * know how many meta docs there are */
    for (i=0; i<nloops; i++) {
      cdoc.word_count = model_weights[i]->num_entries;
      bow_barrel_add_document(class_barrel, &cdoc, model_weights[i]);
      bow_wv_free(model_weights[i]);
    }
    free(model_weights);
  }

  /* the docs were freed before just to save memory - now we need them again
   * & the optimizer's done, so a lot of memory is no longer being used */
  if (svm_weight_style == WEIGHTS_PER_MODEL && vote_type == PAIRWISE) {
    make_doc_array(src_barrel, docs, tdocs, bow_cdoc_is_train);

    /* append these trans docs to the arrays that were filled in above */
    make_doc_array(src_barrel, &(docs[ntrain]), &(tdocs[ntrain]),
			    use_transduction_docs);
  }

  /* now add all of the documents from the doc barrel to the class barrel */
  for (i=0; i<ndocs; i++) {
    /* add the i'th document to the class_barrel */
    /* first we need to make a new cdoc */
    bow_cdoc cdoc;
    memcpy(&cdoc, GET_CDOC_ARRAY_EL(src_barrel, tdocs[i]), sizeof(bow_cdoc));
    cdoc.filename = strdup(cdoc.filename);
    cdoc.class = 0;
    bow_barrel_add_document(class_barrel, &cdoc, docs[i]);
  }

  /* this has to be done after all possible dv's have been created */
  if (!((vote_type == PAIRWISE && weight_type) || weight_type == INFOGAIN)
      && weight_type) { /* if no weights are used at all this isn't nec. */
    bow_dv *dv;
    j = bow_num_words();
    for (i=0; i<j; i++) {
      dv = bow_wi2dvf_dv (class_barrel->wi2dvf, i);
      if (dv) {
	dv->idf = weight_vect[i];
      }
    }
    free(weight_vect);
  }

  if (vote_type == PAIRWISE) {
    BARREL_GET_MAX_NSV(class_barrel) = max_nsv;
  } else {
    BARREL_GET_MAX_NSV(class_barrel) = -1*max_nsv;
  }
  BARREL_GET_NCLASSES(class_barrel) = nclasses;
  BARREL_GET_NMETA_DOCS(class_barrel) = n_meta_docs;

  class_barrel->classnames = bow_int4str_new(0);
  for (i=0; i<nclasses; i++) {
    /* drop a class label in */
    bow_str2int(class_barrel->classnames, bow_int2str(src_barrel->classnames, i));
  }

  for (i=0; i<ndocs; i++) {
    bow_wv_free(docs[i]);
  }
  
  return class_barrel;
}

inline double evaluate_model(bow_wv **docs, double *weights, int *yvect, double b, 
			     bow_wv *query_wv, int nsv) {
  double sum,tmp;
  int i,j;
  for (i=j=0, sum=0.0; j<nsv; i++) {
    if (weights[i] != 0.0) {
      tmp = kernel(docs[i],query_wv);
      sum += yvect[i]*weights[i]*tmp;
      j++;
    }
  }
  return (sum - b);
}

/* similar to above, but to only for when the cache should be used */
inline double evaluate_model_cache(bow_wv **docs, double *weights, int *yvect, double b, 
			     bow_wv *query_wv, int nsv) {
  double sum,tmp;
  int i,j;
  for (i=j=0, sum=0.0; j<nsv; i++) {
    if (weights[i] != 0.0) {
      tmp = svm_kernel_cache(docs[i],query_wv);
      sum += yvect[i]*weights[i]*tmp;
      j++;
    }
  }
  return (sum - b);
}

inline double evaluate_model_hyperplane(double *W, double b, bow_wv *query_wv) {
  return (dprod_sd(query_wv,W)-b);
}

/* this & setup_docs are for "caching" the barrel into its wv form */
static void clear_model_cache () {
  int i;
  if (model_cache.barrel) {
    for (i=0; i<model_cache.ndocs; i++) {
      bow_wv_free(model_cache.docs[i]);
    }

    for (i=0; i<model_cache.nmodels; i++) {
      free(model_cache.indices[i]);
      free(model_cache.weights[i]);
      free(model_cache.yvect[i]);
      if (svm_weight_style == WEIGHTS_PER_MODEL) {
	free(model_cache.word_weights.sub_model[i]);
      }
      if (svm_kernel_type == 0) {
	free(model_cache.W[i]);
      }
    }

    free(model_cache.docs);
    free(model_cache.indices);
    free(model_cache.weights);
    free(model_cache.yvect);
    free(model_cache.bvect);
    free(model_cache.sizes);
    if (svm_weight_style == WEIGHTS_PER_MODEL) {
      free(model_cache.word_weights.sub_model);
    } else if (svm_weight_style == WEIGHTS_PER_BARREL) {
      free(model_cache.word_weights.barrel);
    }
    if (svm_kernel_type == 0) {
      free(model_cache.W);
    }
  }
  model_cache.barrel = NULL;
}

/* this fn fills *sub_docs with the m-th submodel (it pulls the docs
 * from the cache that setup_docs fills & then sets whatever weights
 * are necessary) */
/* the query vector should already be normalized */
void make_sub_model(int m, int weight_style, bow_wv ***sub_docs) {
  bow_wv **docs;
  int     *indices;
  float  *weights;
  bow_we  *v2;

  int i,j;

  docs = *sub_docs;

  for(j=0; j<model_cache.sizes[m]; j++) {
    docs[j] = model_cache.docs[model_cache.indices[m][j]];
  }

  if (weight_style) {
    indices = model_cache.indices[m];
    weights = model_cache.word_weights.sub_model[m];

    for (i=0; i<model_cache.sizes[m]; i++) {
      int n = docs[i]->num_entries;
      int di = indices[i];
      v2 = docs[i]->entry;
      for (j=0; j<n; j++) {
	v2[j].weight = weights[v2[j].wi] * model_cache.oweights[di][j];
      }
    }
  }
}

static void setup_docs(bow_barrel *barrel, int nclasses, int nmodels) {
  bow_cdoc    *cdoc;
  int          classnum, c_old;
  bow_wv      *dtmp;
  bow_dv_heap *heap;
  int          ndocs;
  int          nmeta_docs;
  int          nwords;
  int          total_words;
  int h,i,j,k,l;

  nmeta_docs = BARREL_GET_NMETA_DOCS(barrel);
  ndocs = barrel->cdocs->length - nmeta_docs;
  total_words = bow_num_words();

  clear_model_cache();

  model_cache.docs = (bow_wv **) malloc(sizeof(bow_wv *)*ndocs);

  model_cache.indices = (int **) malloc(sizeof(int *)*nmodels);
  model_cache.weights = (double **) malloc(sizeof(double *)*nmodels);
  model_cache.yvect = (int **) malloc(sizeof(int *)*nmodels);

  model_cache.bvect = (double *) malloc(sizeof(double)*nmodels);
  model_cache.sizes = (int *) malloc(sizeof(int)*nmodels);

  if (weight_type) {
    if (vote_type == PAIRWISE || weight_type == INFOGAIN) {
      svm_weight_style = WEIGHTS_PER_MODEL;
      model_cache.word_weights.sub_model = (float **) malloc(sizeof(float *)*nmodels);
      if (tf_transform_type) 
	model_cache.oweights = (float **) malloc(sizeof(float *)*ndocs);
    } else {
      svm_weight_style = WEIGHTS_PER_BARREL;
      model_cache.word_weights.barrel = (float *) malloc(sizeof(float *)*total_words);
    }
  } else {
    svm_weight_style = NO_WEIGHTS;
  }

  if (svm_kernel_type == 0) {
    model_cache.W = (double **) malloc(sizeof(double *)*nmodels);
  } else {
    model_cache.W = NULL;
  }

  /* Create the Heap of vectors of all documents */
  heap = bow_make_dv_heap_from_wi2dvf(barrel->wi2dvf); 

  /* throw away the first 2 - they hold only ancillary info 
   * (see the macros at the top of the file) */
  bow_heap_next_wv(heap, barrel, &dtmp, bow_cdoc_yes);
  bow_heap_next_wv(heap, barrel, &dtmp, bow_cdoc_yes);

  /* grab the meta documents first & setup the arrays */
  for (h=0,l=2; h<nmodels; h++) {
    classnum=c_old=-1;

    for (nwords=j=0,k=-1; l<nmeta_docs; l++) {    /* only go thru for 2 different classes */
      cdoc = bow_cdocs_di2doc (barrel->cdocs, l);

      /* if this isn't what the last one was,  */
      if ((cdoc->class != classnum) && (c_old != cdoc->class) && (k==1)) {
	break;
      }

      bow_heap_next_wv(heap, barrel, &dtmp, bow_cdoc_yes);
      
      if ((cdoc->class != classnum) && (c_old != cdoc->class)) {
	if (k==-1) {
	  /* do the stuff that needs done once for each model */
	  model_cache.bvect[h] = cdoc->normalizer;
	  nwords = dtmp->num_entries;
	  model_cache.indices[h] = (int *) malloc(sizeof(int)*nwords);
	  model_cache.weights[h] = (double *) malloc(sizeof(double)*nwords);
	  model_cache.yvect[h] = (int *) malloc(sizeof(int)*nwords);
	} else {  /* in an already initialized model, but we need to grow arrays */
	  nwords += dtmp->num_entries;
	  model_cache.indices[h] = (int *) realloc(model_cache.indices[h], sizeof(int)*(nwords));
	  model_cache.weights[h] = (double *) realloc(model_cache.weights[h], sizeof(double)*nwords);
	  model_cache.yvect[h] = (int *) realloc(model_cache.yvect[h], sizeof(int)*nwords);
	}

	k++;
	c_old = classnum;
	classnum = cdoc->class;
      } else {  /* already seen this class - need to grow some arrays */
	nwords += dtmp->num_entries;
	model_cache.indices[h] = (int *) realloc(model_cache.indices[h], sizeof(int)*(nwords));
	model_cache.weights[h] = (double *) realloc(model_cache.weights[h], sizeof(double)*nwords);
	model_cache.yvect[h] = (int *) realloc(model_cache.yvect[h], sizeof(int)*nwords);
      }

      for (i=0; j<nwords; j++,i++) {
	model_cache.indices[h][j] = dtmp->entry[i].count - 1;
	model_cache.weights[h][j] = dtmp->entry[i].weight;
	model_cache.yvect[h][j] = ((k == 0) ? 1.0 : -1.0);
      }

    }
    model_cache.sizes[h] = nwords;    
  }

  /* if there are cached hyperplanes, lets grab them... */
  if (svm_kernel_type == 0) {
    for (i=0; i<nmodels; i++) {
      bow_heap_next_wv(heap, barrel, &dtmp, bow_cdoc_yes);
      model_cache.W[i] = (double *) malloc(total_words*sizeof(double));
      for (h=j=0; j<dtmp->num_entries; h++) {
	if (h == dtmp->entry[j].wi) {
	  model_cache.W[i][h] = dtmp->entry[j].weight;
	  j++;
	} else {
	  model_cache.W[i][h] = 0.0;
	}
      }
      for (; h<total_words; h++) {
	model_cache.W[i][h] = 0.0;
      }
    }

#ifdef DEBUG
    for (j=0; j<total_words; j++) {
      tmp = model_cache.W[0][j] + model_cache.W[1][j];
      assert(tmp >= -1*svm_epsilon_crit && tmp <= svm_epsilon_crit);
    }
#endif
  }

  /* any kind of pairwise weights needs its own set of weights, since the domain
   * for each model is different...  Info-gain also needs it since items relevant
   * & useful in one model may be of no use in another (since there are always only
   * 2 classes...) */
  if (svm_weight_style == WEIGHTS_PER_MODEL) {
    for (h=0; h<nmodels; h++) {
      bow_heap_next_wv(heap, barrel, &dtmp, bow_cdoc_yes);
      model_cache.word_weights.sub_model[h] = (float *) malloc(sizeof(float)*total_words);
      for (i=j=0; i<total_words; i++) {
	if ((j < dtmp->num_entries) && (dtmp->entry[j].wi == i)) {
	  model_cache.word_weights.sub_model[h][i] = dtmp->entry[j].weight;
	  j++;
	} else {
	  model_cache.word_weights.sub_model[h][i] = 0.0;
	}
      }
    }
  } else if (svm_weight_style == WEIGHTS_PER_BARREL) {
    bow_dv *dv;
    
    for (h=0; h<total_words; h++) {
      dv = bow_wi2dvf_dv (barrel->wi2dvf, h);
      if (dv) {
	model_cache.word_weights.barrel[h] = dv->idf;
      } else {
	model_cache.word_weights.barrel[h] = 0.0;
      }
    }
  }
  
  /* the rest of the documents are just the training documents - keep
   * grabbing them until they're gone */
  for (h=0; heap->length; h++) {
    bow_heap_next_wv(heap, barrel, &dtmp, bow_cdoc_yes);
    model_cache.docs[h] = bow_wv_new(dtmp->num_entries);
    
    for (j=0; j<dtmp->num_entries; j++) {
      model_cache.docs[h]->entry[j].wi = dtmp->entry[j].wi;
      model_cache.docs[h]->entry[j].count = dtmp->entry[j].count;
    }
    /*
    if (svm_kernel_type == FISHER) {
      for (j=0; j<model_cache.docs[h]->num_entries; j++) {
	model_cache.docs[h]->entry[j].weight = (float) model_cache.docs[h]->entry[j].count;
      }
      model_cache.docs[h]->normalizer = 1.0;
      continue;
    }*/
    tf_transform(model_cache.docs[h]);

    /* this means that the weights will change with every model & 
     * therefore we need to keep track of what they were initially (after the tf_transform) */
    if (svm_weight_style == WEIGHTS_PER_MODEL && tf_transform_type) {
      model_cache.oweights[h] = (float *) malloc(sizeof(float)*dtmp->num_entries);
      for (j=0; j<model_cache.docs[h]->num_entries; j++) {
	model_cache.oweights[h][j] = model_cache.docs[h]->entry[j].weight;
      }
    } else {
      /* otherwise, the weights should be set now... */
      if (svm_weight_style == NO_WEIGHTS) {
	bow_wv_normalize_weights_by_summing(model_cache.docs[h]);
	for (j=0; j<model_cache.docs[h]->num_entries; j++) {
	  model_cache.docs[h]->entry[j].weight *= model_cache.docs[h]->normalizer;
	}
      } else {
	for (j=0; j<model_cache.docs[h]->num_entries; j++) {
	  model_cache.docs[h]->entry[j].weight *= 
	    model_cache.word_weights.barrel[model_cache.docs[h]->entry[j].wi];
	}
	bow_wv_normalize_weights_by_summing(model_cache.docs[h]);
	for (j=0; j<model_cache.docs[h]->num_entries; j++) {
	  model_cache.docs[h]->entry[j].weight *= model_cache.docs[h]->normalizer;
	}
      }
    }
    /* the oweights (original weights) in the svm_wv now has the proper,
     * tf_transformed & normalized value. */
  }

  model_cache.barrel = barrel;
  model_cache.ndocs = h;
  model_cache.nmodels = nmodels;
}

int svm_score(bow_barrel *barrel, bow_wv *query_wv, bow_score *bscores, 
	      int bscores_len, int loo_class) {
  int          ci;
  int          max_nsv;
  double      *model_vals;
  bow_score   *myscores;
  float       *base_qwv_weights;
  int          nclasses;
  int          nmodels;
  int          ntied;
  int          num_scores;
  int          set_weights;
  bow_wv     **sub_docs;
  int          voting_scheme;

  int i, ii, j, k;

  /* This should be initialized in case BSCORES_LEN is larger than the number
   * of classes in the barrel */
  for (ci=0; ci < bscores_len; ci++) {
    bscores[ci].weight = 0.0;
    bscores[ci].di = 0;
    bscores[ci].name = "default";
  }

  base_qwv_weights = NULL;
  max_nsv = BARREL_GET_MAX_NSV(barrel);
  nclasses = BARREL_GET_NCLASSES(barrel);
  if (max_nsv < 0) {
    max_nsv *= -1;
    nmodels = nclasses;
    voting_scheme = AGAINST_ALL;
  } else {
    nmodels = nclasses*(nclasses-1)/2;
    voting_scheme = PAIRWISE;
  }

  if (model_cache.barrel != barrel) {
    setup_docs(barrel, nclasses, nmodels);
  }
  
  set_weights = svm_weight_style;

  tf_transform(query_wv);
  if (svm_weight_style == WEIGHTS_PER_BARREL) {
    svm_set_wv_weights(query_wv, NULL, model_cache.word_weights.barrel);
  }

  if ((svm_weight_style == NO_WEIGHTS) || (svm_weight_style == WEIGHTS_PER_BARREL)) {    
    bow_wv_normalize_weights_by_summing(query_wv);
    for (i=0; i<query_wv->num_entries; i++) {
      query_wv->entry[i].weight *= query_wv->normalizer;
    }    
    set_weights = 0;
  } else if (tf_transform_type) {
    base_qwv_weights = (float *) malloc(sizeof(float)*query_wv->num_entries);
    for (i=0; i<query_wv->num_entries; i++) {
      base_qwv_weights[i] = query_wv->entry[i].weight;
    }
  }

  
  model_vals = (double *) alloca(sizeof(double)*nmodels);
  sub_docs = (bow_wv **) malloc(sizeof(bow_wv *)*model_cache.ndocs);

  /* classify all of our models */
  if (svm_kernel_type == 0) {
    for (i=0; i<nmodels; i++) {
      if (set_weights) {
	if (tf_transform_type) {
	  svm_set_wv_weights(query_wv, base_qwv_weights, model_cache.word_weights.sub_model[i]);
	} else {
	  svm_set_wv_weights(query_wv, NULL, model_cache.word_weights.sub_model[i]);
	}
      }
      
      if (svml_test_file) {
	for (j=0; j<query_wv->num_entries; j++) {
	  fprintf (svml_test_file,"%d:%f ",1+query_wv->entry[j].wi, query_wv->entry[j].weight);
	}
	fprintf(svml_test_file,"\n");
	model_vals[i] = 1;
      } else {
	model_vals[i] = 
	  evaluate_model_hyperplane(model_cache.W[i], model_cache.bvect[i], query_wv);
      }
    }
  } else {
    for (i=0; i<nmodels; i++) {
      make_sub_model(i, set_weights, &sub_docs);
      if (svm_weight_style == WEIGHTS_PER_MODEL) {
	svm_set_wv_weights(query_wv, base_qwv_weights, model_cache.word_weights.sub_model[i]);
      }

      if (svml_test_file) {
	for (j=0; j<query_wv->num_entries; j++) {
	  fprintf (svml_test_file,"%d:%f ",1+query_wv->entry[j].wi, query_wv->entry[j].weight);
	}
	fprintf(svml_test_file,"\n");
	model_vals[i] = 1;
      } else {
	model_vals[i] = 
	  evaluate_model(sub_docs, model_cache.weights[i], model_cache.yvect[i], 
			 model_cache.bvect[i], query_wv, model_cache.sizes[i]);
      }
    }
  }
  if (base_qwv_weights)
    free(base_qwv_weights);
  free(sub_docs);

  if (!quick_scoring) {
    clear_model_cache();
  }

  /* now I have the outputs for each of the models, if its a linear model,
   * i'm done.  If its pairwise, then I need to put together votes */

  if (voting_scheme == PAIRWISE && nclasses > 2) {
    myscores = (bow_score *) alloca(sizeof(bow_score)*nclasses);

    for (i=0; i<nclasses; i++) {
      myscores[i].di = i;
      myscores[i].name = "default";
      myscores[i].weight = 0.0;
    }

    for (i=ii=0; i<nclasses-1; i++, ii+=j) {
      for (j=0; j<nclasses-i-1; j++) {
	if (model_vals[j+ii] > 0) {
	  myscores[i].weight += 1.0;
	} else {
	  myscores[j+i+1].weight += 1.0;
	}
      }
    }

    /* check for ties */
    qsort(myscores, nclasses, sizeof(bow_score), s_cmp);

    for (ntied=i=1; i<nclasses; i++) {
      if (myscores[i].weight == myscores[0].weight) {
	ntied++;
      } else {
	break;
      }
    }

    /* break ties */
    if (ntied > 1) {
      struct di *div;
      div = (struct di*) alloca(sizeof(struct di)*ntied);

      for (i=0; i<ntied; i++) {
	div[i].d = 0.0;
	div[i].i = myscores[i].di;
      }

      fprintf(stderr,"Warning, %d way tie.\n",ntied);
      fflush(stdout);
      for (i=ii=k=0; k<ntied-1; i++, ii+=(nclasses-i)) {
	if (i == myscores[k].di) {
	  k++;
	  for (j=k; j<ntied; j++) {
	    if (model_vals[ii+myscores[j].di-i-1] > 0) {
	      myscores[k-1].weight += 0.2;
	      div[k-1].d += model_vals[ii+myscores[j].di-i-1];
	    } else {
	      myscores[j].weight += 0.2;
	      div[j].d += -model_vals[ii+myscores[j].di-i-1];
	    }
	  }
	}
      }

      qsort(myscores, ntied, sizeof(bow_score), s_cmp);

      k = ntied;
      for (ntied=i=1; i<k; i++) {
	if (myscores[i].weight == myscores[0].weight) {
	  ntied++;
	} else {
	  break;
	}
      }

      if (ntied > 1) {
	fprintf(stderr,"Warning, taking largest pairwise value to break %d-way tie\n", ntied);
	fflush(stdout);

	for (i=0; i<ntied; i++) {
	  for (j=0; 1; j++) {
	    if (myscores[i].di == div[j].i) {
	      myscores[i].weight += div[j].d/1000;
	      break;
	    }
	  }
	}

	qsort(myscores, ntied, sizeof(bow_score), s_cmp);
      }
    }

    memcpy(bscores, myscores, nclasses*sizeof(bow_score));

    return nclasses;
  } else {
    if (nclasses == 2) {
      model_vals[1] = -1*model_vals[0];
    }
    /* Put SCORES into BSCORES in sorted order */
    /* Each round, find the best remainaing score and put it into bscores */
    for (num_scores=ci=0; ci < nclasses; ci++) {
      if (num_scores < bscores_len
	  || bscores[num_scores-1].weight < model_vals[ci]) {
	int dsi;
	/* We are going to put this score and class index into SCORES
	 * because either 1) there is an empty space in SCORES, or 2)
	 * SCORES[CI] is larger than the smallest score currently there */
	if (num_scores < bscores_len)
	  num_scores++;
	dsi = num_scores - 1;
	/* Shift down all the entries that are smaller than SCORES[CI] */
	for (; dsi > 0 && bscores[dsi-1].weight < model_vals[ci]; dsi--) {
	  bscores[dsi].weight = bscores[dsi-1].weight;
	  bscores[dsi].name = bscores[dsi-1].name;
	  bscores[dsi].di = bscores[dsi-1].di;
	}
	bscores[dsi].weight = model_vals[ci];
	bscores[dsi].di = ci;
	bscores[dsi].name = "default";
      }
    }

    return num_scores;
  }
}

/* since the class_probs field of the cdocs is used & is not a ptr, 
 * the value needs to be nullified before the std free fn is invoked */
void svm_barrel_free(bow_barrel *barrel) {
  BARREL_GET_MAX_NSV(barrel) = 0;
  BARREL_GET_NMETA_DOCS(barrel) = 0;
  return (bow_barrel_free(barrel));
}

rainbow_method rainbow_method_svm = {
  "svm",
  NULL,
  NULL,
  NULL,
  svm_vpc_merge,               /* note: this is written esp. for svms, hence
				* the reason the first 3 fns are undefined */
  NULL,
  svm_score,
  bow_wv_set_weights_to_count, /* any similarity metric will work... */
  NULL,
  svm_barrel_free,
  NULL
};

bow_method bow_method_svm = { "svm" };
void _register_method_svm () __attribute__ ((constructor));

void _register_method_svm () {
  bow_method_register_with_name ((bow_method*)&rainbow_method_svm, "svm", 
				 sizeof(rainbow_method), &svm_argp_child);
  bow_argp_add_child (&svm_argp_child);
}
