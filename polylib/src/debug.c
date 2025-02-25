/*
 * Debug.c
 */

#include <stdarg.h>
#include <stdio.h>

void DEBUG_T(const char* format, ... ) { 
#ifdef DEBUG
        va_list args;
        va_start(args, format);
        printf("PolyStore DEBUG: ");
        vfprintf(stdout, format, args);
        va_end(args);
        printf("\n");
#endif
}

void ERROR_T(const char* format, ... ) { 
        va_list args;
        va_start(args, format);
        printf("PolyStore ERROR: ");
        vfprintf(stdout, format, args);
        va_end(args);
        printf("\n");
}

void WARN_T(const char* format, ... ) { 
        va_list args;
        va_start(args, format);
        printf("PolyStore WARN: ");
        vfprintf(stdout, format, args);
        va_end(args);
        printf("\n");
}

void INFO_T(const char* format, ... ) { 
        va_list args;
        va_start(args, format);
        printf("PolyStore INFO: ");
        vfprintf(stdout, format, args);
        va_end(args);
        printf("\n");
}

