#include <stdio.h>
#include <string.h>

#include <comp421/yalnix.h>
#include <comp421/iolib.h>

/*
 * Create empty files named "file00" through "file31" in "/".
 */
int
main()
{
        int fd;

        // fd = Create("/file00");
        Open("/file00");

        (void) fd;

        Shutdown();

    return (0);
}
