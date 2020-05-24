/*
 * co2mon - programming interface to CO2 sensor.
 * Copyright (C) 2015  Oleg Bulatov <oleg@bulatov.me>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#define _XOPEN_SOURCE 700 /* strnlen */
#define _BSD_SOURCE /* daemon() in glibc before 2.19 */
#define _DEFAULT_SOURCE /* _BSD_SOURCE is deprecated in glibc 2.19+ */
#define _DARWIN_C_SOURCE /* daemon() on macOS */

#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "co2mon.h"

#define CODE_TAMB 0x42 /* Ambient Temperature */
#define CODE_CNTR 0x50 /* Relative Concentration of CO2 */

#define PATH_MAX 4096
#define VALUE_MAX 20

const char *devicefile = NULL;
char *datadir;

static double
decode_temperature(uint16_t w)
{
    return (double)w * 0.0625 - 273.15;
}

static void
device_loop(co2mon_device dev)
{
    co2mon_data_t magic_table = { 0 };
    co2mon_data_t result;

    if (!co2mon_send_magic_table(dev, magic_table))
    {
        fprintf(stdout, "{ \"success\": false, \"error\":\"Unable to send magic table to CO2 device\" }");
        return;
    }

    int has_temp = 0;
    int has_co2 = 0;
    double temperature = 0.0;
    int co2 = 0;

    while (!has_temp | !has_co2)
    {

        int r = co2mon_read_data(dev, magic_table, result);
        if (r <= 0)
        {
            fprintf(stdout, "{ \"success\": false, \"error\":\"Error while reading data from device\" }");
            break;
        }

        if (result[4] != 0x0d)
        {
            fprintf(stdout, "{ \"success\": false, \"error\":\"Unexpected data from device (data[4] = %02hhx, want 0x0d)\" }", result[4]);
            continue;
        }

        unsigned char r0, r1, r2, r3, checksum;
        r0 = result[0];
        r1 = result[1];
        r2 = result[2];
        r3 = result[3];
        checksum = r0 + r1 + r2;
        if (checksum != r3)
        {
            fprintf(stdout, "{ \"success\": false, \"error\":\"checksum error (%02hhx, await %02hhx)\" }", checksum, r3);
            continue;
        }

        uint16_t w = (result[1] << 8) + result[2];

        switch (r0)
        {
        case CODE_TAMB:
            temperature = decode_temperature(w);
            has_temp = 1;
            //snprintf(buf, VALUE_MAX, "%.4f", decode_temperature(w));
            break;
        case CODE_CNTR:
            if ((unsigned)w > 3000) {
                // Avoid reading spurious (uninitialized?) data
                break;
            }
			has_co2 = 1;
			co2 = (int)w;
            break;
        default:
            break;
        }
    }
	fprintf(stdout, "{ \"success\": true, \"data\": { \"temperature\":\"%.4f\", \"co2\":\"%d\" } }", temperature, co2);
}

static co2mon_device
open_device()
{
    if (devicefile)
    {
        return co2mon_open_device_path(devicefile);
    }
    return co2mon_open_device();
}

static void
main_loop()
{
    int error_shown = 0;
    co2mon_device dev = open_device();
    if (dev == NULL)
    {
       if (!error_shown)
       {
            fprintf(stdout, "{ \"success\": false, \"error\":\"Unable to open CO2 device\" }");
            error_shown = 1;
        }
        return;
    }
    else
    {
        error_shown = 0;
    }

    device_loop(dev);

    co2mon_close_device(dev);
}

int main(int argc, char *argv[])
{
    int r = co2mon_init();
    if (r < 0)
    {
        return r;
    }

    main_loop();

    co2mon_exit();
    return 1;
}
