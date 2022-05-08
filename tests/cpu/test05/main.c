#include <stdio.h>
#include <unistd.h>
#include <sys/time.h>
#include <pthread.h>

int pipes[20][3];
char buffer[10];
int running = 1;

void inti()
{
	int i =20;
	while(i--)
	{
		if(pipe(pipes[i])<0)
			exit(1);
		pipes[i][2] = i;
	}
}

void distroy()
{
	int i =20;
	while(i--)
	{
		close(pipes[i][0]);
		close(pipes[i][1]);
	}
}

double self_test()
{
	int i =20000;
	struct timeval start, end;
	gettimeofday(&start, NULL);
	while(i--)
	{
		if(write(pipes[0][1],buffer,10)==-1)
			exit(1);
		read(pipes[0][0],buffer,10);
	}
	gettimeofday(&end, NULL);
	return (double)(1000000*(end.tv_sec-start.tv_sec)+ end.tv_usec-start.tv_usec)/20000;
}

void *_test(void *arg)
{
	int pos = ((int *)arg)[2];
	int in = pipes[pos][0];
	int to = pipes[(pos + 1)%20][1];
	while(running)
	{
		read(in,buffer,10);
		if(write(to,buffer,10)==-1)
			exit(1);
	}
}

double threading_test()
{
	int i = 20;
	struct timeval start, end;
	pthread_t tid;
	while(--i)
	{
		pthread_create(&tid,NULL,_test,(void *)pipes[i]);
	}
	i = 10000;
	gettimeofday(&start, NULL);
	while(i--)
	{
		if(write(pipes[1][1],buffer,10)==-1)
			exit(1);
		read(pipes[0][0],buffer,10);
	}
	gettimeofday(&end, NULL);
	running = 0;
	if(write(pipes[1][1],buffer,10)==-1)
		exit(1);
	return (double)(1000000*(end.tv_sec-start.tv_sec)+ end.tv_usec-start.tv_usec)/10000/20;
}


int main()
{
	inti();
	printf("%6.6f\n",self_test());
	printf("%6.6f\n",threading_test());
	distroy();
	exit(0);
}