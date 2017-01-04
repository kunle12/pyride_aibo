#include <string.h>
#include <errno.h>

char * getcwd( char * buf, int size )
{

	if (size <= 22) {
		errno = EINVAL;
		return NULL;
	}
	strncpy( buf, "/MS/OPEN-R/MW/Python", size );
	return "/MS/OPEN-R/MW/Python";
}
