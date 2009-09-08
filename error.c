/* Error-printing and verbosifying functions

   Copyright (C) 1997, 1998 Andrew McCallum

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

#include <stdarg.h>
#include <bow/libbow.h>
#include <stdio.h>

/* Examined by bow_verbosify() to determine whether to print the message.
   Default is bow_progress. */
int bow_verbosity_level = bow_progress;

/* If this is 0, and the message passed to bow_verbosify() contains
   backspaces, then the message will not be printed.  It is useful to
   turn this off when debugging inside an emacs window.  The default
   value is on. */
int bow_verbosity_use_backspace = 1;

/* Print the printf-style FORMAT string and ARGS on STDERR, only if
   the BOW_VERBOSITY_LEVEL is equal or greater than the argument 
   VERBOSITY_LEVEL. */
int
bow_verbosify (int verbosity_level, const char *format, ...)
{
  int ret;
  va_list ap;

  if ((bow_verbosity_level < verbosity_level)
      || (!bow_verbosity_use_backspace 
	  && strchr (format, '\b')))
    return 0;

  va_start (ap, format);
  ret = vfprintf (stderr, format, ap);
  va_end (ap);
  fflush (stderr);

  return ret;
}

volatile void
_bow_error (const char *format, ...)
{
  va_list ap;

  va_start (ap, format);
  vfprintf (stderr, format, ap);
  va_end (ap);
  fputc ('\n', stderr);
  fflush (stderr);

#if __linux__
  abort ();
#else
  exit (-1);
#endif
}

