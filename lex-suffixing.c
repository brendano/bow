#include <bow/libbow.h>
#include <ctype.h>

/* bow_default_lexer_suffixing should be an indirect lexer, with a
   simple lexer as its underlying lexer.  However, I got lazy, and it
   should be considered that the bow_default_lexer_simple is always
   the underlying lexer */

#define HEADER_TWICE 1

static int suffixing_doing_headers;
static int suffixing_appending_headers;
static char suffixing_suffix[BOW_MAX_WORD_LENGTH];
static int suffixing_suffix_length;

int bow_lexer_html_get_raw_word (bow_lexer *self, bow_lex *lex, 
				 char *buf, int buflen);

/* Put the string before the ':' into SUFFIXING_SUFFIX, and replace the
   following newline with a '\0' */
static void
suffixing_snarf_suffix (bow_lex *lex)
{
  int i;

  /* Hack to get arrow to work on 23k+ research paper index */
  if (! isalpha (lex->document[0]))
    {
      /*lex->document[0] = '\0';*/
      return;
    }
  /*  assert (isalpha (lex->document[0])); */
  suffixing_suffix[0] = 'x';
  suffixing_suffix[1] = 'x';
  suffixing_suffix[2] = 'x';
  suffixing_suffix_length = 3;
  /* Put characters into the suffix until we get to the colon */
  while (lex->document[lex->document_position] != ':')
    {
      assert (lex->document[lex->document_position] != '\n');
      if (!isalpha (lex->document[lex->document_position]))
	{
	  lex->document_position++;
	  continue;
	}
      suffixing_suffix[suffixing_suffix_length++] = 
	tolower (lex->document[lex->document_position++]);
      assert (suffixing_suffix_length < BOW_MAX_WORD_LENGTH);
    }
  suffixing_suffix[suffixing_suffix_length] = '\0';
  /* Throw away everything else until end of string */
  i = 0;
  while (lex->document[lex->document_position + i] != '\n'
	 /* This second condition is necessary if we are going through
	    the header twice (when HEADER_TWICE=1) */
	 && lex->document[lex->document_position + i] != '\0')
    {
      i++;
      assert (lex->document_position + i < lex->document_length);
    }
  lex->document[lex->document_position + i] = '\0';
}

/* Create and return a BOW_LEX, filling the document buffer from
   characters in FP, starting after the START_PATTERN, and ending with
   the END_PATTERN. */
bow_lex *
bow_lexer_suffixing_open_text_fp (bow_lexer *self, 
				  FILE *fp,
				  const char *filename)
{
  bow_lex *ret;

  ret = bow_lexer_simple_open_text_fp (self, fp, filename);

  if (ret)
    {
      /* Make sure that the first line has a header-type suffix. */
      int i;
      for (i = 0; i < ret->document_length && ret->document[i] != ':'; i++)
	if (!isalnum (ret->document[i]) || ret->document[i] == '\n')
	    return 0;

      suffixing_doing_headers = 1;
      suffixing_appending_headers = 1;
      suffixing_snarf_suffix (ret);
    }
  return ret;
}


/* Create and return a BOW_LEX, filling the document buffer from
   characters in FP, starting after the START_PATTERN, and ending with
   the END_PATTERN. */
bow_lex *
bow_lexer_suffixing_open_str (bow_lexer *self, char *buf)
{
  bow_lex *ret;

  ret = bow_lexer_simple_open_str (self, buf);

  if (ret)
    {
      suffixing_doing_headers = 1;
      suffixing_appending_headers = 1;
      suffixing_snarf_suffix (ret);
    }
  return ret;
}


int
bow_lexer_suffixing_postprocess_word (bow_lexer *self, bow_lex *lex, 
				      char *buf, int buflen)
{
  int len;

  /* Postprocess the word */
  len = bow_lexer_next_postprocess_word (self, lex, buf, buflen);
  if (len != 0 && suffixing_doing_headers)
    {
      if (suffixing_appending_headers)
	{
	  strcat (buf, suffixing_suffix);
	  len = strlen (buf);
	  assert (len < buflen);
	}
      else
	{
	  /* Skip the `Reference.*:' words the second time through */
	  if (strstr (suffixing_suffix, "xxxreference"))
	    len = 0;
	}
    }

#if 0
  if (lex->document_position == lex->document_length)
    return 0;
#endif

  /* Set up for the next word */
  if (suffixing_doing_headers && lex->document[lex->document_position] == '\0')
    {
      /* This was two newlines in a row or the end of the file. */
      if (lex->document_position == (lex->document_length - 1)
	  || lex->document[lex->document_position + 1] == '\n')
	{
#if HEADER_TWICE
	  if (!suffixing_appending_headers)
	    suffixing_doing_headers = 0;
	  else
	    {
	      lex->document_position = 0;
	      suffixing_appending_headers = 0;
	      suffixing_snarf_suffix (lex);
	    }
#else
	  lex->document_position++;
#endif
	  suffixing_suffix[0] = '\0';
	}
      else
	{
	  lex->document_position++;
	  /* Handle email messages with multi-line headers */
	  if (isalnum(lex->document[lex->document_position]))
	    suffixing_snarf_suffix (lex);
	  else
	    {
	      /* No need to grab a suffix, but must replace the \n with \0 */
	      int i = 0;
	      while (lex->document[lex->document_position + i] != '\n'
		     /* This second condition is necessary if we are going
			through	the header twice (when HEADER_TWICE=1) */
		     && lex->document[lex->document_position + i] != '\0')
		{
		  i++;
		  assert (lex->document_position + i < lex->document_length);
		}
	      lex->document[lex->document_position + i] = '\0';
	    }
	}
    }

  return len;
}

/* Scan a single token from the LEX buffer, placing it in BUF, and
   returning the length of the token.  BUFLEN is the maximum number of
   characters that will fit in BUF.  If the token won't fit in BUF,
   an error is raised. */
int
bow_lexer_suffixing_get_word (bow_lexer *self, bow_lex *lex, 
			      char *buf, int buflen)
{
  int wordlen;			/* number of characters in the word so far */

  do 
    {
      wordlen = bow_lexer_next_get_raw_word (self, lex, buf, buflen);
      if (wordlen == 0)
	{
	  if (suffixing_doing_headers
	      && lex->document_position < lex->document_length)
	    /* We are just at the end of the headers, not at the end
               of the file.  bow_lexer_suffixing_postprocess_word()
               will deal with this */
	    buf[0] = '\0';
	  else
	    return 0;
	}
    }
  while (((wordlen = bow_lexer_suffixing_postprocess_word 
	   (self, lex, buf, buflen)) == 0)
	 || strstr (suffixing_suffix, "URL"));

  wordlen = strlen (buf);
  return wordlen;
}



/* A lexer that prepends all tokens by the `Date:' string at the 
   beginning of the line. */
const bow_lexer _bow_suffixing_lexer =
{
  sizeof (bow_lex),
  NULL,
  bow_lexer_suffixing_open_text_fp,
  bow_lexer_suffixing_open_str,
  bow_lexer_suffixing_get_word,
  NULL,
  NULL,
  bow_lexer_simple_close,
};
const bow_lexer *bow_suffixing_lexer = &_bow_suffixing_lexer;
