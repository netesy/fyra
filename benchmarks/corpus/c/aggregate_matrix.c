#include <stdint.h>
#include <stdio.h>
typedef struct { int32_t x,y,w,id; } Record;
static Record adjust(Record r,int32_t dx,int32_t dy){r.x+=dx;r.y+=dy;r.w+=r.x-r.y;return r;}
int main(void){Record a[64];int64_t checksum=0;
 for(int rep=0;rep<100000;++rep){for(int i=0;i<64;++i){Record r={i+rep,i*3-rep,i^rep,i};a[i]=adjust(r,7,11);} checksum+=a[rep&63].x+a[(rep+17)&63].y+a[(rep+31)&63].w;}
 int64_t m[4][4],x[4][4],y[4][4];for(int i=0;i<4;++i)for(int j=0;j<4;++j){m[i][j]=i*4+j+1;x[i][j]=(i==j)+j;}
 for(int rep=0;rep<200000;++rep)for(int i=0;i<4;++i)for(int j=0;j<4;++j){int64_t s=0;for(int k=0;k<4;++k)s+=m[i][k]*x[k][j];y[i][j]=s;}
 checksum+=y[0][0]+y[1][2]+y[3][3];printf("checksum: %lld\n",(long long)checksum);}
