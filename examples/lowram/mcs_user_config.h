/* Project config header of the lowram example (see docs/CONFIGURATION.md).
 * Found automatically because examples/lowram is on the include path of the
 * whole build (library + firmware): `make example-lowram`, ports/cortex-m.
 * The #ifndef guards let a build pick another profile with -D, as
 * ports/cortex-m does for its min-profile variant (-DMCS_PROFILE=MCS_PROFILE_MIN). */
#ifndef MCS_USER_CONFIG_H
#define MCS_USER_CONFIG_H

#ifndef MCS_PROFILE
#define MCS_PROFILE      MCS_PROFILE_LOWRAM  /* 32-64 KB RAM: images only, single-precision floats */
#endif
/* the node only computes and prints: no filesystem, HAL classes or scheduler */
#ifndef MCS_ENABLE_FS
#define MCS_ENABLE_FS    0
#endif
#ifndef MCS_ENABLE_HAL
#define MCS_ENABLE_HAL   0
#endif
#ifndef MCS_ENABLE_SCHED
#define MCS_ENABLE_SCHED 0
#endif

#endif
