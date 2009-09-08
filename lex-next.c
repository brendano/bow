/* Implementation of helping functions for lexers that use a nested,
   underlying lexer. */
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

/* Open the underlying lexer. */
bow_lex *
bow_lexer_next_open_text_fp (bow_lexer *self, FILE *fp,
			     const char *filename)
{
  assert (self->next);
  return self->next->open_text_fp (self->next, fp, filename);
}

/* Open the underlying lexer. */
bow_lex *
bow_lexer_next_open_str (bow_lexer *self, char *buf)
{
  assert (self->next);
  return self->next->open_str (self->next, buf);
}

/* Get a word using the next lexer */
int
bow_lexer_next_get_word (bow_lexer *self, bow_lex *lex, 
			 char *buf, int buflen)
{
  assert (self->next);
  return self->next->get_word (self->next, lex, buf, buflen);
}

/* Get a raw word using the next lexer */
int
bow_lexer_next_get_raw_word (bow_lexer *self, bow_lex *lex, 
			     char *buf, int buflen)
{
  assert (self->next);
  return self->next->get_raw_word (self->next, lex, buf, buflen);
}

/* Postprocess a word using the next lexer */
int
bow_lexer_next_postprocess_word (bow_lexer *self, bow_lex *lex, 
				 char *buf, int buflen)
{
  assert (self->next);
  return self->next->postprocess_word (self->next, lex, 
					     buf, buflen);
}

/* Close the underlying lexer. */
void
bow_lexer_next_close (bow_lexer *self, bow_lex *lex)
{
  assert (self->next);
  self->next->close (self->next, lex);
}
