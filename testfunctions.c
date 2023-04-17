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
        // Open("file00");

        // MkDir("testfolder3/bingofolder");
        // RmDir("testfolder3/bingofolder");
        MkDir("newfolder3");
        MkDir("newfolder3/newnewfolder");
        ChDir("newfolder3");
        MkDir("newnewfolder2");
        Create("test.txt");
        struct Stat statbuf;
        Stat("test.txt", &statbuf);

        printf("Stat test: %d\n", statbuf.inum);

        (void) fd;

        Shutdown();

    return (0);
}
