#pragma once
#include <mach/arm/thread_status.h>
using user_regs_struct = arm_thread_state64_t;
using user_fpregs_struct = arm_neon_state64_t;
struct user { user_regs_struct regs{}; user_fpregs_struct i387{}; arm_debug_state64_t debug{}; };
