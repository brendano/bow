/* Managing all the word-vector weighting/scoring methods
   Copyright (C) 1997, 1998, 1999 Andrew McCallum

   Written by:  Andrew Kachites McCallum <mccallum@jprc.com>

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
#include <argp/argp.h>

#define METHOD_ARGP_GROUP_OFFSET 2000

bow_sarray *bow_methods = NULL;

/* Associate method M with the string NAME, so the method structure can
   be retrieved later with BOW_METHOD_AT_NAME(). */
int
bow_method_register_with_name (bow_method *m, const char *name, int size,
			       struct argp_child *child)
{
  int method_number;
  if (!bow_methods)
    {
      /* xxx this should not be fixed at sizeof (rainbow_method) ! */
      bow_methods = bow_sarray_new (0, size, 0);
    }
  assert (bow_methods->array->entry_size == size);
  method_number = bow_sarray_add_entry_with_keystr (bow_methods, m, name);
  if (child)
    child->argp->options[0].group = METHOD_ARGP_GROUP_OFFSET + method_number;
  return method_number;
}

/* Return a pointer to a method structure that was previous registered 
   with string NAME using BOW_METHOD_REGISTER_WITH_NAME(). */
bow_method *
bow_method_at_name (const char *name)
{
  if (!bow_methods)
    bow_error ("methods not yet initialized");
  return bow_sarray_entry_at_keystr (bow_methods, name);
}

/* Return a pointer to a method structure that was assigned index ID
   when previously registered with string NAME using
   BOW_METHOD_REGISTER_WITH_NAME(). */
bow_method *
bow_method_at_index (int id)
{
  if (!bow_methods)
    bow_error ("methods not yet initialized");
  return bow_sarray_entry_at_index (bow_methods, id);
}
