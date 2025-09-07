#include <unistd.h>
#include <signal.h>
volatile sig_atomic_t done=0;
void finish(int){done=1;}
int main(){signal(SIGUSR1,finish);while(!done)usleep(1000);return 0;}
