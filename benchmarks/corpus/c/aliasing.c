#include <stdint.h>
#include <stdio.h>
static void kernel(int64_t *dst,const int64_t *src,int n){for(int i=0;i<n;i++)dst[i]=src[i]+1;}
int main(int argc,char **argv){
 (void)argv; int64_t a[80],b[80];
 for(int i=0;i<80;i++){a[i]=i*3+7;b[i]=i*5+11;}
 kernel(a+8,b+8,64);                 /* no alias, guarded */
 kernel(a+8,a+8,64);                 /* exact alias */
 kernel(a+9,a+8,63);                 /* forward overlap */
 kernel(b+8,b+9,63);                 /* backward overlap */
 int64_t *unknown=(argc==1)?a+8:b+8; /* runtime-unknown */
 kernel(unknown,unknown,64);
 uint64_t sum=0; for(int i=0;i<80;i++)sum=(sum*UINT64_C(131))^(uint64_t)a[i]^(uint64_t)b[i];
 printf("checksum: %llu\n",(unsigned long long)sum); return 0;
}
