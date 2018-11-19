/* tee - read from standard input and write to standard output and files.
   Copyright (C) 1985-2018 Free Software Foundation, Inc.

   This program is free software: you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation, either version 3 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program.  If not, see <https://www.gnu.org/licenses/>.  */

/* Mike Parker, Richard M. Stallman, and David MacKenzie */

#include <config.h>
#include <sys/types.h>
#include <signal.h>
#include <getopt.h>

//additions for Teep functionality
#include <pthread.h>

//TODO FIXME temporary for debugging
#include <stdio.h>
#include <stdlib.h>

#include "system.h"
#include "argmatch.h"
#include "die.h"
#include "error.h"
#include "fadvise.h"
#include "stdio--.h"
#include "xbinary-io.h"

/* The official name of this program (e.g., no 'g' prefix).  */
#define PROGRAM_NAME "tee"

#define AUTHORS \
  proper_name ("Mike Parker"), \
  proper_name ("Richard M. Stallman"), \
  proper_name ("David MacKenzie"), \
  proper_name ("Brandon Foltz")

//this structure is shared by each worker BUT each worker has it's own descriptor
typedef struct parallel_tee_params_s {
	char * buffer;
	ssize_t * bytes_read;
	FILE * descriptor;
	char ** files;
	
	pthread_mutex_t * mutex;
	pthread_cond_t * writable;
	int * numWritersWaiting;
	int threadIndex;
	int numThreads;
	bool * canContinue;
} parallel_tee_params_t; 

static void * parallel_tee(parallel_tee_params_t * params);
static bool tee_files (int nfiles, char **files);

/* If true, append to output files rather than truncating them. */
static bool append;

/* If true, ignore interrupts. */
static bool ignore_interrupts;

enum output_error
  {
    output_error_sigpipe,      /* traditional behavior, sigpipe enabled.  */
    output_error_warn,         /* warn on EPIPE, but continue.  */
    output_error_warn_nopipe,  /* ignore EPIPE, continue.  */
    output_error_exit,         /* exit on any output error.  */
    output_error_exit_nopipe   /* exit on any output error except EPIPE.  */
  };

static enum output_error output_error;

static struct option const long_options[] =
{
  {"append", no_argument, NULL, 'a'},
  {"ignore-interrupts", no_argument, NULL, 'i'},
  {"output-error", optional_argument, NULL, 'p'},
  {GETOPT_HELP_OPTION_DECL},
  {GETOPT_VERSION_OPTION_DECL},
  {NULL, 0, NULL, 0}
};

static char const *const output_error_args[] =
{
  "warn", "warn-nopipe", "exit", "exit-nopipe", NULL
};
static enum output_error const output_error_types[] =
{
  output_error_warn, output_error_warn_nopipe,
  output_error_exit, output_error_exit_nopipe
};
ARGMATCH_VERIFY (output_error_args, output_error_types);

void
usage (int status)
{
  if (status != EXIT_SUCCESS)
    emit_try_help ();
  else
    {
      printf (_("Usage: %s [OPTION]... [FILE]...\n"), program_name);
      fputs (_("\
Copy standard input to each FILE, and also to standard output.\n\
\n\
  -a, --append              append to the given FILEs, do not overwrite\n\
  -i, --ignore-interrupts   ignore interrupt signals\n\
"), stdout);
      fputs (_("\
  -p                        diagnose errors writing to non pipes\n\
      --output-error[=MODE]   set behavior on write error.  See MODE below\n\
"), stdout);
      fputs (HELP_OPTION_DESCRIPTION, stdout);
      fputs (VERSION_OPTION_DESCRIPTION, stdout);
      fputs (_("\
\n\
MODE determines behavior with write errors on the outputs:\n\
  'warn'         diagnose errors writing to any output\n\
  'warn-nopipe'  diagnose errors writing to any output not a pipe\n\
  'exit'         exit on error writing to any output\n\
  'exit-nopipe'  exit on error writing to any output not a pipe\n\
The default MODE for the -p option is 'warn-nopipe'.\n\
The default operation when --output-error is not specified, is to\n\
exit immediately on error writing to a pipe, and diagnose errors\n\
writing to non pipe outputs.\n\
"), stdout);
      emit_ancillary_info (PROGRAM_NAME);
    }
  exit (status);
}

//TODO FIXME delete me! for debugging only!
long debugPrint(char * string); //prototype just to make the warning go away
pthread_mutex_t printMutex;
long debugPrint(char * string) {
    static long timestamp = 0;
    pthread_mutex_lock(&printMutex);
    timestamp++;
    FILE *fp = fopen("/var/tmp/teep_debug.txt", "a"); 
    fprintf(fp, "%ld: %s", timestamp, string);
    fflush(fp);
    fclose(fp);
    pthread_mutex_unlock(&printMutex);
    return timestamp;    
}

int
main (int argc, char **argv)
{
  bool ok;
  int optc;

  initialize_main (&argc, &argv);
  set_program_name (argv[0]);
  setlocale (LC_ALL, "");
  bindtextdomain (PACKAGE, LOCALEDIR);
  textdomain (PACKAGE);

  atexit (close_stdout);

  append = false;
  ignore_interrupts = false;

  while ((optc = getopt_long (argc, argv, "aip", long_options, NULL)) != -1)
    {
      switch (optc)
        {
        case 'a':
          append = true;
          break;

        case 'i':
          ignore_interrupts = true;
          break;

        case 'p':
          if (optarg)
            output_error = XARGMATCH ("--output-error", optarg,
                                      output_error_args, output_error_types);
          else
            output_error = output_error_warn_nopipe;
          break;

        case_GETOPT_HELP_CHAR;

        case_GETOPT_VERSION_CHAR (PROGRAM_NAME, AUTHORS);

        default:
          usage (EXIT_FAILURE);
        }
    }

  if (ignore_interrupts)
    signal (SIGINT, SIG_IGN);

  if (output_error != output_error_sigpipe)
    signal (SIGPIPE, SIG_IGN);

  /* Do *not* warn if tee is given no file arguments.
     POSIX requires that it work when given no arguments.  */

  pthread_mutex_init(&printMutex, NULL); //TODO FIXME delete me!

  ok = tee_files (argc - optind, &argv[optind]);
  if (close (STDIN_FILENO) != 0)
    die (EXIT_FAILURE, errno, "%s", _("standard input"));

  return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}

static void *
parallel_tee(parallel_tee_params_t * params)
{
	char stringBuffer[128] = {0};
	sprintf(stringBuffer, "Thread for file %s started.\n", (*params).files[(*params).threadIndex]);
	debugPrint(stringBuffer);
	static bool ok = true;
	bool canContinue = true;
	bool refilledBuffer = false;
	while (canContinue) {
		//TODO Tee writing goes here
		if ((*params).descriptor
		&& fwrite ((*params).buffer, *(*params).bytes_read, 1, (*params).descriptor) != 1)
		{
			int w_errno = errno;
			bool fail = errno != EPIPE || (output_error == output_error_exit
							              || output_error == output_error_warn);
			if ((*params).descriptor == stdout)
			  clearerr (stdout); // Avoid redundant close_stdout diagnostic.  
			if (fail)
			{
				error (output_error == output_error_exit
				   || output_error == output_error_exit_nopipe,
				   w_errno, "%s", quotef ((*params).files[(*params).threadIndex]));
			}
			(*params).descriptor = NULL;
			if (fail) {
			  ok = false;
			  //n_outputs--; //FIXME This indicates we should quit? Go in critical section?
			  //puts("One thread failed!"); //FIXME only for debugging
			}
		}
	
		pthread_mutex_lock((*params).mutex);
		*(*params).numWritersWaiting += 1;
		if (*(*params).numWritersWaiting == (*params).numThreads) {
			
			//refill the buffer, and trigger all threads quitting if necessary
			*(*params).bytes_read = read (0, (*params).buffer, sizeof (*params).buffer);
		    if (*(*params).bytes_read < 0 && errno == EINTR)
			  *(*params).canContinue = true;
		    if (*(*params).bytes_read <= 0) {
			  *(*params).canContinue = false;
			}
			canContinue = *(*params).canContinue; //signalling other threads to quit (or not)
			
			*(*params).numWritersWaiting = 0;
			refilledBuffer = true; 	
		} else {
			pthread_cond_wait((*params).writable, (*params).mutex);
		}
		pthread_mutex_unlock((*params).mutex);
		if (refilledBuffer) { //avoid spurious wakeup by releasing lock before broadcast
			pthread_cond_broadcast((*params).writable);
			refilledBuffer = false;
		}
	}    

	return &ok;
}

/* Copy the standard input into each of the NFILES files in FILES
   and into the standard output.  As a side effect, modify FILES[-1].
   Return true if successful.  */

static bool
tee_files (int nfiles, char **files)
{
  puts("Tee files"); //TODO FIXME debugging only
  size_t n_outputs = 0;
  FILE **descriptors;
  char buffer[BUFSIZ];
  ssize_t bytes_read = 0;
  int i;
  bool ok = true;
  char const *mode_string =
    (O_BINARY
     ? (append ? "ab" : "wb")
     : (append ? "a" : "w"));

  xset_binary_mode (STDIN_FILENO, O_BINARY);
  xset_binary_mode (STDOUT_FILENO, O_BINARY);
  fadvise (stdin, FADVISE_SEQUENTIAL);

  /* Set up FILES[0 .. NFILES] and DESCRIPTORS[0 .. NFILES].
     In both arrays, entry 0 corresponds to standard output.  */

  descriptors = xnmalloc (nfiles + 1, sizeof *descriptors);
  files--;
  descriptors[0] = stdout;
  files[0] = bad_cast (_("standard output"));
  setvbuf (stdout, NULL, _IONBF, 0);
  n_outputs++; //standard output is always in the set of outputs

  char stringBuffer[128] = {0};
  sprintf(stringBuffer, "315: n_outputs: %d, nfiles: %d\n", n_outputs, nfiles);
  debugPrint(stringBuffer);
  for (i = 1; i <= nfiles; i++)
    {
      /* Do not treat "-" specially - as mandated by POSIX.  */
      descriptors[i] = fopen (files[i], mode_string);
      if (descriptors[i] == NULL)
        {
          error (output_error == output_error_exit
                 || output_error == output_error_exit_nopipe,
                 errno, "%s", quotef (files[i]));
          ok = false;
        }
      else
        {
          setvbuf (descriptors[i], NULL, _IONBF, 0);
          n_outputs++;
        }
    }
    
  sprintf(stringBuffer, "336: n_outputs: %ld, nfiles: %ld\n", n_outputs, nfiles);
  debugPrint(stringBuffer);  
    
  //descriptors are open and ready to go, read in the first data buffer
  bytes_read = read(0, buffer, sizeof buffer);
  bool canContinue = true;
  if (bytes_read < 0 && errno == EINTR) {
    canContinue = true;
  }
  if (bytes_read <= 0) {
    canContinue = false; 
  }
   
  sprintf(stringBuffer, "348: Can continue: %d, bytes_read: %d, errno: %d\n", (int)canContinue, bytes_read, errno);
  debugPrint(stringBuffer);   
   
  if (canContinue) { 
	  //setup and spawn worker threads 
	  pthread_t workers[nfiles];
	  parallel_tee_params_t tee_params[nfiles]; 
	  
	  //these variables are shared among the threads
	  pthread_mutex_t mutex;
	  pthread_cond_t writable;
	  pthread_mutex_init(&mutex, NULL);
	  pthread_cond_init(&writable, NULL);
	  int numWritersWaiting = 0;
	  for (i = 0; i <= nfiles; i++) { 
	  	if (descriptors[i]) { //don't start threads for bad descriptors!
		  	tee_params[i].buffer = buffer;
		  	tee_params[i].bytes_read = &bytes_read;
		  	tee_params[i].descriptor = descriptors[i];
		  	tee_params[i].files = files;
		  	tee_params[i].mutex = &mutex;
		  	tee_params[i].writable = &writable;
		  	tee_params[i].numWritersWaiting = &numWritersWaiting;
		  	tee_params[i].threadIndex = i;
		  	tee_params[i].numThreads = nfiles;
		  	tee_params[i].canContinue = &canContinue; //naughty variable re-use
		  	char stringBuffer[128] = {0};
		  	sprintf(stringBuffer, "Starting a thread for descriptor %d\n", i);
		  	debugPrint(stringBuffer);
		  	pthread_create(&workers[i], NULL, (void *)parallel_tee, &tee_params[i]);
	    }
	  }
  
	  //harvest finished threads
	  //bool retval[nfiles];
	  for (i = 0; i <= nfiles; i++) {
	  	char stringBuffer[128] = {0};
	  	sprintf(stringBuffer, "Waiting thread %d to die...\n", i);
	  	debugPrint(stringBuffer);
	  	
	  	pthread_join(workers[i], (void *)NULL); //&retval[i]);
	  }
  }

  if (bytes_read == -1)
    {
      error (0, errno, _("read error"));
      ok = false;
    }

  /* Close the files, but not standard output.  */
  for (i = 1; i <= nfiles; i++)
    if (descriptors[i] && fclose (descriptors[i]) != 0)
      {
        error (0, errno, "%s", quotef (files[i]));
        ok = false;
      }

  free (descriptors);

  return ok;
}
