/* Define and set the default lexer. */

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

bow_flex_type bow_flex_option = USE_STANDARD_LEXER;

/* The default lexer used by all library functions. */
/* NOTE: Be sure to set this to a value, otherwise some linkers (like
   SunOS's) will not actually include this .o file in the executable,
   and then _bow_default_lexer() will not get called, and then the
   lexer's will not get initialized properly.  Ug. */
bow_lexer *bow_default_lexer = (void*)-1;
bow_lexer _bow_default_lexer;

bow_lexer_parameters *bow_default_lexer_parameters;
bow_lexer_parameters _bow_default_lexer_parameters;

void _bow_default_lexer_init ()  __attribute__ ((constructor));

void
_bow_default_lexer_init ()
{
  static int done = 0;

  if (done)
    return;
  done = 1;

  assert (sizeof(typeof(*bow_simple_lexer)) == sizeof(bow_lexer));
  bow_default_lexer = &_bow_default_lexer;
  memcpy (bow_default_lexer, bow_simple_lexer, 
	  sizeof(typeof(*bow_simple_lexer)));

  assert (sizeof(typeof(*bow_alpha_lexer_parameters)) == 
	  sizeof(bow_lexer_parameters));
  bow_default_lexer_parameters = &_bow_default_lexer_parameters;
  memcpy (bow_default_lexer_parameters, bow_alpha_lexer_parameters,
	  sizeof(typeof(*bow_alpha_lexer_parameters)));
}
