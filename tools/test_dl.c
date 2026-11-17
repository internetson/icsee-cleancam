#include <stdio.h>
#include <dlfcn.h>

int main() {
    void *h = dlopen("/lib/libXmDvr.so", RTLD_NOW);
    if (!h) {
        printf("Error: %s\n", dlerror());
        return 1;
    }
    printf("Success!\n");
    return 0;
}
