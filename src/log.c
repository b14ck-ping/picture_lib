#include <stddef.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <time.h>


#include "log.h"

static bool log_enabled = true;
static log_level_t log_level = LOG_LEVEL_ERROR;

static char* s_get_level_str(log_level_t lev)
{
    switch (lev){
        case LOG_LEVEL_NOYIFY:
            return "NOYIFY";
        case LOG_LEVEL_ERROR:
            return "ERROR";
        case LOG_LEVEL_WARNING:
            return "WARNING";
        case LOG_LEVEL_DEBUG:
            return "DEBUG";
        default:
            return "*";
    }
}

void log_str(log_level_t lev, const char* format_str, ...)
{
    if (!log_enabled || log_level < lev)
        return;
    char buf_str[4096] = {0};
    va_list ap;

    time_t rawtime;
    struct tm *timeinfo;
    char buffer[80];

    time(&rawtime);
    timeinfo = localtime(&rawtime);
    strftime(buffer, sizeof(buffer), "%d/%m/%Y %H:%M:%S", timeinfo);

    sprintf(buf_str, "[%s] [%s] ", buffer, s_get_level_str(lev));

    va_start(ap, format_str);
    vsprintf(&buf_str[strlen(buf_str)], format_str, ap);
    va_end(ap);

    buf_str[strlen(buf_str)] = '\n';
    buf_str[strlen(buf_str)] = '\0';

    printf("%s", buf_str);
    fflush(stdout);
    FILE* log_file = fopen("./log.log", "a");
    if (!log_file){
        printf("Can't open log file.");
        return;
    }
    fputs(buf_str, log_file);
    
    fclose(log_file);
}

