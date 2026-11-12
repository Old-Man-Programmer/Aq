/* $Copyright: $
 * Copyright (c) 2022-2025 by Steve Baker (steve.baker.llc@gmail.com)
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
 */
#include <stdio.h>
#include <unistd.h>
#include <sys/file.h>
#include <stdlib.h>
#include <stdint.h>

int main(int argc, char *argv[])
{
  if (argc < 3) {
    printf("xor <rom> <lock>\n");
    return 1;
  }
  int in = open(argv[1], O_RDONLY);
  if (in < 0) {
    printf("Unable to open romfile\n");
    return 1;
  }
  int xor = atoi(argv[2]);
  
  uint8_t byte;
  while(read(in, &byte, 1) == 1) {
    byte ^= xor;
    if(!write(STDOUT_FILENO, &byte, 1)) {
      perror("write");
      exit(1);
    }
  }
  close(in);
  
  return 0;
}
