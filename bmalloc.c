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


#define _BOW_MALLOC_INLINE_EXTERN

#include <bow/libbow.h>
/* This will compile the `malloc' functions without "inline extern" */

#if BOW_MCHECK

#include <mcheck.h>

#define ptrs_size 100000
static int ptrs_length = 0;
static void *ptrs[ptrs_size];

void
bow_malloc_check_all ()
{
  int i;
  enum mcheck_status status;

  for (i = 0; i < ptrs_length; i++)
    {
      if (ptrs[i])
	{
	  status = mprobe (ptrs[i]);
	  if (status != MCHECK_OK)
	    bow_error ("Bad heap");
	}
    }
}

void
_bow_malloc_hook (void *ptr)
{
  assert (ptrs_length < ptrs_size);
  ptrs[ptrs_length++] = ptr;
  bow_malloc_check_all ();
}

void
_bow_realloc_hook (void *old, void *new)
{
  int i;
  assert (ptrs_length < ptrs_size);
  for (i = 0; i < ptrs_length && ptrs[i] != old; i++);
  if (ptrs[i] == old)
    ptrs[i] = 0;
  ptrs[ptrs_length++] = new;
  bow_malloc_check_all ();
}

void
_bow_free_hook (void *ptr)
{
  int i;
  for (i = 0; i < ptrs_length; i++)
    {
      if (ptrs[i] == ptr)
	{
	  ptrs[i] = 0;
	  break;
	}
    }
  bow_malloc_check_all ();
}

void (*bow_malloc_hook) (void *ptr) = _bow_malloc_hook;
void (*bow_realloc_hook) (void *old, void *new) = _bow_realloc_hook;
void (*bow_free_hook) (void *ptr) = _bow_free_hook;

#else /* BOW_MCHECK */

void (*bow_malloc_hook) (void *ptr) = NULL;
void (*bow_realloc_hook) (void *old, void *new) = NULL;
void (*bow_free_hook) (void *ptr) = NULL;

#endif /* BOW_MCHECK */
