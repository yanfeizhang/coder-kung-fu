#include <stdlib.h>
#include <stdio.h>
#include "clock.h" 

#define MINBYTES (1 << 11)  // 内存测试区域从 2KB 开始 
#define MAXBYTES (1 << 26)  // 最大到 64 MB
#define MAXSTRIDE 64        // 循环步长从 1 到 64 字节
#define MAXELEMS MAXBYTES/sizeof(double) 

double data[MAXELEMS];      // 测试用的全局内存数组

void init_data(double *data, int n);
void run_width_testing();
double get_seque_access_result(int size, int stride, int type);
double get_random_access_result(int size, int type);
void seque_access(int elems, int stride);
void random_access(int* random_index_arr, int count);
void create_rand_array(int max, int count, int* pArr);

int main()
{		
	init_data(data, MAXELEMS); 	
		
	printf("Band Width (MB/sec)\n");
	run_width_testing();
	printf("\n\n");
	
	exit(0);
}

// init_data 初始化要访问的内存数据
void init_data(double *data, int n)
{
	int i;

	for (i = 0; i < n; i++)
	data[i] = i;
}

// 运行内存访问带宽测试
void run_width_testing()
{
	int size;        // 测试内存区域大小 
	int stride;      // 内存区域访问循环步长
	
	// 打印内存区域大小头信息
	printf("\t");
	for (size = MAXBYTES; size >= MINBYTES; size >>= 1) {
		if (size > (1 << 20)){
			printf("%dm\t", size / (1 << 20));
		}else{
			printf("%dk\t", size / 1024);
		}
	}
	printf("\n");	

	// 多次实验，进行内存顺序访问带宽评估
	// 外层循环控制步长依次从 1 到 64，目的是不同的顺序步长的访问效果差异
	// 内存循环控制数据大小依次从 2KB 开始到 64MB，目的是要保证数据大小依次超过 L1、L2、L3
	for (stride = 1; stride <= MAXSTRIDE; stride=stride+1) {
		printf("s%d\t", stride);		
		for (size = MAXBYTES; size >= MINBYTES; size >>= 1) {	
			printf("%.1f\t", get_seque_access_result(size, stride, 0));
		}
		printf("\n");
	}
	
	// 多次实验，进行内存随机访问带宽评估
	printf("random\t");
	for (size = MAXBYTES; size >= MINBYTES; size >>= 1) {		
		printf("%.1f\t", get_random_access_result(size,0));
	}
	printf("\n");
}

// get_seque_access_result 测试存储访问延迟(L1/L2/L3,内存)
// 参数说明
//		- size: 要测试的数据大小
//		- stride: 步长
//		- type: 0 获取带宽测试结果 
//		-		1 获取延时测试结果，单位是 CPU 周期数
double get_seque_access_result(int size, int stride, int type)
{	
	int i;	
	long int operations;
	long int total_accessed_bytes;
	long int used_microseconds;
	
	int samples = 1000;		
	int elems = size / sizeof(double); 
			
	//循环测试 1000 次，以最大程度减少实验计算结果误差
	start_timer();
	for(i=0; i<samples; i++){
		seque_access(elems, stride);
	}
	used_microseconds = get_timer();
	if(0==used_microseconds){
		return 0;
	}
	
	//本次实验所进行的总存储读取次数
	operations = (long int)samples * (long int)elems / stride;	

	//本次实验所读取的总存储读取大小
	total_accessed_bytes = operations * sizeof(double);

	
	double result = 0;
	// 获取带宽结果
	if(0==type){ /* get width */
	
		/* width 	= size(M)/ time(s) 
					= (total_accessed_bytes / 1000000) / (used / 1000000000) 
					= total_accessed_bytes*1000/used_microseconds;
		*/	
		result = total_accessed_bytes * 1000  / used_microseconds;
	// 获取延迟测试结果	
	}else if(1==type){/* get cycles_per_operation */		
		result = (double)used_microseconds/operations; 
	}	
	
	return result;
}

// 内存按照一定的步长进行顺序访问
void seque_access(int elems, int stride) /* The test function */
{
	int i;
	double result = 0.0; 
	volatile double sink; 

	for (i = 0; i < elems; i += stride) {
		result += data[i];  
	}

	//这一行是为了避免编译器把循环给优化掉了
	sink = result;
}


// get_random_access_result 对存储进行随机访问测试(L1/L2/L3,内存)
// 参数说明
//		- size: 要测试的数据大小
//		- type: 0 获取带宽测试结果 
//		-		1 获取延时测试结果，单位是 CPU 周期数
double get_random_access_result(int size, int type)
{	
	int i;
	int *p;
	
	long int operations;
	long int total_accessed_bytes;
	long int used_microseconds;
	
	int samples = 300;		
	int elems = size / sizeof(double); 
	int access_count = elems;
	
	//在实验开始前，提前要随机访问的数组下标准备好
	int* random_access_arr = malloc(access_count*sizeof(int));	
	for(i=0,p=random_access_arr; i<access_count; i++,p++){
		*p = 0;
	}	
	create_rand_array(elems, access_count, random_access_arr);	
	
		
	//开始进行随机访问测试，运行 300 次，以降低实验误差
	start_timer();
	for(i=0; i<samples; i++){
		random_access(random_access_arr, access_count);
	}
	used_microseconds = get_timer();
	
	//本次实验所进行的总存储读取次数
	operations = (long int)samples * (long int)access_count;

	//本次实验所读取的总存储读取大小	
	total_accessed_bytes = operations * sizeof(double);

	
	double result = 0;
	// 获取带宽结果
	if(0==type){
	
		/* width 	= size(M)/ time(s) 
					= (total_accessed_bytes / 1000000) / (used / 1000000000) 
					= total_accessed_bytes*1000/used_microseconds;
		*/	
		result = total_accessed_bytes * 1000  / used_microseconds;
	// 获取延时结果	
	}else if(1==type){	
		result = used_microseconds/operations*2.4; 
	}	
	
	return result;
}

// 提前把要进行随机访问的数组下标准备好，用于随机访问测试
void create_rand_array(int max, int count, int* pArr)
{
	int i;
	for (i = 0; i < count; i ++,pArr++) {
		int rd = rand();		
		int randRet = (long int)rd * max / RAND_MAX;
		*pArr = randRet;
	}
	return;
}

// random_access 按照指定的随机下标对数据进行随机访问
void random_access(int* random_index_arr, int count) /* The test function */
{ 
	int i;
	double result = 0.0; 
	volatile double sink; 

	for (i = 0; i < count; i++) {
		result += data[*(random_index_arr+i)];  
	}

	//这一行是为了避免编译器把循环给优化掉了
	sink = result; 
}



