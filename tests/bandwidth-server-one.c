/*
 * Copyright © 2008-2014 Stéphane Raimbault <stephane.raimbault@gmail.com>
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <stdio.h>
#ifndef _MSC_VER
#include <unistd.h>
#endif
#include <string.h>
#include <stdlib.h>
#include <errno.h>

#include <modbus.h>
#include <modbus-private.h>

#if defined(_WIN32)
#define close closesocket
#endif

enum {
    TCP,
    RTU
};

#define TEST_COUNT 2

int main(int argc, char *argv[])
{
    //////////////////////////////////////////////////////////////////////////
    int i = 0;
    modbus_request_t header;
    int index = 0;
    uint8_t test_data[TEST_COUNT][8] = 
    {
        {0x01, 0x01, 0x00, 0x00, 0x07, 0xD0, 0x3F, 0xA6},
        {0x01, 0x03, 0x00, 0x00, 0x00, 0x7D, 0x85, 0xEB}
    };
    //////////////////////////////////////////////////////////////////////////

    int s = -1;
    modbus_t *ctx = NULL;
    modbus_mapping_t *mb_mapping = NULL;
    int rc;
    int use_backend;

     /* TCP */
    if (argc > 1) {
        if (strcmp(argv[1], "tcp") == 0) {
            use_backend = TCP;
        } else if (strcmp(argv[1], "rtu") == 0) {
            use_backend = RTU;
        } else {
            printf("Usage:\n  %s [tcp|rtu] - Modbus client to measure data bandwith\n\n", argv[0]);
            exit(1);
        }
    } else {
        /* By default */
        use_backend = TCP;
    }

    if (use_backend == TCP) {
        ctx = modbus_new_tcp("127.0.0.1", 1502);
        s = modbus_tcp_listen(ctx, 1);
        modbus_tcp_accept(ctx, &s);

    } else {
        ctx = modbus_new_rtu("COM2", 115200, 'N', 8, 1, 0);///dev/ttyUSB0
        modbus_set_slave(ctx, 1);
        modbus_connect(ctx);
    }

    modbus_set_debug(ctx, TRUE);

    mb_mapping = modbus_mapping_new(MODBUS_MAX_READ_BITS, 0,
                                    MODBUS_MAX_READ_REGISTERS, 0);
    if (mb_mapping == NULL) {
        fprintf(stderr, "Failed to allocate the mapping: %s\n",
                modbus_strerror(errno));
        modbus_free(ctx);
        return -1;
    }

    for(;;) {
        uint8_t query[MODBUS_TCP_MAX_ADU_LENGTH];

        modbus_rtu_set_buffer(ctx, test_data[index], 8);
        
        rc = modbus_receive(ctx, query);
        if (rc > 0) 
        {
            modbus_parse_request(ctx, query, mb_mapping, &header);
            if (header.function == MODBUS_FC_READ_COILS)
            {
                for (i = header.mapping_address; i < header.mapping_address + header.nb; i++) 
                {
                    mb_mapping->tab_bits[i] = (i %8 == 0 ? ON : OFF);
                }
            }
            else if (header.function == MODBUS_FC_READ_DISCRETE_INPUTS)
            {
                for (i = header.mapping_address; i < header.mapping_address + header.nb; i++) 
                {
                    mb_mapping->tab_input_bits[i] = i;
                }
            }
            else if (header.function == MODBUS_FC_READ_HOLDING_REGISTERS)
            {
                for (i = header.mapping_address; i < header.mapping_address + header.nb; i++) 
                {
                    mb_mapping->tab_registers[i] = i;
                }
            }
            else if (header.function == MODBUS_FC_READ_INPUT_REGISTERS)
            {
                for (i = header.mapping_address; i < header.mapping_address + header.nb; i++) 
                {
                    mb_mapping->tab_input_registers[i] = i;
                }
            }
            rc = modbus_reply(ctx, query, rc, mb_mapping);
        } 
        else if (rc  == -1) 
        {
            /* Connection closed by the client or error */
            break;
        }

        index++;
        if (index >= TEST_COUNT)
        {
            index = 0;
        }
    }

    printf("Quit the loop: %s\n", modbus_strerror(errno));

    modbus_mapping_free(mb_mapping);
    if (s != -1) {
        close(s);
    }
    /* For RTU, skipped by TCP (no TCP connect) */
    modbus_close(ctx);
    modbus_free(ctx);

    return 0;
}
