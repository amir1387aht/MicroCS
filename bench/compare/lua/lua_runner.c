#define _POSIX_C_SOURCE 199309L
#include <stdio.h>
#include <time.h>
#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"
static double now_ms(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC,&t); return t.tv_sec * 1000.0 + t.tv_nsec / 1000000.0; }
int main(int argc, char **argv) {
 if (argc != 2) return 2;
 lua_State *L = luaL_newstate(); if (!L) return 3;
 luaL_openlibs(L);
 double start=now_ms();
 int e=luaL_loadfile(L,argv[1]); if (!e) e=lua_pcall(L,0,0,0);
 double t=now_ms()-start;
 if(e) { fprintf(stderr,"lua error: %s\n",lua_tostring(L,-1)); lua_close(L); return 1; }
 fprintf(stderr,"%.3f\n",t);
 lua_close(L); return 0;
}
