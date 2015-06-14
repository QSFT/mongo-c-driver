/*
 * Copyright 2013 MongoDB, Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */


#if defined(__linux__)
# include <sys/syscall.h>
#elif defined(_WIN32)
# include <process.h>
#else
# include <unistd.h>
#endif
#include <stdarg.h>
#include <time.h>

#include "mongoc-log.h"
#include "mongoc-thread-private.h"


static mongoc_mutex_t       gLogMutex;
static mongoc_log_func_t  gLogFunc = mongoc_log_default_handler;
static void              *gLogData;

static MONGOC_ONCE_FUN( _mongoc_ensure_mutex_once)
{
   mongoc_mutex_init(&gLogMutex);

   MONGOC_ONCE_RETURN;
}

void
mongoc_log_set_handler (mongoc_log_func_t  log_func,
                        void              *user_data)
{
   static mongoc_once_t once = MONGOC_ONCE_INIT;
   mongoc_once(&once, &_mongoc_ensure_mutex_once);

   mongoc_mutex_lock(&gLogMutex);
   gLogFunc = log_func;
   gLogData = user_data;
   mongoc_mutex_unlock(&gLogMutex);
}


/* just for testing */
void
_mongoc_log_get_handler (mongoc_log_func_t  *log_func,
                         void              **user_data)
{
   *log_func = gLogFunc;
   *user_data = gLogData;
}


void
mongoc_log (mongoc_log_level_t  log_level,
            const char         *log_domain,
            const char         *format,
            ...)
{
   va_list args;
   char *message;
   static mongoc_once_t once = MONGOC_ONCE_INIT;

   mongoc_once(&once, &_mongoc_ensure_mutex_once);

   if (!gLogFunc)
      return;

   bson_return_if_fail(format);

   va_start(args, format);
   message = bson_strdupv_printf(format, args);
   va_end(args);

   mongoc_mutex_lock(&gLogMutex);
   gLogFunc(log_level, log_domain, message, gLogData);
   mongoc_mutex_unlock(&gLogMutex);

   bson_free(message);
}


const char *
mongoc_log_level_str (mongoc_log_level_t log_level)
{
   switch (log_level) {
   case MONGOC_LOG_LEVEL_ERROR:
      return "ERROR";
   case MONGOC_LOG_LEVEL_CRITICAL:
      return "CRITICAL";
   case MONGOC_LOG_LEVEL_WARNING:
      return "WARNING";
   case MONGOC_LOG_LEVEL_MESSAGE:
      return "MESSAGE";
   case MONGOC_LOG_LEVEL_INFO:
      return "INFO";
   case MONGOC_LOG_LEVEL_DEBUG:
      return "DEBUG";
   case MONGOC_LOG_LEVEL_TRACE:
      return "TRACE";
   default:
      return "UNKNOWN";
   }
}


void
mongoc_log_default_handler (mongoc_log_level_t  log_level,
                            const char         *log_domain,
                            const char         *message,
                            void               *user_data)
{
   struct timeval tv;
   struct tm tt;
   time_t t;
   FILE *stream;
   char nowstr[32];
   int pid;

   bson_gettimeofday(&tv);
   t = tv.tv_sec;

#ifdef _WIN32
#  ifdef _MSC_VER
     localtime_s(&tt, &t);
#  else
     tt = *(localtime(&t));
#  endif
#else
   localtime_r(&t, &tt);
#endif

   strftime (nowstr, sizeof nowstr, "%Y/%m/%d %H:%M:%S", &tt);

   switch (log_level) {
   case MONGOC_LOG_LEVEL_ERROR:
   case MONGOC_LOG_LEVEL_CRITICAL:
   case MONGOC_LOG_LEVEL_WARNING:
      stream = stderr;
      break;
   case MONGOC_LOG_LEVEL_MESSAGE:
   case MONGOC_LOG_LEVEL_INFO:
   case MONGOC_LOG_LEVEL_DEBUG:
   case MONGOC_LOG_LEVEL_TRACE:
   default:
      stream = stdout;
   }

#ifdef __linux__
   pid = syscall (SYS_gettid);
#elif defined(_WIN32)
   pid = (int)_getpid ();
#else
   pid = (int)getpid ();
#endif

   fprintf (stream,
            "%s.%04ld: [%5d]: %8s: %12s: %s\n",
            nowstr,
            tv.tv_usec / 1000L,
            pid,
            mongoc_log_level_str(log_level),
            log_domain,
            message);
}
