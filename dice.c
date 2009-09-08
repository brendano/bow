
/*
Please see attachment for the sample program :  It takes distribution
from stdin, and output to stdout(some information to stderr).
Probabilities don't need to sum up to 1.  In the output, each article is
separated by an empty line; each word occupies a single line.


The job would be to write code that, given a probability distribution
over words, (in the form:

     0.022 foo
     0.015 bar
     0.001 baz
     ...

) would produce 60 documents of 200 words each, where the words would
be sampled from the given distribution.

*/

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <assert.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>

/* Defaults for command-line arguments. */

/* The number of documents to generate */
int ndocs = 100;

/* The number of words per document */
int nwords_per_doc = 20;

/* Prefix to each filename created */
const char *prefix = NULL;

/* Directory into which to place the documens. */
const char *dirname = NULL;

int noise_vocab_size = 0;
float noise_vocab_fraction = 0;

/* maximum number of words */
#define MAX	99999

struct {
  float P;
  char *w;
}word[MAX];

void
print_usage (const char *argv[])
{
  fprintf (stderr, "usage: %s "
	   "[-d dirname] [-p prefix] [-l doclen] [-n ndocs]\n"
	   "[-v noisevocabsize] [-f noisevocabfrac] distfile\n"
	   " Will output NDOCS files each of length DOCLEN with"
	   " filenames having \n"
	   " PREFIX to directory DIRNAME.\n"
	   " With probability NOISEVOCABFRAC, instead of picking"
	   " a word from the\n"
	   " distribution specified by DISTFILE, a word will be chosen"
	   " uniformly \n"
	   " from one of NOISEVOCABSIZE noise-words\n"
	   , argv[0]);
}

int
main (int argc, const char *argv[])
{
 int argi, N, i=0; 
 float x;
 char s[256];
 FILE *fp;
 char docname[1024];
 const char *distfile;
 int e;

 for (argi = 1; argi < argc; argi++)
   {
     if (argv[argi][0] != '-')
       break;
     switch (argv[argi][1])
       {
       case 'd':
	 dirname = argv[++argi];
	 break;
       case 'p':
	 prefix = argv[++argi];
	 break;
       case 'l':
	 nwords_per_doc = atoi (argv[++argi]);
	 break;
       case 'n':
	 ndocs = atoi (argv[++argi]);
	 break;
       case 'v':
	 noise_vocab_size = atoi (argv[++argi]);
	 break;
       case 'f':
	 noise_vocab_fraction = atof (argv[++argi]);
	 break;
       case '?':
       case 'h':
	 print_usage (argv);
	 exit (0);
       default:
	 fprintf (stderr, "%s: unrecognized option `%s'\n", 
		  argv[0], argv[argi]);
	 print_usage (argv);
	 exit (-1);
       }
   }
 distfile = argv[argi];

 if (dirname && dirname[0] == '/')
   fprintf (stderr, "Output to %s\n", dirname);
 else
   fprintf (stderr, "Output to ./%s\n", dirname);
 /* mkdir (dirname, S_IRWXU | S_IRWXG | S_IRWXO); */

 /* read in prob. distribution */
 fp = fopen (distfile, "r");
 while (i<MAX && fscanf(fp, "%f %s", &x, s)==2)
    {
     word[i].P = i==0? x : word[i-1].P+x;
     word[i].w = (char *)malloc(strlen(s)+1);
     strcpy(word[i].w, s);
     i++;
    }
 fclose (fp);
 if (i>=MAX)
   {
     printf("Error: number of words exceeds %d\n", MAX);
     exit (-1);
   }
 N = i;
 fprintf(stderr, "Cumulative Prob.=%f\n", word[N-1].P);

 /* Create the directory if necessary */
 e = mkdir (dirname, 0777);
 if (e != 0 && errno != EEXIST)
   {
     fprintf (stderr, "Error creating directory `%s'\n", dirname);
     perror ("dice");
     exit (-1);
   }

 /* generate documents */
 for (i = 0; i < ndocs; i++)
   {
    /* each with NWORDS_PER_DOC words */
    int j;
    if (prefix)
      sprintf (docname, "%s/%s%05d", dirname, prefix, i);
    else
      sprintf (docname, "%s/%05d", dirname, i);
    fp = fopen (docname, "w");
    assert (fp);
    for (j=0; j<nwords_per_doc; j++)
      {
	if (noise_vocab_fraction 
	    && rand()/(float)RAND_MAX > noise_vocab_fraction)
	  {
	    int wn = rand () % noise_vocab_size;
	    fprintf (fp, "noise");
	    /* Convert number WN into alphabetics */
	    while (wn)
	      {
		fprintf (fp, "%c", 'a' + wn % 10);
		wn /= 10;
	      }
	    fprintf (fp, "\n");
	  }
	else
	  {
	    float r= rand()/(float)RAND_MAX * word[N-1].P;
	    int k=0;
	    while (word[k].P<r) k++;
	    fprintf(fp, "%s\n", word[k].w);
	  }
      }
    fprintf (fp, "\n");
    fclose (fp);
   }
 exit (0);
}
