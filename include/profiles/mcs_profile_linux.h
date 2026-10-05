/* MicroCS build profile: embedded Linux / desktop host (everything on,
 * 64-bit integers, larger stacks). Use with ports/unix.
 * Use: -DMCS_USER_CONFIG_FILE='"profiles/mcs_profile_linux.h"' */
#define MCS_INT64 1
#define MCS_ENABLE_COMPILER 1
#define MCS_ENABLE_DISASM 1
#define MCS_DEFAULT_STACK 4096
#define MCS_DEFAULT_FRAMES 256
