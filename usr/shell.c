// shell.c
//

#include "syscall.h"
#include "error.h"
#include "string.h"

int main(void) {
    char cmdbuf[32];
    char * args[32];
    int argc;
    int fd;
    int child_tid;

    _close(1); // close nullio on descr. 1 (stdout)
    _iodup(2, 1); // send stdout to console also

    for (;;) {
        printf("goober$ ");
        getsn(cmdbuf, sizeof(cmdbuf));

        // erase ending newline
        cmdbuf[sizeof(cmdbuf) - 1] = '\0';

        // split command into words seperated by spaces
        args[0] = cmdbuf;
        for (argc = 1; (args[argc] = strchr(args[argc - 1], ' ')); ++argc)
            *(args[argc]++) = '\0';

        // commands

        if (strcmp(args[0], "\0") == 0) {
            continue;
        } else if (strcmp(args[0], "q") == 0 || strcmp(args[0], "quit") == 0) {
            break;
        }

        // open and fork

        fd = _fsopen(-1, args[0]);

        if (fd < 0) {
            printf("%s: ERROR %d\n", args[0], fd);
            continue;
        }

        child_tid = _fork();

        if (child_tid == 0) {           // child
            _exec(fd, argc, args);
            printf("exec failed\n");
            _exit();
        } else if (child_tid > 0)  {    // parent
            _wait(child_tid);
            printf("%d child exited\n", child_tid);
            // TODO: flush
        } else {
            printf("ERROR: %d fork failed\n", child_tid);
        }
    }

    return 0;
}
