/*
 *      dpAux.c                 (C) 2022-2023, Aurélien Croc (AP²C)
 *      Modified in 2026 by Samsung-Galaxy-Book-12-Linux contributors:
 *      checked I/O, close-on-exec and error reporting.
 *
 *   This program is free software; you can redistribute it and/or modify it under
 *   the terms of the GNU General Public License as published by the Free Software
 *   Foundation; version 2 of the License.
 *
 *   This program is distributed in the hope that it will be useful, but WITHOUT
 *   ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 *   FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 *   details.
 *
 *   You should have received a copy of the GNU General Public License along with
 *   this program; If not, see <http://www.gnu.org/licenses/>.
 */
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include "dpAux.h"

static int lastError;


aux_t dpAuxOpen(const char *path)
{
    int fd;

    if ((fd = open(path, O_RDWR | O_CLOEXEC)) == -1) {
        lastError = errno;
        return -1;
    }
    return fd;
}

int dpAuxRead(aux_t aux, int addr)
{
    unsigned char res;

    if (lseek(aux, addr, SEEK_SET) == -1 || read(aux, &res, 1) != 1) {
        lastError = errno ? errno : EIO;
        return -1;
    }
    return res;
}

int dpAuxWrite(aux_t aux, int addr, unsigned char val)
{
    if (lseek(aux, addr, SEEK_SET) == -1 || write(aux, &val, 1) != 1) {
        lastError = errno ? errno : EIO;
        return -1;
    }
    return 0;
}

int dpAuxWrites(aux_t aux, dpWrite_t data[])
{
    for (int i=0; data[i].addr; i++) {
        if (lseek(aux, data[i].addr, SEEK_SET) == -1 ||
            write(aux, data[i].val, data[i].nVal) != data[i].nVal) {
            lastError = errno ? errno : EIO;
            return -1;
        }
    }
    return 0;
}

void dpAuxClose(aux_t aux)
{
    if (aux >= 0)
        close(aux);
}

void dpAuxClearError(void)
{
    lastError = 0;
}

int dpAuxLastError(void)
{
    return lastError;
}



/* vim: set expandtab tabstop=4 shiftwidth=4 smarttab tw=80 cin fenc=utf8 : */
