__attribute__((noinline)) long call_getpid(){
    register long number asm("x16")=20;
    register long result asm("x0");
    asm volatile("svc #0x80":"=r"(result):"r"(number):"memory","cc");

    return result;
}
int main(){return call_getpid()>0?0:1;}
