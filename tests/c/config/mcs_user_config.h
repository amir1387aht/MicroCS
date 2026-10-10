/* Project config header used by tests/c/test_config.c (make test) and by the
 * CMake project next to it (CI): a profile, a few options that override it and
 * a module limit. The build adds MCS_DEFAULT_PROFILE=MCS_PROFILE_MIN and
 * MCS_GC_GROW=3 on the command line. */
#ifndef MCS_USER_CONFIG_H
#define MCS_USER_CONFIG_H
#define MCS_TEST_USER_CONFIG 1

#define MCS_PROFILE          MCS_PROFILE_LOWRAM  /* wins over the build's MCS_DEFAULT_PROFILE */
#define MCS_DEFAULT_STACK    200                 /* wins over the profile's 128 */
#define MCS_ENABLE_LINQ      0
#define MCS_SHELL_LINE_MAX   128                 /* module option (mcs_shell.h) */

#endif
