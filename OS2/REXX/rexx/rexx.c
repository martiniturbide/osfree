/*
 *  Copyright (C) 2004  Mark Hessling   <M.Hessling@qut.com.au>
 *
 *  This library is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU Library General Public
 *  License as published by the Free Software Foundation; either
 *  version 2 of the License, or (at your option) any later version.
 *
 *  This library is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  Library General Public License for more details.
 *
 *  You should have received a copy of the GNU Library General Public
 *  License along with this library; if not, write to the Free
 *  Software Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

/*
 * $Id: rexx.c,v 1.2 2004/06/06 11:14:01 mark Exp $
 */
#if defined(__OSFREE__)
#define OS2
#define HAVE_STDARG_H
#define HAVE_STDLIB_H
#define HAVE_STRING_H
#undef  HAVE_CONFIG_H
#include <ctype.h>
#endif
 
#if defined(HAVE_CONFIG_H)
# include "config.h"
#endif

#ifndef __OSFREE__
#include "configur.h"
#endif

#include <stdio.h>
//#include <stdarg.h>
//#include <stdlib.h>
//#include <string.h>

#if defined(__OS2__)
# define INCL_DOSSEMAPHORES
# define INCL_DOSMODULEMGR
# define INCL_DOSMISC
# undef INCL_REXXSAA
# include <os2.h>
# define INCL_REXXSAA
# define DONT_TYPEDEF_PFN
# define DYNAMIC_OS2
#endif

#if defined(HAVE_ERRNO_H)
# include <errno.h>
#endif

#if defined(HAVE_MEMORY_H)
# include <memory.h>
#endif

#if defined(HAVE_STDARG_H)
# include <stdarg.h>
#endif

#if defined(HAVE_STDLIB_H)
# include <stdlib.h>
#endif

#undef __USE_BSD
#if defined(HAVE_STRING_H)
# include <string.h>
#endif

#if defined(HAVE_SYS_TYPES_H)
# include <sys/types.h>
#endif

#if defined(HAVE_SYS_STAT_H)
# include <sys/stat.h>
#endif

#if defined(HAVE_UNISTD_H)
# include <unistd.h>
#endif

#include "rexx.h"

#include "rexxsaa.h"

typedef HMODULE handle_type ;

#define FUNCTION_REXXSTART                     0
#define FUNCTION_REXXVARIABLEPOOL              1
#define FUNCTION_REXXSETHALT                   2
#define FUNCTION_REXXSETTRACE                  3
#define FUNCTION_REXXRESETTRACE                4

#define NUM_REXX_FUNCTIONS                     5

static char *MyFunctionName[ NUM_REXX_FUNCTIONS ] =
{
   /*  0 */  "RexxStart",
   /*  1 */  "RexxVariablePool",
   /*  2 */  "RexxSetHalt",
   /*  3 */  "RexxSetTrace",
   /*  4 */  "RexxResetTrace",
};

/*
 * Typedefs for Regina
 */
typedef LONG   APIENTRY OREXXSTART(LONG,PRXSTRING,PSZ,PRXSTRING,PSZ,LONG,PRXSYSEXIT,PSHORT,PRXSTRING );
typedef APIRET APIENTRY OREXXVARIABLEPOOL(PSHVBLOCK );
typedef APIRET APIENTRY OREXXSETHALT(LONG,LONG );
typedef APIRET APIENTRY OREXXSETTRACE(LONG,LONG );
typedef APIRET APIENTRY OREXXRESETTRACE(LONG,LONG );
OREXXSTART               *ORexxStart=NULL;
OREXXVARIABLEPOOL        *ORexxVariablePool=NULL;
OREXXSETHALT             *ORexxSetHalt=NULL;
OREXXSETTRACE            *ORexxSetTrace=NULL;
OREXXRESETTRACE          *ORexxResetTrace=NULL;

static char TraceFileName[256];

int Trace = 0;
int InterpreterIdx = -1;

extern HMTX hmtx;

//static
void TraceString( char *fmt, ... )
{
   FILE *fp=NULL;
   int using_stderr = 0;
   va_list argptr;

   if (! hmtx)
   {
      return;
   }

   DosRequestMutexSem(hmtx, SEM_INDEFINITE_WAIT);

   if ( strcmp( TraceFileName, "stderr" ) == 0 )
      using_stderr = 1;

   if ( using_stderr )
      fp = stderr;
   else
      fp = fopen( TraceFileName, "a" );

   if ( fp )
   {
      va_start( argptr, fmt );
      vfprintf( fp, fmt, argptr );
      va_end( argptr );

      if ( !using_stderr )
         fclose( fp );
   }

   DosReleaseMutexSem(hmtx);
}

static handle_type FindInterpreter( char *library )
{
   handle_type handle=(handle_type)NULL ;
   CHAR LoadError[256];
   PFN addr;
   register int j=0;

   log( "%s: Attempting to load \"%s\" using DosLoadModule()...",
          __FUNCTION__,
          library);

   if ( DosLoadModule( LoadError, sizeof(LoadError), library, &handle ) )
   {
      handle = (handle_type)NULL;
   }

   if ( handle != (handle_type)NULL )
   {
      log( "found %s\n", library );

      for ( j = 0; j < NUM_REXX_FUNCTIONS; j++ )
      {
         if ( DosQueryProcAddr( handle, 0L, MyFunctionName[j], &addr) )
         {
            addr = NULL;
         }

         /*
          * Log the function and address. This is useful if the module
          * doesn't have an address for this procedure.
          */
         log( "%s: Address %x\n",
                MyFunctionName[j],
                (addr == NULL) ? 0 : addr );

         /*
          * Even if the function being processed is not in the module, its
          * address is still stored.  In this case it will simply be set
          * again to NULL.
          */
         switch ( j )
         {
            case FUNCTION_REXXSTART:                ORexxStart =                 (OREXXSTART                *)addr; break;
            case FUNCTION_REXXVARIABLEPOOL:         ORexxVariablePool =          (OREXXVARIABLEPOOL         *)addr; break;
            case FUNCTION_REXXSETHALT:              ORexxSetHalt=                (OREXXSETHALT              *)addr; break;
            case FUNCTION_REXXSETTRACE:             ORexxSetTrace =              (OREXXSETTRACE             *)addr; break;
            case FUNCTION_REXXRESETTRACE:           ORexxResetTrace =            (OREXXRESETTRACE           *)addr; break;
         }
      }
   }
   else
   {
      log( "not found: %s\n", LoadError );
   }
   return handle;
}

void LoadInterpreter( void )
{
   handle_type handle=(handle_type)NULL ;
   char interpreter[9] = "REGINA";
   char *ptr;

   if ( DosScanEnv( "REXX_TRACEFILE", (PSZ *)&ptr ) )
      ptr = NULL;

   if ( ptr != NULL )
   {
      Trace = 1;
      strcpy( TraceFileName, ptr );
   }

   /* REXX switcher feature */
   if ( DosScanEnv( "REXX_DLL", (PSZ *)&ptr ) )
      ptr = NULL;

   if ( ptr && *ptr && strlen(ptr) < 9 )
      strcpy(interpreter, ptr);

   ptr = interpreter;

   while (*ptr)
   {
      *ptr++ = toupper(*ptr);
   }

   handle = (handle_type)FindInterpreter( interpreter );

   if ( handle == (handle_type)NULL )
   {
      fprintf( stderr, "Could not find %s DLL. Cannot continue.\n", interpreter );
      exit( 11 );
   }

   log( "----------- Initialisation Complete - Program Execution Begins -----------\n" );

   InterpreterIdx = 0;

   return;
}

/*
 * RexxStart
 */
LONG APIENTRY RexxStart(
   LONG ArgCount,
   PRXSTRING ArgList,
   PCSZ ProgramName,
   PRXSTRING Instore,
   PCSZ EnvName,
   LONG CallType,
   PRXSYSEXIT Exits,
   PSHORT ReturnCode,
   PRXSTRING Result )
{
   LONG rc = (LONG)RXFUNC_NOTREG;

   log( "%s: ArgCount: %d ArgList: %x ProgramName: \"%s\" Instore: %x EnvName: \"%s\" Calltype: %d Exits: %x ReturnCode: %x Result: %x",
          __FUNCTION__,
          ArgCount,
          ArgList,
          ProgramName,
          Instore,
          EnvName,
          CallType,
          Exits,
          ReturnCode,
          Result );

   if (ORexxStart)
   {
      rc = (*ORexxStart)(
         (ULONG)      ArgCount,
         (PRXSTRING) ArgList,
         (PSZ)       ProgramName,
         (PRXSTRING) Instore,
         (PSZ)       EnvName,
         (LONG)      CallType,
         (PRXSYSEXIT)Exits,
         (PSHORT)    ReturnCode,
         (PRXSTRING) Result ) ;
   }

   log( "<=> ReturnCode %d ResultString \"%s\" Result: %d\n",
          (Result && Result->strptr) ? Result->strptr : "",
          ReturnCode,
          rc );
   
   return rc;
}

/*
 * Variable pool
 */
APIRET APIENTRY RexxVariablePool(
   PSHVBLOCK RequestBlockList )
{
   APIRET rc = RXSHV_NOAVL;

   if (Trace)
   {
      PSHVBLOCK tmp=RequestBlockList;

      log( "%s: RequestBlockList %x\n",
             __FUNCTION__,
             RequestBlockList );

      while(tmp)
      {
         switch( tmp->shvcode )
         {
            case RXSHV_SET:
               log( "in    RXSHV_SET: shvname: \"%s\" shvnamelen: %d shvvalue: \"%s\" shvvaluelen: %d\n",
                      tmp->shvname.strptr, tmp->shvnamelen, tmp->shvvalue.strptr, tmp->shvvaluelen );
               break;

            case RXSHV_SYSET:
               log( "in  RXSHV_SYSET: shvname: \"%s\" shvnamelen: %d shvvalue: \"%s\" shvvaluelen: %d\n",
                      tmp->shvname.strptr, tmp->shvnamelen, tmp->shvvalue.strptr, tmp->shvvaluelen );
               break;

            case RXSHV_FETCH:
               log( "in  RXSHV_FETCH: shvname: \"%s\" shvnamelen: %d\n",
                      tmp->shvname.strptr, tmp->shvnamelen );
               break;

            case RXSHV_DROPV:
               log( "in  RXSHV_DROPV: shvname: \"%s\" shvnamelen: %d\n",
                      tmp->shvname.strptr, tmp->shvnamelen );
               break;

            case RXSHV_SYDRO:
               log( "in  RXSHV_SYDRO: shvname: \"%s\" shvnamelen: %d\n",
                      tmp->shvname.strptr, tmp->shvnamelen );
               break;

            case RXSHV_NEXTV:
               log( "in  RXSHV_NEXTV: shvname: \"%s\" shvnamelen: %d\n",
                      tmp->shvname.strptr, tmp->shvnamelen );
               break;

            case RXSHV_PRIV:
               log( "in   RXSHV_PRIV\n" );
               break;

            case RXSHV_EXIT:
               log( "in   RXSHV_EXIT\n" );
               break;

            default:
               break;
         }
         tmp = tmp->shvnext;
      }
   }

   if (ORexxVariablePool)
   {
      rc = (*ORexxVariablePool)(
         (PSHVBLOCK) RequestBlockList);
   }

   if (Trace)
   {
      PSHVBLOCK tmp=RequestBlockList;

      while(tmp)
      {
         switch( tmp->shvcode )
         {
            case RXSHV_SET:
               log( "out   RXSHV_SET: shvret: %x\n",
                      tmp->shvret );
               break;

            case RXSHV_SYSET:
               log( "out RXSHV_SYSET: shvret: %x\n",
                      tmp->shvret );
               break;

            case RXSHV_FETCH:
               log( "out RXSHV_FETCH: shvret: %x shvvalue: \"%s\" shvvaluelen: %d\n",
                      tmp->shvret, tmp->shvvalue.strptr, tmp->shvvaluelen );
               break;

            case RXSHV_DROPV:
               log( "out RXSHV_DROPV: shvret: %x\n",
                      tmp->shvret );
               break;

            case RXSHV_SYDRO:
               log( "out RXSHV_SYDRO: shvret: %x\n",
                      tmp->shvret );
               break;

            case RXSHV_NEXTV:
               log( "out RXSHV_NEXTV: shvret: %x\n",
                      tmp->shvret );
               break;

            case RXSHV_PRIV:
               log( "out  RXSHV_PRIV: shvret: %x\n",
                      tmp->shvret );
               break;

            case RXSHV_EXIT:
               log( "out  RXSHV_EXIT: shvret: %x\n",
                      tmp->shvret );
               break;

            default:
               break;
         }
         tmp = tmp->shvnext;
      }

      log( "<=> Result: %d\n", rc );
   }

   return rc;
}

/* ============================================================= */
/* Asynchronous Request Rexx API interface */

APIRET APIENTRY RexxSetHalt( PID pid,
                             TID tid)
{
   APIRET rc = RXARI_NOT_FOUND;

   log( "%s: pid: %ld tid: %ld ",
          "RexxSetHalt()",
          pid,
          tid );

   if (ORexxSetHalt)
   {
      rc = (*ORexxSetHalt)(
         (LONG)      pid,
         (LONG)      tid);
   }

   log( "<=> Result: %d\n", rc );

   return rc;
}

APIRET APIENTRY RexxSetTrace( PID pid,
                              TID tid)
{
   APIRET rc = RXARI_NOT_FOUND;

   log( "%s: pid: %ld tid: %ld ",
          "RexxSetTrace()",
          pid,
          tid );

   if (ORexxSetTrace)
   {
      rc = (*ORexxSetTrace)(
         (LONG)      pid,
         (LONG)      tid);
   }

   log( "<=> Result: %d\n", rc );

   return rc;
}

APIRET APIENTRY RexxResetTrace( PID pid,
                                TID tid)
{
   APIRET rc = RXARI_NOT_FOUND;

   log( "%s: pid: %ld tid: %ld ",
          "RexxResetTrace()",
          pid,
          tid );

   if (ORexxResetTrace)
   {
      rc = (*ORexxResetTrace)(
         (LONG)      pid,
         (LONG)      tid);
   }

   log( "<=> Result: %d\n", rc );

   return rc;
}

#if 1

APIRET unimplemented(char *func)
{
  log("%s is not yet implemented!\n", func);
  return 0;
}

/*
 * The following functions do nothing; they are here because they are exported in the DLL
 */
APIRET APIENTRY RexxBreakCleanup(
                VOID )
{
   APIRET rc = 0;

   log( "%s: ", "RxBreakCleanup()" );

   unimplemented(__FUNCTION__);

   log( "<=> Result: %d\n", rc );

   return rc;
}

#endif
