# Scheduler (modules/sched, `MCS_ENABLE_SCHED`)

A fixed table (`MCS_SCHED_MAX_JOBS` = 8) of jobs. A job is a script file (from the VFS)
or a C# delegate. The scheduler owns no thread: the host calls `mcs_sched_poll(&s)` from
its loop/task; due jobs run there one after another as normal top-level runs, so the
execution limits, hook and error reporting apply to each job. `mcs_sched_poll` returns
the milliseconds until the next job is due (sleep that long) or -1 when nothing is left.

```c
mcs_sched_t s; mcs_sched_init(&s, vm, &vfs, millis, NULL);
mcs_sched_open_lib(vm, &s);                       /* C# Scheduler class */
mcs_sched_load_config(&s, jobs_cfg_text);          /* optional jobs table */
for (;;) { int32_t ms = mcs_sched_poll(&s); rtos_sleep(ms < 0 ? 100 : ms); }
```

## jobs.cfg
```
# kind    time   path            policy
startup          /init.cs
after     5s     /selftest.cs
every     500ms  /blink.cs       restart=always
every     1m     /report.cs      restart=3
```
`startup /main.cs` is skipped at boot: the shell runs `/main.cs` anyway (it would run twice).

`<time>` = number with optional `ms`/`s`/`m`/`h` suffix. `restart=never` (default: stop
after the first failure), `always` (never stop), or `N` consecutive failures. Periodic jobs
that fall behind run once late, then continue one period later (no burst catch-up).

## Reloading a Studio project

The shell command `project /path/jobs.cfg` (or `mcs_sched_start_project(&s, text)` in C)
validates a staged configuration and compile-checks every listed file without executing it.
It requires at least one job, absolute `.cs` paths and a compiler-enabled build. Invalid syntax,
missing files and compile errors leave the existing job table intact. On success it cancels
all old jobs, installs exactly the new file jobs, and starts relative delays from the commit
point; the next scheduler poll runs due entries. The VM must be idle. Unlike the boot loader,
this explicit launch does not skip `startup /main.cs`.

This replaces jobs, not the filesystem or VM global state, and does not produce a persistent
multi-file artifact. Each file check/run resolves namespace providers across the mounted VFS referenced
by `using`; dependency declarations load in that job's VM without executing dependency
top-level statements. Source checks still cannot guarantee runtime success. Uploads happen
before this command, one file at a time; stop old jobs before deployment if they must not observe partially updated files.
See [Studio Project mode](../tools/studio/README.md#project-mode) for editor controls.

## C# API
```csharp
int id = Scheduler.Every(1000, () => GPIO.Toggle(13));   // optional 3rd arg: maxFailures
Scheduler.After(250, () => Console.WriteLine("once"));
Scheduler.Cancel(id); Scheduler.CancelAll(); int n = Scheduler.Count;
```
Delegates are kept alive by one pinned list per scheduler.

## Jobs while a script runs
The VM is single-threaded (with an OS chosen, file jobs run on their own OS threads unless marked `main` — [THREADS.md](THREADS.md)), but due jobs also run while a script waits in `Thread.Sleep`
(`MCS_SCHED_DURING_SLEEP`, default 1), so a main loop and jobs work together:

```csharp
Scheduler.Every(1000, () => Console.WriteLine("tick"));   // or `every 1s /report.cs` in jobs.cfg
while (true) { /* poll sensors... */ Thread.Sleep(50); }  // jobs run during the sleeps
```
A loop that never sleeps starves them. A job that sleeps does not start other jobs. A script
job (jobs.cfg) that runs inside a sleeping script gets its own top-level variables: the outer
script's values are restored afterwards (`mcs_globals_save` / `mcs_globals_restore`).

## Not implemented (planned)
Cron expressions, wall-clock/RTC schedules, priorities, preemption, job persistence
across reboots (other than re-reading jobs.cfg at boot).
