/* crossbow.h - public declartions for clustering frontend to libbow.
   Copyright (C) 1998, 1999 Andrew McCallum

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

#ifndef __crossbow_h_INCLUDE
#define __crossbow_h_INCLUDE

#include <bow/treenode.h>

/* The beginning of this structure must match bow_doc in bow/libbow.h */
typedef struct _crossbow_doc {
  bow_doc_type tag;
  int ci;			/* class index, used only when supervised */
  const char *filename;
  int word_count;
  int wv_seek_pos;		/* fseek position in CROSSBOW_DOCUMENT_FP */
  int di;
  bow_wv *wv;
  /* For multi-label classification tasks: */
  int cis_size;
  int *cis;
  double *cis_mixture;
} crossbow_doc;

typedef struct _crossbow_method {
  const char *name;
  void (*index)();
  void (*train_classifier)();
  void (*cluster)();
  /* Returns non-zero if correctly classified */
  int (*classify_doc)(crossbow_doc *doc, int verbose, FILE *out);
} crossbow_method;


/* Global variables. */

/* The top of the hierarchy */
extern treenode *crossbow_root;

/* The array of documents */
extern bow_array *crossbow_docs;

/* A hashtable from doc filenames to document indices */
extern bow_int4str *crossbow_filename2di;

/* The number of classes in a "supervised" setting */
extern int crossbow_classes_count;

/* FILE* to file containing WV's of the documents */
extern FILE *crossbow_wv_fp;

/* The number of documents to be clustered. */
#define crossbow_docs_count (crossbow_docs->length)

extern int crossbow_argc;
extern char **crossbow_argv;



/* Crossbow doc Functions */

/* Boolean functions for selecting crossbow docs. */
int crossbow_doc_is_model (crossbow_doc *doc);
int crossbow_doc_is_test (crossbow_doc *doc);
int crossbow_doc_is_ignore (crossbow_doc *doc);



/* Crossbow functions */

/* Return the WV of the DI'th document */
bow_wv *crossbow_wv_at_di (int di);

/* Print the filenames of the documents that are most probable in each
   leaf. */
void crossbow_leaf_document_probs_print (int num_to_print);

/* Convert the array of log-probabilities into a normalized array of
   probabilities. */
void crossbow_convert_log_probs_to_probs (double *log_probs, int num_entries);

/* Return a bow_wa containing the classification scores (log
	 probabilities) of DOC indexed by the leaf indices */
bow_wa *crossbow_classify_doc_new_wa (bow_wv *wv);

#endif /* __crossbow_h_INCLUDE */
