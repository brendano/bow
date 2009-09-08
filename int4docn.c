/* A convient interface to int4str.c, specifically for document names. */

/* Copyright (C) 1997 Andrew McCallum

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

static bow_int4str *doc_map = NULL;

const char *
bow_int2docname (int index)
{
  if (!doc_map)
    bow_error ("No documents yet added to the int-docname mapping.\n");
  return bow_int2str (doc_map, index);
}

int
bow_docname2int (const char *docname)
{
  if (!doc_map)
    doc_map = bow_int4str_new (0);
  return bow_str2int (doc_map, docname);
}

int
bow_num_docnames ()
{
  return doc_map->str_array_length;
}

void
bow_docnames_write (FILE *fp)
{
  bow_int4str_write (doc_map, fp);
}

void
bow_docnames_read_from_fp (FILE *fp)
{
  if (doc_map)
    bow_int4str_free (doc_map);
  doc_map = bow_int4str_new_from_fp (fp);
}
