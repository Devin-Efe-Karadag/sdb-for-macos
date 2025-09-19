#include <thread>
#include <atomic>
std::atomic<int> value{0};
__attribute__((noinline)) void worker(){
