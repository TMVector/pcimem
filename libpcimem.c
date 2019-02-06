/*
 * libpcimem.c: Simple library to read/write from/to a pci device from userspace.
 *              Designed for use from Python.
 *
 *
 *  Based on the pcimem.c code
 *  Copyright (C) 2010, Bill Farrow (bfarrow@beyondelectronics.us)
 *  See pcimem.c for more details.
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

#include <stdlib.h>
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

// For handling files and mmap
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <stdio.h>

// For memcpy
#include <string.h>

#define MOCK(f_, ...) fprintf(stderr, "Pcimem[mock]> " f_ "\n", ##__VA_ARGS__)
#define MOCKBREAK() fprintf(stderr, "\n")

struct Pcimem_h
{
  int fd;
  void *map_base;
  size_t map_length;
  bool mock;
};

struct Pcimem_h *Pcimem_new(const char *const file_path,
                            const bool mock)
{
  struct Pcimem_h *ph = malloc(sizeof(struct Pcimem_h));

  ph->mock = mock;
  if (ph->mock) {
    MOCK("Opened %s", file_path);
    MOCKBREAK();
    return ph;
  }

  // Open the file
  ph->fd = open(file_path,  O_RDWR | O_SYNC);
  if (ph->fd == -1)
    goto allocated_err;

  // Get file length
  struct stat st;
  stat(file_path, &st);
  ph->map_length = st.st_size;

  // mmap the entire file
  ph->map_base = mmap( NULL /* addr; let kernel choose */
                     , ph->map_length /* length */
                     , PROT_READ | PROT_WRITE /* prot */
                     , MAP_SHARED /* flags */
                     , ph->fd /*fd */
                     , 0 /* offset */
                     );
  if(ph->map_base == (void*) -1)
    goto opened_err;

  return ph;

opened_err:
  close(ph->fd);
allocated_err:
  free(ph);
  return NULL;
}

void Pcimem_close(struct Pcimem_h *ph)
{
  if (ph->mock) {
    MOCK("Closed");
    MOCKBREAK();
    return;
  }

  // We can't really do anything if these cleanup calls fail,
  // but the kernel will cleanup on process exit in any case.

  munmap(ph->map_base, ph->map_length);
  close(ph->fd);
  free((void*)ph);
}

uint32_t Pcimem_read_word(const struct Pcimem_h *const ph,
                          const uint64_t address)
{
  if (ph->mock) {
    MOCK("READ WORD   0x%08lx", address);
    MOCKBREAK();
    return 0;
  }

  uint32_t *virt_addr = (uint32_t*)(ph->map_base + address);
  return *virt_addr;
}

void Pcimem_write_word(const struct Pcimem_h *const ph,
                       const uint64_t address,
                       const uint32_t value)
{
  if (ph->mock) {
    MOCK("WRITE WORD  0x%08lx 0x%08x", address, value);
    MOCKBREAK();
    return;
  }

  uint32_t *virt_addr = (uint32_t*)(ph->map_base + address);
  *virt_addr = value;
}

// TODO: use memcpy?

void Pcimem_read_range(const struct Pcimem_h *const ph,
                       const uint32_t num_words,
                       const uint64_t address,
                       uint32_t *const words)
{
  if (ph->mock) {
    MOCK("READ RANGE  0x%08lx * %d", address, num_words);
    MOCKBREAK();
    return;
  }

  uint32_t *virt_addr = (uint32_t*)(ph->map_base + address);

  for (uint32_t i = 0; i < num_words; ++i) {
    words[i] = virt_addr[i];
  }
}

void Pcimem_write_range(const struct Pcimem_h *const ph,
                        const uint32_t num_words,
                        const uint64_t address,
                        const uint32_t *const words)
{
  if (ph->mock) {
    MOCK("WRITE RANGE 0x%08lx * %d:", address, num_words);
    for (uint32_t i = 0; i < num_words; ++i) {
      MOCK("      DATA  0x%08lx 0x%08x", address + 4 * i, words[i]);
    }
    MOCKBREAK();
    return;
  }

  uint32_t *virt_addr = (uint32_t*)(ph->map_base + address);

  for (uint32_t i = 0; i < num_words; ++i) {
    virt_addr[i] = words[i];
  }
}

void Pcimem_read_range_memcpy(const struct Pcimem_h *const ph,
                              const uint32_t num_words,
                              const uint64_t address,
                              uint32_t *const words)
{
  uint32_t *virt_addr = (uint32_t*)(ph->map_base + address);

  memcpy(words, virt_addr, sizeof(uint32_t)*num_words);
}

void Pcimem_write_range_memcpy(const struct Pcimem_h *const ph,
                               const uint32_t num_words,
                               const uint64_t address,
                               const uint32_t *const words)
{
  uint32_t *virt_addr = (uint32_t*)(ph->map_base + address);

  memcpy(virt_addr, words, sizeof(uint32_t)*num_words);
}

void Pcimem_copy_fifo(const struct Pcimem_h *const ph,
                      const uint32_t num_words,
                      const uint64_t fifo_fill_level_address,
                      const uint32_t *src,
                      uint32_t *dst,
                      bool src_is_fifo)
{
  uint32_t remaining = num_words;

  while (remaining > 0) {
    // Check how much we are allowed/should read/write
    uint32_t fill_level = Pcimem_read_word(ph, fifo_fill_level_address);
    fill_level = fill_level < remaining ? fill_level : remaining;

    remaining -= fill_level;

    if (src_is_fifo) {
      for (; fill_level > 0; --fill_level) {
        *(dst++) = *src;
      }
    }
    else {
      for (; fill_level > 0; --fill_level) {
        *dst = *(src++);
      }
    }
  }
}

void Pcimem_read_fifo(const struct Pcimem_h *const ph,
                      const uint32_t num_words,
                      const uint64_t fifo_fill_level_address,
                      const uint64_t address,
                      uint32_t *const words)
{
  if (ph->mock) {
    MOCK("READ FIFO   0x%08lx * %d", address, num_words);
    MOCKBREAK();
    return;
  }

  uint32_t *virt_addr = (uint32_t*)(ph->map_base + address);
  Pcimem_copy_fifo(ph, num_words, fifo_fill_level_address, virt_addr, words, true);
}

void Pcimem_write_fifo(const struct Pcimem_h *const ph,
                       const uint32_t num_words,
                       const uint64_t fifo_fill_level_address,
                       const uint64_t address,
                       const uint32_t *const words)
{
  if (ph->mock) {
    MOCK("WRITE FIFO  0x%08lx * %d:", address, num_words);
    for (uint32_t i = 0; i < num_words; ++i) {
      MOCK("      DATA  0x%08lx 0x%08x", address, words[i]);
    }
    MOCKBREAK();
    return;
  }

  uint32_t *virt_addr = (uint32_t*)(ph->map_base + address);
  Pcimem_copy_fifo(ph, num_words, fifo_fill_level_address, words, virt_addr, false);
}

void Pcimem_read_fifo_unsafe(const struct Pcimem_h *const ph,
                             const uint32_t num_words,
                             const uint64_t address,
                             uint32_t *const words)
{
  uint32_t *virt_addr = (uint32_t*)(ph->map_base + address);

  for (uint32_t i = 0; i < num_words; ++i) {
    words[i] = virt_addr[0];
  }
}

void Pcimem_write_fifo_unsafe(const struct Pcimem_h *const ph,
                              const uint32_t num_words,
                              const uint64_t address,
                              const uint32_t *const words)
{
  uint32_t *virt_addr = (uint32_t*)(ph->map_base + address);

  for (uint32_t i = 0; i < num_words; ++i) {
    virt_addr[0] = words[i];
  }
}
