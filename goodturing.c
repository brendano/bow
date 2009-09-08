/* Simple Good-Turing smoothing */

/* Copyright (C) 1997, 1998, 1999 Andrew McCallum

   Written by:  Kamal Nigam <knigam@cs.cmu.edu> and 
                Rich Caruana <caruana@jprc.com>

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


/*
 *
 *
 *	Simple Good-Turing Frequency Estimator
 *
 *
 *	Geoffrey Sampson, with help from Miles Dennis
 *
 *	School of Cognitive and Computing Sciences
 *	University of Sussex
 *	England
 *
 *	27 June 1995
 *
 *
 *	Takes a set of (frequency, frequency-of-frequency) pairs, and 
 *	applies the Simple Good-Turing technique (devised by William A. Gale 
 *	of AT&T Bell Labs and described in Gale & Sampson, "Good-Turing
 *	Frequency Estimation Without Tears") for estimating 
 *	the probabilities corresponding to the observed frequencies, 
 *	and P.0, the joint probability of all unobserved species.
 *	Code is in ANSI standard C.
 *
 *	The program is written to take input from "stdin" and send output
 *	to "stdout"; redirection can be used to take input from and
 *	send output to permanent files.
 *
 *	The input file should be a series of lines separated by newline
 *	characters, where all nonblank lines contain two positive integers
 *	(an observed frequency, followed by the frequency of that frequency)
 *	separated by whitespace.  (Blank lines are ignored.)
 *	The lines should be in ascending order of frequency.
 *
 *	No checks are made for linearity; the program simply assumes that the 
 *	requirements for using the SGT estimator are met.
 *
 *	The output is a series of lines each containing an integer followed  
 *	by a probability (a real number between zero and one), separated by a 
 *	tab.  In the first line, the integer is 0 and the real number is the 
 *	estimate for P.0.  In subsequent lines, the integers are the  
 *	successive observed frequencies, and the reals are the estimated  
 *	probabilities corresponding to those frequencies.
 *
 *	No warranty is given as to absence of bugs.
 *
 *
 */
 



#include <bow/libbow.h>
#include <argp/argp.h>
#include <stdio.h>
#include <math.h>
#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#define TRUE	1
#define FALSE	0
#define MAX_ROWS	100

static int r[MAX_ROWS], n[MAX_ROWS];
static double Z[MAX_ROWS], log_r[MAX_ROWS], log_Z[MAX_ROWS], 
  rStar[MAX_ROWS], p[MAX_ROWS];

static long int bigN;
static int rows;
static double PZero, bigNprime;
static double slope, intercept;

static double sq(double x)
{
  return(x * x);
}
 
static void findBestFit(void)
{
  double XYs, Xsquares, meanX, meanY;
  double sq(double);
  int i;
 	
  XYs = Xsquares = meanX = meanY = 0.0;
  for (i = 0; i < rows; ++i)
    {
      meanX += log_r[i];
      meanY += log_Z[i];
    }
  meanX /= rows;
  meanY /= rows;
  for (i = 0; i < rows; ++i)
    {
      XYs += (log_r[i] - meanX) * (log_Z[i] - meanY);
      Xsquares += sq(log_r[i] - meanX);
    }
  slope = XYs / Xsquares;
  intercept = meanY - slope * meanX;
}
 	
static double smoothed(int i)
{
  return(exp(intercept + slope * log(i)));
}
 	
static int row(int i)
{
  int j = 0;
 	
  while (r[j] < i && j < rows)
    ++j;
  return((j < rows && r[j] == i) ? j : -1);
}


/*
        - terms 0-(k-1) will be smoothed
        - freq must be of length k, and the last element must 
          be a legit count (SGT needs k count to smooth term (k-1))
        - simple_good_turing does not use freq[0]
        - disc must be of length k-1 or greater
        - arrays freq and disc have element 0 correspond to terms
          with 0 observed frequency, element 1 terms with frequency
          1, ...
        - because we are smoothing just terms 0-(k-1), probabilities 
          will not sum to one without external normalization because 
          terms >(k+1) where not smoothed
	  */
int simple_good_turing (int length, int *freq, double *disc)
{
  int i, j, next_n;
  double k, x, y;
  int indiffValsSeen = FALSE;
  int row(int);
  void findBestFit(void);
  double smoothed(int);
  double sq(double);
  void showEstimates(void);

  assert(length < MAX_ROWS);

  for (i=1; i<length; i++)
    {
      r[i-1] = i;
      n[i-1] = freq[i];
    }
  rows = length-1;

#if 0
  for (i=0; i<length; i++)
    printf ("%d %d\n", r[i], n[i]);
#endif

  bigN = 0;
  for (j = 0; j < rows; ++j)
    bigN += r[j] * n[j];
  PZero = n[row(1)] / (double) bigN;
  for (j = 0; j < rows; ++j)
    {
      i = (j == 0 ? 0 : r[j - 1]);
      if (j == rows - 1)
	k = (double) (2 * r[j] - i);
      else
	k = (double) r[j + 1];
      Z[j] = 2 * n[j] / (k - i);
      log_r[j] = log(r[j]);
      log_Z[j] = log(Z[j]);
    }
  findBestFit();
  for (j = 0; j < rows; ++j)
    {
      y = (r[j] + 1) * smoothed(r[j] + 1) / smoothed(r[j]);
      if (row(r[j] + 1) < 0)
	indiffValsSeen = TRUE;
      if (! indiffValsSeen)
	{
	  x = (r[j] + 1) * (next_n = n[row(r[j] + 1)]) / 
	    (double) n[j];
	  if (fabs(x - y) <= 1.96 * sqrt(sq(r[j] + 1.0) *
					 next_n / (sq((double) n[j])) * 
					 (1 + next_n / (double) n[j])))
	    indiffValsSeen = TRUE;
	  else
	    rStar[j] = x;
	}
      if (indiffValsSeen)
	rStar[j] = y;
    }
  bigNprime = 0.0;
  for (j = 0; j < rows; ++j)
    bigNprime += n[j] * rStar[j];
  for (j = 0; j < rows; ++j)
    p[j] = (1 - PZero) * rStar[j] / bigNprime;

  disc[0] = PZero;
  for (j=1; j<length; j++)
    {
      bow_verbosify(bow_progress, "(%d %f) ", j, p[j-1]);
      disc[j] = p[j-1] * (double) bigN / (double) j;
    }
  bow_verbosify(bow_progress, "\n");
  

  return(0);
}
