#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <unistd.h>
#include "../src/lab.h"


int main(int argc, char **argv)
{
  int opt;

    // Use getopt to parse command line arguments
    while ((opt = getopt(argc, argv, "v")) != -1) {
        switch (opt) {
            case 'v':
                // Print the version and exit
                printf("Shell version: %d.%d\n", lab_VERSION_MAJOR, lab_VERSION_MINOR);
                exit(0);
            default:
                fprintf(stderr, "Usage: %s [-v]\n", argv[0]);
                exit(EXIT_FAILURE);
        }
    }
  return 0;
}