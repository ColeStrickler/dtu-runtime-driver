

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sched.h>
#include <stdint.h>
#include <sys/mman.h>


#define DEVICE_FILE "/dev/DTU-RUNTIME-DRIVER"
#define IOCTL_NEW_PA        _IO('a', 1)
#define IOCTL_REMAP_PA      _IO('a', 2)
typedef struct RemapPARequest
{
    void* u_VA

}RemapPARequest;



int main()
{
    int fd;
    int ret;

     fd = open(DEVICE_FILE, O_RDWR);
    if (fd < 0) {
        perror("Failed to open the device file");
        return 1;
    }

   void *user_ptr = mmap(NULL, 5*1024*1024, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    printf("Got user_ptr %llx\n", user_ptr);
    
    RemapPARequest req;
    req.u_VA = user_ptr;


    ret = ioctl(fd, IOCTL_REMAP_PA, &req);
    printf("ret from IOCTL_REMAP_PA %d\n", ret);

    return 0;
}