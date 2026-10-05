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
`<time>` = number with optional `ms`/`s`/`m`/`h` suffix. `restart=never` (default: stop
after the first failure), `always` (never stop), or `N` consecutive failures. Periodic jobs
that fall behind run once late, then continue one period later (no burst catch-up).

## C# API
```csharp
int id = Scheduler.Every(1000, () => GPIO.Toggle(13));   // optional 3rd arg: maxFailures
Scheduler.After(250, () => Console.WriteLine("once"));
Scheduler.Cancel(id); Scheduler.CancelAll(); int n = Scheduler.Count;
```
Delegates are kept alive by one pinned list per scheduler.

## Not implemented (planned)
Cron expressions, wall-clock/RTC schedules, priorities, preemption, job persistence
across reboots (other than re-reading jobs.cfg at boot).
