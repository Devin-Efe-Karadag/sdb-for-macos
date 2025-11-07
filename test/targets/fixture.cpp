#include <cstdio>
#include <thread>
#include <atomic>
#include <csignal>
#include <string_view>
volatile long watched = 7;
__attribute__((noinline)) long add(long a,long b){return a+b;}
__attribute__((noinline)) double add_double(double a,double b){return a+b;}
__attribute__((noinline)) int inner(int x){
