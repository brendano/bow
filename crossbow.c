/* A clustering front-end to libbow. */

/* Copyright (C) 1997, 1998, 1999, 2000 Andrew McCallum

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

/* Some naming conventions:

   Use x_count; not num_x or x_size.

   func_x_all() is the same as func_x(), except that is iterates over
   all nodes in the tree.

*/

#include <bow/libbow.h>
#include <argp.h>
#include <bow/crossbow.h>

/* For query serving on a socket */
#include <errno.h>		/* needed on DEC Alpha's */
#include <unistd.h>		/* for getopt(), maybe */
#include <stdlib.h>		/* for atoi() */
#include <string.h>		/* for strrchr() */
#include <sys/types.h>
#include <sys/socket.h>
#ifndef WINNT
#include <sys/un.h>
#endif /* WINNT */
#include <netinet/in.h>
#include <netdb.h>
#include <strings.h>
#include <signal.h>
#include <unistd.h>
#include <fcntl.h>

/* For mkdir() and stat() */
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

/* For opendir */
#include <dirent.h>

#define SHRINKAGE 1
#define CLASSES_FROM_DIRS 1

extern void crossbow_hem_cluster ();
extern void crossbow_hem_full_em ();
extern void crossbow_hem_fienberg ();
extern int crossbow_hem_deterministic_horizontal;
extern int crossbow_hem_restricted_horizontal;
extern int crossbow_hem_shrinkage;

/* The version number of this program. */
#define CROSSBOW_MAJOR_VERSION 0
#define CROSSBOW_MINOR_VERSION 0




/* Global variables. */

/* The top of the hierarchy */
treenode *crossbow_root;

/* The list of documents */
bow_array *crossbow_docs;

/* A hashtable from doc filenames to document indices */
bow_int4str *crossbow_filename2di;

/* The number of classes in a "supervised" setting */
int crossbow_classes_count;

/* A mapping between classnames and class indices */
bow_int4str *crossbow_classnames;

/* FILE* to file containing WV's of the documents */
FILE *crossbow_wv_fp;

/* Access to the arguments of main() */
int crossbow_argc;
char **crossbow_argv;


struct crossbow_arg_state
{
  /* What this invocation of crossbow to do? */
  void (*what_doing)();
  int non_option_argi;
  const char *server_port_num;
  int serve_with_forking;
  const char *cluster_output_dir;
  int build_hier_from_dir;
  const char *print_file_prefix;
  const char *printing_tag;
  const char *classify_files_dirname;
  const char *multiclass_list_filename;
  bow_int4str *vocab_map;
} crossbow_arg_state;




/* Functions for creating, reading, writing a crossbow_doc */

int
crossbow_doc_write (crossbow_doc *doc, FILE *fp)
{
  int ret;
  int i;

  ret = bow_fwrite_string (doc->filename, fp);
  ret += bow_fwrite_int (doc->tag, fp);
  ret += bow_fwrite_int (doc->word_count, fp);
  ret += bow_fwrite_int (doc->wv_seek_pos, fp);
  ret += bow_fwrite_int (doc->di, fp);
  ret += bow_fwrite_int (doc->ci, fp);
  if (bow_file_format_version >= 7)
    {
      ret += bow_fwrite_int (doc->cis_size, fp);
      for (i = 0; i < doc->cis_size; i++)
	ret += bow_fwrite_int (doc->cis[i], fp);
    }
  return ret;
}

int
crossbow_doc_read (crossbow_doc *doc, FILE *fp)
{
  int ret;
  int tag;
  int i;

  ret = bow_fread_string ((char**)&(doc->filename), fp);
  ret += bow_fread_int (&tag, fp);
  doc->tag = tag;
  ret += bow_fread_int (&(doc->word_count), fp);
  ret += bow_fread_int (&(doc->wv_seek_pos), fp);
  ret += bow_fread_int (&(doc->di), fp);
  ret += bow_fread_int (&(doc->ci), fp);
  if (bow_file_format_version >= 7)
    {
      ret += bow_fread_int (&(doc->cis_size), fp);
      if (doc->cis_size)
	{
	  doc->cis = bow_malloc (doc->cis_size
				 * sizeof (typeof (doc->cis[0])));
	  for (i = 0; i < doc->cis_size; i++)
	    ret += bow_fread_int (&(doc->cis[i]), fp);
	}
      else
	doc->cis = NULL;
    }
  doc->wv = NULL;
  doc->cis_mixture = NULL;
  return ret;
}

void
crossbow_doc_free (crossbow_doc *doc)
{
  if (doc->filename)
    free ((void*)doc->filename);
}





/* Return the WV of the DI'th document */
bow_wv *
crossbow_wv_at_di (int di)
{
  crossbow_doc *doc;
  
  doc = bow_array_entry_at_index (crossbow_docs, di);
  if (!doc->wv)
    {
      fseek (crossbow_wv_fp, doc->wv_seek_pos, SEEK_SET);
      doc->wv = bow_wv_new_from_data_fp (crossbow_wv_fp);
    }
  return doc->wv;
}

/* Load all the document WV's into their DOC's */
void
crossbow_load_doc_wvs ()
{
  int di;
  crossbow_doc *doc;

  fseek (crossbow_wv_fp, 0, SEEK_SET);
  for (di = 0; di < crossbow_docs_count; di++)
    {
      doc = bow_array_entry_at_index (crossbow_docs, di);
      assert (doc->wv == NULL);
      assert (doc->wv_seek_pos == ftell (crossbow_wv_fp));
      doc->wv = bow_wv_new_from_data_fp (crossbow_wv_fp);
    }
}

/* Convert the array of log-probabilities into a normalized array of
   probabilities. */
void
crossbow_convert_log_probs_to_probs (double *log_probs, int num_entries)
{
  double max_log_prob;
  int i;
  double normalizer;
  
  /* Renormalize by adding a constant to the LOG_PROBS */
  max_log_prob = -DBL_MAX;
  for (i = 0; i < num_entries; i++)
    {
      assert (log_probs[i] <= 0);
      if (log_probs[i] > max_log_prob)
	max_log_prob = log_probs[i];
    }
  assert (max_log_prob != -DBL_MAX);
  for (i = 0; i < num_entries; i++)
    log_probs[i] -= max_log_prob;

  /* Exponentiate the log_probs to get probabilities, and renormalize
     by dividing by their sum. */
  normalizer = 0;
  for (i = 0; i < num_entries; i++)
    {
      log_probs[i] = exp (log_probs[i]);
      normalizer += log_probs[i];
    }
  for (i = 0; i < num_entries; i++)
    {
      log_probs[i] /= normalizer;
      assert (log_probs[i] >= 0);
      assert (log_probs[i] <= 1);
    }
}

/* Print the filenames of the documents that are most probable in each
   leaf. */
void
crossbow_leaf_document_probs_print (int num_to_print)
{
  int num_leaves;
  treenode *iterator, *leaf;
  double *leaf_membership;
  int li, di, i;
  bow_wa **was;			/* being used with di's instead of wi's */
  bow_wv *wv;
  crossbow_doc *doc;

  num_leaves = bow_treenode_leaf_count (crossbow_root);
  leaf_membership = alloca (num_leaves * sizeof (double));
  was = alloca (num_leaves * sizeof (void*));
  for (li = 0; li < num_leaves; li++)
    was[li] = bow_wa_new (crossbow_root->words_capacity+2);

  for (di = 0; di < crossbow_docs->length; di++)
    {
      wv = crossbow_wv_at_di (di);
      for (iterator = crossbow_root, li = 0;
	   (leaf = bow_treenode_iterate_leaves (&iterator)); 
	   li++)
	{
	  leaf_membership[li] = (log (leaf->prior)
				 + bow_treenode_log_prob_of_wv (leaf, wv));
	  leaf_membership[li] /= bow_wv_word_count (wv);
	  //leaf_membership[li] = 1.0 / leaf_membership[li];
	}
      //crossbow_convert_log_probs_to_probs (leaf_membership, num_leaves);
      for (li = 0; li < num_leaves; li++)
	bow_wa_append (was[li], di, leaf_membership[li]);
    }
  for (iterator = crossbow_root, li = 0;
       (leaf = bow_treenode_iterate_leaves (&iterator)); 
       li++)
    {
      bow_wa_sort (was[li]);
      fprintf (stdout, "%s\n", leaf->name);
      for (i = 0; i < num_to_print; i++)
	{
	  char buf[1024];
	  doc = bow_array_entry_at_index (crossbow_docs, was[li]->entry[i].wi);
	  fprintf (stdout, "%20.10f %s\n",
		   was[li]->entry[i].weight,
		   strrchr (doc->filename, '/') + 1);
	  //fflush (stdout);
	  sprintf (buf, "/net/server1/cora/bin/spit-info %s", doc->filename);
	  assert (strlen (buf) < 1024);
	  //system (buf);
	}
    }

  /* Free the WAS */
  for (li = 0; li < num_leaves; li++)
    bow_wa_free (was[li]);

}



/* Write CROSSBOW_ROOT to disk in directory DIRNAME */
void 
crossbow_archive (const char *dirname)
{
  FILE *fp;
  char buf[1024];
#if 0
  struct stat st;
  int e;

  /* Create the data directory, if it doesn't exist already. */
  e = stat (crossbow_arg_state.cluster_output_dir, &st);
  if (e == 0)
    {
      /* Assume this means the file exists. */
      if (!S_ISDIR (st.st_mode))
	bow_error ("`%s' already exists, but is not a directory");
    }
  else
    {
      if (mkdir (crossbow_arg_state.cluster_output_dir, 0777) == 0)
	bow_verbosify (bow_quiet, "Created directory `%s'.\n", 
		       dirname);
      else if (errno != EEXIST)
	bow_error ("Couldn't create default data directory `%s'",
		   dirname);
    }
#endif
      
  /* Write archive file format version */
  sprintf (buf, "%s/version", dirname);
  bow_write_format_version_to_file (buf);

  /* Write the tree */
  sprintf (buf, "%s/tree", dirname);
  fp = bow_fopen (buf, "wb");
  bow_treenode_write (crossbow_root, fp);
  fclose (fp);

  /* Write the list of info about documents */
  sprintf (buf, "%s/docs", dirname);
  fp = bow_fopen (buf, "wb");
  bow_array_write (crossbow_docs, (int(*)(void*,FILE*))crossbow_doc_write, fp);
  fclose (fp);

  /* Write the classnames map. */
  sprintf (buf, "%s/classnames", dirname);
  fp = bow_fopen (buf, "w");
  bow_int4str_write (crossbow_classnames, fp);
  fclose (fp);

  /* Write the vocabulary */
  sprintf (buf, "%s/vocabulary", dirname);
  fp = bow_fopen (buf, "wb");
  bow_words_write (fp);
  fclose (fp);

  /* Write the map from doc filenames to document indices */
  sprintf (buf, "%s/filename2di", dirname);
  fp = bow_fopen (buf, "w");
  bow_int4str_write (crossbow_filename2di, fp);
  fclose (fp);

  /* Write the CROSSBOW_CLASSES_COUNT */
  sprintf (buf, "%s/classes-count", dirname);
  fp = bow_fopen (buf, "wb");
  bow_fwrite_int (crossbow_classes_count, fp);
  fclose (fp);
}

/* Read CROSSBOW_ROOT from disk in directory DIRNAME */
void
crossbow_unarchive (const char *dirname)
{
  FILE *fp;
  char buf[1024];
  int di;
  crossbow_doc *doc;

  /* Read the archive file format version */
  sprintf (buf, "%s/version", dirname);
  bow_read_format_version_from_file (buf);

  /* Read the hierarchy */
  sprintf (buf, "%s/tree", dirname);
  fp = bow_fopen (buf, "rb");
  crossbow_root = bow_treenode_new_from_fp (fp);
  fclose (fp);

  /* Read the list of info about documents */
  sprintf (buf, "%s/docs", dirname);
  fp = bow_fopen (buf, "rb");
  crossbow_docs = 
    bow_array_new_with_entry_size_from_data_fp
    (sizeof (crossbow_doc), 
     (int(*)(void*,FILE*))crossbow_doc_read,
     crossbow_doc_free, fp);
  fclose (fp);

  /* Read the classnames map */
  sprintf (buf, "%s/classnames", dirname);
  fp = bow_fopen (buf, "r");
  crossbow_classnames = bow_int4str_new_from_fp (fp);
  fclose (fp);

  /* Read the vocabulary */
  sprintf (buf, "%s/vocabulary", dirname);
  fp = bow_fopen (buf, "rb");
  bow_words_read_from_fp (fp);
  fclose (fp);

  /* Read the mapping from doc filenames to document indices */
  sprintf (buf, "%s/filename2di", dirname);
  fp = bow_fopen (buf, "r");
  crossbow_filename2di = bow_int4str_new_from_fp (fp);
  fclose (fp);

  /* Open the document FP so it is ready for later reading. */
  sprintf (buf, "%s/wvs", dirname);
  crossbow_wv_fp = bow_fopen (buf, "rb");

  /* Load all the WV's of the documents into memory. */
  crossbow_load_doc_wvs ();

  /* Initialize CROSSBOW_CLASSES_COUNT. */
  if (bow_file_format_version >= 7)
    {
      sprintf (buf, "%s/classes-count", dirname);
      fp = bow_fopen (buf, "rb");
      bow_fread_int (&crossbow_classes_count, fp);
      fclose (fp);
    }
  else
    {
      crossbow_classes_count = 0;
      /* Step through all the documents, and record the largest class index */
      for (di = 0; di < crossbow_docs->length; di++)
	{
	  doc = bow_array_entry_at_index (crossbow_docs, di);
	  if (doc->ci + 1 > crossbow_classes_count)
	    crossbow_classes_count = doc->ci + 1;
	}
    }
}


treenode *
crossbow_new_root_from_dir (const char *dirname0, treenode *parent)
{
  DIR *dir;
  struct dirent *dirent_p;
  struct stat st;
  char dirname[PATH_MAX];
  char child_dirname[PATH_MAX];
  treenode *ret;
  const char *basename;
  int i;

  /* Make DIRNAME be a copy of DIRNAME0, but without any trailing / */
  strcpy (dirname, dirname0);
  i = strlen (dirname);
  if (dirname[i-1] == '/')
    dirname[i-1] = '\0';

  if ((basename = strrchr (dirname, '/')))
    basename++;
  else
    basename = dirname;

  if (parent)
    bow_verbosify (bow_progress, "Building tree for %s%s\n", 
		   parent->name, basename);
  else
    bow_verbosify (bow_progress, "Building root for %s\n", 
		   basename);

  ret = bow_treenode_new (parent, 8, basename);

  if (!(dir = opendir (dirname)))
    bow_error ("Couldn't open directory `%s'", dirname);
  while ((dirent_p = readdir (dir)))
    {
      sprintf (child_dirname, "%s/%s", dirname, dirent_p->d_name);
      stat (child_dirname, &st);
      if (S_ISDIR (st.st_mode)
	  && strcmp (dirent_p->d_name, "unlabeled")
	  && strcmp (dirent_p->d_name, ".")
	  && strcmp (dirent_p->d_name, ".."))
	{
	  /* This directory entry is a subdirectory.  Recursively 
	     descend into it and append its files also. */
	  crossbow_new_root_from_dir (child_dirname, ret);
	}
      else if (S_ISREG (st.st_mode))
	{
	  bow_verbosify (bow_verbose,
			 "Ignoring file in hier non-leaf `%s'\n",
			 dirent_p->d_name);
	}
    }
  closedir (dir);

  return ret;
}


/* Forward declare this function */
void crossbow_index_multiclass_list ();

static int text_file_count;

/* CONTEXT points to the class index CI */
int crossbow_index_filename (const char *filename, void *context)
{
  FILE *fp;
  bow_wv *wv;
  crossbow_doc doc;
  int i;
  char dir[1024];
  char munged_filename[1024];
  char *last_slash;
      
  fp = fopen (filename, "r");
  if (fp == NULL)
    {
      perror ("crossbow_index_filename");
      fprintf (stderr, "Couldn't open `%s' for reading\n", filename);
      return 0;
    }

  if (bow_fp_is_text (fp))
    {
      text_file_count++;
      wv = bow_wv_new_from_text_fp (fp, filename);
      if (strstr (filename, "unlabeled/"))
	{
	  const char *u = strstr (filename, "unlabeled/");
	  int ulen = strlen ("unlabeled/");
	  strncpy (munged_filename, filename, u - filename);
	  strcpy (munged_filename + (u - filename), u + ulen);
	}
      else
	strcpy (munged_filename, filename);
      if (wv)
	{
	  /* Create and add an entry for the doc-info array */
	  doc.filename = strdup (munged_filename);
	  assert (doc.filename);
	  doc.tag = bow_doc_train;
	  doc.word_count = bow_wv_word_count (wv);
	  doc.wv_seek_pos = ftell (crossbow_wv_fp);
	  doc.di = bow_array_next_index (crossbow_docs);
	  /* Make DIR be the directory portion of FILENAME.
	     It will be used as a classname. */
	  strcpy (dir, munged_filename);
	  last_slash = strrchr (dir, '/');
	  *last_slash = '\0';
	  if (crossbow_arg_state.what_doing == crossbow_index_multiclass_list)
	    {
	      doc.ci = -1;
	    }
	  else
	    {
#if CLASSES_FROM_DIRS
	    doc.ci = bow_str2int (crossbow_classnames, dir);
	    bow_verbosify (bow_progress, "Putting file %s into class %s\n",
			   filename, dir);
#else
	    doc.ci = *(int*)context;
#endif
	    }
	  doc.cis_size = 0;
	  doc.cis = NULL;
	  i = bow_array_append (crossbow_docs, &doc);
	  assert (i == doc.di);
	  i = bow_str2int (crossbow_filename2di, munged_filename);
	  assert (i == doc.di);
	  /* Write the WV to disk. */
	  bow_wv_write (wv, crossbow_wv_fp);
	}
      else
	bow_verbosify (bow_progress, "Empty WV in file `%s'\n", filename);
      bow_wv_free (wv);
    }
  fclose (fp);
  bow_verbosify (bow_progress,
		 "\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b"
		 "%6d : %6d", 
		 text_file_count, bow_num_words ());
  return 1;
}

void
crossbow_index_multiclass_list ()
{
  FILE *listfp;
  char fn[BOW_MAX_WORD_LENGTH];
  char line[BOW_MAX_WORD_LENGTH];
  char *lineptr;
  char *classptr, *fileptr;
  crossbow_doc *doc;
  int ci, cisi;
  treenode *tn;

  bow_random_set_seed ();
  listfp = bow_fopen (crossbow_arg_state.multiclass_list_filename, "r");

  /* Create some empty recepticals */
  assert (crossbow_docs == NULL);
  crossbow_docs = bow_array_new (0, sizeof (crossbow_doc), crossbow_doc_free);
  crossbow_filename2di = bow_int4str_new (0);
  crossbow_classnames = bow_int4str_new (0);

  /* Create a root */
  crossbow_root = bow_treenode_new_root (10);

  /* Open the file to which we will write the WV's for each indexed file. */
  sprintf (fn, "%s/wvs", bow_data_dirname);
  crossbow_wv_fp = bow_fopen (fn, "wb");

  /* If we are pruning the vocabulary by occurrence count, then read
     all the documents to get the word counts, and limit the
     vocabulary appropriately. */
  if (bow_prune_vocab_by_occur_count_n)
    {
      bow_verbosify (bow_progress, "Scanning files to remove words "
		     "occurring less than %d times...", 
		     bow_prune_vocab_by_occur_count_n);
      while (fgets (line, BOW_MAX_WORD_LENGTH, listfp)) 
	{
	  /* Skip empty lines (LINE[] includes the newline. */
	  if (strlen (line) <= 1) 
	    continue;
	  assert (strlen (line) < BOW_MAX_WORD_LENGTH-1);
	  lineptr = line;
	  fileptr = strtok (lineptr, " \t\n\r");
	  assert (fileptr);
	  bow_words_add_occurrences_from_file (fileptr);
	}
      /* Rewind the file back to the beginning so we can read it again. */
      fseek (listfp, 0, SEEK_SET);
      bow_words_remove_occurrences_less_than
	(bow_prune_vocab_by_occur_count_n);
      /* Now insist that future calls to bow_word2int*() will not
	 register new words. */
      bow_word2int_do_not_add = 1;
      bow_verbosify (bow_progress, "Done.\n");
    }

  /* From all the lines, read the filenames, one per line, until
     there are no more left. */
  while (fgets (line, BOW_MAX_WORD_LENGTH, listfp)) 
    {
      /* Skip empty lines (LINE[] includes the newline. */
      if (strlen (line) <= 1) 
	continue;
      assert (strlen (line) < BOW_MAX_WORD_LENGTH-1);
      lineptr = line;
      fileptr = strtok (lineptr, " \t\n\r");
      assert (fileptr);
      crossbow_index_filename (fileptr, NULL);
      /* Get the DOC just created by CROSSBOW_INDEX_FILENAME */
      doc = bow_array_entry_at_index (crossbow_docs, crossbow_docs->length-1);
      /* Grab all the classnames, and set their CLASSPROBS to non-zero */
      while ((classptr = strtok (lineptr, " \t\n\r")))
	{
	  if (strlen (classptr) == 0)
	    continue;
	  /* No good reason, just a silly check: */
	  assert (strlen (classptr) > 2);
	  if (bow_str2int_no_add (crossbow_classnames, classptr) == -1)
	    {
	      /* This class hasn't been seen before; create a node for it */
	      tn = bow_treenode_new (crossbow_root, 2, classptr);
	      ci = bow_str2int (crossbow_classnames, classptr);
	      assert (tn->ci_in_parent == ci);
	    }
	  if (doc->cis == NULL)
	    {
	      doc->cis_size = 1;
	      doc->cis = bow_malloc (sizeof (typeof (doc->cis[0]))
				     * doc->cis_size);
	    }
	  else 
	    {
	      doc->cis_size++;
	      doc->cis = bow_realloc (doc->cis, (sizeof (typeof (doc->cis[0]))
						 * doc->cis_size));
	    }
	  ci = bow_str2int (crossbow_classnames, classptr);
	  /* Be that the same class isn't listed twice for the same file */
	  for (cisi = 0; cisi < doc->cis_size-1; cisi++)
	    assert (doc->cis[cisi] != ci);
	  doc->cis[doc->cis_size-1] = ci;
	}
      /* Set DOC->CI from a randomly-selected entry in DOC->CIS so that
	 the test/train splitting routines in split.c will work. */
      doc->ci = doc->cis[random() % doc->cis_size];
    }

  /* Reallocate space for WORDS and NEW_WORDS distributions now that
     we have indexed all the files and know what the complete
     vocabulary size is */
  bow_treenode_realloc_words_all (crossbow_root);

  crossbow_classes_count = crossbow_classnames->str_array_length;
  fclose (crossbow_wv_fp);
  crossbow_wv_fp = NULL;
  fclose (listfp);

  crossbow_archive (bow_data_dirname);
}

void
crossbow_index ()
{
  char fn[1024];
  int argi;
  int ci;

  text_file_count = 0;
  /* If we are pruning the vocabulary by occurrence count, then read
     all the documents to get the word counts, and limit the
     vocabulary appropriately. */
  if (bow_prune_vocab_by_occur_count_n)
    {
      /* Parse all the documents to get word occurrence counts. */
      for (argi = crossbow_arg_state.non_option_argi; 
	   argi < crossbow_argc; 
	   argi++)
	bow_words_add_occurrences_from_text_dir (crossbow_argv[argi], "");
      bow_words_remove_occurrences_less_than
	(bow_prune_vocab_by_occur_count_n);
      /* Now insist that future calls to bow_word2int*() will not
	 register new words. */
      bow_word2int_do_not_add = 1;
    }
  if (crossbow_arg_state.vocab_map)
    {
      /* Set the vocabulary to be the vocab map. */
      bow_words_set_map (crossbow_arg_state.vocab_map, 1);
      /* Now insist that future calls to bow_word2int*() will not
	 register new words. */
      bow_word2int_do_not_add = 1;
    }      

  assert (crossbow_docs == NULL);
  crossbow_docs = bow_array_new (0, sizeof (crossbow_doc), crossbow_doc_free);
  crossbow_filename2di = bow_int4str_new (0);
  crossbow_classnames = bow_int4str_new (0);

  /* Read all the documents and write them as bow_wv's to the
     appropriate file in the model directory.  Also add entries to the
     CROSSBOW_CLASSNAMES map.  This must be done before creating
     TREENODE's so that we know how big to make the their vocabulary
     distributions. */
  sprintf (fn, "%s/wvs", bow_data_dirname);
  crossbow_wv_fp = bow_fopen (fn, "wb");
  for (argi = crossbow_arg_state.non_option_argi; argi < crossbow_argc; argi++)
    {
      ci = argi - crossbow_arg_state.non_option_argi;
      bow_map_filenames_from_dir (crossbow_index_filename, &ci,
				  crossbow_argv[argi], "");
#if !CLASSES_FROM_DIRS
      bow_str2int (crossbow_classnames, crossbow_argv[argi]);
#endif
    }
  fclose (crossbow_wv_fp);
  crossbow_wv_fp = NULL;
  bow_verbosify (bow_progress, "\n");

#if CLASSES_FROM_DIRS
  /* The number of classes equals the number of entries in the
     classname -> class index map. */
  crossbow_classes_count = crossbow_classnames->str_array_length;
#else
  /* Remember the number of topic class tags */
  crossbow_classes_count = crossbow_argc - crossbow_arg_state.non_option_argi;
#endif

  /* Build the hierarchy of treenode's.  This must be done after the
     documents have been read so that the treenode's know how big to
     make their vocabulary distributions. */
  if (crossbow_arg_state.build_hier_from_dir)
    {
      /* xxx This scheme currently makes it difficult to have a class
	 distribution in each node, and to set the hierarchy from a
	 directory structure. */
      assert (crossbow_argc - crossbow_arg_state.non_option_argi == 1);
      crossbow_root = crossbow_new_root_from_dir
	(crossbow_argv[crossbow_arg_state.non_option_argi], NULL);
      bow_treenode_set_classes_uniform (crossbow_root, crossbow_classes_count);
    }
  else
    {
      /* Create just a single root node */
      crossbow_root = bow_treenode_new_root (10);
      /* If there was more than one directory specified on command-line
	 allocate space to hold a class distribution */
      if (crossbow_classes_count > 1)
	bow_treenode_set_classes_uniform (crossbow_root, 
					  crossbow_classes_count);
    }

  crossbow_archive (bow_data_dirname);
}

/* Return a bow_wa containing the classification scores (log
   probabilities) of DOC indexed by the leaf indices */
bow_wa *
//crossbow_classify_doc_new_wa (crossbow_doc *doc)
crossbow_classify_doc_new_wa (bow_wv *wv)
{
  int li, leaf_count;
  double leaf_membership;
  treenode **leaves, *iterator, *leaf;
  bow_wa *wa;

  /* Classify the documents in the TAG-set */
  leaf_count = bow_treenode_leaf_count (crossbow_root);
  leaves = alloca (leaf_count * sizeof (void*));

  wa = bow_wa_new (leaf_count);

  /* Get the membership probability of each leaf */
  for (iterator = crossbow_root, li = 0;
       (leaf = bow_treenode_iterate_leaves (&iterator)); 
       li++)
    {
      if (crossbow_hem_shrinkage)
	leaf_membership = bow_treenode_log_prob_of_wv (leaf, wv);
      else
	leaf_membership = bow_treenode_log_local_prob_of_wv (leaf, wv);
      leaf_membership += log (leaf->prior);
      bow_wa_append (wa, li, leaf_membership);
      leaves[li] = leaf;
    }

  return wa;
}


int
crossbow_classify_doc (crossbow_doc *doc, int verbose, FILE *out)
{
  int li, leaf_count;
  double leaf_membership;
  treenode **leaves, *iterator, *leaf;
  bow_wv *wv;
  bow_wa *wa;
  int ret;
#if 0
  double inverse_rank_sum = 0;
  int rank;
  double score_diff_sum = 0;
#endif
  int word_count;

  /* Classify the documents in the TAG-set */
  leaf_count = bow_treenode_leaf_count (crossbow_root);
  leaves = alloca (leaf_count * sizeof (void*));

  wv = crossbow_wv_at_di (doc->di);
  assert (wv);
  word_count = bow_wv_word_count (wv);

  wa = bow_wa_new (leaf_count);

  /* Get the membership probability of each leaf */
  for (iterator = crossbow_root, li = 0;
       (leaf = bow_treenode_iterate_leaves (&iterator)); 
       li++)
    {
      if (crossbow_hem_shrinkage)
	leaf_membership = bow_treenode_log_prob_of_wv (leaf, wv);
      else
	leaf_membership = bow_treenode_log_local_prob_of_wv (leaf, wv);
      leaf_membership += log (leaf->prior);
#define DOC_LENGTH_SCORE_TRANSFORM 1
#if DOC_LENGTH_SCORE_TRANSFORM
      leaf_membership /= ((word_count + 1) / MIN(9,word_count));
#endif
      bow_wa_append (wa, li, leaf_membership);
      leaves[li] = leaf;
    }

  /* Print the results. */
  assert (wa->length == leaf_count);
  bow_wa_sort (wa);
  leaf = bow_treenode_descendant_matching_name (crossbow_root, 
						doc->filename);
  if (leaf && !strcmp (leaf->name, leaves[wa->entry[0].wi]->name))
    ret = 1;
  else
    ret = 0;

#if 0
  for (rank = 0; rank < wa->length; rank++)
    if (! strcmp (leaf->name, leaves[wa->entry[rank].wi]->name))
      {
	inverse_rank_sum += 1.0 / (rank + 1);
	score_diff_sum += wa->entry[0].weight - wa->entry[rank].weight;
	break;
      }
#endif
  if (verbose)
    {
      fprintf (out, "%s %s ", doc->filename, leaf ? leaf->name : "<unknown>");
      if (verbose >= 2)
	for (li = 0; li < leaf_count; li++)
	  fprintf (out, "%s:%g ", 
		   leaves[wa->entry[li].wi]->name,
		   wa->entry[li].weight);
      else
	fprintf (out, "%s", leaves[wa->entry[0].wi]->name);
      fprintf (out, "\n");
    }
  bow_wa_free (wa);
  return (ret);
}

void
crossbow_classify_tagged_docs (int tag, int verbose, FILE *out)
{
  int di;
  int doc_count = 0;
  int correct_count = 0;
  crossbow_doc *doc;
  
  for (di = 0; di < crossbow_docs->length; di++)
    {
      doc = bow_array_entry_at_index (crossbow_docs, di);
      if (tag != -1 && doc->tag != tag)
	continue;
      doc_count++;
      if ((((crossbow_method*)bow_argp_method)->classify_doc)
	  (doc, verbose, out))
	correct_count++;
    }

  if (!verbose)
    {
      fprintf (out, "Fraction correct %f (%d/%d)\n",
	       ((double)correct_count) / doc_count,
	       correct_count, doc_count);
#if 0
      fprintf (out, "Average Inverse Rank %f\n",
	       inverse_rank_sum / doc_count);
      fprintf (out, "Average Score Difference %f\n",
	       score_diff_sum / doc_count);
#endif
    }
}

void
crossbow_classify_docs_in_dirname (const char *dirname, int verbose)
{
  int classify_filename (const char *filename, void *context)
    {
      crossbow_doc doc;
      bow_wv *wv;
      FILE *fp;

      fp = bow_fopen (filename, "r");
      if (!bow_fp_is_text (fp))
	return 0;
      wv = bow_wv_new_from_text_fp (fp, filename);
      fclose (fp);
      if (!wv) 
	return 0;
      doc.tag = bow_doc_test;
      doc.ci = -1;
      doc.filename = filename;
      doc.word_count = bow_wv_word_count (wv);
      doc.wv_seek_pos = -1;
      doc.di = -1;
      doc.wv = wv;
      doc.cis_size = -1;
      doc.cis = NULL;
      ((((crossbow_method*)bow_argp_method)->classify_doc)
       (&doc, verbose, stdout));
      return 0;
    }

  bow_map_filenames_from_dir (classify_filename, NULL, dirname, "");
}



void
crossbow_cluster ()
{
  bow_verbosify (bow_progress, "Starting clustering\n");
  assert (((crossbow_method*)bow_argp_method)->cluster);
  ((crossbow_method*)bow_argp_method)->cluster ();
}

void
crossbow_classify ()
{
  bow_verbosify (bow_progress, "Starting classification\n");

  /* Train the vertical mixture model with EM. */
  if (!crossbow_hem_restricted_horizontal)
    crossbow_hem_deterministic_horizontal = 1;

  ((crossbow_method*)bow_argp_method)->train_classifier ();

  /* Classify the test documents and output results */
  if (crossbow_arg_state.classify_files_dirname)
    crossbow_classify_docs_in_dirname
      (crossbow_arg_state.classify_files_dirname, 1);
  else
    crossbow_classify_tagged_docs (bow_doc_test, 2, stdout);
}


/* Code for query serving */

static int crossbow_sockfd;

void
crossbow_socket_init (const char *socket_name, int use_unix_socket)
{
  int servlen, type, bind_ret;
  struct sockaddr_in in_addr;
  struct sockaddr *sap;

  type = use_unix_socket ? AF_UNIX : AF_INET;
   
  crossbow_sockfd = socket(type, SOCK_STREAM, 0);
  assert(crossbow_sockfd >= 0);

  if (type == AF_UNIX)
    {
#ifdef WINNT
      servlen = 0;  /* so that the compiler is happy */
      sap = 0;
      assert(WINNT == 0);
#else /* !WINNT */
      struct sockaddr_un un_addr;
      sap = (struct sockaddr *)&un_addr;
      bzero((char *)sap, sizeof(un_addr));
      strcpy(un_addr.sun_path, socket_name);
      servlen = strlen(un_addr.sun_path) + sizeof(un_addr.sun_family) + 1;
#endif /* WINNT */
    }
  else
    {
      sap = (struct sockaddr *)&in_addr;
      bzero((char *)sap, sizeof(in_addr));
      in_addr.sin_port = htons(atoi(socket_name));
      in_addr.sin_addr.s_addr = htonl(INADDR_ANY);
      servlen = sizeof(in_addr);
    }

  sap->sa_family = type;     

  bind_ret = bind (crossbow_sockfd, sap, servlen);
  assert(bind_ret >= 0);

  listen (crossbow_sockfd, 5);
}

/* Read a single document from the socket, classify it and return
   classification */
void
crossbow_serve ()
{
  int newsockfd, clilen;
  struct sockaddr cli_addr;
  FILE *in, *out;
  int ci;

  clilen = sizeof(cli_addr);
  newsockfd = accept(crossbow_sockfd, &cli_addr, &clilen);

  bow_verbosify (bow_progress, "Accepted connection\n");
  assert (newsockfd >= 0);

  in = fdopen(newsockfd, "r");
  out = fdopen(newsockfd, "w");

  while (!feof(in))
    {
      bow_wv *wv = bow_wv_new_from_text_fp (in, NULL);
      bow_wa *wa;

      if (!wv)
	{
	  fprintf (out, ".\n");
	  fflush (out);
	  break;
	}
      wa = crossbow_classify_doc_new_wa (wv);
      bow_wa_sort (wa);
      for (ci = 0; ci < wa->length; ci++)
	fprintf (out, "%s %g\n", 
		 bow_int2str (crossbow_classnames, wa->entry[ci].wi),
		 wa->entry[ci].weight);
      fprintf (out, ".\n");
      fflush (out);
      bow_wa_free (wa);
      bow_wv_free (wv);
    }

  fclose(in);
  fclose(out);

  close(newsockfd);
  bow_verbosify (bow_progress, "Closed connection\n");
}

void
crossbow_query_serving ()
{
  bow_verbosify (bow_progress, "Starting query server\n");

  /* Don't add any new words from the queries to the vocabulary */
  bow_word2int_do_not_add = 1;

  /* Train the vertical mixture model with EM. */
  if (!crossbow_hem_restricted_horizontal)
    crossbow_hem_deterministic_horizontal = 1;

  ((crossbow_method*)bow_argp_method)->train_classifier ();

  bow_verbosify (bow_progress, "Ready to serve!\n");
  crossbow_socket_init (crossbow_arg_state.server_port_num, 0);
  while (1)
    {
      bow_verbosify (bow_progress, "Waiting for connection\n");
      crossbow_serve();
    }

}

void
crossbow_print_word_probabilities ()
{
  bow_error ("Not implemented");
}

void
crossbow_print_doc_names ()
{
  int di;
  int tag = -1;
  crossbow_doc *doc;

  if (crossbow_arg_state.printing_tag)
    {
      tag = bow_str2type (crossbow_arg_state.printing_tag);
      if (tag == -1)
	bow_error ("Argument to --print-doc-names, `%s', is not a tag\n"
		   "Try `train', `test', `unlabeled', etc");
    }
  for (di = 0; di < crossbow_docs->length; di++)
    {
      doc = bow_array_entry_at_index (crossbow_docs, di);
      if (crossbow_arg_state.printing_tag == NULL
	  || (tag >= 0 && doc->tag == tag))
	printf ("%s\n", doc->filename);
    }
}

void
crossbow_print_matrix ()
{
  int di, wvi;
  crossbow_doc *doc;
  bow_wv *wv;
  for (di = 0; di < crossbow_docs->length; di++)
    {
      doc = bow_array_entry_at_index (crossbow_docs, di);
      printf ("%s ", doc->filename);
      wv = crossbow_wv_at_di (di);
      for (wvi = 0; wvi < wv->num_entries; wvi++)
	printf ("%s %d ", 
		bow_int2word (wv->entry[wvi].wi), wv->entry[wvi].count);
      printf ("\n");
    }
}


/* Definitions for using argp command-line processing */

const char *argp_program_version =
"crossbow " STRINGIFY(CROSSBOW_MAJOR_VERSION) "." STRINGIFY(CROSSBOW_MINOR_VERSION);

const char *argp_program_bug_address = "<mccallum@cs.cmu.edu>";

static char crossbow_argp_doc[] =
"Crossbow -- a document clustering front-end to libbow";

static char crossbow_argp_args_doc[] = "[ARG...]";

enum {
  PRINT_IDF_KEY = 13000,
  QUERY_SERVER_KEY,
  QUERY_FORK_SERVER_KEY,
  CLUSTER_OUTPUT_DIR_KEY,
  BUILD_HIER_FROM_DIR_KEY,
  CLASSIFY_KEY,
  CLASSIFY_FILES_KEY,
  PRINT_WORD_PROBABILITIES_KEY,
  PRINT_DOC_NAMES_KEY,
  INDEX_MULTICLASS_LIST_KEY,
  PRINT_MATRIX_KEY,
  USE_VOCAB_IN_FILE_KEY,
};

static struct argp_option crossbow_options[] =
{
  {0, 0, 0, 0,
   "For building data structures from text files:", 1},
  {"index", 'i', 0, 0,
   "tokenize training documents found under ARG..., build weight vectors, "
   "and save them to disk"},
  {"index-multiclass-list", INDEX_MULTICLASS_LIST_KEY, "FILE", 0,
   "Index the files listed in FILE.  Each line of FILE should contain "
   "a filenames followed by a list of classnames to which that file belongs."},
  {"cluster", 'c', 0, 0,
   "cluster the documents, and write the results to disk"},
  {"cluster-output-dir", CLUSTER_OUTPUT_DIR_KEY, "DIR", 0,
   "After clustering is finished, write the cluster to directory DIR"},
  {"build-hier-from-dir", BUILD_HIER_FROM_DIR_KEY, 0, 0,
   "When indexing a single directory, use the directory structure to build "
   "a class hierarchy"},
  {"classify", CLASSIFY_KEY, 0, 0,
   "Split the data into train/test, and classify the test data, outputing "
   "results in rainbow format"},
  {"classify-files", CLASSIFY_FILES_KEY, "DIRNAME", 0,
   "Classify documents in DIRNAME, outputing `filename classname' pairs "
   "on each line."},
  {"query-server", QUERY_SERVER_KEY, "PORTNUM", 0,
   "Run crossbow in server mode, listening on socket number PORTNUM.  "
   "You can try it by executing this command, then in a different shell "
   "window on the same machine typing `telnet localhost PORTNUM'."},
  {"print-word-probabilities", PRINT_WORD_PROBABILITIES_KEY, "FILEPREFIX", 0,
   "Print the word probability distribution in each leaf to files named "
   "FILEPREFIX-classname"},
  {"print-doc-names", PRINT_DOC_NAMES_KEY, "TAG", OPTION_ARG_OPTIONAL,
   "Print the filenames of documents contained in the model.  "
   "If the optional TAG argument is given, print only the documents "
   "that have the specified tag."},
  {"print-matrix", PRINT_MATRIX_KEY, 0, 0,
   "Print the word/document count matrix in an awk- or perl-accessible "
   "format.  Format is sparse and includes the words and the counts."},
  {"use-vocab-in-file", USE_VOCAB_IN_FILE_KEY, "FILENAME", 0,
   "Limit vocabulary to just those words read as space-separated strings "
   "from FILE."},

  { 0 }
};

static error_t
crossbow_parse_opt (int key, char *arg, struct argp_state *state)
{
  switch (key)
    {
    case 'i':
      crossbow_arg_state.what_doing = crossbow_index;
      break;
    case INDEX_MULTICLASS_LIST_KEY:
      crossbow_arg_state.what_doing = crossbow_index_multiclass_list;
      crossbow_arg_state.multiclass_list_filename = arg;
      break;
    case 'c':
      crossbow_arg_state.what_doing = crossbow_cluster;
      break;
    case CLUSTER_OUTPUT_DIR_KEY:
      crossbow_arg_state.cluster_output_dir = arg;
      break;
    case BUILD_HIER_FROM_DIR_KEY:
      crossbow_arg_state.build_hier_from_dir = 1;
      break;
    case CLASSIFY_FILES_KEY:
      crossbow_arg_state.classify_files_dirname = arg;
    case CLASSIFY_KEY:
      crossbow_arg_state.what_doing = crossbow_classify;
      break;
    case QUERY_SERVER_KEY:
      crossbow_arg_state.what_doing = crossbow_query_serving;
      crossbow_arg_state.server_port_num = arg;
      bow_lexer_document_end_pattern = "\n.\r\n";
      break;
    case PRINT_WORD_PROBABILITIES_KEY:
      crossbow_arg_state.what_doing = crossbow_print_word_probabilities;
      break;
    case PRINT_DOC_NAMES_KEY:
      crossbow_arg_state.what_doing = crossbow_print_doc_names;
      crossbow_arg_state.printing_tag = arg;
      break;
    case PRINT_MATRIX_KEY:
      crossbow_arg_state.what_doing = crossbow_print_matrix;
      break;
    case USE_VOCAB_IN_FILE_KEY:
      crossbow_arg_state.vocab_map = bow_int4str_new_from_text_file (arg);
      bow_verbosify (bow_progress,
		     "Using vocab with %d words from file `%s'\n",
		     crossbow_arg_state.vocab_map->str_array_length, arg);
      break;

    case ARGP_KEY_ARG:
      /* Now we consume all the rest of the arguments.  STATE->next is the
	 index in STATE->argv of the next argument to be parsed, which is the
	 first STRING we're interested in, so we can just use
	 `&state->argv[state->next]' as the value for RAINBOW_ARG_STATE->ARGS.
	 IN ADDITION, by setting STATE->next to the end of the arguments, we
	 can force argp to stop parsing here and return.  */
      crossbow_arg_state.non_option_argi = state->next - 1;
      if (crossbow_arg_state.what_doing == crossbow_index
	  && state->next > state->argc)
	{
	  /* Zero directory names is not enough. */
	  fprintf (stderr, "Need at least one directory to index.\n");
	  argp_usage (state);
	}
      state->next = state->argc;
      break;

    default:
      return ARGP_ERR_UNKNOWN;
    }
  return 0;
}

static struct argp crossbow_argp = 
{ crossbow_options, crossbow_parse_opt, crossbow_argp_args_doc,
  crossbow_argp_doc, bow_argp_children};

/* This method structure is defined in hem.c, 
   and is the default bow_argp_method */
extern crossbow_method hem_cluster_method;


int
main (int argc, char *argv[])
{
  /* Default command-line argument values */
  crossbow_arg_state.what_doing = crossbow_cluster;
  crossbow_arg_state.cluster_output_dir = NULL;
  crossbow_arg_state.build_hier_from_dir = 0;
  crossbow_arg_state.print_file_prefix = NULL;
  crossbow_arg_state.printing_tag = NULL;
  crossbow_arg_state.classify_files_dirname = NULL;
  crossbow_arg_state.vocab_map = NULL;
  bow_argp_method = (bow_method*)&hem_cluster_method;

  /* bow_lexer_toss_words_longer_than = 20; */

  /* Parse the command-line arguments. */
  argp_parse (&crossbow_argp, argc, argv, 0, 0, &crossbow_arg_state);

  crossbow_argv = argv;
  crossbow_argc = argc;

  if (*crossbow_arg_state.what_doing != crossbow_index
      && *crossbow_arg_state.what_doing != crossbow_index_multiclass_list)
    {
      crossbow_unarchive (bow_data_dirname);

      /* Do test/train splits. */
      bow_set_doc_types (crossbow_docs, crossbow_classes_count,
			 crossbow_classnames);
    }

  (*crossbow_arg_state.what_doing) ();

  if (crossbow_arg_state.cluster_output_dir
      && *crossbow_arg_state.what_doing != crossbow_index)
    crossbow_archive (bow_data_dirname);

  exit (0);
}
