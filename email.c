/* Functions for parsing email files. */

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
#include <assert.h>
#include <ctype.h>		/* for tolower() */
#include <string.h>

/* Read characters from the file pointer FP until the string STRING is
   found or EOF if reached.  Return 0 if EOF was reached, 1 otherwise.
   The search is case-insensitive.  If 1 is returned, the file pointer
   will be at the character after the last character in STRING.  If
   ONELINE is non-zero, insist that the string appear before a newline
   character. */
static inline int
_scan_fp_for_string (FILE *fp, const char *string, int oneline)
{
  int byte;			/* character read from the FP */
  const char *string_ptr;	/* a placeholder into STRING */

  if (!string || !string[0])
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
  for (string_ptr = string+1; *string_ptr; string_ptr++)
    {
      byte = fgetc (fp);
      if (byte == EOF || (oneline && byte == '\n'))
	return 0;
      if (tolower (byte) != tolower (*string_ptr))
	/* A mismatch; start the search again. */
	goto again;
    }

  /* Success!  We found the string. */
  return 1;
}

#if 0
/* Read characters from FP into BUF until the string STOPSTR is
   reached.  On success, returns the number of characters read.  If
   EOF is reached before reading the STOPSTR, return the negative of
   the number of characters read.  If BUFLEN is reached before reading
   the STOPCHAR, return 0. */
static inline int
_scan_fp_into_buffer_until_string (FILE *fp, char *buf, int buflen,
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

  /* xxx The structure of this function is a bit repetative... */

again:
  /* Read forward until we find the first character of STRING. */
  do
    {
      byte = fgetc (fp);
      if (byte == EOF)
	{
	  buf[count] = '\0';
	  return -count;
	}
      *buf_ptr++ = byte;
      if (++count >= buflen)
	{
	  buf[buflen-1] = '\0';
	  return 0;
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
	  buf[count] = '\0';
	  return -count;
	}
      *buf_ptr++ = byte;
      if (++count >= buflen)
	{
	  buf[buflen-1] = '\0';
	  return 0;
	}
      if (tolower (byte) != tolower (*stopstr_ptr))
	/* A mismatch; start the search again. */
	goto again;
    }

  /* Success!  We found the stopstr. */
  count =- strlen (stopstr);
  buf[count] = '\0';
  return count;
}
#endif


/* Read characters from FP into BUF until the character STOPCHAR is
   reached.  On success, returns the number of characters read.  If
   EOF is reached before reading the STOPCHAR, return the negative of
   the number of characters read.  If BUFLEN is reached before reading
   the STOPCHAR, return 0.  If NEGFLAG is 1, the sense of the test is
   reversed. */
static inline int
_scan_fp_into_buffer_until_char (FILE *fp, char *buf, int buflen,
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
static inline int
_scan_fp_into_buffer_until_chars (FILE *fp, char *buf, int buflen,
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


int
bow_email_get_headers (FILE *fp, char *buf, int buflen)
{
  return 0;
}

/* Read in BUF the characters inside the `<>' of the `Message-Id:'
   field of the email message contain in the file pointer FP.  Return
   the number of characters placed in BUF.  Signal an error if more
   than BUFLEN characters are necessary.  Return -1 if no matching
   field is found. */
int
bow_email_get_msgid (FILE *fp, char *buf, int buflen)
{
  int len;
  if (!_scan_fp_for_string (fp, "\nMessage-Id: <", 0))
    return -1;
  if ((len = _scan_fp_into_buffer_until_char (fp, buf, buflen, '>', 0)) <= 0)
    bow_error ("%s: No Message-Id: `>' terminator found.",
	       __PRETTY_FUNCTION__);
  buf[len] = '\0';
  return len;
}

/* Read in BUF the characters inside the `<>' of the `References:'
   field of the news message contain in the file pointer FP.  Return
   the number of characters placed in BUF.  Signal an error if more
   than BUFLEN characters are necessary.  Return -1 if no matching
   field is found. */
int
bow_email_get_references (FILE *fp, char *buf, int buflen)
{
  int len;
  if (!_scan_fp_for_string (fp, "\nReferences: <", 0))
    return -1;
  if ((len = _scan_fp_into_buffer_until_char (fp, buf, buflen, '>', 0)) <= 0)
    bow_error ("%s: No Message-Id: `>' terminator found.",
	       __PRETTY_FUNCTION__);
  buf[len] = '\0';
  return len;
}

static inline int
_bow_email_get_email_address (FILE *fp, char *buf, int buflen)
{
  int len;
  int fpos;
  /* Grab the email address of the sender from inside the `<...>' */
  fpos = ftell (fp);
  if ((len = _scan_fp_for_string (fp, "<", 1)))
    {
      if ((len = _scan_fp_into_buffer_until_chars (fp, buf, buflen, "> ", 0))
	  <= 0)
	bow_error ("No email address `>' terminator found.");
      buf[len] = '\0';
      return len;
    }
  /* There wasn't a `<' in the From: header.  Just grab the text up to
     the next white space character. */
  fseek (fp, fpos, SEEK_SET);
  /* Chop off whitespace at beginning. */
  len = _scan_fp_into_buffer_until_chars (fp, buf, buflen, " \n\t", 1);
  len = _scan_fp_into_buffer_until_chars (fp, buf, buflen, " \n\t", 0);
  assert (len > 0);
  buf[len] = '\0';
  return len;
}

/* Read into BUF the characters inside the `<>' of the `From:' field
   of the email message contain in the file pointer FP.  Return the
   number of characters placed in BUF.  Signal an error if more than
   BUFLEN characters are necessary.  Return -1 if no matching field is
   found. */
int
bow_email_get_sender (FILE *fp, char *buf, int buflen)
{
  int fpos;
  fpos = ftell (fp);
  if (!_scan_fp_for_string (fp, "\nFrom: ", 0))
    {
      fseek (fp, fpos, SEEK_SET);
      if (!_scan_fp_for_string (fp, "\nReturn-Path: ", 0))
	return -1;
    }
  return _bow_email_get_email_address (fp, buf, buflen);
}

/* Read into BUF the characters inside the `<>' of the `To:' field
   of the email message contain in the file pointer FP.  Return the
   number of characters placed in BUF.  Signal an error if more than
   BUFLEN characters are necessary.  Return -1 if no matching field is
   found. */
int
bow_email_get_recipient (FILE *fp, char *buf, int buflen)
{
  if (!_scan_fp_for_string (fp, "\nTo: ", 0))
    return -1;
  return _bow_email_get_email_address (fp, buf, buflen);
}


/* Read into BUF the day, month and year of the `Date:' field of the
   email message contain in the file pointer FP.  The format is
   something like `21 Jul 1996'.  Return the number of characters
   placed in BUF.  Signal an error if more than BUFLEN characters are
   necessary.  Return -1 if no matching field is found. */
int
bow_email_get_date (FILE *fp, char *buf, int buflen)
{
  int len;
  int byte;

  if (!_scan_fp_for_string (fp, "\nDate: ", 0))
    return -1;
  /* Scan up until the comma separator. */
  if (!_scan_fp_for_string (fp, ", ", 1))
    return -1;
  /* Scan into BUF the next 7 characters, which should contain
     something like `19 Jun '. */
  assert (buflen >= 7);
  for (len = 0; len < 7; len++)
    {
      byte = fgetc (fp);
      assert (byte != EOF);
      buf[len] = byte;
    }
  /* Scan into buf the year, which may be something like `96' or `1996'. 
     Just keep scanning until whitespace is found. */
  len = _scan_fp_into_buffer_until_chars (fp, buf+7, buflen-7, " \t\n", 0);
  if (len <= 0)
    return 0;
  len += 7;
  buf[len] = '\0';
  return len;
}

/* Read in BUF the characters between the `Received: from ' and the
   following space, and the characters between the ` id ' and the
   following `;' in the file pointer FP.  Return the number of
   characters placed in BUF.  Signal an error if more than BUFLEN
   characters are necessary.  Return -1 if no matching field is
   found. */
int
bow_email_get_receivedid (FILE *fp, char *buf, int buflen)
{
  int len;
  if (!_scan_fp_for_string (fp, "\nReceived: from ", 0))
    return -1;
  if ((len = _scan_fp_into_buffer_until_char (fp, buf, buflen, ' ', 0)) <= 0)
    bow_error ("%s: No `Received: from' ` ' terminator found.",
	       __PRETTY_FUNCTION__);
  if (!_scan_fp_for_string (fp, " id ", 1))
    return -1;
  if ((len += _scan_fp_into_buffer_until_char (fp, buf+len, buflen, ';', 0))
      <= 0)
    bow_error ("%s: No `Received: from id' `;' terminator found.",
	       __PRETTY_FUNCTION__);
  buf[len] = '\0';
  return len;
}


/* Read in BUF the characters inside the `<>' of the `In-Reply-To:'
   field of the email message contain in the file pointer FP.  Return
   the number of characters placed in BUF.  Signal an error if more
   than BUFLEN characters are necessary.  Return -1 if no matching
   field is found. */
int
bow_email_get_replyid (FILE *fp, char *buf, int buflen)
{
  int len;
  if (!_scan_fp_for_string (fp, "\nIn-Reply-To: ", 0))
    return -1;
  if (!_scan_fp_for_string (fp, "<", 0))
    return -1;
  if ((len = _scan_fp_into_buffer_until_char (fp, buf, buflen, '>', 0)) <= 0)
    bow_error ("%s: No Message-Id: `>' terminator found.",
	       __PRETTY_FUNCTION__);
  buf[len] = '\0';
  return len;
}

/* Read in BUF the characters inside the `<>' of the
   `Resent-Message-Id:' field of the email message contain in the file
   pointer FP.  Return the number of characters placed in BUF.  Signal
   an error if more than BUFLEN characters are necessary.  Return -1
   if no matching field is found. */
int
bow_email_get_resent_msgid (FILE *fp, char *buf, int buflen)
{
  int len;
  if (!_scan_fp_for_string (fp, "\nResent-Message-Id: <", 0))
    return -1;
  if ((len = _scan_fp_into_buffer_until_char (fp, buf, buflen, '>', 0)) <= 0)
    bow_error ("%s: No Message-Id: `>' terminator found.",
	       __PRETTY_FUNCTION__);
  buf[len] = '\0';
  return len;
}
