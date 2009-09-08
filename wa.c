/* An array of word indices, each associated with a floating point number. 
   Useful for lists of words by information gain, etc. */

/* Copyright (C) 1998, 1999 Andrew McCallum

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
#include <stdlib.h>		/* for qsort() */

/* Create a new, empty array of word/score entries, with CAPACITY entries. */
bow_wa *
bow_wa_new (int capacity)
{
  bow_wa *ret;

  if (capacity == 0)
    capacity = 8;		/* default */
  ret = bow_malloc (sizeof (bow_wa));
  ret->entry = bow_malloc (sizeof (bow_ws) * capacity);
  ret->size = capacity;
  ret->length = 0;
  return ret;
}

/* Add a new word and score to the array. */
int
bow_wa_append (bow_wa *wa, int wi, float score)
{
  if (wa->length + 1 >= wa->size)
    {
      wa->size *= 2;
      wa->entry = bow_realloc (wa->entry, wa->size * sizeof (bow_ws));
    }
  wa->entry[wa->length].wi = wi;
  wa->entry[wa->length].weight = score;
  wa->length++;
  assert (wa->length < wa->size);
  return wa->length;
}

/* Add a score to the array.  If there is already an entry for WI, the
   SCORE gets added to WI's current score.  If WI is not already in
   the array, then this function behaves like bow_wa_append(). */
int
bow_wa_add (bow_wa *wa, int wi, float score)
{
  int i;
  for (i = 0; i < wa->length; i++)
    {
      if (wa->entry[i].wi == wi)
	{
	  wa->entry[i].weight += score;
	  goto add_done;
	}
    }
  bow_wa_append (wa, wi, score);
 add_done:
  return wa->length;
}

/* Add a score to the array.  If there is already an entry for WI at
   the end, the SCORE gets added to WI's current score.  If WI is
   greater than the WI at the end, then this function behaves like
   bow_wa_append(), otherwise an error is raised. */
int
bow_wa_add_to_end (bow_wa *wa, int wi, float score)
{
  int last_i = wa->length - 1;
  if (wa->length == 0
      || wa->entry[last_i].wi < wi)
    {
      bow_wa_append (wa, wi, score);
    }
  else
    {
      assert (wa->entry[wa->length-1].wi == wi);
      wa->entry[last_i].weight += score;
    }
  return wa->length;
}

/* Remove the entry corresponding to word WI.  Return the new length
   of the word array. */
int
bow_wa_remove (bow_wa *wa, int wi)
{
  int wai;
  for (wai = 0; wai < wa->length; wai++)
    if (wa->entry[wai].wi == wi)
      break;
  if (wai < wa->length)
    wa->length--;
  else
    return wa->length;
  for ( ; wai < wa->length; wai++)
    wa->entry[wai] = wa->entry[wai+1];
  return wa->length;
}

int
bow_wa_weight (bow_wa *wa, int wi, float *weight)
{
  int i;
  for (i = 0; i < wa->length; i++)
    {
      if (wa->entry[i].wi == wi)
	{
	  *weight = wa->entry[i].weight;
	  return 1;
	}
    }
  return 0;
}

/* Add to WA all the WI/WEIGHT entries from WA2.  Uses bow_wa_add(). */
int
bow_wa_union (bow_wa *wa, bow_wa *wa2)
{
  int i;
  for (i = 0; i < wa2->length; i++)
    bow_wa_add (wa, wa2->entry[i].wi, wa2->entry[i].weight);
  return wa->length;
}

/* Return a new array containing only WI entries that are in both 
   WA1 and WA2. */
bow_wa *
bow_wa_intersection (bow_wa *wa1, bow_wa *wa2)
{
  int i;
  float weight1;
  bow_wa *ret = bow_wa_new (0);
  for (i = 0; i < wa2->length; i++)
    if (bow_wa_weight (wa1, wa2->entry[i].wi, &weight1))
      bow_wa_add (ret, wa2->entry[i].wi, wa2->entry[i].weight + weight1);
  return ret;
}

/* Add weights to WA1 for those entries appearing in WA2 */
int
bow_wa_overlay (bow_wa *wa1, bow_wa *wa2)
{
  int i;
  float weight2;
  for (i = 0; i < wa1->length; i++)
    if (bow_wa_weight (wa2, wa1->entry[i].wi, &weight2))
      wa1->entry[i].weight += weight2;
  return wa1->length;
}

/* Return a new array containing only WI entries that are in WA1 but
   not in WA2. */
bow_wa *
bow_wa_diff (bow_wa *wa1, bow_wa *wa2)
{
  int i;
  float weight;
  bow_wa *ret = bow_wa_new (0);
  for (i = 0; i < wa1->length; i++)
    if (!bow_wa_weight (wa2, wa1->entry[i].wi, &weight))
      bow_wa_add (ret, wa1->entry[i].wi, wa1->entry[i].weight);
  return ret;
}

static int
compare_wa_high_first (const void *ws1, const void *ws2)
{
  if (((bow_ws*)ws1)->weight > ((bow_ws*)ws2)->weight)
    return -1;
  else if (((bow_ws*)ws1)->weight == ((bow_ws*)ws2)->weight)
    return 0;
  else
    return 1;
}

static int
compare_wa_high_last (const void *ws1, const void *ws2)
{
  if (((bow_ws*)ws1)->weight < ((bow_ws*)ws2)->weight)
    return -1;
  else if (((bow_ws*)ws1)->weight == ((bow_ws*)ws2)->weight)
    return 0;
  else
    return 1;
}

/* Sort the word array. */
void
bow_wa_sort (bow_wa *wa)
{
  qsort (wa->entry, wa->length, sizeof (bow_ws), compare_wa_high_first);
}

void
bow_wa_sort_reverse (bow_wa *wa)
{
#if 0
  int wai1, wai2;
  bow_ws ws;
  bow_wa_sort (wa);
  for (wai1 = 0, wai2 = wa->length-1; wai1 < wai2; wai1++, wai2--)
    {
      ws = wa->entry[wai1];
      wa->entry[wai1] = wa->entry[wai2];
      wa->entry[wai2] = ws;
    }
#endif
  qsort (wa->entry, wa->length, sizeof (bow_ws), compare_wa_high_last);
}

/* Print the first N entries of the word array WA to stream FP. */
void
bow_wa_fprintf (bow_wa *wa, FILE *fp, int n)
{
  int i;

  if (n > wa->length || n < 0)
    n = wa->length;
  for (i = 0; i < n; i++)
    fprintf (fp, "%20.10f %s\n",
	     wa->entry[i].weight,
	     bow_int2word (wa->entry[i].wi));
}

/* Remove all entries from the word array */
void
bow_wa_empty (bow_wa *wa)
{
  wa->length = 0;
}

/* Free the word array */
void
bow_wa_free (bow_wa *wa)
{
  bow_free (wa->entry);
  bow_free (wa);
}
