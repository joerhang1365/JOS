// shell.c
//

#include "syscall.h"
#include "error.h"
#include "string.h"

#define SHELL_EXIT 1
#define SHELL_BUILTIN 2

int shell_null(char ** args);
int shell_echo(char ** args);
int shell_cd(char ** args);
int shell_help(char ** args);
int shell_exit(char ** args);

char * builtin_str[] =
{
    "\0",   // do nothing
    "echo", // write back to console
    "cd",   // change directory
    "help",
    "exit"
};

int (* builtin_func[]) (char **) =
{
    &shell_null,
    &shell_echo,
    &shell_cd,
    &shell_help,
    &shell_exit
};

int main(void) {
    char cmdbuf[32];
    char * args[32];
    int argc;
    int fd;
    int child_tid;
    int i;
    int result;

    getsn(cmdbuf, sizeof(cmdbuf));

    printf("  #  #   ##\n");
    printf("  # # # #  \n");
    printf("  # # #  # \n");
    printf("  # # #   #\n");
    printf("###  #  ## \n");

    while (1)
    {
        printf("goober$ ");
        getsn(cmdbuf, sizeof(cmdbuf));

        // erase ending newline
        cmdbuf[sizeof(cmdbuf) - 1] = '\0';

        // split command into words seperated by spaces
        args[0] = cmdbuf;
        for (argc = 1; (args[argc] = strchr(args[argc - 1], ' ')); ++argc)
        {
            *(args[argc]++) = '\0';
        }


        // builtin commands

        int builtin_count = sizeof(builtin_str) / sizeof(char *);
        result = 0;

        for (i = 0; i < builtin_count; i++)
        {
            if (strcmp(args[0], builtin_str[i]) == 0)
            {
                result = (*builtin_func[i])(args);
                break;
            }
        }

        if (result == SHELL_EXIT)
        {
            break;
        }
        else if (result == SHELL_BUILTIN)
        {
            continue;
        }

        // open and fork

        fd = _fsopen(-1, args[0]);

        if (fd < 0)
        {
            printf("ERROR: %s invalid command %d\n", args[0], fd);
            continue;
        }

        child_tid = _fork();

        if (child_tid == 0) // child
        {
            _exec(fd, argc, args);
            printf("exec failed\n");
            _exit();
        }
        else if (child_tid > 0) // parent
        {
            _close(fd);
            _wait(child_tid);
            printf("%d child exited\n", child_tid);
            // TODO: flush
        }
        else
        {
            printf("ERROR: %d fork failed\n", child_tid);
        }
    }

    return 0;
}

int shell_null(char ** args)
{
    return SHELL_BUILTIN;
}

int shell_echo(char ** args)
{
    int i = 1;

    while (args[i] != NULL)
    {
        printf("%s", args[i]);
        putc(' ');
        i++;
    }

    putc('\n');

    return SHELL_BUILTIN;
}

int shell_cd(char ** args)
{
    printf("cd time\n");
    return SHELL_BUILTIN;
}

int shell_help(char ** args)
{
    int i;

    printf("Welcome to JOS\n");
    printf("You are on your own buddy\n");
    printf("list of commands:\n");

    int builtin_count = sizeof(builtin_str) / sizeof(char *);

    for (i = 1; i < builtin_count; i++)
    {
        printf("  %s\n", builtin_str[i]);
    }

    return SHELL_BUILTIN;
}

int shell_exit(char ** args)
{
    return SHELL_EXIT;
}
