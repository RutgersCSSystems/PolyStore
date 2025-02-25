#include <getopt.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "heteroio.h"

char* FAST_DIR;
char* SLOW_DIR;
char* BCACHE_DIR;
char* HETERO_DIR;

void DEBUG_T(const char* format, ... ) {
#ifdef DEBUG
        va_list args;
        va_start( args, format );
        vfprintf(stdout, format, args );
        va_end( args );
#endif
}

unsigned long capacity_stoul(char* origin) {
        char *str = malloc(strlen(origin) + 1);
        strcpy(str, origin);

        /* magnitude is last character of size */
        char size_magnitude = str[strlen(str)-1];

        /* erase magnitude char */
        str[strlen(str)-1] = 0;

        unsigned long file_size_bytes = strtoul(str, NULL, 0);

        switch(size_magnitude) {
                case 'g':
                case 'G':
                        file_size_bytes *= 1024;
                case 'm':
                case 'M':
                        file_size_bytes *= 1024;
                case '\0':
                case 'k':
                case 'K':
                        file_size_bytes *= 1024;
                        break;
                case 'p':
                case 'P':
                        file_size_bytes *= 4;
                        break;
                case 'b':
                case 'B':
                        break;
                default:
                        printf("incorrect size format\n");
                        break;
        }
        free(str);
        return file_size_bytes;
}

int getargs(int argc, char **argv) {
        int c = 0;

        while (1) {
                static struct option long_options[] = {
                        {"thread",      required_argument, 0, 'r'},
                        {"workload",    required_argument, 0, 'j'},
                        {"iosize",      required_argument, 0, 's'},
                        {"iotype",      required_argument, 0, 't'},
                        {"fsync",       required_argument, 0, 'y'},
                        {"sharefile",   required_argument, 0, 'x'},
                        {"filesize",    required_argument, 0, 'z'},
                        {"heterofactor",required_argument, 0, 'f'},
                        {"randomseed",  required_argument, 0, 'd'},
                        {0, 0, 0, 0}
                };

                /* getopt_long stores the option index here. */
                int option_index = 0;

                c = getopt_long (argc, argv, "r:j:s:t:y:x:z:f:d:",
                                                long_options, &option_index);

                /* Detect the end of the options. */
                if (c == -1)
                        break;

                switch (c) {
                        case 0:
                                /* If this option set a flag, do nothing else now. */
                                if (long_options[option_index].flag != 0)
                                        break;
                                if (optarg)
                                        printf (" with arg %s\n", optarg);
                                break;

                        case 'r':
                                config_thread_nr = atoi(optarg);
                                break;

                        case 'j':
                                config_workload = (workload_t) atoi(optarg);
                                break;

                        case 's':
                                config_io_size = atoi(optarg);
                                break;

                        case 't':
                                config_io_type = (io_type_t) atoi(optarg);
                                break;

                        case 'y':
                                config_fsync_freq = atoi(optarg);
                                break;

                        case 'x':
                                config_sharefile = atoi(optarg);
                                break;

                        case 'z':
                                config_filesize = capacity_stoul(optarg);
                                break;

                        case 'f':
                                config_hetero_factor = (hetero_factor_t) atoi(optarg);
                                break;

                        case 'd':
                                config_rand_seed = (random_seed_t) atoi(optarg);
                                break;

                        case '?':
                                /* getopt_long already printed an error message. */
                                break;

                        default:
                                abort();
                }
        }

        /* Print any remaining command line arguments (not options). */
        if (optind < argc) {
                printf ("non-option ARGV-elements: ");
                while (optind < argc)
                printf ("%s ", argv[optind++]);
                putchar ('\n');
        }

        return 0;
}

