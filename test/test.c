#include <errno.h>
#include <string.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <stdarg.h>
#include <unistd.h>
#include <stdint.h>

#define DEVFILE "/dev/pcidemo0"
#define DEMO_IOCTL_PING 0x20001

int main(int argc, char *argv[])
{
	int ret;
	int fd;
	char ping_str[20];
		
	if((fd = open(DEVFILE, O_RDWR)) < 0)
	{
		printf("open device error\n");
		return 0;
	}	
		
	ret = ioctl(fd, DEMO_IOCTL_PING, ping_str);
	if(ret < 0)
	{
		printf("ioctl wrong\n");
	}
	else
	{	
		printf("%s\n",ping_str);
	}

	close(fd);
	
	return 0;	
}