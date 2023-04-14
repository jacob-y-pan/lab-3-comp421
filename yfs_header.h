# define OPEN_M 1
# define CLOSE_M 2
# define CREATE_M 3
# define READ_M 4
# define WRITE_M 5
# define SEEK_M 6
# define LINK_M 7
# define UNLINK_M 8
# define SYMLINK_M 9
# define READLINK_M 10
# define MKDIR_M 11
# define RMDIR_M 12
# define CHDIR_M 13
# define STAT_M 14
# define SYNC_M 15
# define SHUTDOWN_M 16

struct my_msg {
    int type;
    int data1;
    char data2[16];
    void *ptr;
};