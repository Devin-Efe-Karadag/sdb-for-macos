#include <thread>
#include <atomic>
std::atomic<int> value{0};
__attribute__((noinline)) void worker(){
    int local=41;
    value.store(local+1);
}
int main(){std::thread t(worker);t.join();return value==42?0:1;}
