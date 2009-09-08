/* Input and ouput functions */

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

#define _BOW_IO_INLINE_EXTERN

#include <bow/libbow.h>
/* This will compile the `io' functions without "inline extern" */

int bow_file_format_version = BOW_DEFAULT_FILE_FORMAT_VERSION;

/* History of file format versions:

   Before version 4:
   In Rainbow, write bow_file_format_version to disk in a separate file.

   Before version 5:
   Changed bow_cdoc.class, bow_de.di, bow_de.count from short to int.

   */

void
bow_write_format_version_to_file (const char *filename)
{
  FILE *fp;

  fp = bow_fopen (filename, "w");
  fprintf (fp, "%d\n", bow_file_format_version);
  fclose (fp);
}

void
bow_read_format_version_from_file (const char *filename)
{
  FILE *fp;
  int got_it;

  fp = bow_fopen (filename, "r");
  got_it = fscanf (fp, "%d\n", &bow_file_format_version);
  fclose (fp);
  if (got_it != 1)
    bow_error ("Failed to read bow_file_format_version from %s", filename);
}
