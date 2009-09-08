/* primelist.c - a list of prime numbers close to a power of 2.
   Source: http://www.utm.edu/research/primes/lists/2small/0bit.html

   Copyright (C) 2000 Andrew McCallum

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

#include <limits.h>
#include <assert.h>

static unsigned
bow_primelist[] = {
  (1 << 8) - 5,
  (1 << 9) - 3,
  (1 << 10) - 3,
  (1 << 11) - 9,
  (1 << 12) - 3,
  (1 << 13) - 1,
  (1 << 14) - 3,
  (1 << 15) - 19,
  (1 << 16) - 15,
  (1 << 17) - 1,
  (1 << 18) - 5,
  (1 << 19) - 1,
  (1 << 20) - 3,
  (1 << 21) - 9,
  (1 << 22) - 3,
  (1 << 23) - 15,
  (1 << 24) - 3,
  (1 << 25) - 39,
  (1 << 26) - 5,
  (1 << 27) - 39,
  (1 << 28) - 57,
  (1 << 29) - 3,
  (1U << 30) - 35,
  (1U << 31) - 1,
  UINT_MAX
};

/* Return a prime number that is "near" two times N */
int
_bow_primedouble (unsigned n)
{
  int i;
  for (i = 0; bow_primelist[i] < 2*n - n/2; i++)
    ;
  assert (bow_primelist[i] != UINT_MAX);
  return bow_primelist[i];
}
