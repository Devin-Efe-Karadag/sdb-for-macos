#include <dlfcn.h>
#ifndef SDB_TEST_LIBRARY
#error missing library path
#endif
int main(){
    auto library=dlopen(SDB_TEST_LIBRARY,RTLD_NOW);

    if(!library)return 1;

    auto fn=reinterpret_cast<int(*)(int)>(dlsym(library,"library_value"));

    int result=fn?fn(30):0;
    dlclose(library);

    return result==42?0:2;
}
