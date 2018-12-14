/*
 * pcimem.c: Simple program to read/write from/to a pci device from userspace.
 *
 *  Copyright (C) 2010, Bill Farrow (bfarrow@beyondelectronics.us)
 *
 *  Based on the devmem2.c code
 *  Copyright (C) 2000, Jan-Derk Bakker (J.D.Bakker@its.tudelft.nl)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 */

/*
 * This program has been hijacked for a quick test/demo.
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <signal.h>
#include <fcntl.h>
#include <ctype.h>
#include <termios.h>
#include <sys/types.h>
#include <sys/mman.h>
#include <sys/stat.h>

#define PRINT_ERROR \
	do { \
		fprintf(stderr, "Error at line %d, file %s (%d) [%s]\n", \
		__LINE__, __FILE__, errno, strerror(errno)); exit(1); \
	} while(0)


int main(int argc, char **argv) {
    int fd;
    void *map_base, *virt_addr;
    uint64_t read_result, writeval, prev_read_result = 0;
    char *filename;
    off_t target, target_base;
    int access_type = 'w';
    int items_count = 1;
    int verbose = 1;
    int read_result_dupped = 0;
    int type_width;
    int i;
    int map_size = 4096UL;

    FILE *input_fp;
    bool processing_file = false;
    char *input_filename;
    size_t len;
    ssize_t read;
    char *line = NULL;
    char userop[] = "expect"; // longest valid op
    uint64_t userop_addr = 0, userop_val = 0;
    uint64_t expect_fail_count = 0, expect_total_count = 0;

    if(argc == 4 && !strcmp(argv[2], "-f")) {
        fprintf(stderr, "\nFile Processing Mode\n\n");
        processing_file = true;
        filename = argv[1];
        input_filename = argv[3];
    }
    else {
        if(argc < 3) {
            // pcimem /sys/bus/pci/devices/0001\:00\:07.0/resource0 0x100 w 0x00
            // argv[0]  [1]                                         [2]   [3] [4]
            fprintf(stderr, "\nUsage:\t%s { sysfile } { offset } [ type*count [ data ] ]\n"
                    "\tsys file: sysfs file for the pci resource to act on\n"
                    "\toffset  : offset into pci memory region to act upon\n"
                    "\ttype    : access operation type : [b]yte, [h]alfword, [w]ord, [d]ouble-word\n"
                    "\t*count  : number of items to read:  w*100 will dump 100 words\n"
                    "\tdata    : data to be written\n\n"
                    "\n"
                    "Usage:\t%s { sysfile } -f { inputfile }\n"
                    "\tsys file: sysfs file for the pci resource to act on\n"
                    "\tinput file: source file containing data to be written in \"operation 0xaddress [0xvalue]\",\n"
                    "\t\tline-separated format. This supports 32-bit values only. Operations: read, write, expect.\n"
                    "\t\tE.g. read 0x08f00000\n"
                    "\t\t     write 0x08f00000 0x0fa1afe1\n"
                    "\t\t     expect 0x08f00000 0x0fa1afe1\n\n",
                    argv[0], argv[0]);
            exit(1);
        }
        filename = argv[1];
        target = strtoul(argv[2], 0, 0);

        if(argc > 3) {
            access_type = tolower(argv[3][0]);
            if (argv[3][1] == '*')
                items_count = strtoul(argv[3]+2, 0, 0);
        }
    }

    switch(access_type) {
        case 'b':
            type_width = 1;
            break;
        case 'h':
            type_width = 2;
            break;
        case 'w':
            type_width = 4;
            break;
        case 'd':
            type_width = 8;
            break;
        default:
            fprintf(stderr, "Illegal data type '%c'.\n", access_type);
            exit(2);
    }

    if((fd = open(filename, O_RDWR | O_SYNC)) == -1) PRINT_ERROR;
    printf("%s opened.\n", filename);
    printf("Target offset is 0x%x, page size is %ld\n", (int) target, sysconf(_SC_PAGE_SIZE));
    fflush(stdout);

    /* Map entire file */

    target_base = 0;

    // Get file length
    struct stat st;
    stat(filename, &st);
    map_size = st.st_size;

    printf("mmap(%d, 0x%x, 0x%x, 0x%x, %d, 0x%x)\n", 0, map_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, (int) target);

    map_base = mmap(0, map_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, target_base);
    if(map_base == (void *) -1) PRINT_ERROR;
    printf("PCI Memory mapped to address 0x%08lx.\n", (unsigned long) map_base);
    fflush(stdout);

    if(processing_file) {
        if(verbose) fprintf(stderr, "Opening file...\n");
        if((input_fp = fopen(input_filename, "r")) == NULL)
            PRINT_ERROR;
        if(verbose) fprintf(stderr, "Opened file.\n");

        while ((read = getline(&line, &len, input_fp)) != -1) {
            if(verbose)
                fprintf(stderr, "Read line: %s", line); // Line will end in \n?
            if(*line == '\n' || *line == '#')
                continue;

            // Parse line. Op has max length 6 (expect)
            if(verbose) fprintf(stderr, "Trying to parse with 3 words...\n");
            if(sscanf(line, "%6s 0x%lx 0x%lx", userop, &userop_addr, &userop_val) != 3) {
                // If we don't parse all 3, try parsing a "read" op line
                strcpy(userop, "read");
                userop_val = 0;
                if(verbose) fprintf(stderr, "Trying to parse as read command...\n");
                if (sscanf(line, "read 0x%lx", &userop_addr) != 1)
                    PRINT_ERROR;
            }
            if(verbose) fprintf(stderr, "Parsed.\n");

            // Check the op is valid
            if(strcmp(userop, "write") && strcmp(userop, "read") && strcmp(userop, "expect")) {
                fprintf(stderr, "Invalid op: %s\n", userop);
                PRINT_ERROR;
            }
            if(userop_addr >= map_size) {
                fprintf(stderr, "Address out of bounds: 0x%lx\n", userop_addr);
                PRINT_ERROR;
            }

            /* Do the op */
            if(!strcmp(userop, "write")) {
                if(verbose)
                    fprintf(stderr, "Writing to 0x%lx: 0x%lx\n", userop_addr, userop_val);
                // Write the value
                virt_addr = map_base + userop_addr;
                *((uint32_t *) virt_addr) = userop_val;
            }
            else {
                if(verbose)
                    fprintf(stderr, "Reading from 0x%lx\n", userop_addr);
                // Read the value
                virt_addr = map_base + userop_addr;
                read_result = *((uint32_t *) virt_addr);

                // Check expected value, if required
                if(!strcmp(userop, "expect")) {
                    ++expect_total_count;
                    if(read_result == userop_val) {
                        if(verbose)
                            fprintf(stderr, "Read expected value (0x%lx) \n", read_result);
                    }
                    else {
                        fprintf(stderr, "Read UNEXPECTED value: 0x%lx (expected 0x%lx)\n", read_result, userop_val);
                        ++expect_fail_count;
                    }
                }
                else {
                    // userop == "read"
                    fprintf(stdout, "0x%lx 0x%lx\n", userop_addr, read_result);
                }
            }

            if(verbose) {
                fprintf(stderr, "\n");
                fflush(stderr);
                fflush(stdout);
            }
        }

        fprintf(stderr, "Expected checks: %ld / %ld\n", expect_total_count - expect_fail_count, expect_total_count);
        if(expect_fail_count > 0)
          fprintf(stderr, "FAILED %ld checks\n", expect_fail_count);

        fclose(input_fp);
        // getline requires we free the buffer even on failure
        if (line)
            free(line);
    }
    else {
        /* Handle reads/writes specified directly on the CLI */

        if(argc > 4) {
            // Do a write
            virt_addr = map_base + target;
            writeval = strtoull(argv[4], NULL, 0);
            switch(access_type) {
                case 'b':
                    *((uint8_t *) virt_addr) = writeval;
                    break;
                case 'h':
                    *((uint16_t *) virt_addr) = writeval;
                    break;
                case 'w':
                    *((uint32_t *) virt_addr) = writeval;
                    break;
                case 'd':
                    *((uint64_t *) virt_addr) = writeval;
                    break;
            }
            printf("Written 0x%0*lX\n", type_width, writeval);
            fflush(stdout);
        }
        else {
            // Do a read
            for (i = 0; i < items_count; i++) {

                virt_addr = map_base + target + i*type_width;
                switch(access_type) {
                    case 'b':
                            read_result = *((uint8_t *) virt_addr);
                            break;
                    case 'h':
                            read_result = *((uint16_t *) virt_addr);
                            break;
                    case 'w':
                            read_result = *((uint32_t *) virt_addr);
                            break;
                    case 'd':
                            read_result = *((uint64_t *) virt_addr);
                            break;
                }

                if (verbose)
                    printf("Value at offset 0x%X (%p): 0x%0*lX\n", (int) target + i*type_width, virt_addr, type_width*2, read_result);
                else {
                    if (read_result != prev_read_result || i == 0) {
                        printf("0x%04X: 0x%0*lX\n", (int)(target + i*type_width), type_width*2, read_result);
                        read_result_dupped = 0;
                    } else {
                        if (!read_result_dupped)
                            printf("...\n");
                        read_result_dupped = 1;
                    }
                }

                prev_read_result = read_result;

            }

            fflush(stdout);
        }
    }

    if(munmap(map_base, map_size) == -1) PRINT_ERROR;
    close(fd);
    return ((expect_fail_count == 0) ? 0 : 1);
}
