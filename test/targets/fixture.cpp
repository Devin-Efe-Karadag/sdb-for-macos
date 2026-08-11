#include <cstdio>
#include <thread>
#include <atomic>
#include <csignal>
#include <string_view>
volatile long watched = 7;
__attribute__((noinline)) long add(long a,long b){return a+b;}
__attribute__((noinline)) double add_double(double a,double b){return a+b;}
__attribute__((noinline)) int inner(int x){
    int local = x + 3;
    watched = local;

    std::printf("local=%d\n",local);

    return local;
}
__attribute__((noinline)) int outer(int x){return inner(x)+1;}
int main(int argc,char** argv){
    if(argc>1) return argc==3 && argv[1]==std::string_view("first") && argv[2]==std::string_view("second") ? 0:2;

    int result=outer(4);

    return result==8 ? 0:1;
}
struct pair_double{double a,b;};
struct triple_long{long a,b,c;};
pair_double pair_input{1.5,2.5};
triple_long triple_input{2,3,4};
__attribute__((noinline)) pair_double pair_add(pair_double p){return {p.a+1,p.b+2};}
__attribute__((noinline)) triple_long triple_add(triple_long p){return {p.a+1,p.b+2,p.c+3};}
__attribute__((noinline)) long many(long a,long b,long c,long d,long e,long f,long g,long h,long i,long j){return a+b+c+d+e+f+g+h+i+j;}
