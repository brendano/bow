/* Functions for reading FILE*'s up to certain strings or characters. */

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
#include <ctype.h>		/* for tolower() */

/* Read characters from the file pointer FP until the string STRING is
   found or EOF if reached.  Return 0 if EOF was reached, 1 otherwise.
   The search is case-insensitive.  If 1 is returned, the file pointer
   will be at the character after the last character in STRING.  If
   ONELINE is non-zero, insist that the string appear before a newline
   character.  If STRING is NULL, scan until EOF. */
int
bow_scan_fp_for_string (FILE *fp, const char *string, int oneline)
{
  int byte;			/* character read from the FP */
  const char *string_ptr;	/* a placeholder into STRING */

  /* If STRING is NULL, scan forward to the end of the file. */
  if (!string)
    {
      while (fgetc (fp) != EOF)
	;
      return 1;
    }
  /* If STRING is the empty string, return without scanning forward at all */
  if (!string[0])
    return 0;

  /* Read forward until we find the first character of STRING. */
  /* Make an initial newline in STRING match the beginning of the file. */
  if (!(ftell (fp) == 0 && string[0] == '\n'))
    {
    again:
      do
	{
	  byte = fgetc (fp);
	  if (byte == EOF || (string[0] != '\n' && oneline && byte == '\n'))
	    return 0;
	}
      while (tolower (byte) != tolower (string[0]));
    }

  /* Step through the characters in STRING, starting all over again
     if we encounter a mismatch. */
  string_ptr = string+1;
  while (*string_ptr)
    {
      byte = fgetc (fp);
      if (byte == EOF || (oneline && byte == '\n'))
	return 0;
      /* Ignore Carriage-Return characters, so we can match MIME headers
	 like "\r\n\r\n" with a search STRING of "\n\n" */
      if (byte == '\r')
	continue;
      if (tolower (byte) != tolower (*string_ptr))
	/* A mismatch; start the search again. */
	goto again;
      /* Move on to next character in the pattern. */
      string_ptr++;
    }

  /* Success!  We found the string. */
  return 1;
}

/* Read characters from the string BUF until the string STRING is
   found or the terminating null character if reached.  Return the
   character position in BUF immediately following the location where
   STRING was found.  If STRING is not found, the position of the
   terminating null character is returned.  The search is
   case-insensitive.  If ONELINE is non-zero, insist that the string
   appear before a newline character.  If STRING is NULL, scan until
   the terminating null character is found. */
int
bow_scan_str_for_string (char *buf, const char *string, int oneline)
{
  int byte;			/* character read from the FP */
  const char *string_ptr;	/* a placeholder into STRING */
  int bufpos = 0;               /* placeholder into BUF */

  /* If STRING is NULL, scan forward to the end of the file. */
  if (!string)
    return strlen (buf);

  /* If STRING is the empty string, return without scanning forward at all */
  if (!string[0])
    return 0;

  /* Read forward until we find the first character of STRING. */
  /* Make an initial newline in STRING match the beginning of the file. */
  if (!(bufpos == 0 && string[0] == '\n'))
    {
    again:
      do
	{
	  byte = buf[bufpos++];
	  if ((byte == '\0') || (string[0] != '\n' && oneline && byte == '\n'))
	    return (bufpos-1);
	}
      while (tolower (byte) != tolower (string[0]));
    }

  /* Step through the characters in STRING, starting all over again
     if we encounter a mismatch. */
  string_ptr = string+1;
  while (*string_ptr)
    {
      byte = buf[bufpos++];
      if ((byte == '\0') || (oneline && byte == '\n'))
	return (bufpos-1);
      /* Ignore Carriage-Return characters, so we can match MIME headers
	 like "\r\n\r\n" with a search STRING of "\n\n" */
      if (byte == '\r')
	continue;
      if (tolower (byte) != tolower (*string_ptr))
	/* A mismatch; start the search again. */
	goto again;
      /* Move on to next character in the pattern. */
      string_ptr++;
    }

  /* Success!  We found the string. */
  return bufpos;
}

/* Read characters from FP into BUF until the character STOPCHAR is
   reached.  On success, returns the number of characters read.  If
   EOF is reached before reading the STOPCHAR, return the negative of
   the number of characters read.  If BUFLEN is reached before reading
   the STOPCHAR, return 0.  If NEGFLAG is 1, the sense of the test is
   reversed. */
int
bow_scan_fp_into_buffer_until_char (FILE *fp, char *buf, int buflen,
				    char stopchar, int negflag)
{
  int byte;
  int count = 0;

  assert (buflen > 0 && buf);
  while (buflen--)
    {
      byte = fgetc (fp);
      if (byte == EOF)
	{
	  buf[count] = '\0';
	  return -count;
	}
      if (negflag ? (byte != stopchar) : (byte == stopchar))
	{
	  fseek (fp, -1, SEEK_CUR);
	  buf[count] = '\0';
	  return count;
	}
      buf[count++] = byte;
    }
  buf[count-1] = '\0';
  return 0;
}

/* Read characters from FP into BUF until any of the characters in the
   string STOPCHARS is reached.  On success, returns the number of
   characters read.  If EOF is reached before reading any of the
   STOPCHARS, return the negative of the number of characters read.
   If BUFLEN is reached before reading the STOPCHAR, return 0. 
   If NEGFLAG is 1, the sense of the test is reversed. */
int
bow_scan_fp_into_buffer_until_chars (FILE *fp, char *buf, int buflen,
				     const char *stopchars, int negflag)
{
  int byte;
  int count = 0;

  assert (buflen > 0 && buf);
  while (buflen--)
    {
      byte = fgetc (fp);
      if (byte == EOF)
	{
	  buf[count] = '\0';
	  return -count;
	}
      if (negflag
	  ? (strchr (stopchars, byte) == 0)
	  : (strchr (stopchars, byte) != 0))
	{
	  fseek (fp, -1, SEEK_CUR);
	  buf[count] = '\0';
	  return count;
	}
      buf[count++] = byte;
    }
  buf[count-1] = '\0';
  return 0;
}

/* Read characters from FP into BUF until the string STOPSTR is
   reached.  On success, returns the number of characters read.  If
   EOF is reached before reading the STOPSTR, return the negative of
   the number of characters read.  If BUFLEN is reached before reading
   the STOPCHAR, return 0. */
int
bow_scan_fp_into_buffer_until_string (FILE *fp, char *buf, int buflen,
				      char* stopstr)
{
  int byte;			/* character read from the FP */
  const char *stopstr_ptr;	/* a placeholder into STOPSTR */
  char *buf_ptr;		/* a placeholder into BUF */
  int count;			/* the number of characters added to BUF */

  if (!stopstr || !stopstr[0])
    return 0;

  count = 0;
  buf_ptr = buf;

again:
  /* Read forward until we find the first character of STRING. */
  do
    {
      byte = fgetc (fp);
      if (byte == EOF)
	{
	  if (buf)
	    buf[count] = '\0';
	  return -count;
	}
      count++;
      if (buf)
	{
	  *buf_ptr++ = byte;
	  if (count >= buflen)
	    {
	      buf[buflen-1] = '\0';
	      return 0;
	    }
	}
    }
  while (tolower (byte) != tolower (stopstr[0]));

  /* Step through the characters in STRING, starting all over again
     if we encounter a mismatch. */
  for (stopstr_ptr = stopstr+1; *stopstr_ptr; stopstr_ptr++)
    {
      byte = fgetc (fp);
      if (byte == EOF)
	{
	  if (buf)
	    buf[count] = '\0';
	  return -count;
	}
      if (buf)
	*buf_ptr++ = byte;
      if (++count >= buflen)
	{
	  if (buf)
	    buf[buflen-1] = '\0';
	  return 0;
	}
      if (tolower (byte) != tolower (*stopstr_ptr))
	/* A mismatch; start the search again. */
	goto again;
    }

  /* Success!  We found the stopstr. */
  count =- strlen (stopstr);
  if (buf)
    buf[count] = '\0';
  return count;
}
