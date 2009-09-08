/* Managing lists of document names. */

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
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <dirent.h>
#include <string.h>
#include <assert.h>
#include <stdio.h>

int bow_map_verbosity_level = bow_chatty;

/* Calls the function CALLBACK for each of the filenames encountered
   when recursively descending the directory named DIRNAME.  CALLBACK
   should be a pointer to function that takes a filename char-pointer,
   and a void-pointer as arguments and returns an integer.  Currently
   the return value is ignored, but it may be used in the future to
   cut short, causing bow_map_filesnames_from_dir to return
   immediately.  The value CONTEXT will be passed as the second
   argument to the CALLBACK function; it provides you with a way to
   transfer context you may need inside the implementation of the
   callback function.  EXCLUDE_PATTERNS is currently ignored. */
int
bow_map_filenames_from_dir (int (*callback)(const char *filename, 
					    void *context),
			    void *context,
			    const char *dirname_arg,
			    const char *exclude_patterns)
{
  DIR *dir;
  struct dirent *dirent_p;
  struct stat st;
  char cwd[PATH_MAX];
  int num_files = 0;
  char dirname[1024];

  strcpy (dirname, dirname_arg);
#if 0
  /* strcat (dirname, "/"); */

  stat (dirname, &st);
  if (!S_ISDIR (st.st_mode))
    {
      /* assert ((dirname[0] == '/') || (dirname[0] == '\\') || 
	      ((dirname[1] == ':') && (dirname[2] == '\\'))); */
      /* DIRNAME is not a directory, it's a file. */
      (*callback) (dirname, context);
      return 1;
    }
#endif

  if (!(dir = opendir (dirname)))
    {
      perror (__PRETTY_FUNCTION__);
      getcwd (cwd, PATH_MAX);
      fprintf (stderr, "CWD is `%s'\n", cwd);
      fprintf (stderr,"Couldn't open directory `%s'.  Skipping.\n", dirname);
      return 1;
#if 0
      fprintf ("Opening as file.", dirname);
      (*callback) (dirname, context);
      return 1;
      /*fprintf (stderr,"Couldn't open directory `%s'.  Skipping.", dirname);*/
      /*bow_error ("Couldn't open directory `%s'", dirname);*/
#endif
    }

#if 0
  getcwd (initial_cwd, PATH_MAX);
  chdir (dirname);
  getcwd (cwd, PATH_MAX);
  strcat (cwd, "/");
  cwdlen = strlen (cwd);
#endif

  if (bow_verbosity_use_backspace 
      && bow_verbosity_level >= bow_map_verbosity_level)
    bow_verbosify (bow_progress, "%s:       ", dirname);
  while ((dirent_p = readdir (dir)))
    {
      char subname[strlen(dirname) + strlen(dirent_p->d_name) + 5];
      strcpy (subname, dirname);
      strcat (subname, "/");
      strcat (subname, dirent_p->d_name);
      
      stat (subname, &st);
      if (S_ISDIR (st.st_mode)
	  && strcmp (dirent_p->d_name, ".")
	  && strcmp (dirent_p->d_name, ".."))
	{
	  /* This directory entry is a subdirectory.  Recursively 
	     descend into it and append its files also. */
	  num_files += 
	    bow_map_filenames_from_dir (callback, context,
					subname, exclude_patterns);
	}
      else if (S_ISREG (st.st_mode))
	{
	  /* It's a regular file; add it to the list. */

	  /* xxx Move this out of this function, and subsitute
             BOW_EXCLUDE_FILENAME with EXCLUDE_PATTERNS here? */
	  if (bow_exclude_filename
	      && !strcmp (dirent_p->d_name, bow_exclude_filename))
	    continue;

	  /* Here is where we actually call the map-function with
	     the filename. */
	  (*callback) (subname, context);
	  num_files++;
	  if (!bow_verbosify (bow_screaming, "%6d Adding %s\n",
			      num_files, subname))
	    if (bow_verbosity_level >= bow_map_verbosity_level)
	      bow_verbosify (bow_progress, "\b\b\b\b\b\b%6d", num_files);
	}
    }
  closedir (dir);
#if 0
  chdir (initial_cwd);
#endif

  if (bow_verbosity_use_backspace
      && bow_verbosity_level >= bow_map_verbosity_level)
    bow_verbosify (bow_progress, "\n");
  
  return num_files;
}

/* Create a linked list of filenames, and append the document list
   pointed to by DL to it; return the new concatenated lists in *DL.
   The function returns the total number of filenames.  When creating
   the list, look for files (and recursively descend directories) in
   the directory DIRNAME, but don't include those matching
   EXCLUDE_PATTERNS. */
int
bow_doc_list_append (bow_doc_list **dl,
		     const char *dirname,
		     const char *exclude_patterns)
{
  bow_doc_list *dl_next = NULL;
  int append_filename (const char *filename, void *context)
    {
      dl_next = *dl;
      *dl = bow_malloc (sizeof (bow_doc_list) + strlen (filename) + 1);
      (*dl)->next = dl_next;
      strcpy ((*dl)->filename, filename);
      return 0;
    }

  return bow_map_filenames_from_dir (append_filename, NULL,
				     dirname, exclude_patterns);
}

/* Return the number of entries in the "docname list" DL. */
int
bow_doc_list_length (bow_doc_list *dl)
{
  int c = 0;
  for ( ; dl; dl = dl->next)
    c++;
  return c;
}

void
bow_doc_list_fprintf (FILE *fp, bow_doc_list *dl)
{
  while (dl)
    {
      fprintf (fp, "%s\n", dl->filename);
      dl = dl->next;
    }
}

void
bow_doc_list_free (bow_doc_list *dl)
{
  bow_doc_list *next_dl;

  while (dl)
    {
      next_dl = dl->next;
      bow_free (dl);
      dl = next_dl;
    }
}
