package main

import (
	"fmt"
	"time"
	"runtime"
)

func cal()  {
	for i :=0 ; i<1000000 ;i++{
		//fmt.Printf("call:%d\n",i)
		runtime.Gosched()
	}
}

func main() {
	runtime.GOMAXPROCS(1)

	currentTime:=time.Now()
	fmt.Println(currentTime)

	go cal()
	for i :=0 ; i<1000000 ;i++{
		//fmt.Printf("main:%d\n",i)
		runtime.Gosched()
	}

	currentTime=time.Now()
	fmt.Println(currentTime)
}
