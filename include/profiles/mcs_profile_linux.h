/* MicroCS build profile: embedded Linux / desktop host (everything on,
 * 64-bit integers, larger stacks). Use with ports/unix.
 * Use: #define MCS_PROFILE MCS_PROFILE_LINUX in mcs_user_config.h, or
 *      -DMCS_PROFILE=MCS_PROFILE_LINUX (CMake: MICROCS_PROFILE=linux) */
#ifndef MCS_INT64
#define MCS_INT64 1
#endif
#ifndef MCS_ENABLE_COMPILER
#define MCS_ENABLE_COMPILER 1
#endif
#ifndef MCS_ENABLE_DISASM
#define MCS_ENABLE_DISASM 1
#endif
#ifndef MCS_DEFAULT_STACK
#define MCS_DEFAULT_STACK 4096
#endif
#ifndef MCS_DEFAULT_FRAMES
#define MCS_DEFAULT_FRAMES 256
#endif
