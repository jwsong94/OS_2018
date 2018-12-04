/**
 * OS Assignment #5
 *
 * @file person.c
 **/

#include "person.h"
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <errno.h>

static Person *person;

static int
open_mmap (const char *file_name)
{
  int fd;

  fd = open (file_name, O_RDWR);
  if (fd < 0)
    {
      Person dummy;

      /* Create the file. */
      fd = open (file_name, O_RDWR | O_CREAT, 0666);
      if (fd < 0)
	{
	  fprintf (stderr, "failed create file '%s' - %s(%d)\n", file_name, strerror (errno), errno);
	  return -1;
	}

      memset (&dummy, 0x00, sizeof (Person));
      if (write (fd, &dummy, sizeof (Person)) != sizeof (Person))
	{
	  fprintf (stderr, "failed write file '%s' - %s(%d)\n", file_name, strerror (errno), errno);
	  close (fd);
	  return -1;
	}
    }

  person = mmap (NULL,
		 sizeof (Person),
		 PROT_READ | PROT_WRITE,
		 MAP_SHARED,
		 fd,
		 0);
  if (person == MAP_FAILED)
    {
      fprintf (stderr, "failed map to file '%s' - %s(%d)\n", file_name, strerror (errno), errno);
      close (fd);
      return -1;
    }

  return 0;
}

static void
close_mmap (int fd)
{
  munmap (person, sizeof (Person));
  close (fd);
}

static void
set_person_attr (const char *attr_name,
		 const char *value)
{
  char         *ptr;
  size_t       offset;
  int          i;

  offset = person_get_offset_of_attr (attr_name);
  if (offset < 0)
    return;

  ptr = (char *) person + offset;
  if (person_attr_is_integer (attr_name))
    {
      int val;

      val = atoi (value);
      if (val == *(int *) ptr)
	return;

      *(int *) ptr = val;
    }
  else
    {
      if (!strcmp (ptr, value))
	return;

      strcpy (ptr, value);
    }

  for (i = 0; i < NOTIFY_MAX; i++)
    {
      union sigval sval;
      pid_t pid;

      pid = person->watchers[i];
      if (pid == 0)
	continue;

      /* TODO: Notify the change to watchers. */
      // Send signal
      kill(pid, SIGUSR1);
    }
}

static const char *
get_person_attr_str (const char *attr_name)
{
  size_t offset;

  offset = person_get_offset_of_attr (attr_name);
  if (offset < 0)
    return NULL;

  return (char *) person + offset;
}

static const int
get_person_attr_int (const char *attr_name)
{
  const char *val;

  val = get_person_attr_str (attr_name);

  return * (int *) val;
}

static void
signal_received (int        signo,
		 siginfo_t *info,
		 void      *context)
{
  size_t      offset;
  const char *attr_name;

  if (signo == SIGINT || signo == SIGTERM)
    {
      int i;

      /* Unregister notifier and exit. */
      for (i = 0; i < NOTIFY_MAX; i++)
	if (person->watchers[i] == getpid ())
	  {
	    person->watchers[i] = 0;
	    break;
	  }
      fprintf (stdout, "\n");
      exit (0);
    }

  if (signo != SIGUSR1)
    {
      fprintf (stderr, "unknown signal[%d] received\n", signo);
      return;
    }

  offset = info->si_value.sival_int;
  attr_name = person_lookup_attr_with_offset (offset);
  if (!attr_name)
    {
      fprintf (stderr, "invalid person attribute offset '%d' - %s(%d)\n", (int) offset, strerror (errno), errno);
      return;
    }

  if (person_attr_is_integer (attr_name))
    {
      int value;

      value = get_person_attr_int (attr_name);
      fprintf (stdout, "%s: '%d'", attr_name, value);
    }
  else
    {
      const char *value;

      value = get_person_attr_str (attr_name);
      fprintf (stdout, "%s: '%s'", attr_name, value);
    }

  fprintf (stdout, " from '%d'\n", info->si_pid);
}

static int
watch_person (void)
{
  struct sigaction action;
  int i;

  /* TODO: Register signal handlers. */
  // Set signal handler
  signal(SIGUSR1, (void *)signal_received);

  /* Register pid for signal notification. */
  for (i = 0; i < NOTIFY_MAX; i++)
    if (person->watchers[i] == 0)
      {
	person->watchers[i] = getpid ();
	break;
      }
  if (i == NOTIFY_MAX)
    person->watchers[0] = getpid ();

  fprintf (stdout, "watching... \n");

  while (1)
    usleep (1000);

  return 0;
}

static void
print_usage (const char *prog)
{
  fprintf (stderr, "usage: %s [-f file] [-w] [-s value] attr_name\n", prog);
  fprintf (stderr, "  -f file  : person file, default is './person.dat'\n");
  fprintf (stderr, "  -w       : watch mode\n");
  fprintf (stderr, "  -s value : set the value for the given 'attr_name'\n");
}

int
main (int    argc,
      char **argv)
{
  const char *file_name;
  const char *set_value;
  const char *attr_name;
  int         watch_mode;
  int         fd;

  /* Parge command line arguments. */
  file_name  = "./person.dat";
  set_value  = NULL;
  watch_mode = 0;
  while (1)
    {
      int opt;

      opt = getopt (argc, argv, "ws:f:");
      if (opt < 0)
	break;

      switch (opt)
	{
	case 'w':
	  watch_mode = 1;
	  break;
	case 's':
	  set_value = optarg;
	  break;
	case 'f':
	  file_name = optarg;
	  break;
	default:
	  print_usage (argv[0]);
	  return -1;
	  break;
	}
    }
  if (!watch_mode && optind >= argc)
    {
      print_usage (argv[0]);
      return -1;
    }
  attr_name = argv[optind];

  /* Check whether attribute name is valid. */
  if (!watch_mode && person_get_offset_of_attr (attr_name) < 0)
    {
      fprintf (stderr, "invalid attr name '%s'\n", attr_name);
      return -1;
    }

  /* Create a memory map for the give file. */
  fd = open_mmap (file_name);
  if (fd < 0)
    {
      fprintf (stderr, "failed to map to the file '%s'\n", file_name);
      return -1;
    }

  /* Run into the watch mode. */
  if (watch_mode)
    {
      watch_person ();
    }
  else
    {
      /* Set the value for the given attribute. */
      if (set_value)
	set_person_attr (attr_name, set_value);

      /* Print the value for the given attribute. */
      if (person_attr_is_integer (attr_name))
	{
	  int value;

	  value = get_person_attr_int (attr_name);
	  fprintf (stdout, "%d\n", value);
	}
      else
	{
	  const char *value;
	  
	  value = get_person_attr_str (attr_name);
	  fprintf (stdout, "%s\n", value);
	}
    }

  /* Close the mapped file. */
  close_mmap (fd);

  return 0;
}
