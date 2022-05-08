#include <fcntl.h>  
#include <stdio.h> 
#include <stdlib.h> 

int main()  
{  	
	char    c;  
	int     in;
	int 	i;

	in = open("in.txt", O_RDONLY);  
	for(i=0; i<100; i++){
		read(in,&c,1);
	}
	return 0;  
} 