/* A simple implementation of using EM to estimate naive Bayes
   parameters from labeled and unlabeled documents */

/* Copyright (C) 1997, 1998, 1999 Andrew McCallum

   Written by:  Kamal Nigam <knigam@cs.cmu.edu>

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

#include <bow/libbow.h>
#include <bow/nbsimple.h>
#include <math.h>
#include <argp/argp.h>
#include <stdlib.h>

/* This function is in rainbow.c */
extern void bow_print_log_odds_ratio (FILE *fp,
				      bow_barrel *barrel, int num_to_print);

enum {
  EMSIMPLE_NUM_RUNS = 19000,
  EMSIMPLE_PRINT_ACCURACY,
  EMSIMPLE_NO_INIT
};

static int bow_emsimple_num_em_runs = 10;
static int (* emsimple_accuracy_docs)(bow_cdoc *) = NULL;
static int emsimple_no_init = 0;

static struct argp_option emsimple_options[] =
{
  {0,0,0,0,
   "EMSIMPLE options:", 90},
  {"emsimple-num-iterations", EMSIMPLE_NUM_RUNS, "NUM", 0, 
   "Number of EM iterations to run when building model."},
  {"emsimple-print-accuracy", EMSIMPLE_PRINT_ACCURACY, "TYPE", 0,
   "When running emsimple, print the accuracy of documents at each EM round.  "
   "Type can be validation, train, or test."},
  {"emsimple-no-init", EMSIMPLE_NO_INIT, 0, 0,
   "Use this option when using emsimple as the secondary method for genem"},
  {0, 0}
};

error_t
emsimple_parse_opt (int key, char *arg, struct argp_state *state)
{
  switch (key)
    {
    case EMSIMPLE_NUM_RUNS:
      bow_emsimple_num_em_runs = atoi(arg);
      break;
    case EMSIMPLE_PRINT_ACCURACY:
      if (!strcmp (arg, "validation"))
	emsimple_accuracy_docs = bow_cdoc_is_validation;
      else if (!strcmp (arg, "train"))
	emsimple_accuracy_docs = bow_cdoc_is_train;
      else if (!strcmp (arg, "test"))
	emsimple_accuracy_docs = bow_cdoc_is_test;
      else
	bow_error("Unknown document type for --emsimple-print-accuracy");
      break;
    case EMSIMPLE_NO_INIT:
      emsimple_no_init = 1;
      break;
    default:
      return ARGP_ERR_UNKNOWN;
    }
  return 0;
}

static const struct argp emsimple_argp =
{
  emsimple_options,
  emsimple_parse_opt
};

static struct argp_child emsimple_argp_child =
{
  &emsimple_argp,	/* This child's argp structure */
  0,			/* flags for child */
  0,			/* optional header in help message */
  0			/* arbitrary group number for ordering */
};

/* End of command-line options specific to EMSIMPLE */


/* Calculate the accuracy of documents indicated by emsimple_accuracy_docs */
float
emsimple_calculate_accuracy (bow_barrel *doc_barrel, bow_barrel *class_barrel)
{
  bow_dv_heap *test_heap;	/* we'll extract test WV's from here */
  bow_wv *query_wv;
  int di;			/* a document index */
  bow_score *hits;
  int actual_num_hits;
  bow_cdoc *doc_cdoc;
  int num_tested = 0;
  int num_correct = 0;

  /* Create the heap from which we'll get WV's. Initialize QUERY_WV so
     BOW_TEST_NEXT_WV() knows not to try to free. */
  hits = alloca (sizeof (bow_score));
  test_heap = bow_test_new_heap (doc_barrel);
  query_wv = NULL;

  /* Loop once for each test document. Check to see if it was
     correctly classified.*/
  while ((di = bow_heap_next_wv (test_heap, doc_barrel, &query_wv, 
				 emsimple_accuracy_docs))
	 != -1)
    {
      doc_cdoc = bow_array_entry_at_index (doc_barrel->cdocs, 
					   di);
      bow_wv_set_weights (query_wv, class_barrel);
      bow_wv_normalize_weights (query_wv, class_barrel);
      actual_num_hits = bow_barrel_score (class_barrel, query_wv, hits, 1, -1);
      assert (actual_num_hits == 1);
      if (doc_cdoc->class == hits[0].di)
	num_correct++;
      num_tested++;
    }

  return (((float) num_correct) / ((float) num_tested));
}


/* Create a naive Bayes style class barrel using EM to estimate
   parameters from labeled and unlabeled documents. */
bow_barrel *
bow_emsimple_new_vpc_with_weights (bow_barrel *doc_barrel)
{
  bow_barrel *vpc_barrel;   /* the vector-per-class barrel */
  int wi;                   /* word index */
  int max_wi;               /* the number of words */
  int dvi;                  /* document vector index */
  int ci;                   /* class index */
  bow_dv *dv;               /* document vector */
  int di;                   /* document index */
  bow_dv_heap *test_heap=NULL;	/* heap for extracting document word vectors */
  bow_wv *query_wv;         /* word vector */
  bow_score *hits;          /* classification scores */
  int actual_num_hits;      /* number of scores */
  int hi;		    /* hit index */
  bow_cdoc *doc_cdoc;       /* a document barrel cdoc */
  int num_tested;           /* number of documents classified */
  int em_runs = 0;          /* number of rounds of EM performed so far */
  int max_ci;               /* the number of classes */

  /* initialize some variables */
  max_ci = bow_barrel_num_classes(doc_barrel);
  max_wi = MIN (doc_barrel->wi2dvf->size, bow_num_words ());
  
  /* set the cdoc->word_count to be the number of word occurrences in
     each document. If any vocab selection has occurred, this is set
     to an incorrect value. */
  {
    /* Create the heap from which we'll get word vectors. */
    query_wv = NULL;
    test_heap = bow_test_new_heap (doc_barrel);
    
    /* Iterate over each document. */
    while (-1 != (di = bow_heap_next_wv (test_heap, doc_barrel, &query_wv, 
					 bow_cdoc_yes)))
      {
	int word_count = 0;
	int wvi;
	
	doc_cdoc = bow_array_entry_at_index (doc_barrel->cdocs, 
					     di);
	
	/* count the total number of word occurrences for this document */
	for (wvi = 0; wvi < query_wv->num_entries; wvi++)
	  word_count += query_wv->entry[wvi].count;
	
	/* set the document's word_count to the true value */
	doc_cdoc->word_count = word_count;
      }
  }

  if (!emsimple_no_init) 
    {
      
      /* Initialize memory and values for each document cdoc->class_probs.
	 This holds the class membership for each labeled and unlabeled
	 document.  For labeled documents, this is the true class
	 membership.  For unlabeled documents, we initialize to zero, but
	 later estimate these memberships using EM. */
      for (di=0; di < doc_barrel->cdocs->length; di++)
	{
	  bow_cdoc *cdoc = bow_array_entry_at_index (doc_barrel->cdocs, di);
	
	  cdoc->class_probs = bow_malloc (sizeof (float) * max_ci);
	  
	  /* initialize the class_probs to all zeros */
	  for (ci=0; ci < max_ci; ci++)
	    cdoc->class_probs[ci] = 0.0;
	  
	  /* If document is a training document, set its class_probs to
	     match its class.  This implicitly starts the EM search using
	     just the labeled data. */
	  if (cdoc->type == bow_doc_train)
	    cdoc->class_probs[cdoc->class] = 1.0;
	}
    }
  
  /* Create an empty barrel; we fill it with vector-per-class
     data and return it. */
  {
    vpc_barrel = bow_barrel_new (doc_barrel->wi2dvf->size,
				 doc_barrel->cdocs->length,
				 doc_barrel->cdocs->entry_size,
				 doc_barrel->cdocs->free_func); 
    vpc_barrel->method = doc_barrel->method;
    vpc_barrel->classnames = bow_int4str_new (0);
    
    /* setup the cdoc structure for the class barrel, except for the
       word counts, which we'll do later.  */
    for (ci = 0; ci < max_ci; ci++)
      {
	bow_cdoc cdoc;
	
	/* create the cdoc structure */
	cdoc.type = bow_doc_train;
	cdoc.normalizer = -0.0f; /* unused value*/
	cdoc.word_count = 0; /* just a temporary value */
	cdoc.prior = 0; /* just a temporary value */
	cdoc.filename = strdup (bow_barrel_classname_at_index (doc_barrel, 
							       ci));
	if (!cdoc.filename)
	  bow_error ("Memory exhausted.");
	bow_barrel_add_classname(vpc_barrel, cdoc.filename);
	cdoc.class_probs = NULL;
	cdoc.class = ci;
	bow_array_append (vpc_barrel->cdocs, &cdoc);
      }
  }
  
  /* let's do some EM */
  while (em_runs < bow_emsimple_num_em_runs)
    {
      em_runs++;

      /* The M-step.  Build the wi2dvf of the class barrel by counting words */
      bow_verbosify (bow_progress, 
		     "Making class barrel by counting words:       ");

      /* get a new wi2dvf structure for our class barrel */
      if (vpc_barrel->wi2dvf != NULL)
	bow_wi2dvf_free(vpc_barrel->wi2dvf);
      vpc_barrel->wi2dvf = bow_wi2dvf_new (doc_barrel->wi2dvf->size);

      /* Add entries to the wi2dvf.  Sum together the word weights for
	 individual labeled and unlabeled documents, multiplying by
	 their class membership. */
      for (wi = 0; wi < max_wi; wi++)
	{
	  dv = bow_wi2dvf_dv (doc_barrel->wi2dvf, wi);

	  /* skip words that do not occur in any documents */
	  if (!dv)
	    continue;

	  /* iterate over all documents that have an occurrence of word wi */
	  for (dvi = 0; dvi < dv->length; dvi++)
	    {
	      bow_cdoc *cdoc; 
	      
	      di = dv->entry[dvi].di;
	      cdoc = bow_array_entry_at_index (doc_barrel->cdocs, di);

	      /* skip documents that have no words */
	      if (cdoc->word_count == 0)
		continue;

	      /* Use only labeled and unlabeled documents when
                 building our barrel's wi2dvf */
	      if (cdoc->type == bow_doc_train ||
		  cdoc->type == bow_doc_unlabeled)
		{

		  /* add weights to all classes, based on the class
                     membership */
		  for (ci=0; ci < max_ci; ci++)
		    if (cdoc->class_probs[ci] > 0)
		      {
			float addition = 0;

			if (bow_event_model == bow_event_word)
			  addition = cdoc->class_probs[ci] * 
			    (float) dv->entry[dvi].count;
			else if (bow_event_model == bow_event_document_then_word)
			  addition = cdoc->class_probs[ci] * 
			    (float) dv->entry[dvi].count * 
			    (float) bow_event_document_then_word_document_length / 
			    (float) cdoc->word_count;
			else
			  bow_error("No implementation of this event model.");
			
			/* add addition weight to the class-word pair */
			bow_wi2dvf_add_wi_di_count_weight 
			  (&(vpc_barrel->wi2dvf), 
			   wi, ci, 
			   1,  /* dummy value */
			   addition);
		      }
		}
	    }

	  if (wi % 100 == 0)
	    bow_verbosify (bow_progress, "\b\b\b\b\b\b%6d", max_wi - wi);
	}
	  
      bow_verbosify (bow_progress, "\n");

      /* bow_print_log_odds_ratio (stderr, vpc_barrel, 5); */
      
      /* set the word_count of each class to the total number of word
         occurrences per class */      
      bow_nbsimple_set_cdoc_word_count_from_wi2dvf_weights (vpc_barrel);

      /* set priors */
      if (doc_barrel->method->vpc_set_priors && !bow_uniform_class_priors)
	(*doc_barrel->method->vpc_set_priors) (vpc_barrel, doc_barrel);
      
      /* Calculate accuracy of the validation set*/
      if (emsimple_accuracy_docs)
	bow_verbosify (bow_progress, "Correct: %f\n", 
		       emsimple_calculate_accuracy (doc_barrel, vpc_barrel));

      /* We have a new vpc barrel to use.  Let's now do the E-step,
	 and classify all our documents. */

      /* Skip the E-step if on the last iteration of EM */
      if (em_runs < bow_emsimple_num_em_runs)
	{
	  /* Classify all the unlabeled documents to re-estimate their
             class membership */
	  bow_verbosify(bow_progress, "\nClassifying unlabeled documents:       ");
	  
	  /* Initialize QUERY_WV so BOW_TEST_NEXT_WV() knows not to
             try to free.  Create the heap from which we'll get
             WV's. */
	  query_wv = NULL;
	  hits = alloca (sizeof (bow_score) * max_ci);
	  num_tested = 0;
	  test_heap = bow_test_new_heap (doc_barrel);
	  
	  /* Loop once for each unlabeled document. */
	  while ((di = bow_heap_next_wv (test_heap, doc_barrel, &query_wv, 
					 bow_cdoc_is_unlabeled))
		 != -1)
	    {
	      doc_cdoc = bow_array_entry_at_index (doc_barrel->cdocs, 
						   di);
	      bow_wv_set_weights (query_wv, vpc_barrel);
	      bow_wv_normalize_weights (query_wv, vpc_barrel);
	      actual_num_hits = 
		bow_barrel_score (vpc_barrel, 
				  query_wv, hits,
				  max_ci, -1);
	      assert (actual_num_hits == max_ci);

	      /* set the class probs to the naive bayes score */
	      for (hi = 0; hi < actual_num_hits; hi++)
		doc_cdoc->class_probs[hits[hi].di] = hits[hi].weight;

	      if (num_tested % 100 == 0)
		bow_verbosify (bow_progress, "\b\b\b\b\b\b%6d", num_tested);
	      
	      num_tested++;	  
	    }
	  
	  bow_verbosify(bow_progress, "\b\b\b\b\b\b%6d\n", num_tested);
	}
    }

  return vpc_barrel;
}



/* Set the class prior probabilities by doing a weighted (by class
   membership) count of the number of labeled and unlabeled documents in
   each class.  */
void
bow_emsimple_set_priors_using_class_probs (bow_barrel *vpc_barrel,
					   bow_barrel *doc_barrel)
     
{
  float prior_sum = 0;
  int ci;
  int max_ci = vpc_barrel->cdocs->length;
  int di;

  /* Zero them. */
  for (ci = 0; ci < max_ci; ci++)
    {
      bow_cdoc *cdoc;
      cdoc = bow_array_entry_at_index (vpc_barrel->cdocs, ci);
      cdoc->prior = 0;
    }

  /* Count each document for each class according to the
     class_probs. */
  for (di = 0; di < doc_barrel->cdocs->length; di++)
    {
      bow_cdoc *doc_cdoc = bow_array_entry_at_index (doc_barrel->cdocs, di);
      bow_cdoc *vpc_cdoc;
      
      if (doc_cdoc->type == bow_doc_train ||
	  doc_cdoc->type == bow_doc_unlabeled)
	{
	  for (ci = 0; ci < max_ci; ci++)
	    {
	      vpc_cdoc = bow_array_entry_at_index (vpc_barrel->cdocs, ci); 
	      vpc_cdoc->prior += doc_cdoc->class_probs[ci];
	    }
	}
    }
  
  /* Sum them all. */
  for (ci = 0; ci < max_ci; ci++)
    {
      bow_cdoc *cdoc;
      cdoc = bow_array_entry_at_index (vpc_barrel->cdocs, ci);
      prior_sum += cdoc->prior;
    }

  /* Normalize to set the prior. */
  for (ci = 0; ci < max_ci; ci++)
    {
      bow_cdoc *cdoc;
      cdoc = bow_array_entry_at_index (vpc_barrel->cdocs, ci);
      cdoc->prior /= prior_sum;
      if (cdoc->prior == 0)
	bow_verbosify (bow_progress, 
		       "WARNING: class `%s' has zero prior\n",
		       cdoc->filename);
      assert (cdoc->prior >= 0.0 && cdoc->prior <= 1.0);
    }
}


rainbow_method bow_method_emsimple = 
{
  "emsimple",
  NULL, /* no weight setting function */
  NULL,	/* no weight scaling function */
  NULL, /* no weight normalizing function, */
  bow_emsimple_new_vpc_with_weights,
  bow_emsimple_set_priors_using_class_probs,
  bow_nbsimple_score,
  bow_wv_set_weights_to_count,
  NULL,	/* no word vector weight normalization */
  bow_barrel_free,
  NULL  /* no parameters */
};

void _register_method_emsimple () __attribute__ ((constructor));
void _register_method_emsimple ()
{
  static int done = 0;
  if (done) 
    return;
  bow_method_register_with_name ((bow_method*)&bow_method_emsimple,
				 "emsimple",
				 sizeof (rainbow_method),
				 &emsimple_argp_child);
  bow_argp_add_child (&emsimple_argp_child);
  done = 1;
}
