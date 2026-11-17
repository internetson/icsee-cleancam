#include <stdio.h>
#include <stdlib.h>

extern void* XmDvr_JsonParser_getProductParams();
extern int LibXmDvr_Frontboard_initResetKey();
extern int LibXmDvr_Frontboard_getResetKeyValue(int *val);

int main() {
    printf("Calling XmDvr_JsonParser_getProductParams...\n");
    char *p = (char*)XmDvr_JsonParser_getProductParams();
    printf("Params ptr: %p\n", p);
    if (p) {
        int grp = *(int*)(p + 0x180);
        int pin = *(int*)(p + 0x184);
        int act = *(int*)(p + 0x188);
        printf("Reset GPIO from params: grp=%d, pin=%d, active_level=%d\n", grp, pin, act);
        printf("Linux GPIO num = grp * 8 + pin = %d\n", grp * 8 + pin);
    }
    
    printf("Calling LibXmDvr_Frontboard_initResetKey...\n");
    int init_ret = LibXmDvr_Frontboard_initResetKey();
    printf("initResetKey ret = %d\n", init_ret);
    
    int val = -1;
    int ret = LibXmDvr_Frontboard_getResetKeyValue(&val);
    printf("getResetKeyValue ret = %d, val = %d\n", ret, val);
    
    return 0;
}
