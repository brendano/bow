/* A trie for testing membership in a list of lowercase alphabetic
   strings (e.g. a stoplist). */

/* Copyright (C) 200 Andrew McCallum

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
#include <stdlib.h>
#include <ctype.h>		/* for islower */

/* Return a new strtrie */
bow_strtrie *
bow_strtrie_new ()
{
  int i;
  bow_strtrie *strie = bow_malloc (sizeof (bow_strtrie));
  for (i = 0; i < 27; i++)
    strie->next[i] = NULL;
  return strie;
}

/* Add the string STR to the trie STRIE */
void
bow_strtrie_add (bow_strtrie *strie, const char *str)
{
  int index;
  assert (str && *str);
  while (*str)
    {
      assert (islower (*str));
      index = (unsigned)(*str) - 'a';
      if (strie->next[index] == NULL)
	strie->next[index] = bow_strtrie_new ();
      str++;
      strie = strie->next[index];
    }
  strie->next[26] = (void*)1;
}

/* Return non-zero if the string STR is present in the trie STRIE */
int
bow_strtrie_present (bow_strtrie *strie, const char *str)
{
  int index;
  //assert (str && *str);
  while (*str)
    {
      if (!islower (*str))
	return 0;
      index = (unsigned)(*str) - 'a';
      if (strie->next[index] == NULL)
	return 0;
      str++;
      strie = strie->next[index];
    }
  return (strie->next[26] == (void*)1);
}

/* Free the memory occupied by STRIE */
void
bow_strtrie_free (bow_strtrie *strie)
{
  int i;
  for (i = 0; i < 26; i++)
    {
      if (strie->next[i] != NULL)
	bow_strtrie_free (strie->next[i]);
    }
  bow_free (strie);
}
