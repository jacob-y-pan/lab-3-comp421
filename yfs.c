#include <comp421/filesystem.h>
#include <comp421/yalnix.h>

int
main(int argc, char **argv)
{
    (void) argc;

    // Per page 19, register this server
    Register(FILE_SERVER);

    // Fork and exec the first user process
    if (Fork() == 0) {
        // As child, execute the child process
        Exec(argv[1], argv + 1);
    } else {

    }

    return 0;
}