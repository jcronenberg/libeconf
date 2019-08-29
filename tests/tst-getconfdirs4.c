#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <stdio.h>
#include <string.h>

#include "libeconf.h"

/* Test case:
   /etc/getconfdir.conf exists
   No configuration files in /usr/etc exists

   This is to make migration easier, if the same config file is
   read by different projects.
*/

int
check_key(Key_File *key_file, char *key, char *expected_val)
{
  char *val = NULL;
  econf_err error = econf_getStringValue (key_file, "", key, &val);
  if (expected_val == NULL)
    {
      if (val == NULL)
        return 0;

      fprintf (stderr, "ERROR: %s has value \"%s\"\n", key, val);
      return 1;
    }
  if (val == NULL || strlen(val) == 0)
    {
      fprintf (stderr, "ERROR: %s returns nothing! (%s)\n", key,
               econf_errString(error));
      return 1;
    }
  if (strcmp (val, expected_val) != 0)
    {
      fprintf (stderr, "ERROR: %s is not \"%s\"\n", key, expected_val);
      return 1;
    }

  printf("Ok: %s=%s\n", key, val);
  free (val);
  return 0;
}

int
main(int argc, char **argv)
{
  Key_File *key_file;
  int retval = 0;
  econf_err error;

  error = econf_get_conf_from_dirs (&key_file,
				    TESTSDIR"tst-getconfdirs4-data/usr/etc",
				    TESTSDIR"tst-getconfdirs4-data/etc",
				    "getconfdir", ".conf", "=", '#');
  if (error)
    {
      fprintf (stderr, "ERROR: econf_get_conf_from_dirs returned NULL: %s\n",
	       econf_errString(error));
      return 1;
    }

  if (check_key(key_file, "KEY1", "etc") != 0)
    retval = 1;
  if (check_key(key_file, "ETC", "true") != 0)
    retval = 1;
  if (check_key(key_file, "USRETC", NULL) != 0)
    retval = 1;
  if (check_key(key_file, "OVERRIDE", NULL) != 0)
    retval = 1;

  econf_destroy (key_file);

  return retval;
}
