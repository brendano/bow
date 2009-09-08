/* istext.c - test if a file contains text or not. */

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
#include <ctype.h>		/* for isprint(), etc. */

/* The percentage of characters that must be text-like in order for
   us to say this is a text file. */
#define TEXT_PRINTABLE_PERCENT 95

#define NUM_TEST_CHARS 4096

/* bow_*_is_text() always returns `yes'.  This is useful for Japanese
   byte codes. */
int bow_is_text_always_yes = 0;

/* Examine the first NUM_TEST_CHARS characters of `fp', and return a 
   non-zero value iff TEXT_PRINTABLE_PERCENT of them are printable. */
int
bow_fp_is_text (FILE *fp)
{
  char buf[NUM_TEST_CHARS];
  int num_read;
  int num_printable = 0;
  int num_spaces = 0;
  int fpos;
  int i;

  if (bow_is_text_always_yes)
    return 1;

  fpos = ftell (fp);
  num_read = fread (buf, sizeof (char), NUM_TEST_CHARS, fp);
  fseek (fp, fpos, SEEK_SET);

  for (i = 0; i < num_read; i++)
    {
      if (isprint (buf[i]) || isspace (buf[i]))
	num_printable++;
      if (isspace (buf[i]))
	num_spaces++;
    }

  if (!(num_read > 0 
	&& (((100 * num_printable) / num_read) > TEXT_PRINTABLE_PERCENT)))
    return 0;

  if (bow_istext_avoid_uuencode)
    {
      int num_newlines;
      static int NUM_LINE_LENGTHS = NUM_TEST_CHARS;
      int line_lengths[NUM_LINE_LENGTHS];
      int line_length_histogram[NUM_LINE_LENGTHS];
      int max_line_length_histogram_height;
      int max_line_length_histogram_length;

      /* Test for uuencoded blocks by seeing if over 1/3 of the lines have
	 identical length. */
      for (i = 0, num_newlines = 0, line_lengths[num_newlines] = 0;
	   i < num_read;
	   i++)
	{
	  if (buf[i] == '\n')
	    {
	      num_newlines++;
	      assert (num_newlines < NUM_LINE_LENGTHS);
	      line_lengths[num_newlines] = 0;
	    }
	  else
	    {
	      line_lengths[num_newlines]++;
	    }
	}
      for (i = 0; i < NUM_LINE_LENGTHS; i++)
	line_length_histogram[i] = 0;
      for (i = 0; i < num_newlines; i++)
	line_length_histogram[line_lengths[i]]++;
      max_line_length_histogram_height = line_length_histogram[0];
      max_line_length_histogram_length = 0;
      for (i = 1; i < NUM_LINE_LENGTHS; i++)
	if (max_line_length_histogram_height < line_length_histogram[i])
	  {
	    max_line_length_histogram_height = line_length_histogram[i];
	    max_line_length_histogram_length = i;
	  }
      /* If over a 1/2 of the lines have the same height, and there
	 aren't many spaces in the text, and the line length with the
	 most lines is greater than or equal to 50, then this file
	 probably contains a uuencoded block. */
      if (max_line_length_histogram_height > num_newlines / 2
	  && num_spaces < num_read / 10
	  && max_line_length_histogram_length >= 50
	  && max_line_length_histogram_length <= 80)
	return 0;
    }

  return 1;
}


/* Examine the first NUM_TEST_CHARS characters of STR, and return a 
   non-zero value iff TEXT_PRINTABLE_PERCENT of them are printable. */
int
bow_str_is_text (char *buf)
{
  int num_read;
  int num_printable = 0;
  int num_spaces = 0;
  int i;

  if (bow_is_text_always_yes)
    return 1;

  /* Loop until we found the end or until we hit our limit */
  for (i = 0; buf[i] && i < NUM_TEST_CHARS; i++)
    {
      if (isprint (buf[i]) || isspace (buf[i]))
	num_printable++;
      if (isspace (buf[i]))
	num_spaces++;
    }

  num_read = i;

  if (!(num_read > 0 
	&& (((100 * num_printable) / num_read) > TEXT_PRINTABLE_PERCENT)))
    return 0;

  if (bow_istext_avoid_uuencode)
    {
      int num_newlines;
      static int NUM_LINE_LENGTHS = NUM_TEST_CHARS;
      int line_lengths[NUM_LINE_LENGTHS];
      int line_length_histogram[NUM_LINE_LENGTHS];
      int max_line_length_histogram_height;
      int max_line_length_histogram_length;

      /* Test for uuencoded blocks by seeing if over 1/3 of the lines have
	 identical length. */
      for (i = 0, num_newlines = 0, line_lengths[num_newlines] = 0;
	   i < num_read;
	   i++)
	{
	  if (buf[i] == '\n')
	    {
	      num_newlines++;
	      assert (num_newlines < NUM_LINE_LENGTHS);
	      line_lengths[num_newlines] = 0;
	    }
	  else
	    {
	      line_lengths[num_newlines]++;
	    }
	}
      for (i = 0; i < NUM_LINE_LENGTHS; i++)
	line_length_histogram[i] = 0;
      for (i = 0; i < num_newlines; i++)
	line_length_histogram[line_lengths[i]]++;
      max_line_length_histogram_height = line_length_histogram[0];
      max_line_length_histogram_length = 0;
      for (i = 1; i < NUM_LINE_LENGTHS; i++)
	if (max_line_length_histogram_height < line_length_histogram[i])
	  {
	    max_line_length_histogram_height = line_length_histogram[i];
	    max_line_length_histogram_length = i;
	  }
      /* If over a 1/2 of the lines have the same height, and there
	 aren't many spaces in the text, and the line length with the
	 most lines is greater than or equal to 50, then this file
	 probably contains a uuencoded block. */
      if (max_line_length_histogram_height > num_newlines / 2
	  && num_spaces < num_read / 10
	  && max_line_length_histogram_length >= 50
	  && max_line_length_histogram_length <= 80)
	return 0;
    }

  return 1;
}
