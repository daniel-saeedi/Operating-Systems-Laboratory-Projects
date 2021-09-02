#include "types.h"
#include "stat.h"
#include "user.h"
#include "fs.h"
#include "fcntl.h"

void touch(char* filename) 
{
    unlink(filename);
    int fd;
    fd = open(filename, O_CREATE);
    printf(0, "done!\n");
    close(fd);
    return;
}

void touch_w(char* filename) 
{
    char buf[512];
    int fd, n;
    unlink(filename);
    fd = open(filename, O_CREATE | O_RDWR);
    while((n = read(0, buf, sizeof(buf))) > 0) {
        if (write(fd, buf, n) != n) {
            printf(0, "write error!\n");
            close(fd);
            return;
        }
    }
    if(n < 0){
        printf(0, "read error!\n");
        close(fd);
        return;
    }
    printf(0, "\ndone!\n");
    close(fd);
    return;
}

int main(int argc, char *argv[])
{
    if(argc == 2){
        touch(argv[1]);
        exit();
    }
    else if(argc == 3){
        touch_w(argv[2]);
        exit();
    }
    else {
        
    }
    exit();
}