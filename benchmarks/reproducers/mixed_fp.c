#include <stdio.h>
static int mixed64(int n){double sum=0.0;int count=0;for(int i=0;i<n;i++){double x=(double)i*0.5+1.25;sum+=x;if(sum>(double)(i+1))count++;}return count;}
static int mixed32(int n){float sum=0.0f;int count=0;for(int i=0;i<n;i++){float x=(float)i*0.5f+1.25f;sum+=x;if(sum>(float)(i+1))count++;}return count;}
int main(void){printf("checksum: %d\n",mixed64(1000000)+mixed32(1000000));return 0;}
