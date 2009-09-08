/* A lexer will special features for handling HTML. */

/* Copyright (C) 1997, 1999 Andrew McCallum

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
#include <ctype.h>		/* for tolower() */

static const struct
{
    char         *entityName;
    unsigned char entityChar;
} htmlEntities[] = {
    { "nbsp", 32 },   /* non-breaking whitespace */
    { "quot", 34 },   /* double quote */
    { "amp", 38 },   /* ampersand */
    { "lt", 60 },   /* less than */
    { "gt", 62 },   /* greater than */
    { "iexcl", 161 },   /* inverted exclamation mark */
    { "cent", 161 },   /* cent sign */
    { "pound", 163 },   /* pound sign */
    { "yen", 165 },   /* yen sign */
    { "brvbar", 166 },   /* broken vertical bar */
    { "sect", 167 },   /* section sign */
    { "copy", 169 },   /* copyright sign */
    { "laquo", 171 },   /* angle quotation mark, left */
    { "raquo", 187 },   /* angle quotation mark, right */
    { "not", 172 },   /* negation sign */
    { "reg", 174 },   /* circled r registered sign */
    { "deg", 176 },   /* degree sign */
    { "plusmn", 177 },   /* plus or minus sign */
    { "sup2", 178 },   /* superscript 2 */
    { "sup3", 179 },   /* superscript 3 */
    { "micro", 181 },   /* micro sign */
    { "para", 182 },   /* paragraph sign */
    { "sup1", 185 },   /* superscript 1 */
    { "middot", 183 },   /* center dot */
    { "frac14", 188 },   /* fraction 1/4 */
    { "frac12", 189 },   /* fraction 1/2 */
    { "iquest", 191 },   /* inverted question mark */
    { "frac34", 190 },   /* fraction 3/4 */
    { "aelig", 198 },   /* capital ae diphthong (ligature) */
    { "aacute", 193 },   /* capital a, acute accent */
    { "acirc", 194 },   /* capital a, circumflex accent */
    { "agrave", 192 },   /* capital a, grave accent */
    { "aring", 197 },   /* capital a, ring */
    { "atilde", 195 },   /* capital a, tilde */
    { "auml", 196 },   /* capital a, dieresis or umlaut mark */
    { "ccedil", 199 },   /* capital c, cedilla */
    { "eth", 208 },   /* capital eth, icelandic */
    { "eacute", 201 },   /* capital e, acute accent */
    { "ecirc", 202 },   /* capital e, circumflex accent */
    { "egrave", 200 },   /* capital e, grave accent */
    { "euml", 203 },   /* capital e, dieresis or umlaut mark */
    { "iacute", 205 },   /* capital i, acute accent */
    { "icirc", 206 },   /* capital i, circumflex accent */
    { "igrave", 204 },   /* capital i, grave accent */
    { "iuml", 207 },   /* capital i, dieresis or umlaut mark */
    { "ntilde", 209 },   /* capital n, tilde */
    { "oacute", 211 },   /* capital o, acute accent */
    { "ocirc", 212 },   /* capital o, circumflex accent */
    { "ograve", 210 },   /* capital o, grave accent */
    { "oslash", 216 },   /* capital o, slash */
    { "otilde", 213 },   /* capital o, tilde */
    { "ouml", 214 },   /* capital o, dieresis or umlaut mark */
    { "thorn", 222 },   /* capital thorn, icelandic */
    { "uacute", 218 },   /* capital u, acute accent */
    { "ucirc", 219 },   /* capital u, circumflex accent */
    { "ugrave", 217 },   /* capital u, grave accent */
    { "uuml", 220 },   /* capital u, dieresis or umlaut mark */
    { "yacute", 221 },   /* capital y, acute accent */
    { "aacute", 225 },   /* small a, acute accent */
    { "acirc", 226 },   /* small a, circumflex accent */
    { "aelig", 230 },   /* small ae diphthong (ligature) */
    { "agrave", 224 },   /* small a, grave accent */
    { "aring", 229 },   /* small a, ring */
    { "atilde", 227 },   /* small a, tilde */
    { "auml", 228 },   /* small a, dieresis or umlaut mark */
    { "ccedil", 231 },   /* small c, cedilla */
    { "eacute", 233 },   /* small e, acute accent */
    { "ecirc", 234 },   /* small e, circumflex accent */
    { "egrave", 232 },   /* small e, grave accent */
    { "eth", 240 },   /* small eth, icelandic */
    { "euml", 235 },   /* small e, dieresis or umlaut mark */
    { "iacute", 237 },   /* small i, acute accent */
    { "icirc", 238 },   /* small i, circumflex accent */
    { "igrave", 236 },   /* small i, grave accent */
    { "iuml", 239 },   /* small i, dieresis or umlaut mark */
    { "ntilde", 241 },   /* small n, tilde */
    { "oacute", 243 },   /* small o, acute accent */
    { "ocirc", 244 },   /* small o, circumflex accent */
    { "ograve", 242 },   /* small o, grave accent */
    { "oslash", 248 },   /* small o, slash */
    { "otilde", 245 },   /* small o, tilde */
    { "ouml", 246 },   /* small o, dieresis or umlaut mark */
    { "szlig", 223 },   /* small sharp s, german (sz ligature) */
    { "thorn", 254 },   /* small thorn, icelandic */
    { "uacute", 250 },   /* small u, acute accent */
    { "ucirc", 251 },   /* small u, circumflex accent */
    { "ugrave", 249 },   /* small u, grave accent */
    { "uuml", 252 },   /* small u, dieresis or umlaut mark */
    { "yacute", 253 },   /* small y, acute accent */
    { "yuml", 255 }   /* small y, dieresis or umlaut mark */
};
int entityMaxLen;

static bow_int4str *entityMap;

#define PARAMS (bow_default_lexer_parameters)

static void initEntityMap()
{
    int i;

    if (entityMap != NULL)
        return;

    entityMap = bow_int4str_new(255);

    /* the below must be done from 0-N or logic in get_word below breaks */
    for (i = 0; i < sizeof(htmlEntities)/sizeof(htmlEntities[0]); i++) {
        bow_str2int (entityMap, htmlEntities[i].entityName);
        if (entityMaxLen < strlen(htmlEntities[i].entityName))
            entityMaxLen = strlen(htmlEntities[i].entityName);
    }
}

int
bow_lexer_html_get_raw_word (bow_lexer *self, bow_lex *lex,
			     char *buf, int buflen)
{
  int byte;			/* characters read from the FP */
  int wordlen;			/* number of characters in the word so far */
  int html_bracket_nestings = 0;

  assert (lex->document_position <= lex->document_length);
  if (entityMap == NULL)
      initEntityMap();
  
  /* Ignore characters until we get an beginning character. */
  do
    {
      byte = lex->document[lex->document_position++];
      if (byte == 0)
	{
	  if (html_bracket_nestings)
	    bow_verbosify (bow_verbose,
			   "Found unterminated `<' in HTML\n");
	  lex->document_position--;
	  return 0;
	}
      if (byte == '&')
      {
          char *begEntity = &lex->document[lex->document_position];
          char *s = strchr(begEntity, ';');
          int hashCode;

          if (s != NULL)
              if ((s - begEntity) <= entityMaxLen) { /* can it be an entity */
                  *s = '\0';
                  if (begEntity[0] == '#') {
                      byte = atoi(&begEntity[1]);
                      lex->document_position++;
                      do 
                      {
                         byte = lex->document[lex->document_position++];
                      } while (isdigit(byte));
                  }
                  else {
                      hashCode = bow_str2int_no_add(entityMap, begEntity);
                      if (hashCode != -1) {
                          byte = htmlEntities[hashCode].entityChar;
                          lex->document_position += s - begEntity + 1;
                      }
                  }
                  *s = ';';
              }
      }
      if (byte == '<')
	{
	  if (html_bracket_nestings)
	    bow_verbosify (bow_verbose,
			   "Found nested '<' in HTML\n");
	  html_bracket_nestings = 1;
	}
      else if (byte == '>')
	{
	  if (html_bracket_nestings == 0)
	    bow_verbosify (bow_verbose,
			   "Found `>' outside HTML token\n");
	  html_bracket_nestings = 0;
	}
    }
  while (html_bracket_nestings || !PARAMS->true_to_start (byte));

  /* Add the first alphabetic character to the word. */
  buf[0] = (bow_lexer_case_sensitive) ? byte : tolower (byte);

  /* Add all the satisfying characters to the word - stripping out all HTML
     markup.  "<FONT SIZE=+2>R</FONT>ainbow " becomes "Rainbow" */
  for (wordlen = 1; wordlen < buflen; wordlen++)
    {
      byte = lex->document[lex->document_position++];
      if (byte == 0)
	{
	  lex->document_position--;
	  break;
	}

      if (!PARAMS->false_to_end (byte) && html_bracket_nestings == 0)
	{
	  lex->document_position--;
	  break;
	}
      if (byte == '<')
	{
	  if (html_bracket_nestings)
	    bow_verbosify (bow_verbose,
			   "Found nested '<' in HTML\n");
	  html_bracket_nestings = 1;
	}
      if (byte == '>')
	{
	  if (html_bracket_nestings == 0)
	    bow_verbosify (bow_verbose,
			   "Found `>' outside HTML token\n");
	  html_bracket_nestings = 0;
	}
      
      if (html_bracket_nestings == 0)
	buf[wordlen] = tolower (byte);
    }

  assert (lex->document_position <= lex->document_length);

  if (wordlen >= buflen)
    bow_error ("Encountered word longer than buffer length=%d", buflen);

  /* Terminate it. */
  buf[wordlen] = '\0';

  return wordlen;
}


/* A lexer that ignores all HTML directives, ignoring all characters
   between angled brackets: < and >. */
const bow_lexer _bow_html_lexer =
{
  sizeof (bow_lex),
  NULL,
  bow_lexer_simple_open_text_fp,
  bow_lexer_simple_open_str,
  bow_lexer_simple_get_word,
  bow_lexer_html_get_raw_word,
  bow_lexer_simple_postprocess_word,
  bow_lexer_simple_close
};
const bow_lexer *bow_html_lexer = &_bow_html_lexer;
