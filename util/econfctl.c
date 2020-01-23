/*
  Copyright (C) 2020 SUSE Software Solutions Germany GmbH
  Author: Dominik Gedon <dgedon@suse.de>

  Permission is hereby granted, free of charge, to any person obtaining a copy
  of this software and associated documentation files (the "Software"), to deal
  in the Software without restriction, including without limitation the rights
  to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
  copies of the Software, and to permit persons to whom the Software is
  furnished to do so, subject to the following conditions:
  The above copyright notice and this permission notice shall be included in all
  copies or substantial portions of the Software.
  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
  OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
  SOFTWARE.
*/

#include <errno.h>
#include <getopt.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>

#include "libeconf.h"

static void newProcess(const char *, char *, const char *, econf_file *);
static void deleteTmpFiles(void);
static void usage(void);

static const char *utilname = "econfctl";
static const char *TMPPATH= "/tmp";
static const char *TMPFILE_ORIG = "econfctl.tmp";
static const char *TMPFILE_EDIT = "econfctl_edits.tmp";
static const char *DROPINFILENAME = "90_econfctl.conf";
static bool isRoot = false;
static bool isDropinFile = true;

int main (int argc, char *argv[])
{
    static const char CONFDIR[] = "/.config";
    econf_file *key_file = NULL;
    econf_err error;
    char *suffix = NULL; /* the suffix of the filename e.g. .conf */
    char *posLastDot;
    char path[4096]; /* the path of the config file */
    char home[4096]; /* the path of the home directory */
    char filename[4096]; /* the filename without the suffix */
    char filenameSuffix[4096]; /* the filename with the suffix */
    char pathFilename[4096]; /* the path concatenated with the filename and the suffix */
    uid_t uid = getuid();
    uid_t euid = geteuid();
    char username[256];
    struct passwd *pw = getpwuid(uid);

    memset(path, 0, 4096);
    memset(home, 0, 4096);
    memset(filename, 0, 4096);
    memset(pathFilename, 0, 4096);

    /* getopt_long */
    int opt;
    int index = 0;
    static struct option longopts[] = {
    /*   name,     arguments,      flag, value */
        {"full",   no_argument,       0, 'f'},
        {"help",   no_argument,       0, 'h'},
        {0,        0,                 0,  0 }
    };

     while ((opt = getopt_long(argc, argv, "hf",longopts, &index)) != -1)
    {
        switch(opt)
        {
            case 'f':
                /* overwrite path */
                snprintf(path, strlen("/etc") + 1, "%s", "/etc");
                isDropinFile = false;
                break;
            case 'h':
                usage();
                break;
            case '?':
            default:
                fprintf(stderr, "Try '%s --help' for more information.\n", utilname);
                exit(EXIT_FAILURE);
                break;
        }
    }

    /* only do something if we have an input */
    if (argc < 2)
    {
        usage();
    } else if (argc < 3)
    {
        fprintf(stderr, "Missing filename!\n");
        exit(EXIT_FAILURE);
    } else if (argc > 4)
    {
        fprintf(stderr, "Too many arguments!\n");
        exit(EXIT_FAILURE);
    } else if (argc == 3 && (strcmp(argv[2], "--full") == 0))
    {
        fprintf(stderr, "Missing filename!\n");
        exit(EXIT_FAILURE);
    }

    /**** initialization ****/

    /* retrieve the username from the password file entry */
    if (pw)
    {
      snprintf(username, strlen(pw->pw_name) + 1, "%s", pw->pw_name);
    }

    /* basic write permission check */
    if (uid == 0 && uid == euid)
    {
        isRoot = true;
    } else
    {
        isRoot = false;
    }  

    /* get the position of the last dot in the filename to extract
     * the suffix from it. With edit we have to include the
     * possibility that someone uses the --full option and take
     * that into account when extracting the suffix.
     */
    if (argc == 3)
    {
        posLastDot = strrchr(argv[2], 46); /* . (dot) in ASCII is 46 */
    } else
    {
        posLastDot = strrchr(argv[3], 46); /* . (dot) in ASCII is 46 */
    }
    if (posLastDot == NULL)
    {
        fprintf(stderr, "-->Currently only works with a dot in the filename!\n");
        exit(EXIT_FAILURE);
    }
    suffix = posLastDot;

    /* set filename to the proper argv argument
     * argc == 3 -> edit
     * argc == 4 -> edit --full
     */
    if (argc == 4)
    {
        snprintf(filename, strlen(argv[3]) -  strlen(posLastDot) + 1, "%s", argv[3]);
        snprintf(filenameSuffix, strlen(argv[3]) + 1, "%s", argv[3]);
    } else
    {
        snprintf(filename, strlen(argv[2]) -  strlen(posLastDot) + 1, "%s", argv[2]);
        snprintf(filenameSuffix, strlen(argv[2]) + 1, "%s", argv[2]);
    }    

    if (isDropinFile)
    {
        snprintf(path, strlen("/etc/") + strlen(filenameSuffix) + 5, "%s%s%s", "/etc/", filenameSuffix, ".d");
    }
    snprintf(pathFilename, strlen(path) + strlen(filenameSuffix) + 4, "%s%s%s", path, "/", filenameSuffix);
    snprintf(home, strlen(getenv("HOME")) + 1, "%s", getenv("HOME"));

    const char *editor = getenv("EDITOR");
    if(editor == NULL)
    {
        /* if no editor is specified take vim as default */
        editor = "/usr/bin/vim";
    }
    char *xdgConfigDir = getenv("XDG_CONFIG_HOME");
    if (xdgConfigDir == NULL)
    {
        /* if no XDG_CONFIG_HOME ist specified take ~/.config as
         * default
         */
        strncat(home, CONFDIR, sizeof(home) - strlen(home) - 1);
        xdgConfigDir = home;        
    }
    
    fprintf(stdout, "|--Initial values-- \n");
    fprintf(stdout, "|filename: %s\n", filename); /* debug */
    fprintf(stdout, "|suffix: %s\n", suffix); /* debug */
    fprintf(stdout, "|filenameSuffix: %s\n", filenameSuffix); /* debug */
    fprintf(stdout, "|XDG conf dir: %s\n", xdgConfigDir); /* debug */
    fprintf(stdout, "|home: %s\n", home); /* debug */
    fprintf(stdout, "|path: %s\n", path); /* debug */
    fprintf(stdout, "|pathFilename: %s\n\n", pathFilename); /* debug */
    

    /****************************************************************
     * @brief This command will read all snippets for filename.conf
     *        (econf_readDirs) and print all groups, keys and their
     *        values as an application would see them.
     */
    if (strcmp(argv[optind], "show") == 0)
    {
        fprintf(stdout, "command: econfctl show\n\n"); /* debug */

        if ((error = econf_readDirs(&key_file, "/usr/etc", "/etc", filename, suffix,"=", "#")))
        {
            fprintf(stderr, "%s\n", econf_errString(error));
            econf_free(key_file);
            return EXIT_FAILURE;
        }

        char **groups = NULL;
        char *value = NULL;
        size_t group_count = 0;

        /* show groups, keys and their value */
        if ((error = econf_getGroups(key_file, &group_count, &groups)))
        {
            fprintf(stderr, "%s\n", econf_errString(error));
            econf_free(groups);
            econf_free(key_file);
            return EXIT_FAILURE;
        }
        char **keys = NULL;
        size_t key_count = 0;

        for (size_t g = 0; g < group_count; g++)
        {
            if ((error = econf_getKeys(key_file, groups[g], &key_count, &keys)))
            {
                fprintf(stderr, "%s\n", econf_errString(error));
                econf_free(groups);
                econf_free(keys);
                econf_free(key_file);
                return EXIT_FAILURE;
            }
            printf("%s\n", groups[g]);
            for (size_t k = 0; k < key_count; k++) {
                if ((error = econf_getStringValue(key_file, groups[g], keys[k], &value))
                    || value == NULL || strlen(value) == 0)
                {
                    fprintf(stderr, "%s\n", econf_errString(error));
                    econf_free(groups);
                    econf_free(keys);
                    econf_free(key_file);
                    free(value);
                    return EXIT_FAILURE;
                }
                printf("%s = %s\n", keys[k], value);
                free(value);
            }
            printf("\n");
            econf_free(keys);
        }
        econf_free(groups);

    /****************************************************************
     * @brief This command will print the content of the files and the name of the
     *        file in the order as read by econf_readDirs.
     * TODO
     */
    } else if (strcmp(argv[optind], "cat") == 0)
    {

    /****************************************************************
     * @brief This command will start an editor (EDITOR environment variable),
     *        which shows all groups, keys and their values (like econfctl
     *        show output), allows the admin to modify them, and stores the
     *        changes afterwards in a drop-in file in /etc/filename.conf.d.
     *
     * --full: copy the original config file to /etc instead of creating drop-in
     *         files.
     *
     * If not root, the file will be created in XDG_CONF_HOME, which is normally
     * $HOME/.config.
     *
     *  TODO:
     *      - Replace static values of the path with future libeconf API calls
     */
    } else if (strcmp(argv[optind], "edit") == 0)
    {
        fprintf(stdout, "|command: edit\n\n"); /* debug */
        fprintf(stdout, "|filename: %s\n", filename); /* debug */
        fprintf(stdout, "|filenameSuffix: %s\n", filenameSuffix); /* debug */
        fprintf(stdout, "|path: %s\n", path); /* debug */
        fprintf(stdout, "|pathFilename: %s\n", pathFilename); /* debug */

        if (argc == 4 && (strcmp(argv[optind - 1], "--full") != 0))
        {
            fprintf(stderr, "Unknown command!\n");
            exit(EXIT_FAILURE);
        } else if (argc == 3 && (strcmp(argv[optind - 1], "--full") == 0))
        {
                fprintf(stderr, "Missing filename!\n");
                exit(EXIT_FAILURE);
        } else
        {  
            fprintf(stdout, "|Reading key_file\n"); /* debug */
            error = econf_readDirs(&key_file, "/usr/etc", "/etc", filename, suffix,"=", "#");

            if (error == 3)
            { /* the file does not exist */
                
                /* create empty key file */
                fprintf(stdout, "|File does not exist\n"); /* debug */
                fprintf(stdout, "|Creating empty key_file\n"); /* debug */
                if ((error = econf_newIniFile(&key_file)))
                {
                    fprintf(stderr, "%s\n", econf_errString(error));
                    econf_free(key_file);
                    return EXIT_FAILURE;
                }
            } else if ((error =! 3) || (error != 0))
            {
                /* other errors besides "missing config file" or "no error" */
                fprintf(stderr, "%s\n", econf_errString(error));
                econf_free(key_file);
                return EXIT_FAILURE;
            }
            if (!isRoot)
            {
                /* adjust path to home directory of the user.*/
                snprintf(path, strlen(xdgConfigDir) + 1, "%s", xdgConfigDir);
                fprintf(stdout, "|Not root\n"); /* debug */
                fprintf(stdout, "|Overwriting path with XDG_CONF_DIR\n\n"); /* debug */
            } else
            {
                if(isDropinFile)
                {
                    memset(filename, 0, 4096);
                    snprintf(filenameSuffix, strlen(DROPINFILENAME) + 1, "%s", DROPINFILENAME);
                }
            }
            /* Open $EDITOR in new process */
            newProcess(editor, path, filenameSuffix, key_file);
    }

    /****************************************************************
     * @biref Revert all changes to the vendor versions. In the end this means
     *        most likely to delete all files in /etc for this.
     */
    } else if (strcmp(argv[optind], "revert") == 0)
    {
        fprintf(stdout, "|command: revert\n\n"); /* debug */
        fprintf(stdout, "|filename: %s\n", filename); /* debug */
        fprintf(stdout, "|filenameSuffix: %s\n", filenameSuffix); /* debug */
        fprintf(stdout, "|path: %s\n", path); /* debug */
        fprintf(stdout, "|pathFilename: %s\n\n", pathFilename); /* debug */
    
        char input[2] = "";

        /* let the user verify 2 times that the file should really be deleted */ 
        do
        {
            fprintf(stdout, "Delete file /etc/%s?\nYes [y], no [n]\n", argv[2]);
            scanf("%2s", input);
        } while (strcmp(input, "y") != 0 && strcmp(input, "n") != 0);

        if (strcmp(input, "y") == 0)
        {
            memset(input, 0, 2);
            do
            {
                fprintf(stdout, "Do you really wish to delete the file /etc/%s?\n", argv[2]);
                fprintf(stdout, "There is no going back!\nYes [y], no [n]\n");
                scanf("%2s", input);
            } while (strcmp(input, "y") != 0 && strcmp(input, "n") != 0);

            if(strcmp(input, "y") == 0)
            {
                snprintf(pathFilename, strlen(filename) + strlen(suffix) + 8, "%s%s%s", "/etc/", filename, suffix);

                int status = remove(pathFilename);
                if (status != 0)
                {
                    fprintf(stdout, "%s\n", strerror(errno));
                } else
                {
                    fprintf(stdout, "File %s deleted!\n", pathFilename);
                }
            }
        }
    } else
    {
        fprintf(stderr, "Unknown command!\n");
        exit(EXIT_FAILURE);
    }  

    /* cleanup */
    econf_free(key_file);
    deleteTmpFiles();
    return EXIT_SUCCESS;
}

/**
 * @brief Creates a new process to execute a command.
 * @param  command The command which should be executed
 * @param  path The constructed path for saving the file later
 * @param  filenameSuffix The filename with suffix
 * @param  key_file the stored config file information
 *
 * TODO: - make a diff of the two tmp files to see what actually changed and
 *         write only that change e.g. into a drop-in file.
 */
static void newProcess(const char *command, char *path, const char *filenameSuffix, econf_file *key_file)
{
    fprintf(stdout, "\n|----newProcess()----\n"); /* debug */
    fprintf(stdout, "|command: %s\n", command); /* debug */
    fprintf(stdout, "|path: %s\n", path); /* debug */
    fprintf(stdout, "|filename with suffix: %s\n", filenameSuffix); /* debug */

    char pathFilename[4096];
    memset(pathFilename, 0, 4096);
    econf_err error;
    int wstatus = 0;
    pid_t pid = fork();

    if (pid == -1)
    {
        fprintf(stderr, "-->Error with fork()\n");
        exit(EXIT_FAILURE);

    } else if (pid == 0)
    { /* child */

        /* write contents of key_file to 2 temporary files. In the future this
         * will be used to make a diff between the two files and only save the
         * changes the user made.
         */
        if ((error = econf_writeFile(key_file, TMPPATH, TMPFILE_ORIG)))
        {
            fprintf(stderr, "-->Child: econf_writeFile() 1 Error!\n"); /* debug */
            fprintf(stderr, "%s\n", econf_errString(error));
            econf_free(key_file);
            deleteTmpFiles();
            exit(EXIT_FAILURE);
        }
        if ((error = econf_writeFile(key_file, TMPPATH, TMPFILE_EDIT)))
        {
            fprintf(stderr, "-->Child: econf_writeFile() 2  Error!\n"); /* debug */
            fprintf(stderr, "%s\n", econf_errString(error));
            econf_free(key_file);
            deleteTmpFiles();
            exit(EXIT_FAILURE);
        }

        /* combine path and filename of the tmp files and set permission to 600 */
        size_t combined_length = strlen(TMPPATH) + strlen(TMPFILE_ORIG) + 2;
        char combined_tmp1[combined_length];
        memset(combined_tmp1, 0, combined_length);
        snprintf(combined_tmp1, combined_length, "%s%s%s", TMPPATH, "/", TMPFILE_ORIG);

        int perm = chmod(combined_tmp1, S_IRUSR | S_IWUSR);
        if (perm != 0)
        {
        	econf_free(key_file);
        	deleteTmpFiles();
            exit(EXIT_FAILURE);
        }

        combined_length = strlen(TMPPATH) + strlen(TMPFILE_EDIT) + 2;
        char path_tmpfile_edit[combined_length];
        memset(path_tmpfile_edit, 0, combined_length);
        snprintf(path_tmpfile_edit, combined_length, "%s%s%s", TMPPATH, "/", TMPFILE_EDIT);

        perm = chmod(path_tmpfile_edit, S_IRUSR | S_IWUSR);
        if (perm != 0)
        {
        	econf_free(key_file);
        	deleteTmpFiles();
            exit(EXIT_FAILURE);
        }

        /* execute given command and save in TMPFILE_EDIT */
        execlp(command, command, path_tmpfile_edit, (char *) NULL);

    } else
    { /* parent */
        if (waitpid(pid, &wstatus, 0) == - 1)
        {
            fprintf(stderr, "-->Error using waitpid().\n");
            econf_free(key_file);
            deleteTmpFiles();
            exit(EXIT_FAILURE);
        }
        if (WIFEXITED(wstatus))
        {
            fprintf(stdout, "|Exitstatus child (0 = OK): %d\n\n", WEXITSTATUS(wstatus)); /* debug */
        }

        /* save edits from TMPFILE_EDIT in new key_file */
        econf_file *key_file_edit = NULL;
        size_t combined_length = strlen(TMPPATH) + strlen(TMPFILE_EDIT) + 2;
        char tmpfile_edited[combined_length];
        memset(tmpfile_edited, 0, combined_length);
        snprintf(tmpfile_edited, combined_length, "%s%s%s", TMPPATH, "/", TMPFILE_EDIT);

        if ((error = econf_readFile(&key_file_edit, tmpfile_edited, "=", "#")))
        {
            fprintf(stderr, "-->econf_readFile() 3 Error!\n"); /* debug */
            fprintf(stderr, "%s\n", econf_errString(error));
            econf_free(key_file);
            econf_free(key_file_edit);
            deleteTmpFiles();
            exit(EXIT_FAILURE);
        }

        /* if /etc/filename.conf.d does not exist, create it, otherwise
         * econf_writeFile() will fail
         */
        if (access(path, F_OK) == -1 && errno == ENOENT)
        {
            
            fprintf(stdout, "|create parent directory\n");  /* debug */
            int mkDir = mkdir(path, S_IRUSR | S_IWUSR | S_IXUSR | S_IRGRP | S_IXGRP | S_IROTH | S_IXOTH); // 755
            if (mkDir != 0)
            {
                fprintf(stderr, "-->Error with mkdir()!\n");
                econf_free(key_file);
                deleteTmpFiles();
                exit(EXIT_FAILURE);
            }               
        }

        /* check if file already exists */
        snprintf(pathFilename, strlen(path) + strlen(filenameSuffix) + 4, "%s%s%s", path, "/", filenameSuffix);
        if (access(pathFilename, F_OK) == 0)
        {
            char input[2] = "";

            /* let the user verify that the file should really be overwritten */ 
            do
            {
                fprintf(stdout, "The file %s%s%s already exists!\nYes [y], no [n]\n", path, "/", filenameSuffix);
                fprintf(stdout, "Do you really want to overwrite it?\nYes [y], no [n]\n");
                scanf("%2s", input);
            } while (strcmp(input, "y") != 0 && strcmp(input, "n") != 0);

            if (strcmp(input, "y") == 0)
            {
                fprintf(stdout, "|The file already exists!\n"); /* debug */
                fprintf(stdout, "|Save as %s\n", pathFilename); /* debug */
                if ((error = econf_writeFile(key_file_edit, path, filenameSuffix)))
                {
                    fprintf(stderr, "-->Saving file: econf_writeFile() 5 Error!\n"); /* debug */
                    fprintf(stderr, "%s\n", econf_errString(error));
                    econf_free(key_file);
                    econf_free(key_file_edit);
                    deleteTmpFiles();
                    exit(EXIT_FAILURE);
                }
            } else 
            { /* do nothing and just return */
                econf_free(key_file_edit);
                return;
            }
        } else
        {
            fprintf(stdout, "|The file does not exist!\n"); /* debug */
            fprintf(stdout, "|Save as %s\n", pathFilename); /* debug */
            if ((error = econf_writeFile(key_file_edit, path, filenameSuffix)))
            {
                fprintf(stderr, "-->Saving file: econf_writeFile() 5 Error!\n"); /* debug */
                fprintf(stderr, "%s\n", econf_errString(error));
                econf_free(key_file);
                econf_free(key_file_edit);
                deleteTmpFiles();
                exit(EXIT_FAILURE);
            }
        }
        
        /* cleanup */
        econf_free(key_file_edit);
    }
}

/**
 * @brief Deletes the 2 created tmp files in /tmp.
 */
static void deleteTmpFiles(void)
{
    size_t combined_length = strlen(TMPPATH) + strlen(TMPFILE_ORIG) + 2;
    char tmpfile_editedOne[combined_length];
    memset(tmpfile_editedOne, 0, combined_length);
    snprintf(tmpfile_editedOne, combined_length, "%s%s%s", TMPPATH, "/", TMPFILE_ORIG);
    remove(tmpfile_editedOne);

    combined_length = strlen(TMPPATH) + strlen(TMPFILE_EDIT) + 2;
    char tmpfile_editedTwo[combined_length];
    memset(tmpfile_editedTwo, 0, combined_length);
    snprintf(tmpfile_editedTwo, combined_length, "%s%s%s", TMPPATH, "/", TMPFILE_EDIT);
    remove(tmpfile_editedTwo);
}

/**
 * @brief Shows the usage.
 */
static void usage(void)
{
    fprintf(stderr, "Usage: %s COMMANDS [OPTIONS] filename.conf\n\n", utilname);
    fprintf(stderr, "COMMANDS:\n");
    fprintf(stderr, "show     reads all snippets for filename.conf and prints all groups,\n");
    fprintf(stderr, "         keys and their values.\n");
    fprintf(stderr, "cat      prints the content and the name of the file in the order as\n");
    fprintf(stderr, "         read by libeconf.\n");
    fprintf(stderr, "edit     starts the editor EDITOR (environment variable) where the\n");
    fprintf(stderr, "         groups, keys and values can be modified and saved afterwards.\n");
    fprintf(stderr, "   --full:   copy the original configuration file to /etc instead of\n");
    fprintf(stderr, "             creating drop-in files.\n");
    fprintf(stderr, "revert   reverts all changes to the vendor versions. Basically deletes\n");
    fprintf(stderr, "         the config file in /etc.\n\n");
    exit(EXIT_FAILURE);
}