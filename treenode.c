/* treenode.c - Functions for hierarchical word distributions. */

/* Copyright (C) 1997, 1998, 1999 Andrew McCallum

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

#include <bow/libbow.h>
#include <argp.h>
#include <bow/crossbow.h>


#define USE_ACCELERATED_EM 0
#define MISC_STAYS_FLAT 1

/* Accellerated EM: still gauranteed to converge below 2.0.  1.8 is good */
#define EM_ACCELERATION 1.8

/* Functions for creating, reading, writing the treenode */

/* Create and return a new treenode, adding it as a child of PARENT,
   if PARENT is non-NULL. */
treenode *
bow_treenode_new (treenode *parent, 
	      int children_capacity,
	      const char *name)
{
  treenode *ret;
  int i;

  /* To avoid malloc'ing zero bytes */
  if (children_capacity == 0)
    children_capacity = 1;

  ret = bow_malloc (sizeof (treenode));

  /* Set relationship with parent. */
  ret->parent = parent;
  if (parent)
    {
      ret->ci_in_parent = parent->children_count;
      if (parent->children_count >= parent->children_capacity)
	{
	  parent->children_capacity *= 2;
	  parent->children = 
	    bow_realloc (parent->children, 
			 parent->children_capacity * sizeof (void*));
	}
      parent->children[parent->children_count++] = ret;
      if (name)
	{
	  /* +1 for the /, +1 for the terminating 0 */
	  ret->name = bow_malloc (strlen (parent->name) + strlen (name) + 2);
	  sprintf ((char*)ret->name, "%s%s/", parent->name, name);
	}
      else
	{
	  ret->name = bow_malloc (strlen (parent->name) + 20);
	  sprintf ((char*)ret->name, "%s%d/", parent->name, ret->ci_in_parent);
	}
      ret->words_capacity = parent->words_capacity;
      ret->classes_capacity = parent->classes_capacity;
    }
  else
    {
      ret->ci_in_parent = -1;
      ret->name = strdup ("/");
      ret->words_capacity = bow_num_words ();
      ret->classes_capacity = 0;
    }

  ret->children_count = 0;
  ret->children_capacity = children_capacity;
  assert (children_capacity);
  ret->children = bow_malloc (ret->children_capacity * sizeof (void*));
  ret->words = bow_malloc (ret->words_capacity * sizeof (double));
  ret->new_words = bow_malloc (ret->words_capacity * sizeof (double));
  ret->new_words_normalizer = 0;
  for (i = 0; i < ret->words_capacity; i++)
    {
      ret->words[i] = 0;
      ret->new_words[i] = 0;
    }
  ret->prior = 1.0;
  ret->new_prior = 1.0;
  if (parent)
    ret->depth = parent->depth + 1;
  else
    ret->depth = 0;

  /* Initialize ancestor mixture weights, LAMBDAS, to use exclusively
     the local estimate. */
  ret->lambdas = bow_malloc ((ret->depth + 2) * sizeof (double));
  ret->new_lambdas = bow_malloc ((ret->depth + 2) * sizeof (double));
  ret->lambdas[0] = 1.0;
  ret->new_lambdas[0] = 1.0;
  for (i = 1; i < ret->depth + 2; i++)
    {
      ret->lambdas[i] = 0;
      ret->new_lambdas[i] = 0;
    }

  /* Initialize the CLASSES distribution later, only if requested. */
  if (ret->classes_capacity == 0)
    {
      ret->classes = NULL;
      ret->new_classes = NULL;
    }
  else
    {
      ret->classes = bow_malloc (ret->classes_capacity * sizeof (double));
      ret->new_classes = bow_malloc (ret->classes_capacity * sizeof (double));
      for (i = 0; i < ret->classes_capacity; i++)
	{
	  ret->classes[i] = 1.0 / ret->classes_capacity;
	  ret->new_classes[i] = 0.0;
	}
    }

  /* Initialize the DI_WI_NEW_WORDS later, only if requested. */
  ret->di_loo = NULL;
  ret->di_wvi_loo = NULL;
  ret->new_di_loo = NULL;
  ret->new_di_wvi_loo = NULL;

  return ret;
}

/* Free the memory allocate by TN and its children */
void
bow_treenode_free (treenode *tn)
{
  int ci;

  assert (tn == NULL && tn->ci_in_parent == -1);
  for (ci = 0; ci < tn->children_count; ci++)
    bow_treenode_free (tn->children[ci]);

  if (tn->children) bow_free (tn->children);
  if (tn->words) bow_free (tn->words);
  if (tn->new_words) bow_free (tn->new_words);
  if (tn->lambdas) bow_free (tn->lambdas);
  if (tn->new_lambdas) bow_free (tn->new_lambdas);
  if (tn->name) bow_free ((char*)tn->name);
  bow_free (tn);
}


/* Create and return a new treenode with the proper settings to be the
   root treenode */
treenode *
bow_treenode_new_root (int children_count)
{
  return bow_treenode_new (NULL, children_count, NULL);
}

/* Reallocate memory for the WORDS and NEW_WORDS arrays, big enough 
   for the vocabulary of size bow_num_words().  This is useful when the 
   tree has been created before all the documents have been indexed. */
void
bow_treenode_realloc_words_all (treenode *root)
{
  int i;
  if (bow_num_words () > root->words_capacity)
    {
      root->words_capacity = bow_num_words ();
      root->words = 
	bow_realloc (root->words, root->words_capacity * sizeof (double));
      root->new_words = 
	bow_realloc (root->words, root->words_capacity * sizeof (double));
      root->new_words_normalizer = 0;
      for (i = 0; i < root->words_capacity; i++)
	{
	  root->words[i] = 0;
	  root->new_words[i] = 0;
	}
    }

  for (i = 0; i < root->children_count; i++)
    bow_treenode_realloc_words_all (root->children[i]);
}


/* Add to parent a CHILD that was previously created with a NULL parent. */
void
bow_treenode_add_child (treenode *parent, treenode *child)
{
  assert (parent->children_count < parent->children_capacity);
  assert (child->ci_in_parent == -1);
  child->parent = parent;
  child->ci_in_parent = parent->children_count;
  parent->children[parent->children_count++] = child;
}

/* Detach CHILD from PARENT, shifting remaining children, and updating
   the remaining children's CI_IN_PARENT. */
void
bow_treenode_remove_child (treenode *parent, treenode *child)
{
  bow_error ("%s: Not yet implemented", __PRETTY_FUNCTION__);
}


/* To this node and all its children, add a child named "Misc" if not
   there already. */
void
bow_treenode_add_misc_child_all (treenode *root)
{
  int ci;

  /* If ROOT is a leaf, just return immediately */
  if (root->children_count == 0)
    return;

  /* Search for a pre-existing "Misc" child */
  for (ci = 0; ci < root->children_count; ci++)
    if (strstr (root->children[ci]->name, "/Misc/"))
      goto do_children;

  /* Add a "Misc" child */
  bow_treenode_new (root, 0, "Misc");

  /* Recursively handle children */
 do_children:
  for (ci = 0; ci < root->children_count; ci++)
    bow_treenode_add_misc_child_all (root->children[ci]);
}

/* Return the "next" treenode in a traversal of the tree.  CONTEXT
   should be initialized to the root of the tree, otherwise strange
   results will ensue: nodes on the path from the initial CONTEXT node
   to the root will be skipped by the iteration.  When the traversal
   is finished, NULL will be returned. */
treenode *
bow_treenode_iterate_all (treenode **context)
{
  treenode *ret;

  if (*context == NULL)
    return NULL;

  /* Save the context as this call's return value */
  ret = *context;

  /* Update the context for the next call. */
  if ((*context)->children_count)
    *context = (*context)->children[0];
  else
    {
      while ((*context)->parent
	     && ((*context)->ci_in_parent
		 == (*context)->parent->children_count-1))
	{
	  *context = (*context)->parent;
	}
      if ((*context)->parent)
	*context = (*context)->parent->children[(*context)->ci_in_parent+1];
      else
	*context = NULL;
    }
  return ret;
}

/* Same as above, but only return the leaf nodes. */
treenode *
bow_treenode_iterate_leaves (treenode **context)
{
  treenode *ret;
  while ((ret = bow_treenode_iterate_all (context))
	 && ret->children_count != 0)
    ;
  return ret;
}

treenode *
bow_treenode_iterate_all_under_node (treenode **context, treenode *node)
{
  treenode *ret;

  if (*context == NULL)
    return NULL;

  /* Save the context as this call's return value */
  ret = *context;

  /* Update the context for the next call. */
  if ((*context)->children_count)
    *context = (*context)->children[0];
  else
    {
      while ((*context)->parent != node
	     && ((*context)->ci_in_parent
		 == (*context)->parent->children_count-1))
	{
	  *context = (*context)->parent;
	}
      if ((*context)->parent != node)
	*context = (*context)->parent->children[(*context)->ci_in_parent+1];
      else
	*context = NULL;
    }
  return ret;
}

/* Same as above, but only return the leaf nodes. */
treenode *
bow_treenode_iterate_leaves_under_node (treenode **context, treenode *node)
{
  treenode *ret;
  while ((ret = bow_treenode_iterate_all_under_node (context, node))
	 && ret->children_count != 0)
    ;
  return ret;
}

/* Return the deepest descendant with a ->NAME that is contained in NAME */
treenode *
bow_treenode_descendant_matching_name (treenode *root, const char *name)
{
  int ci;
  treenode *tr;
#if WHIZBANG
  char buf[256];
  
  strcpy (buf, "./data");
  strcat (buf, root->name);
  if (strstr (name, buf) != name)
#else
  if (!strstr (name, root->name))
    return NULL;
#endif

  for (ci = 0; ci < root->children_count; ci++)
    {
      if ((tr = bow_treenode_descendant_matching_name
	   (root->children[ci], name)))
	return tr;
    }
#if WHIZBANG
  assert (root->depth == 2);
#endif
  return root;
}


/* Archiving */

#define BOW_TREENODE_HEADER_STRING "treenode\n"

/* Write a treenode (and all its children) to FP. */
void
bow_treenode_write (treenode *tn, FILE *fp)
{
  int i;

  /* Write a tag that will later help verify we are reading correctly. */
  bow_fwrite_string (BOW_TREENODE_HEADER_STRING, fp);

  /* If TN is NULL, write a 0 and return */
  if (tn)
    bow_fwrite_int (1, fp);
  else
    {
      bow_fwrite_int (0, fp);
      return;
    }

  /* Write the name */
  bow_fwrite_string (tn->name, fp);

  /* Write the multinomial */
  bow_fwrite_int (tn->words_capacity, fp);
  bow_fwrite_double (tn->new_words_normalizer, fp);
  for (i = 0; i < tn->words_capacity; i++)
    bow_fwrite_double (tn->words[i], fp);

  /* Write the prior */
  bow_fwrite_double (tn->prior, fp);
  bow_fwrite_double (tn->new_prior, fp);

  /* Write the lambda mixture weights */
  bow_fwrite_int (tn->depth, fp);
  for (i = 0; i < tn->depth + 2; i++)
    {
      bow_fwrite_double (tn->lambdas[i], fp);
      bow_fwrite_double (tn->new_lambdas[i], fp);
    }

  /* Write the class distribution */
  bow_fwrite_int (tn->classes_capacity, fp);
  if (tn->classes_capacity)
    {
      for (i = 0; i < tn->classes_capacity; i++)
	bow_fwrite_double (tn->classes[i], fp);
    }

  /* Write the children treenodes */
  bow_fwrite_int (tn->children_count, fp);
  bow_fwrite_int (tn->children_capacity, fp);
  bow_fwrite_int (tn->ci_in_parent, fp);
  for (i = 0; i < tn->children_count; i++)
    bow_treenode_write (tn->children[i], fp);
}

/* Read and return a new treenode (and all its children) from FP. */
treenode *
bow_treenode_new_from_fp (FILE *fp)
{
  char *header;
  treenode *tn;
  int i;

  /* Verify that we are starting read from the correct place in the FP */
  bow_fread_string (&header, fp);
  if (strcmp (header, BOW_TREENODE_HEADER_STRING) != 0)
    bow_error ("Trying to read a treenode from bad FILE* location");
  bow_free (header);

  /* If a NULL treenode was written, return NULL. */
  bow_fread_int (&i, fp);
  if (i == 0)
    return NULL;

  tn = bow_malloc (sizeof (treenode));
  tn->parent = NULL;

  /* Read the name */
  bow_fread_string ((char**)&(tn->name), fp);

  /* Read the multinomial */
  bow_fread_int (&(tn->words_capacity), fp);
  tn->words = bow_malloc (tn->words_capacity * sizeof (double));
  tn->new_words = bow_malloc (tn->words_capacity * sizeof (double));
  bow_fread_double (&(tn->new_words_normalizer), fp);
  for (i = 0; i < tn->words_capacity; i++)
    {
      bow_fread_double (&(tn->words[i]), fp);
      tn->new_words[i] = 0;
    }

  /* Read the prior */
  bow_fread_double (&(tn->prior), fp);
  bow_fread_double (&(tn->new_prior), fp);

  /* Read the lambda mixture weights */
  bow_fread_int (&(tn->depth), fp);
  tn->lambdas = bow_malloc ((tn->depth + 2) * sizeof (double));
  tn->new_lambdas = bow_malloc ((tn->depth + 2) * sizeof (double));
  for (i = 0; i < tn->depth + 2; i++)
    {
      bow_fread_double (&(tn->lambdas[i]), fp);
      bow_fread_double (&(tn->new_lambdas[i]), fp);
    }

  /* Read in the class distribution */
  bow_fread_int (&(tn->classes_capacity), fp);
  if (tn->classes_capacity)
    {
      tn->classes = bow_malloc (tn->classes_capacity * sizeof (double));
      tn->new_classes = bow_malloc (tn->classes_capacity * sizeof (double));
      for (i = 0; i < tn->classes_capacity; i++)
	{
	  bow_fread_double (&(tn->classes[i]), fp);
	  tn->new_classes[i] = 0;
	}
    }
  else
    tn->classes = tn->new_classes = NULL;

  /* Read the children treenodes */
  bow_fread_int (&(tn->children_count), fp);
  bow_fread_int (&(tn->children_capacity), fp);
  tn->children = bow_malloc (tn->children_capacity * sizeof (void*));
  bow_fread_int (&(tn->ci_in_parent), fp);
  for (i = 0; i < tn->children_count; i++)
    {
      tn->children[i] = bow_treenode_new_from_fp (fp);
      tn->children[i]->parent = tn;
      assert (tn->children[i]->ci_in_parent == i);
    }

  /* Initialize the DI_WI_NEW_WORDS later, only if requested. */
  tn->di_loo = NULL;
  tn->di_wvi_loo = NULL;
  tn->new_di_loo = NULL;
  tn->new_di_wvi_loo = NULL;

  //bow_verbosify (bow_progress, "Read treenode %s\n", tn->name);

  return tn;
}


/* Set all of TN's ancestor mixture weights, LAMBDAS, to equal values. */
void
bow_treenode_set_lambdas_uniform (treenode *tn)
{
  int i;
  double lambda = 1.0 / (tn->depth + 2);

  for (i = 0; i < tn->depth + 2; i++)
    tn->lambdas[i] = lambda;
}

/* Same as above, but for all leaves in the tree. */
void
bow_treenode_set_lambdas_uniform_all (treenode *tn)
{
  treenode *iterator, *leaf;

  assert (tn->parent == NULL);
  for (iterator = tn; (leaf = bow_treenode_iterate_leaves (&iterator)); )
    bow_treenode_set_lambdas_uniform (leaf);
}

/* Set TN's mixture weights, LAMBDAS, to use only the estimates. */
void
bow_treenode_set_lambdas_leaf_only (treenode *tn)
{
  int i;

  tn->lambdas[0] = 1;
  for (i = 1; i < tn->depth + 2; i++)
    tn->lambdas[i] = 0;
}

/* Same as above, but for all leaves in the tree. */
void
bow_treenode_set_lambdas_leaf_only_all (treenode *tn)
{
  treenode *iterator, *leaf;

  assert (tn->parent == NULL);
  for (iterator = tn; (leaf = bow_treenode_iterate_leaves (&iterator)); )
    bow_treenode_set_lambdas_leaf_only (leaf);
}


/* Add WEIGHT to treenode TN's record of how much probability mass
   document DI contributed to TN's NEW_WORDS for the word at DI's
   WVI'th word.  This mass can later be subtracted to do leave-one-out
   calculations.  DI_WV_NUM_ENTRIES-1 is the maximum WVI that can be
   expected for DI; DI_COUNT-1 is the maximum DI that can be expected;
   both are used to know how much space to allocate. */
void
bow_treenode_add_new_loo_for_di_wvi (treenode *tn, 
				     double weight, int di, int wvi,
				     int di_wv_num_entries, int di_count)
{
  int i;

  if (tn->new_di_loo == NULL)
    {
      tn->new_di_loo =
	bow_malloc (di_count * sizeof (double));
      for (i = 0; i < di_count; i++)
	tn->new_di_loo[i] = 0;
    }
  if (tn->new_di_wvi_loo == NULL)
    {
      tn->new_di_wvi_loo = 
	bow_malloc (di_count * sizeof (void*));
      for (i = 0; i < di_count; i++)
	tn->new_di_wvi_loo[i] = NULL;
    }
  if (tn->new_di_wvi_loo[di] == NULL)
    {
      tn->new_di_wvi_loo[di] = 
	bow_malloc (di_wv_num_entries * sizeof (double));
      for (i = 0; i < di_wv_num_entries; i++)
	tn->new_di_wvi_loo[di][i] = 0;
    }
  tn->new_di_loo[di] += weight;
  tn->new_di_wvi_loo[di][wvi] += weight;
}

/* Clear all LOO info for treenode TN */
void
bow_treenode_free_loo (treenode *tn, int di_count)
{
  int i;

  /* For now, clear by freeing */
  if (tn->di_loo)
    {
      bow_free (tn->di_loo);
      tn->di_loo = NULL;
    }
  if (tn->di_wvi_loo)
    {
      for (i = 0; i < di_count; i++)
	{
	  if (tn->di_wvi_loo[i])
	    bow_free (tn->di_wvi_loo[i]);
	}
      bow_free (tn->di_wvi_loo);
      tn->di_wvi_loo = NULL;
    }
}

/* Same as above, over all nodes of the tree. */
void
bow_treenode_free_loo_all (treenode *root, int di_count)
{
  int ci;
  bow_treenode_free_loo (root, di_count);
  for (ci = 0; ci < root->children_count; ci++)
    bow_treenode_free_loo_all (root->children[ci], di_count);
}

/* Clear all LOO info for treenode TN */
void
bow_treenode_free_loo_and_new_loo (treenode *tn, int di_count)
{
  int i;

  if (tn->di_loo)
    {
      bow_free (tn->di_loo);
      tn->di_loo = NULL;
    }
  if (tn->new_di_loo)
    {
      bow_free (tn->new_di_loo);
      tn->new_di_loo = NULL;
    }
  if (tn->di_wvi_loo)
    {
      for (i = 0; i < di_count; i++)
	{
	  if (tn->di_wvi_loo[i])
	    bow_free (tn->di_wvi_loo[i]);
	}
      bow_free (tn->di_wvi_loo);
      tn->di_wvi_loo = NULL;
    }
  if (tn->new_di_wvi_loo)
    {
      for (i = 0; i < di_count; i++)
	{
	  if (tn->new_di_wvi_loo[i])
	    bow_free (tn->new_di_wvi_loo[i]);
	}
      bow_free (tn->new_di_wvi_loo);
      tn->new_di_wvi_loo = NULL;
    }
}

/* Same as above, over all nodes of the tree. */
void
bow_treenode_free_loo_and_new_loo_all (treenode *root, int di_count)
{
  int ci;
  bow_treenode_free_loo_and_new_loo (root, di_count);
  for (ci = 0; ci < root->children_count; ci++)
    bow_treenode_free_loo_and_new_loo_all (root->children[ci], di_count);
}

/* Set the leave-one-out information used for future BOW_TREENODE_PR_WI*()
   calculations from the NEW_*_LOO variables, then clear the NEW_*_LOO
   variables so they are ready for the next round. */
static void
bow_treenode_set_loo_from_new_loo (treenode *tn, int di_count)
{
  bow_treenode_free_loo (tn, di_count);
  tn->di_loo = tn->new_di_loo;
  tn->di_wvi_loo = tn->new_di_wvi_loo;
  tn->new_di_loo = NULL;
  tn->new_di_wvi_loo = NULL;
}


/* Normalize the NEW_WORDS distribution, move it into the WORDS array
   and zero the NEW_WORDS array.  ALPHA is the parameter for the
   Dirichlet prior. */
void
bow_treenode_set_words_from_new_words (treenode *tn, double alpha)
{
  int wi;
  double total_word_count = 0.0;

  /* A special case for "Misc" nodes: increase their smoothing.  NOTE:
     This has no effect if MISC_STAYS_FLAT is non-zero. */
  if (strstr (tn->name, "/Misc/"))
    alpha++;

  /* Calculate the normalizing constant */
  for (wi = 0; wi < tn->words_capacity; wi++)
    total_word_count += tn->new_words[wi];
  total_word_count += alpha * tn->words_capacity;

  //assert (total_word_count);
  if (total_word_count == 0)
    {
      alpha = 1.0 / tn->words_capacity;;
      total_word_count = 1.0;
    }
  for (wi = 0; wi < tn->words_capacity; wi++)
    {
      //assert (tn->new_words[wi] > 0);
      assert (tn->new_words[wi] >= 0);
#if !USE_ACCELERATED_EM
#if !MISC_STAYS_FLAT
      tn->words[wi] = (alpha + tn->new_words[wi]) / total_word_count;
#else
      /* A special case for "Misc" nodes: they stay flat */
      if (strstr (tn->name, "/Misc/"))
	tn->words[wi] = 1.0 / tn->words_capacity;
      else
	tn->words[wi] = (alpha + tn->new_words[wi]) / total_word_count;
#endif /* MISC_STAYS_FLAT */
#else
      tn->words[wi] = 
	(((1.0 - EM_ACCELERATION) * tn->words[wi])
	 + (EM_ACCELERATION * (alpha + tn->new_words[wi]) / total_word_count));
      if (tn->words[wi] < 0)
	tn->words[wi] = 0;
#endif /* USE_ACCELERATED_EM */
      assert (tn->words[wi] >= 0);
      assert (tn->words[wi] <= 1);
      tn->new_words[wi] = 0;
    }
#if USE_ACCELERATED_EM
  /* Renormalize after setting some to zero. */
  total_word_count = 0;
  for (wi = 0; wi < tn->words_capacity; wi++)
    total_word_count += tn->words[wi];
  for (wi = 0; wi < tn->words_capacity; wi++)
    tn->words[wi] /= total_word_count;
#endif

/* Why was this conditioned on MISC_STAYS_FLAT?
   The bow_treenode_pr_wi_loo_local function doesn't work with the
   new_words_normalizer equal to zero! */
  if (!MISC_STAYS_FLAT || !strstr (tn->name, "/Misc/"))
    tn->new_words_normalizer = total_word_count;
  else
    tn->new_words_normalizer = 0;

  /* Also roll over the LOO information. */
  bow_treenode_set_loo_from_new_loo (tn, crossbow_docs->length);
}

/* Over all nodes of the tree, normalize the NEW_WORDS distribution,
   move it into the WORDS array and zero the NEW_WORDS array. */
void
bow_treenode_set_words_from_new_words_all (treenode *root, double alpha)
{
  int ci;

  bow_treenode_set_words_from_new_words (root, alpha);
  for (ci = 0; ci < root->children_count; ci++)
    bow_treenode_set_words_from_new_words_all (root->children[ci], alpha);
}

/* Set NEW_WORDS counts to zero. */
void
bow_treenode_set_new_words_to_zero (treenode *tn)
{
  int wi;
  for (wi = 0; wi < tn->words_capacity; wi++)
    tn->new_words[wi] = 0;
}

/* Same as above, over all nodes of the tree. */
void
bow_treenode_set_new_words_to_zero_all (treenode *root)
{
  int ci;
  bow_treenode_set_new_words_to_zero (root);
  for (ci = 0; ci < root->children_count; ci++)
    bow_treenode_set_new_words_to_zero_all (root->children[ci]);
}

/* Set the NEW_WORDS distribution from the addition of the WORDS
   distribution and some random noise.  NOISE_WEIGHT 0.5 gives equal
   weight to the data and the noise. */
void
bow_treenode_set_new_words_from_perturbed_words (treenode *tn, 
						 double noise_weight)
{
  int wi;
  
  for (wi = 0; wi < tn->words_capacity; wi++)
    tn->new_words[wi] = ((1 - noise_weight) * tn->words[wi]
			 + noise_weight * bow_random_01()/tn->words_capacity);
}

/* Same as above, over all nodes of the tree. */
void
bow_treenode_set_new_words_from_perturbed_words_all (treenode *root, 
						 double noise_weight)
{
  int ci;

  bow_treenode_set_new_words_from_perturbed_words (root, noise_weight);
  for (ci = 0; ci < root->children_count; ci++)
    bow_treenode_set_new_words_from_perturbed_words_all (root->children[ci],
						     noise_weight);
}

/* Over all leaves of the tree, set the PRIOR by the results of
   smoothing and normalizing the NEW_PRIOR distribution.  ALPHA is the
   parameter for the Dirichlet prior. */
void
bow_treenode_set_leaf_prior_from_new_prior_all (treenode *root, double alpha)
{
  treenode *iterator, *leaf;
  double prior_sum = 0;

  assert (root->parent == NULL);
  for (iterator = root; (leaf = bow_treenode_iterate_leaves (&iterator)); )
    {
      if (strstr (leaf->name, "/Misc/"))
	{
	  /* Arbitrarily give /Misc/ node the same weight as the average
	     of the first two children of LEAF's parent. */
	  assert (leaf->parent->children_count >= 2);
	  leaf->new_prior = (leaf->parent->children[0]->new_prior
			     + leaf->parent->children[1]->new_prior) / 2;
	}
      prior_sum += leaf->new_prior + alpha;
    }

  assert (prior_sum);
  for (iterator = root; (leaf = bow_treenode_iterate_leaves (&iterator)); )
    {
      leaf->prior = (leaf->new_prior + alpha) / prior_sum;
      leaf->new_prior = 0;
    }
}

/* Over all nodes (including interior and root) of the tree, set the
   PRIOR by the results of smoothing and normalizing the NEW_PRIOR
   distribution.  ALPHA is the parameter for the Dirichlet prior. */
void
bow_treenode_set_prior_from_new_prior_all (treenode *root, double alpha)
{
  treenode *iterator, *leaf;
  double prior_sum = 0;

  assert (root->parent == NULL);
  for (iterator = root; (leaf = bow_treenode_iterate_all (&iterator)); )
    prior_sum += leaf->new_prior + alpha;

  assert (prior_sum);
  for (iterator = root; (leaf = bow_treenode_iterate_all (&iterator)); )
    {
      leaf->prior = (leaf->new_prior + alpha) / prior_sum;
      leaf->new_prior = 0;
    }
}

/* Over all nodes (including interior and root) of the tree, plus one
   "extra" quantity (intended for the prior probability of the uniform
   distribution), set the PRIOR by the results of smoothing and
   normalizing the NEW_PRIOR distribution, and set EXTRA as part of
   the normalization.  ALPHA is the parameter for the Dirichlet
   prior. */
void
bow_treenode_set_prior_and_extra_from_new_prior_all (treenode *root, 
						     double *new_extra, 
						     double *extra, 
						     double alpha)
{
  treenode *iterator, *leaf;
  double prior_sum = 0;

  assert (root->parent == NULL);
  for (iterator = root; (leaf = bow_treenode_iterate_all (&iterator)); )
    prior_sum += leaf->new_prior + alpha;
  prior_sum += *new_extra + alpha;

  assert (prior_sum);
  for (iterator = root; (leaf = bow_treenode_iterate_all (&iterator)); )
    {
      leaf->prior = (leaf->new_prior + alpha) / prior_sum;
      leaf->new_prior = 0;
    }
  *extra = (*new_extra + alpha) / prior_sum;
  *new_extra = 0;
}

/* Normalize the NEW_LAMBDAS distribution, move it into the LAMBDAS array
   and zero the NEW_LAMBDAS array.  ALPHA is the parameter for the
   Dirichlet prior. */
void
bow_treenode_set_lambdas_from_new_lambdas (treenode *tn, double alpha)
{
  int ai;
  double total_lambdas_count = 0.0;

#if MISC_STAYS_FLAT
  if (strstr (tn->name, "/Misc/"))
    {
      bow_treenode_set_lambdas_uniform (tn);
      for (ai = 0; ai < tn->depth + 2; ai++)
	tn->new_lambdas[ai] = 0;
    }
#endif

  /* Calculate the normalizing constant */
  for (ai = 0; ai < tn->depth + 2; ai++)
    total_lambdas_count += tn->new_lambdas[ai];
  total_lambdas_count += alpha * (tn->depth + 2);

  //assert (total_lambdas_count);
  if (total_lambdas_count == 0)
    {
      alpha = 1.0 / (tn->depth + 2);
      total_lambdas_count = 1.0;
    }

  for (ai = 0; ai < tn->depth + 2; ai++)
    {
      assert (tn->new_lambdas[ai] >= 0);
#if 1 || !USE_ACCELERATED_EM
      tn->lambdas[ai] = (alpha + tn->new_lambdas[ai]) / total_lambdas_count;
#else
      tn->lambdas[ai] = 
	(((1.0 - EM_ACCELERATION) * tn->lambdas[ai])
	 + (EM_ACCELERATION * (alpha + tn->new_lambdas[ai])
	    / total_lambdas_count));
      if (tn->lambdas[ai] < 0)
	tn->lambdas[ai] = 0;
#endif
      assert (tn->lambdas[ai] >= 0);
      assert (tn->lambdas[ai] <= 1);
      tn->new_lambdas[ai] = 0;
    }
}

/* Set the CLASSES distribution to uniform, allocating space for it if
   necessary */
void
bow_treenode_set_classes_uniform (treenode *tn, int classes_capacity)
{
  int ci;
  if (tn->classes_capacity == 0)
    {
      tn->classes_capacity = classes_capacity;
      tn->classes = bow_malloc (classes_capacity * sizeof (double));
      tn->new_classes = bow_malloc (classes_capacity * sizeof (double));
    }
  assert (classes_capacity == tn->classes_capacity);
  for (ci = 0; ci < classes_capacity; ci++)
    {
      tn->classes[ci] = 1.0 / classes_capacity;
      tn->new_classes[ci] = 0.0;
    }
}

/* Normalize the NEW_CLASSES distribution, move it into the CLASSES array
   and zero the NEW_CLASSES array.  ALPHA is the parameter for the
   Dirichlet prior. */
void
bow_treenode_set_classes_from_new_classes (treenode *tn, double alpha)
{
  int ci;
  double total_classes_count = 0;

  assert (tn->classes_capacity > 0);

  for (ci = 0; ci < tn->classes_capacity; ci++)
    total_classes_count += tn->new_classes[ci];
  total_classes_count += alpha * tn->classes_capacity;

  if (total_classes_count == 0)
    {
      alpha = 1.0 / tn->classes_capacity;
      total_classes_count = 1.0;
    }

  for (ci = 0; ci < tn->classes_capacity; ci++)
    {
      tn->classes[ci] = (alpha + tn->new_classes[ci]) / total_classes_count;
      tn->new_classes[ci] = 0;
    }
}


/* Return the log-probability of node TN's WORD distribution having
   produced the word vector WV. */
double
bow_treenode_log_local_prob_of_wv (treenode *tn, bow_wv *wv)
{
  int wvi;
  double log_prob = 0;

  for (wvi = 0; wvi < wv->num_entries; wvi++)
    log_prob += wv->entry[wvi].count * log (tn->words[wv->entry[wvi].wi]);
  return log_prob;
}

/* Return the expected complete log likelihood of node TN's word
   distribution having produced the word vector WV. */
double
bow_treenode_complete_log_prob_of_wv (treenode *tn, bow_wv *wv)
{
  int wvi, ai;
  double log_prob = 0;
  treenode *ancestor;
  double *ancestor_membership;
  double ancestor_membership_normalizer;

  ancestor_membership = alloca ((tn->depth + 2) * sizeof (double));
  for (wvi = 0; wvi < wv->num_entries; wvi++)
    {
      ancestor_membership_normalizer = 0;
      for (ancestor = tn, ai = 0; ancestor; 
	   ancestor = ancestor->parent, ai++)
	{
	  ancestor_membership[ai] = (tn->lambdas[ai]
				     * ancestor->words[wv->entry[wvi].wi]);
	  ancestor_membership_normalizer += ancestor_membership[ai];
	}
      ancestor_membership[ai] = tn->lambdas[ai] * 1.0 / tn->words_capacity;
      ancestor_membership_normalizer += ancestor_membership[ai];

      for (ancestor = tn, ai = 0; ancestor; 
	   ancestor = ancestor->parent, ai++)
	{
	  log_prob += (wv->entry[wvi].count
		       * (ancestor_membership[ai]
			  / ancestor_membership_normalizer)
		       * log (ancestor->words[wv->entry[wvi].wi]));
	}
      log_prob += (wv->entry[wvi].count
		   * (ancestor_membership[ai]
		      / ancestor_membership_normalizer)
		   * log (1.0 / tn->words_capacity));
    }
  assert (log_prob == log_prob);
  return log_prob;
}

/* Return the probability of word WI in LEAF, using the hierarchical
   mixture */
double
bow_treenode_pr_wi (treenode *node, int wi)
{
  int i;
  treenode *ancestor;
  double ret = 0;

  if (node->children_count == 0)
    {
      /* NODE is a leaf.  Return the vertical mixture using shrinkage */
      for (ancestor = node, i = 0; 
	   ancestor; ancestor = ancestor->parent, i++)
	ret += node->lambdas[i] * ancestor->words[wi];
      /* Add in the uniform distribution */
      ret += node->lambdas[i] / node->words_capacity;
    }
  else
    {
      /* NODE is an interior node of the tree.  Return a weighted
	 average of the leaves under NODE. */
      double prior_sum = 0;
      treenode *iterator, *leaf;

      for (iterator = node;
	   (leaf = bow_treenode_iterate_leaves_under_node (&iterator, node)); )
	{
	  prior_sum += leaf->prior;
	  ret += leaf->prior * bow_treenode_pr_wi (leaf, wi);
	}
      ret /= prior_sum;
    }
  return ret;
}

/* Return the probability of word WI in node TN, but with the
   probability mass of document LOO_DI removed. */
double
bow_treenode_pr_wi_loo_local (treenode *tn, int wi, 
			      int loo_di, int loo_wvi)
{
  double ret;
  double denominator;

  /* If there is no LOO information, return the non-LOO estimate. */
  if (!(tn->di_loo) || !(tn->di_wvi_loo) || !(tn->di_wvi_loo[loo_di]))
    ret = tn->words[wi];
  else
    {
#if 0
      double foo1 = ((tn->words[wi] * tn->new_words_normalizer)
	      - tn->di_wvi_loo[loo_di][loo_wvi]);
      double foo2 = (tn->new_words_normalizer - tn->di_loo[loo_di]);
      if (foo1 < -1e-14)
	bow_error ("Foo1 %g orig %.18f minus %.18f", 
		   foo1,
		   (tn->words[wi] * tn->new_words_normalizer),
		   tn->di_wvi_loo[loo_di][loo_wvi]);
      assert (foo2 >= -1e-14);
#endif
      denominator = (tn->new_words_normalizer - tn->di_loo[loo_di]);
      assert (denominator >= 0);
      /* Make sure it is non-negative, but account for round-off error */
      assert ((tn->words[wi] * tn->new_words_normalizer)
	      - tn->di_wvi_loo[loo_di][loo_wvi] >= -1e-7);
      if (denominator)
	ret = (((tn->words[wi] * tn->new_words_normalizer)
		- tn->di_wvi_loo[loo_di][loo_wvi])
	       / denominator);
      else
	/* Without this document, there is no training data for this class.
	   Return a uniform distribution. */
	ret = 1.0 / tn->words_capacity;

      if (ret < 0)
	{
	  /* Account for roundoff error */
	  assert (ret > -1e-14);
	  ret = 0;
	}
    }
  assert (ret >= 0);
  assert (ret <= 1);
  assert (tn);			/* to keep tn available in debugger */
  assert (loo_di >= 0 && loo_wvi >= 0);
  return ret;
}

/* Return the probability of word WI in LEAF, using the hierarchical
   mixture, but with the probability mass of document LOO_DI removed. */
double
bow_treenode_pr_wi_loo (treenode *tn, int wi,
		    int loo_di, int loo_wvi)
{
  int i;
  treenode *ancestor;
  double ret = 0;

  if (tn->children_count == 0)
    {
      /* TN is a leaf.  Return the vertical mixture using shrinkage */
      for (ancestor = tn, i = 0; 
	   ancestor; ancestor = ancestor->parent, i++)
	ret += (tn->lambdas[i] * 
		bow_treenode_pr_wi_loo_local (ancestor, wi, loo_di, loo_wvi));
      /* Add in the uniform distribution */
      ret += tn->lambdas[i] / tn->words_capacity;
    }
  else
    {
      /* TN is an interior node of the tree.  Return a weighted
	 average of the leaves under TN. */
      double prior_sum = 0;
      treenode *iterator, *leaf;

      for (iterator = tn;
	   (leaf = bow_treenode_iterate_leaves_under_node (&iterator, tn)); )
	{
	  prior_sum += leaf->prior;
	  ret += leaf->prior * bow_treenode_pr_wi_loo (leaf, wi, 
						       loo_di, loo_wvi);
	}
      ret /= prior_sum;
    }
  assert (ret > 0);
  assert (ret < 1);
  return ret;
}


/* Return the prior probability of TN being in a path selected for
   generating a document. */
double
bow_treenode_prior (treenode *tn)
{
  if (tn->children_count != 0)
    {
      /* TN is an interior node of the tree; sum the priors of the
	 leaves under it. */
      treenode *iterator, *leaf;
      double ret = 0;
      for (iterator = tn;
	   (leaf = bow_treenode_iterate_leaves_under_node (&iterator, tn)); )
	{
	  ret += leaf->prior;
	}
      return ret;
    }

  /* TN is a leaf; simply return its prior. */
  return tn->prior;
}

/* Return the log-probability of node TN's WORD distribution having
   produced the word vector WV. */
double
bow_treenode_log_prob_of_wv (treenode *tn, bow_wv *wv)
{
  int wvi;
  double log_prob = 0;

  for (wvi = 0; wvi < wv->num_entries; wvi++)
    log_prob += (wv->entry[wvi].count
		 * log (bow_treenode_pr_wi (tn, wv->entry[wvi].wi)));
  return log_prob;
}

/* Same as above, but return a probability instead of a log-probability */
double
bow_treenode_prob_of_wv (treenode *tn, bow_wv *wv)
{
  return exp (bow_treenode_log_prob_of_wv (tn, wv));
}

/* Return the log-probability of node TN's WORD distribution having
   produced the word vector WV, but with document DI removed from TN's
   WORD distribution. */
double
bow_treenode_log_prob_of_wv_loo (treenode *tn, bow_wv *wv, int loo_di)
{
  int wvi;
  double log_prob = 0;

  for (wvi = 0; wvi < wv->num_entries; wvi++)
    log_prob += (wv->entry[wvi].count
		 * log (bow_treenode_pr_wi_loo (tn, wv->entry[wvi].wi, 
					    loo_di, wvi)));
  assert (log_prob < 0);
  return log_prob;
}

/* Return the local log-probability of node TN's WORD distribution
   having produced the word vector WV, but with document DI removed
   from TN's WORD distribution. */
double
bow_treenode_log_local_prob_of_wv_loo (treenode *tn, bow_wv *wv, int loo_di)
{
  int wvi;
  double log_prob = 0;
  
  for (wvi = 0; wvi < wv->num_entries; wvi++)
    log_prob += (wv->entry[wvi].count
		 * log (bow_treenode_pr_wi_loo_local (tn, wv->entry[wvi].wi, 
						  loo_di, wvi)));
  assert (log_prob < 0);
  return log_prob;
}

/* Return the number of leaves under (and including) TN */
int
bow_treenode_leaf_count (treenode *tn)
{
  if (tn->children_count == 0)
    return 1;
  else
    {
      int ci, lc = 0;
      for (ci = 0; ci < tn->children_count; ci++)
	lc += bow_treenode_leaf_count (tn->children[ci]);
      return lc;
    }
}

/* Return the number of tree nodes under (and including) TN */
int
bow_treenode_node_count (treenode *tn)
{
  if (tn->children_count == 0)
    return 1;
  else
    {
      int ci, lc = 0;
      for (ci = 0; ci < tn->children_count; ci++)
	lc += bow_treenode_node_count (tn->children[ci]);
      /* Plus one for TN itself. */
      return lc + 1;
    }
}

/* Return an array of words with their associated likelihood ratios,
   calculated relative to its siblings. */
bow_wa *
bow_treenode_word_likelihood_ratios (treenode *tn)
{
  int wi, ci;
  bow_wa *wa;
  double pr_wi_given_tn;
  double pr_wi_given_not_tn;
  double lr;

  if (tn->parent == NULL)
    return NULL;

  wa = bow_wa_new (tn->words_capacity+2);
  for (wi = 0; wi < tn->words_capacity; wi++)
    {
      pr_wi_given_tn = bow_treenode_pr_wi (tn, wi);
      pr_wi_given_not_tn = 0;
      for (ci = 0; ci < tn->parent->children_count; ci++)
	{
	  if (ci != tn->ci_in_parent)
	    pr_wi_given_not_tn += 
	      (bow_treenode_pr_wi (tn->parent->children[ci], wi)
	       / (tn->parent->children_count - 1));
	}
      if (pr_wi_given_tn == 0)
	lr = -1;
      else if (pr_wi_given_not_tn == 0)
	lr = 1;
      else
	lr = (pr_wi_given_tn 
	      * log (pr_wi_given_tn / pr_wi_given_not_tn));
      //assert (lr < 1);
      bow_wa_append (wa, wi, lr);
    }
  return wa;
}

/* Return an array of words with their associated likelihood ratios,
   calculated relative to all the leaves. */
bow_wa *
bow_treenode_word_leaf_likelihood_ratios (treenode *tn)
{
  int wi, leaf_count;
  bow_wa *wa;
  double pr_wi_given_tn;
  double pr_wi_given_not_tn;
  double lr;
  treenode *root, *iterator, *leaf;

  if (tn->children_count != 0)
    return NULL;

  root = tn;
  while (root->parent)
    root = root->parent;
  leaf_count = bow_treenode_leaf_count (root);

  wa = bow_wa_new (tn->words_capacity+2);
  for (wi = 0; wi < tn->words_capacity; wi++)
    {
      //pr_wi_given_tn = tn->words[wi];
      pr_wi_given_tn = bow_treenode_pr_wi (tn, wi);
      pr_wi_given_not_tn = 0;
      for (iterator = root; (leaf = bow_treenode_iterate_leaves (&iterator)); )
	{
	  if (leaf != tn)
	    pr_wi_given_not_tn += (bow_treenode_pr_wi (leaf, wi)
				   / (leaf_count-1));
	  //pr_wi_given_not_tn += leaf->words[wi] / (leaf_count - 1);
	}
      if (pr_wi_given_tn == 0)
	lr = -1;
      else if (pr_wi_given_not_tn == 0)
	lr = 1;
      else
	lr = (pr_wi_given_tn 
	      * log (pr_wi_given_tn / pr_wi_given_not_tn));
      //assert (lr < 1);
      bow_wa_append (wa, wi, lr);
    }
  return wa;
}

/* Return an array of words with their associated odds ratios,
   calculated relative to all the leaves. */
bow_wa *
bow_treenode_word_leaf_odds_ratios (treenode *tn)
{
  int wi, leaf_count;
  bow_wa *wa;
  double pr_wi_given_tn;
  double pr_wi_given_not_tn;
  double lr;
  treenode *root, *iterator, *leaf;

  if (tn->children_count != 0)
    return NULL;

  root = tn;
  while (root->parent)
    root = root->parent;
  leaf_count = bow_treenode_leaf_count (root);

  wa = bow_wa_new (tn->words_capacity+2);
  for (wi = 0; wi < tn->words_capacity; wi++)
    {
      pr_wi_given_tn = tn->words[wi];
      pr_wi_given_not_tn = 0;
      for (iterator = root; (leaf = bow_treenode_iterate_leaves (&iterator)); )
	{
	  if (leaf != tn)
	    pr_wi_given_not_tn += leaf->words[wi] / (leaf_count - 1);
	}
      lr = (/* pr_wi_given_tn * */
	    log ((pr_wi_given_tn * (1 - pr_wi_given_not_tn))
		 / (pr_wi_given_not_tn * (1 - pr_wi_given_tn))));
      bow_wa_append (wa, wi, lr);
    }
  return wa;
}

/* Return an array of words with their associated likelihood ratios,
   calculated relative to all the leaves. */
bow_wa *
bow_treenode_word_leaf_mean_ratios (treenode *tn)
{
  int wi, leaf_count;
  bow_wa *wa;
  double pr_wi_given_tn;
  double pr_wi;
  double lr;
  treenode *root, *iterator, *leaf;

  if (tn->children_count != 0)
    return NULL;

  root = tn;
  while (root->parent)
    root = root->parent;
  leaf_count = bow_treenode_leaf_count (root);

  wa = bow_wa_new (tn->words_capacity+2);
  for (wi = 0; wi < tn->words_capacity; wi++)
    {
      pr_wi_given_tn = tn->words[wi];
      pr_wi = 0;
      for (iterator = root; (leaf = bow_treenode_iterate_leaves (&iterator)); )
	{
	  pr_wi += leaf->words[wi] / leaf_count;
	}
      assert (pr_wi > 0);
      lr = pr_wi_given_tn / pr_wi;
      bow_wa_append (wa, wi, lr);
    }
  return wa;
}

/* Print the NUM_TO_PRINT words with highest likelihood ratios,
   calculated relative to its siblings. */
void
bow_treenode_word_likelihood_ratios_print (treenode *tn, int num_to_print)
{
  bow_wa *wa;

  wa = bow_treenode_word_likelihood_ratios (tn);
  if (wa)
    {
      bow_wa_sort (wa);
      bow_wa_fprintf (wa, stdout, num_to_print);
      bow_wa_free (wa);
    }
}

/* Print the NUM_TO_PRINT words with highest likelihood ratios,
   calculated relative to all the leaves. */
void
bow_treenode_word_leaf_likelihood_ratios_print (treenode *tn, int num_to_print)
{
  bow_wa *wa;

  wa = bow_treenode_word_leaf_likelihood_ratios (tn);
  if (wa)
    {
      bow_wa_sort (wa);
      bow_wa_fprintf (wa, stdout, num_to_print);
      bow_wa_free (wa);
    }
}

/* Print the NUM_TO_PRINT words with highest odds ratios,
   calculated relative to all the leaves. */
void
bow_treenode_word_leaf_odds_ratios_print (treenode *tn, int num_to_print)
{
  bow_wa *wa;

  wa = bow_treenode_word_leaf_odds_ratios (tn);
  if (wa)
    {
      bow_wa_sort (wa);
      bow_wa_fprintf (wa, stdout, num_to_print);
      bow_wa_free (wa);
    }
}

/* Same as above, for all nodes in the tree. */
void
bow_treenode_word_likelihood_ratios_print_all (treenode *tn, int num_to_print)
{
  int ci;

  printf ("%s\nprior=%g\n", tn->name, tn->prior);
  bow_treenode_word_likelihood_ratios_print (tn, num_to_print);
  for (ci = 0; ci < tn->children_count; ci++)
    bow_treenode_word_likelihood_ratios_print_all (tn->children[ci], 
						   num_to_print);
}

/* Return a bow_wa array of words with their associated probabilities */
bow_wa *
bow_treenode_word_probs (treenode *tn)
{
  int wi;
  bow_wa *wa;

  wa = bow_wa_new (tn->words_capacity+2);
  for (wi = 0; wi < tn->words_capacity; wi++)
    bow_wa_append (wa, wi, tn->words[wi]);
  return wa;
}

/* Print the NUM_TO_PRINT words with highest probability */
void
bow_treenode_word_probs_print (treenode *tn, int num_to_print)
{
  bow_wa *wa;

  wa = bow_treenode_word_probs (tn);
  if (wa)
    {
      bow_wa_sort (wa);
      bow_wa_fprintf (wa, stdout, num_to_print);
      bow_wa_free (wa);
    }
}

/* Same as above, for all nodes in the tree. */
void
bow_treenode_word_probs_print_all (treenode *tn, int num_to_print)
{
  int ci;

  printf ("%s\n", tn->name);
  if (tn->children_count == 0)
    printf ("  prior=%g\n", tn->prior);
  bow_treenode_word_probs_print (tn, num_to_print);
  for (ci = 0; ci < tn->children_count; ci++)
    bow_treenode_word_probs_print_all (tn->children[ci], num_to_print);
}

/* Print most probable words in one line, and only if parent's
   WKL is high enough */
void
bow_treenode_keywords_print (treenode *tn, FILE *fp)
{
  bow_wa *wa;
  int wai;
  //double kldiv;

  if (tn->parent == NULL)
    return;

  //if (bow_treenode_children_weighted_kl_div (tn->parent) < 500) return;

#if 0
  if ((kldiv = bow_treenode_pair_kl_div (tn, tn->parent)) < 0.5)
    {
      fprintf (fp, "alias %s %s\n", 
	       tn->name, tn->parent->name);
      bow_verbosify (bow_progress, "%s kldiv versus parent %g SKIP\n",
		     tn->name, kldiv);
      return;
    }
  else
    {
      bow_verbosify (bow_progress, "%s kldiv versus parent %g\n",
		     tn->name, kldiv);
    }

  for (ci = 0; ci < tn->ci_in_parent; ci++)
    {
      if (((kldiv = bow_treenode_pair_kl_div
	    (tn, tn->parent->children[ci]))
	   < 0.5))
	{
	  fprintf (fp, "alias %s %s\n", 
		   tn->name, tn->parent->children[ci]->name);
	  bow_verbosify (bow_progress, "%s %s kldiv versus sibling %g SKIP\n",
			 tn->name, tn->parent->children[ci]->name, kldiv);
	  return;
	}
      else
	{
	  bow_verbosify (bow_progress, "%s %s kldiv versus sibling %g\n",
			 tn->name, tn->parent->children[ci]->name, kldiv);
	}
    }
#endif

  wa = bow_treenode_word_probs (tn);
  if (wa)
    {
      fprintf (fp, "%s %g ", tn->name, 
	       bow_treenode_pair_kl_div (tn, tn->parent));
      bow_wa_sort (wa);
      for (wai = 0; wai < 10; wai++)
	fprintf (fp, "%s ", bow_int2word (wa->entry[wai].wi));
      fprintf (fp, "\n");
      bow_wa_free (wa);
    }
}

/* Same as above, but for TN and all treenodes under TN */
void
bow_treenode_keywords_print_all (treenode *tn, FILE *fp)
{
  int ci;

  bow_treenode_keywords_print (tn, fp);
  for (ci = 0; ci < tn->children_count; ci++)
    bow_treenode_keywords_print_all (tn->children[ci], fp);
}

/* Print the (normalized) probability of word WI in each of the nodes
   of the tree rooted at ROOT. */
void
bow_treenode_normalized_word_prob_all_print (treenode *root, int wi)
{
  int leaf_count;
  double *nodes, nodes_total;
  int ni;
  treenode *iterator, *node;

  leaf_count = bow_treenode_leaf_count (root);
  nodes = alloca (sizeof (double) * leaf_count);
  nodes_total = 0;
  for (iterator = root, ni = 0;
       (node=bow_treenode_iterate_all (&iterator)); 
       ni++)
    {
      nodes[ni] = node->words[wi];
      nodes_total += nodes[ni];
    }
  for (iterator = root, ni = 0;
       (node=bow_treenode_iterate_all (&iterator)); 
       ni++)
    printf ("%10f %s\n", nodes[ni] / nodes_total, node->name);
}

/* Print the word distribution for each leaf to a separate file, each
   file having prefix FILENAME_PREFIX.  Use vertical mixture if
   SHRINKAGE is non-zero. */
void
bow_treenode_print_all_word_probabilities_all (const char *filename_prefix,
					       int shrinkage)
{
  int li, wi;
  char *s;
  treenode *iterator, *leaf;
  char leafname[BOW_MAX_WORD_LENGTH];
  char filename[BOW_MAX_WORD_LENGTH];
  FILE *fp;
  double pr_w;

  bow_verbosify (bow_progress, "Starting word probability printing\n");

  for (iterator = crossbow_root, li = 0;
       (leaf = bow_treenode_iterate_leaves (&iterator)); 
       li++)
    {
      strcpy (leafname, leaf->name);
      /* Convert '/' to '-' */
      for (s = leafname; *s; s++)
	if (*s == '/')
	  *s = '-';
      sprintf (filename, "%s-%s", filename_prefix, leafname);
      fp = bow_fopen (filename, "w");
      for (wi = 0; wi < leaf->words_capacity; wi++)
	{
	  if (shrinkage)
	    pr_w = bow_treenode_pr_wi (leaf, wi);
	  else
	    pr_w = leaf->words[wi];
	  fprintf (fp, "%f %s\n", pr_w, bow_int2word (wi));
	}
      fclose (fp);
    }
}


/* Return the "KL Divergence to the Mean" among the children of TN */
double
bow_treenode_children_kl_div (treenode *tn)
{
  double *mean;
  double kldiv;
  int wi, ci;

  if (tn->children_count < 2)
    return 0;

  /* Calculate the mean distribution */
  mean = bow_malloc (tn->words_capacity * sizeof (double));
  for (wi = 0; wi < tn->words_capacity; wi++)
    {
      mean[wi] = 0;
      for (ci = 0; ci < tn->children_count; ci++)
	mean[wi] += tn->children[ci]->words[wi];
      mean[wi] /= tn->children_count;
    }

  /* Calculate "KL Divergence to the Mean" for each child. */
  kldiv = 0;
  for (ci = 0; ci < tn->children_count; ci++)
    {
      for (wi = 0; wi < tn->words_capacity; wi++)
	{
	  /* Testing for tn->children[ci]->words[wi] is legitimate.
	     Testing for mean[wi] is a concession to round-off error */
	  if (tn->children[ci]->words[wi] && mean[wi])
	    kldiv += (tn->children[ci]->words[wi]
		      * log (tn->children[ci]->words[wi] / mean[wi]));
	  //assert (kldiv < 10);
	}
    }
  bow_free (mean);
  kldiv /= tn->children_count;
  return kldiv;
}

/* Return the weighted "KL Divergence to the mean among the children
   of TN" multiplied by the number of words of training data in the
   children. */
double
bow_treenode_children_weighted_kl_div (treenode *tn)
{
  double weight = 0;
  int ci;

  for (ci = 0; ci < tn->children_count; ci++)
    weight += tn->children[ci]->new_words_normalizer;
  return weight * bow_treenode_children_kl_div (tn);
}

/* Return the "KL Divergence to the mean" between TN1 and TN2. */
double
bow_treenode_pair_kl_div (treenode *tn1, treenode *tn2)
{
  double *mean;
  double kldiv;
  int wi;

  /* Calculate the mean distribution */
  mean = bow_malloc (tn1->words_capacity * sizeof (double));
  for (wi = 0; wi < tn1->words_capacity; wi++)
    {
      mean[wi] = 0;
      mean[wi] += tn1->words[wi];
      mean[wi] += tn2->words[wi];
      mean[wi] /= 2;
    }

  /* Calculate "KL Divergence to the Mean" for each one. */
  kldiv = 0;
  for (wi = 0; wi < tn1->words_capacity; wi++)
    {
      /* Testing for tn->children[ci]->words[wi] is legitimate.
	 Testing for mean[wi] is a concession to round-off error */
      if (mean[wi])
	{
	  if (tn1->words[wi])
	    kldiv += tn1->words[wi] * log (tn1->words[wi] / mean[wi]);
	  if (tn2->words[wi])
	    kldiv += tn2->words[wi] * log (tn2->words[wi] / mean[wi]);
	}
    }
  bow_free (mean);
  kldiv /= 2;
  return kldiv;
}

/* Same as above, but multiply by the number of words in TN1 and TN2. */
double
bow_treenode_pair_weighted_kl_div (treenode *tn1, treenode *tn2)
{
  return ((tn1->new_words_normalizer + tn2->new_words_normalizer)
	  * bow_treenode_pair_kl_div (tn1, tn2));
}

/* Return non-zero if any of TN's children are leaves */
int
bow_treenode_is_leaf_parent (treenode *tn)
{
  int ci;

  for (ci = 0; ci < tn->children_count; ci++)
    if (tn->children[ci]->children_count == 0)
      return 1;
  return 0;
}
