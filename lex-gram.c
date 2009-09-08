/* A simple N-gram lexer. */

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

#define SELF ((bow_lexer_gram*)self)
#define LEX ((bow_lex_gram*)lex)

bow_lex *
bow_lexer_gram_open_text_fp (bow_lexer *self, FILE *fp,
			     const char *filename)
{
  bow_lex *lex = bow_lexer_next_open_text_fp (self, fp, filename);
  if (lex == NULL)
    return NULL;
  lex = bow_realloc (lex, self->sizeof_lex);
  LEX->gram_size_this_time = SELF->gram_size;
  return lex;
}

bow_lex *
bow_lexer_gram_open_str (bow_lexer *self, char *buf)
{
  bow_lex *lex = bow_lexer_next_open_str (self, buf);
  if (lex == NULL)
    return NULL;
  LEX->gram_size_this_time = SELF->gram_size;
  return lex;
}

int
bow_lexer_gram_get_word (bow_lexer *self, bow_lex *lex, 
			 char *buf, int buflen)
{
  int i;
  char **tokens;
  int s;
  int len;
  
#if BOW_MCHECK
  mprobe (lex);
#endif /* BOW_MCHECK */

  tokens = alloca (sizeof (char*) * LEX->gram_size_this_time);
  for (i = 0; i < LEX->gram_size_this_time; i++)
    tokens[i] = alloca (BOW_MAX_WORD_LENGTH);

  /* Remember where we started. */
  s = LEX->lex.document_position;

  /* Get the first token. */
  if (self->next->get_word (self->next, lex, tokens[0], BOW_MAX_WORD_LENGTH)
      == 0)
    return 0;

  /* Get the next n-1 tokens. */
  for (i = 1; i < LEX->gram_size_this_time; i++)
    if (self->next->get_word (self->next, lex, tokens[i], BOW_MAX_WORD_LENGTH)
	== 0)
      *(tokens[i]) = '\0';

  /* Make sure it will fit. */
  for (i = 0, len = 0; i < LEX->gram_size_this_time; i++)
    len += strlen (tokens[i]) + 1;
  assert (len < BOW_MAX_WORD_LENGTH);

  /* Fill buf with the tokens concatenated. */
  strcpy (buf, tokens[0]);
  for (i = 1; i < LEX->gram_size_this_time; i++)
    {
      strcat (buf, ";");
      strcat (buf, tokens[i]);
    }

  /* Put us back to the second token so we can get it with the next call */
  if (LEX->gram_size_this_time > 1)
    LEX->lex.document_position = s;

  if (LEX->gram_size_this_time == 1)
    LEX->gram_size_this_time = SELF->gram_size;
  else
    LEX->gram_size_this_time--;

#if BOW_MCHECK
  mprobe (lex);
#endif /* BOW_MCHECK */

  /* Don't distinguish between bi-grams in the middle of a document
     and a tri-gram that only got two words before the end of the
     document.  Do this by removing trailing `;'s */
  for (i = strlen (buf)-1; i >= 0 && buf[i] == ';'; i--)
    buf[i] = '\0';

  return strlen (buf);
}


const bow_lexer_gram _bow_gram_lexer =
{
  {
    sizeof (bow_lex_gram),
    NULL,			/* This must be non-NULL at run-time */
    bow_lexer_gram_open_text_fp,
    bow_lexer_gram_open_str,
    bow_lexer_gram_get_word,
    NULL,
    NULL,
    bow_lexer_simple_close
  },
  1				/* default gram-size is 1 */
};
const bow_lexer_gram *bow_gram_lexer = &_bow_gram_lexer;
