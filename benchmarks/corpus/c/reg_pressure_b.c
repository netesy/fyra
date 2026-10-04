#include <stdint.h>
#include <stdio.h>
int main(void) {
    int64_t a=1,b=2,c=3,d=4,e=5,f=6;
    for (int64_t i=1;i<=10000000;i++) {
        int64_t x=(i*3)^a, y=(i+17)*5+b, z=(x+y)^(i<<2);
        a+=x; b^=y; c+=z; d+=(x-y); e^=(z+d); f+=(x^y^z);
    }
    printf("checksum: %lld\n",(long long)(a^b^c^d^e^f));
    return 0;
}
