/*
 * Copyright © Stéphane Raimbault <stephane.raimbault@gmail.com>
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <errno.h>
#include <modbus.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
//#include <unistd.h>

#include "unit-test.h"

const int EXCEPTION_RC = 2;

enum {
    TCP,
    TCP_PI,
    RTU
};

int test_server(modbus_t *ctx, int use_backend);
int send_crafted_request(modbus_t *ctx,
                         int function,
                         uint8_t *req,
                         int req_size,
                         uint16_t max_value,
                         uint16_t bytes,
                         int backend_length,
                         int backend_offset);
int equal_dword(uint16_t *tab_reg, const uint32_t value);
int is_memory_equal(const void *s1, const void *s2, size_t size);

#define BUG_REPORT(_cond, _format, _args...) \
    printf(                                  \
        "\nLine %d: assertion error for '%s': " _format "\n", __LINE__, #_cond, ##_args)

#define ASSERT_TRUE(_cond, _format, __args...)    \
    {                                             \
        if (_cond) {                              \
            printf("OK\n");                       \
        } else {                                  \
            BUG_REPORT(_cond, _format, ##__args); \
            goto close;                           \
        }                                         \
    };

int is_memory_equal(const void *s1, const void *s2, size_t size)
{
    return (memcmp(s1, s2, size) == 0);
}

int equal_dword(uint16_t *tab_reg, const uint32_t value)
{
    return ((tab_reg[0] == (value >> 16)) && (tab_reg[1] == (value & 0xFFFF)));
}

int main(int argc, char *argv[])
{
    /* Length of report slave ID response slave ID + ON/OFF + 'LMB' + version */
    const int NB_REPORT_SLAVE_ID = 2 + 3 + strlen(LIBMODBUS_VERSION_STRING);
    uint8_t *tab_rp_bits = NULL;
    uint16_t *tab_rp_registers = NULL;
    uint16_t *tab_rp_registers_bad = NULL;
    modbus_t *ctx = NULL;
    int i;
    uint8_t value;
    int nb_points;
    int rc;
    float real;
    uint32_t old_response_to_sec;
    uint32_t old_response_to_usec;
    uint32_t new_response_to_sec;
    uint32_t new_response_to_usec;
    uint32_t old_byte_to_sec;
    uint32_t old_byte_to_usec;
    int use_backend;
    int success = FALSE;
    int old_slave;
    char *ip_or_device;

    if (argc > 1) {
        if (strcmp(argv[1], "tcp") == 0) {
            use_backend = TCP;
        } else if (strcmp(argv[1], "tcppi") == 0) {
            use_backend = TCP_PI;
        } else if (strcmp(argv[1], "rtu") == 0) {
            use_backend = RTU;
        } else {
            printf("Modbus client for unit testing\n");
            printf("Usage:\n  %s [tcp|tcppi|rtu]\n", argv[0]);
            printf("Eg. tcp 127.0.0.1 or rtu /dev/ttyUSB1\n\n");
            exit(1);
        }
    } else {
        /* By default */
        use_backend = TCP;
    }

    if (argc > 2) {
        ip_or_device = argv[2];
    } else {
        switch (use_backend) {
        case TCP:
            ip_or_device = "127.0.0.1";
            break;
        case TCP_PI:
            ip_or_device = "::1";
            break;
        case RTU:
            ip_or_device = "/dev/ttyUSB1";
            break;
        default:
            break;
        }
    }

    if (use_backend == TCP) {
        ctx = modbus_new_tcp(ip_or_device, 1502);
    } else if (use_backend == TCP_PI) {
        ctx = modbus_new_tcp_pi(ip_or_device, "1502");
    } else {
        ctx = modbus_new_rtu(ip_or_device, 115200, 'N', 8, 1, 0);
    }
    if (ctx == NULL) {
        fprintf(stderr, "Unable to allocate libmodbus context\n");
        return -1;
    }

    modbus_set_debug(ctx, TRUE);
    modbus_set_error_recovery(
        ctx, MODBUS_ERROR_RECOVERY_LINK | MODBUS_ERROR_RECOVERY_PROTOCOL);

    if (use_backend == RTU) {
        modbus_set_slave(ctx, SERVER_ID);
    }

    modbus_get_response_timeout(ctx, &old_response_to_sec, &old_response_to_usec);
    if (modbus_connect(ctx) == -1) {
        fprintf(stderr, "Connection failed: %s\n", modbus_strerror(errno));
        modbus_free(ctx);
        return -1;
    }

    /* Allocate and initialize the memory to store the bits */
    nb_points = (UT_BITS_NB > UT_INPUT_BITS_NB) ? UT_BITS_NB : UT_INPUT_BITS_NB;
    tab_rp_bits = (uint8_t *) malloc(nb_points * sizeof(uint8_t));
    memset(tab_rp_bits, 0, nb_points * sizeof(uint8_t));

    /* Allocate and initialize the memory to store the registers */
    nb_points = (UT_REGISTERS_NB > UT_INPUT_REGISTERS_NB) ? UT_REGISTERS_NB
                                                          : UT_INPUT_REGISTERS_NB;
    tab_rp_registers = (uint16_t *) malloc(nb_points * sizeof(uint16_t));
    memset(tab_rp_registers, 0, nb_points * sizeof(uint16_t));

    printf("** UNIT TESTING **\n");

    printf("1/1 No response timeout modification on connect: ");
    modbus_get_response_timeout(ctx, &new_response_to_sec, &new_response_to_usec);
    ASSERT_TRUE(old_response_to_sec == new_response_to_sec &&
                    old_response_to_usec == new_response_to_usec,
                "");

    printf("\nTEST WRITE/READ:\n");

    /** COIL BITS **/

    /* Single */
    rc = modbus_write_bit(ctx, UT_BITS_ADDRESS, ON);
    printf("1/2 modbus_write_bit: ");
    ASSERT_TRUE(rc == 1, "");

    rc = modbus_read_bits(ctx, UT_BITS_ADDRESS, 1, tab_rp_bits);
    printf("2/2 modbus_read_bits: ");
    ASSERT_TRUE(rc == 1, "FAILED (nb points %d)\n", rc);
    ASSERT_TRUE(tab_rp_bits[0] == ON, "FAILED (%0X != %0X)\n", tab_rp_bits[0], ON);

    /* End single */

    /* Multiple bits */
    {
        uint8_t tab_value[UT_BITS_NB];

        modbus_set_bits_from_bytes(tab_value, 0, UT_BITS_NB, UT_BITS_TAB);
        rc = modbus_write_bits(ctx, UT_BITS_ADDRESS, UT_BITS_NB, tab_value);
        printf("1/2 modbus_write_bits: ");
        ASSERT_TRUE(rc == UT_BITS_NB, "");
    }

    rc = modbus_read_bits(ctx, UT_BITS_ADDRESS, UT_BITS_NB, tab_rp_bits);
    printf("2/2 modbus_read_bits: ");
    ASSERT_TRUE(rc == UT_BITS_NB, "FAILED (nb points %d)\n", rc);

    i = 0;
    nb_points = UT_BITS_NB;
    while (nb_points > 0) {
        int nb_bits = (nb_points > 8) ? 8 : nb_points;

        value = modbus_get_byte_from_bits(tab_rp_bits, i * 8, nb_bits);
        ASSERT_TRUE(
            value == UT_BITS_TAB[i], "FAILED (%0X != %0X)\n", value, UT_BITS_TAB[i]);

        nb_points -= nb_bits;
        i++;
    }
    printf("OK\n");
    /* End of multiple bits */

    /** DISCRETE INPUTS **/
    rc =
        modbus_read_input_bits(ctx, UT_INPUT_BITS_ADDRESS, UT_INPUT_BITS_NB, tab_rp_bits);
    printf("1/1 modbus_read_input_bits: ");
    ASSERT_TRUE(rc == UT_INPUT_BITS_NB, "FAILED (nb points %d)\n", rc);

    i = 0;
    nb_points = UT_INPUT_BITS_NB;
    while (nb_points > 0) {
        int nb_bits = (nb_points > 8) ? 8 : nb_points;
        value = modbus_get_byte_from_bits(tab_rp_bits, i * 8, nb_bits);
        ASSERT_TRUE(value == UT_INPUT_BITS_TAB[i],
                    "FAILED (%0X != %0X)\n",
                    value,
                    UT_INPUT_BITS_TAB[i]);

        nb_points -= nb_bits;
        i++;
    }
    printf("OK\n");

    /** HOLDING REGISTERS **/

    /* Single register */
    rc = modbus_write_register(ctx, UT_REGISTERS_ADDRESS, 0x1234);
    printf("1/2 modbus_write_register: ");
    ASSERT_TRUE(rc == 1, "");

    rc = modbus_read_registers(ctx, UT_REGISTERS_ADDRESS, 1, tab_rp_registers);
    printf("2/2 modbus_read_registers: ");
    ASSERT_TRUE(rc == 1, "FAILED (nb points %d)\n", rc);
    ASSERT_TRUE(tab_rp_registers[0] == 0x1234,
                "FAILED (%0X != %0X)\n",
                tab_rp_registers[0],
                0x1234);
    /* End of single register */

    /* Many registers */
    rc = modbus_write_registers(
        ctx, UT_REGISTERS_ADDRESS, UT_REGISTERS_NB, UT_REGISTERS_TAB);
    printf("1/5 modbus_write_registers: ");
    ASSERT_TRUE(rc == UT_REGISTERS_NB, "");

    rc = modbus_read_registers(
        ctx, UT_REGISTERS_ADDRESS, UT_REGISTERS_NB, tab_rp_registers);
    printf("2/5 modbus_read_registers: ");
    ASSERT_TRUE(rc == UT_REGISTERS_NB, "FAILED (nb points %d)\n", rc);

    for (i = 0; i < UT_REGISTERS_NB; i++) {
        ASSERT_TRUE(tab_rp_registers[i] == UT_REGISTERS_TAB[i],
                    "FAILED (%0X != %0X)\n",
                    tab_rp_registers[i],
                    UT_REGISTERS_TAB[i]);
    }

    rc = modbus_read_registers(ctx, UT_REGISTERS_ADDRESS, 0, tab_rp_registers);
    printf("3/5 modbus_read_registers (0): ");
    ASSERT_TRUE(rc == -1, "FAILED (nb_points %d)\n", rc);

    nb_points = (UT_REGISTERS_NB > UT_INPUT_REGISTERS_NB) ? UT_REGISTERS_NB
                                                          : UT_INPUT_REGISTERS_NB;
    memset(tab_rp_registers, 0, nb_points * sizeof(uint16_t));

    /* Write registers to zero from tab_rp_registers and store read registers
       into tab_rp_registers. So the read registers must set to 0, except the
       first one because there is an offset of 1 register on write. */
    rc = modbus_write_and_read_registers(ctx,
                                         UT_REGISTERS_ADDRESS + 1,
                                         UT_REGISTERS_NB - 1,
                                         tab_rp_registers,
                                         UT_REGISTERS_ADDRESS,
                                         UT_REGISTERS_NB,
                                         tab_rp_registers);
    printf("4/5 modbus_write_and_read_registers: ");
    ASSERT_TRUE(
        rc == UT_REGISTERS_NB, "FAILED (nb points %d != %d)\n", rc, UT_REGISTERS_NB);

    ASSERT_TRUE(tab_rp_registers[0] == UT_REGISTERS_TAB[0],
                "FAILED (%0X != %0X)\n",
                tab_rp_registers[0],
                UT_REGISTERS_TAB[0]);

    for (i = 1; i < UT_REGISTERS_NB; i++) {
        ASSERT_TRUE(
            tab_rp_registers[i] == 0, "FAILED (%0X != %0X)\n", tab_rp_registers[i], 0);
    }

    /* End of many registers */

    /** INPUT REGISTERS **/
    rc = modbus_read_input_registers(
        ctx, UT_INPUT_REGISTERS_ADDRESS, UT_INPUT_REGISTERS_NB, tab_rp_registers);
    printf("1/1 modbus_read_input_registers: ");
    ASSERT_TRUE(rc == UT_INPUT_REGISTERS_NB, "FAILED (nb points %d)\n", rc);

    for (i = 0; i < UT_INPUT_REGISTERS_NB; i++) {
        ASSERT_TRUE(tab_rp_registers[i] == UT_INPUT_REGISTERS_TAB[i],
                    "FAILED (%0X != %0X)\n",
                    tab_rp_registers[i],
                    UT_INPUT_REGISTERS_TAB[i]);
    }

    /* MASKS */
    printf("1/1 Write mask: ");
    rc = modbus_write_register(ctx, UT_REGISTERS_ADDRESS, 0x12);
    rc = modbus_mask_write_register(ctx, UT_REGISTERS_ADDRESS, 0xF2, 0x25);
    ASSERT_TRUE(rc != -1, "FAILED (%x == -1)\n", rc);
    rc = modbus_read_registers(ctx, UT_REGISTERS_ADDRESS, 1, tab_rp_registers);
    ASSERT_TRUE(
        tab_rp_registers[0] == 0x17, "FAILED (%0X != %0X)\n", tab_rp_registers[0], 0x17);

    printf("\nTEST FLOATS\n");
    /** FLOAT **/
    printf("1/4 Set/get float ABCD: ");
    modbus_set_float_abcd(UT_REAL, tab_rp_registers);
    ASSERT_TRUE(is_memory_equal(tab_rp_registers, UT_IREAL_ABCD, 4),
                "FAILED Set float ABCD");
    real = modbus_get_float_abcd(UT_IREAL_ABCD);
    ASSERT_TRUE(real == UT_REAL, "FAILED (%f != %f)\n", real, UT_REAL);

    printf("2/4 Set/get float DCBA: ");
    modbus_set_float_dcba(UT_REAL, tab_rp_registers);
    ASSERT_TRUE(is_memory_equal(tab_rp_registers, UT_IREAL_DCBA, 4),
                "FAILED Set float DCBA");
    real = modbus_get_float_dcba(UT_IREAL_DCBA);
    ASSERT_TRUE(real == UT_REAL, "FAILED (%f != %f)\n", real, UT_REAL);

    printf("3/4 Set/get float BADC: ");
    modbus_set_float_badc(UT_REAL, tab_rp_registers);
    ASSERT_TRUE(is_memory_equal(tab_rp_registers, UT_IREAL_BADC, 4),
                "FAILED Set float BADC");
    real = modbus_get_float_badc(UT_IREAL_BADC);
    ASSERT_TRUE(real == UT_REAL, "FAILED (%f != %f)\n", real, UT_REAL);

    printf("4/4 Set/get float CDAB: ");
    modbus_set_float_cdab(UT_REAL, tab_rp_registers);
    ASSERT_TRUE(is_memory_equal(tab_rp_registers, UT_IREAL_CDAB, 4),
                "FAILED Set float CDAB");
    real = modbus_get_float_cdab(UT_IREAL_CDAB);
    ASSERT_TRUE(real == UT_REAL, "FAILED (%f != %f)\n", real, UT_REAL);

    printf("\nAt this point, error messages doesn't mean the test has failed\n");

    /** ILLEGAL DATA ADDRESS **/
    printf("\nTEST ILLEGAL DATA ADDRESS:\n");

    /* The mapping begins at the defined addresses and ends at address +
     * nb_points so these addresses are not valid. */

    rc = modbus_read_bits(ctx, 0, 1, tab_rp_bits);
    printf("* modbus_read_bits (0): ");
    ASSERT_TRUE(rc == -1 && errno == EMBXILADD, "");

    rc = modbus_read_bits(ctx, UT_BITS_ADDRESS, UT_BITS_NB + 1, tab_rp_bits);
    printf("* modbus_read_bits (max): ");
    ASSERT_TRUE(rc == -1 && errno == EMBXILADD, "");

    rc = modbus_read_input_bits(ctx, 0, 1, tab_rp_bits);
    printf("* modbus_read_input_bits (0): ");
    ASSERT_TRUE(rc == -1 && errno == EMBXILADD, "");

    rc = modbus_read_input_bits(
        ctx, UT_INPUT_BITS_ADDRESS, UT_INPUT_BITS_NB + 1, tab_rp_bits);
    printf("* modbus_read_input_bits (max): ");
    ASSERT_TRUE(rc == -1 && errno == EMBXILADD, "");

    rc = modbus_read_registers(ctx, 0, 1, tab_rp_registers);
    printf("* modbus_read_registers (0): ");
    ASSERT_TRUE(rc == -1 && errno == EMBXILADD, "");

    rc = modbus_read_registers(
        ctx, UT_REGISTERS_ADDRESS, UT_REGISTERS_NB_MAX + 1, tab_rp_registers);
    printf("* modbus_read_registers (max): ");
    ASSERT_TRUE(rc == -1 && errno == EMBXILADD, "");

    rc = modbus_read_input_registers(ctx, 0, 1, tab_rp_registers);
    printf("* modbus_read_input_registers (0): ");
    ASSERT_TRUE(rc == -1 && errno == EMBXILADD, "");

    rc = modbus_read_input_registers(
        ctx, UT_INPUT_REGISTERS_ADDRESS, UT_INPUT_REGISTERS_NB + 1, tab_rp_registers);
    printf("* modbus_read_input_registers (max): ");
    ASSERT_TRUE(rc == -1 && errno == EMBXILADD, "");

    rc = modbus_write_bit(ctx, 0, ON);
    printf("* modbus_write_bit (0): ");
    ASSERT_TRUE(rc == -1 && errno == EMBXILADD, "");

    rc = modbus_write_bit(ctx, UT_BITS_ADDRESS + UT_BITS_NB, ON);
    printf("* modbus_write_bit (max): ");
    ASSERT_TRUE(rc == -1 && errno == EMBXILADD, "");

    rc = modbus_write_bits(ctx, 0, 1, tab_rp_bits);
    printf("* modbus_write_bits (0): ");
    ASSERT_TRUE(rc == -1 && errno == EMBXILADD, "");

    rc = modbus_write_bits(ctx, UT_BITS_ADDRESS + UT_BITS_NB, UT_BITS_NB, tab_rp_bits);
    printf("* modbus_write_bits (max): ");
    ASSERT_TRUE(rc == -1 && errno == EMBXILADD, "");

    rc = modbus_write_register(ctx, 0, tab_rp_registers[0]);
    printf("* modbus_write_register (0): ");
    ASSERT_TRUE(rc == -1 && errno == EMBXILADD, "");

    rc = modbus_write_register(
        ctx, UT_REGISTERS_ADDRESS + UT_REGISTERS_NB_MAX, tab_rp_registers[0]);
    printf("* modbus_write_register (max): ");
    ASSERT_TRUE(rc == -1 && errno == EMBXILADD, "");

    rc = modbus_write_registers(ctx, 0, 1, tab_rp_registers);
    printf("* modbus_write_registers (0): ");
    ASSERT_TRUE(rc == -1 && errno == EMBXILADD, "");

    rc = modbus_write_registers(ctx,
                                UT_REGISTERS_ADDRESS + UT_REGISTERS_NB_MAX,
                                UT_REGISTERS_NB,
                                tab_rp_registers);
    printf("* modbus_write_registers (max): ");
    ASSERT_TRUE(rc == -1 && errno == EMBXILADD, "");

    rc = modbus_mask_write_register(ctx, 0, 0xF2, 0x25);
    printf("* modbus_mask_write_registers (0): ");
    ASSERT_TRUE(rc == -1 && errno == EMBXILADD, "");

    rc = modbus_mask_write_register(
        ctx, UT_REGISTERS_ADDRESS + UT_REGISTERS_NB_MAX, 0xF2, 0x25);
    printf("* modbus_mask_write_registers (max): ");
    ASSERT_TRUE(rc == -1 && errno == EMBXILADD, "");

    rc = modbus_write_and_read_registers(
        ctx, 0, 1, tab_rp_registers, 0, 1, tab_rp_registers);
    printf("* modbus_write_and_read_registers (0): ");
    ASSERT_TRUE(rc == -1 && errno == EMBXILADD, "");

    rc = modbus_write_and_read_registers(ctx,
                                         UT_REGISTERS_ADDRESS + UT_REGISTERS_NB_MAX,
                                         UT_REGISTERS_NB,
                                         tab_rp_registers,
                                         UT_REGISTERS_ADDRESS + UT_REGISTERS_NB_MAX,
                                         UT_REGISTERS_NB,
                                         tab_rp_registers);
    printf("* modbus_write_and_read_registers (max): ");
    ASSERT_TRUE(rc == -1 && errno == EMBXILADD, "");

    /** TOO MANY DATA **/
    printf("\nTEST TOO MANY DATA ERROR:\n");

    rc = modbus_read_bits(ctx, UT_BITS_ADDRESS, MODBUS_MAX_READ_BITS + 1, tab_rp_bits);
    printf("* modbus_read_bits: ");
    ASSERT_TRUE(rc == -1 && errno == EMBMDATA, "");

    rc = modbus_read_input_bits(
        ctx, UT_INPUT_BITS_ADDRESS, MODBUS_MAX_READ_BITS + 1, tab_rp_bits);
    printf("* modbus_read_input_bits: ");
    ASSERT_TRUE(rc == -1 && errno == EMBMDATA, "");

    rc = modbus_read_registers(
        ctx, UT_REGISTERS_ADDRESS, MODBUS_MAX_READ_REGISTERS + 1, tab_rp_registers);
    printf("* modbus_read_registers: ");
    ASSERT_TRUE(rc == -1 && errno == EMBMDATA, "");

    rc = modbus_read_input_registers(
        ctx, UT_INPUT_REGISTERS_ADDRESS, MODBUS_MAX_READ_REGISTERS + 1, tab_rp_registers);
    printf("* modbus_read_input_registers: ");
    ASSERT_TRUE(rc == -1 && errno == EMBMDATA, "");

    rc = modbus_write_bits(ctx, UT_BITS_ADDRESS, MODBUS_MAX_WRITE_BITS + 1, tab_rp_bits);
    printf("* modbus_write_bits: ");
    ASSERT_TRUE(rc == -1 && errno == EMBMDATA, "");

    rc = modbus_write_registers(
        ctx, UT_REGISTERS_ADDRESS, MODBUS_MAX_WRITE_REGISTERS + 1, tab_rp_registers);
    printf("* modbus_write_registers: ");
    ASSERT_TRUE(rc == -1 && errno == EMBMDATA, "");

    /** SLAVE ADDRESS **/
    old_slave = modbus_get_slave(ctx);

    printf("\nTEST SLAVE ADDRESS:\n");

    printf("1/2 Not compliant slave address is refused: ");
    rc = modbus_set_slave(ctx, 248);
    ASSERT_TRUE(rc == -1, "Slave address of 248 shouldn't be allowed");

    printf("2/2 Not compliant slave address is allowed: ");
    modbus_enable_quirks(ctx, MODBUS_QUIRK_MAX_SLAVE);
    rc = modbus_set_slave(ctx, 248);
    ASSERT_TRUE(rc == 0, "Not compliant slave address should have been accepted");

    modbus_disable_quirks(ctx, MODBUS_QUIRK_MAX_SLAVE);
    rc = modbus_set_slave(ctx, old_slave);
    ASSERT_TRUE(rc == 0, "Unable to restore slave value")

    /** BAD USE OF REPLY FUNCTION **/
    rc = modbus_write_bit(ctx, UT_BITS_ADDRESS_INVALID_REQUEST_LENGTH, ON);
    printf("* modbus_write_bit (triggers invalid reply): ");
    ASSERT_TRUE(rc == -1 && errno == EMBXILVAL, "");

    rc = modbus_write_register(ctx, UT_REGISTERS_ADDRESS_SPECIAL, 0x42);
    printf("* modbus_write_register (triggers invalid reply): ");
    ASSERT_TRUE(rc == -1 && errno == EMBXILVAL, "");

    /** SLAVE REPLY **/

    printf("\nTEST SLAVE REPLY:\n");
    modbus_set_slave(ctx, INVALID_SERVER_ID);
    rc = modbus_read_registers(
        ctx, UT_REGISTERS_ADDRESS, UT_REGISTERS_NB, tab_rp_registers);
    if (use_backend == RTU) {
        const int RAW_REQ_LENGTH = 6;
        uint8_t raw_req[] = {INVALID_SERVER_ID, 0x03, 0x00, 0x01, 0x01, 0x01};
        /* Too many points */
        uint8_t raw_invalid_req[] = {INVALID_SERVER_ID, 0x03, 0x00, 0x01, 0xFF, 0xFF};
        const int RAW_RSP_LENGTH = 7;
        uint8_t raw_rsp[] = {INVALID_SERVER_ID, 0x03, 0x04, 0, 0, 0, 0};
        uint8_t rsp[MODBUS_RTU_MAX_ADU_LENGTH];

        /* No response in RTU mode */
        printf("1-A/3 No response from slave %d: ", INVALID_SERVER_ID);
        ASSERT_TRUE(rc == -1 && errno == ETIMEDOUT, "");

        /* The slave raises a timeout on a confirmation to ignore because if an
         * indication for another slave is received, a confirmation must follow */

        /* Send a pair of indication/confirmation to the slave with a different
         * slave ID to simulate a communication on a RS485 bus. At first, the
         * slave will see the indication message then the confirmation, and it must
         * ignore both. */
        modbus_send_raw_request(ctx, raw_req, RAW_REQ_LENGTH * sizeof(uint8_t));
        modbus_send_raw_request(ctx, raw_rsp, RAW_RSP_LENGTH * sizeof(uint8_t));
        rc = modbus_receive_confirmation(ctx, rsp);

        printf("1-B/3 No response from slave %d on indication/confirmation messages: ",
               INVALID_SERVER_ID);
        ASSERT_TRUE(rc == -1 && errno == ETIMEDOUT, "");

        /* Send an INVALID request for another slave */
        modbus_send_raw_request(ctx, raw_invalid_req, RAW_REQ_LENGTH * sizeof(uint8_t));
        rc = modbus_receive_confirmation(ctx, rsp);

        printf("1-C/3 No response from slave %d with invalid request: ",
               INVALID_SERVER_ID);
        ASSERT_TRUE(rc == -1 && errno == ETIMEDOUT, "");

        rc = modbus_set_slave(ctx, MODBUS_BROADCAST_ADDRESS);
        ASSERT_TRUE(rc == 0, "Invalid broadcast address");

        rc = modbus_read_registers(
            ctx, UT_REGISTERS_ADDRESS, UT_REGISTERS_NB, tab_rp_registers);
        printf("2/3 No reply after a broadcast query: ");
        ASSERT_TRUE(rc == -1 && errno == ETIMEDOUT, "");
    } else {
        /* Response in TCP mode */
        printf("1/3 Response from slave %d: ", INVALID_SERVER_ID);
        ASSERT_TRUE(rc == UT_REGISTERS_NB, "");

        rc = modbus_set_slave(ctx, MODBUS_BROADCAST_ADDRESS);
        ASSERT_TRUE(rc == 0, "Invalid broacast address");

        rc = modbus_read_registers(
            ctx, UT_REGISTERS_ADDRESS, UT_REGISTERS_NB, tab_rp_registers);
        printf("2/3 Reply after a query with unit id == 0: ");
        ASSERT_TRUE(rc == UT_REGISTERS_NB, "");
    }

    /* Restore slave */
    modbus_set_slave(ctx, old_slave);

    printf("3/3 Response with an invalid TID or slave: ");
    rc = modbus_read_registers(
        ctx, UT_REGISTERS_ADDRESS_INVALID_TID_OR_SLAVE, 1, tab_rp_registers);
    ASSERT_TRUE(rc == -1, "");

    printf("1/2 Report slave ID truncated: \n");
    /* Set a marker to ensure limit is respected */
    tab_rp_bits[NB_REPORT_SLAVE_ID - 1] = 42;
    rc = modbus_report_slave_id(ctx, NB_REPORT_SLAVE_ID - 1, tab_rp_bits);
    /* Return the size required (response size) but respects the defined limit */
    ASSERT_TRUE(rc == NB_REPORT_SLAVE_ID && tab_rp_bits[NB_REPORT_SLAVE_ID - 1] == 42,
                "Return is rc %d (%d) and marker is %d (42)",
                rc,
                NB_REPORT_SLAVE_ID,
                tab_rp_bits[NB_REPORT_SLAVE_ID - 1]);

    printf("2/2 Report slave ID: \n");
    /* tab_rp_bits is used to store bytes */
    rc = modbus_report_slave_id(ctx, NB_REPORT_SLAVE_ID, tab_rp_bits);
    ASSERT_TRUE(rc == NB_REPORT_SLAVE_ID, "");

    /* Slave ID is an arbitrary number for libmodbus */
    ASSERT_TRUE(rc > 0, "");

    /* Run status indicator is ON */
    ASSERT_TRUE(rc > 1 && tab_rp_bits[1] == 0xFF, "");

    /* Print additional data as string */
    if (rc > 2) {
        printf("Additional data: ");
        for (i = 2; i < rc; i++) {
            printf("%c", tab_rp_bits[i]);
        }
        printf("\n");
    }

    /* Save original timeout */
    modbus_get_response_timeout(ctx, &old_response_to_sec, &old_response_to_usec);
    modbus_get_byte_timeout(ctx, &old_byte_to_sec, &old_byte_to_usec);

    rc = modbus_set_response_timeout(ctx, 0, 0);
    printf("1/8 Invalid response timeout (zero): ");
    ASSERT_TRUE(rc == -1 && errno == EINVAL, "");

    rc = modbus_set_response_timeout(ctx, 0, 1000000);
    printf("2/8 Invalid response timeout (too large us): ");
    ASSERT_TRUE(rc == -1 && errno == EINVAL, "");

    rc = modbus_set_byte_timeout(ctx, 0, 1000000);
    printf("3/8 Invalid byte timeout (too large us): ");
    ASSERT_TRUE(rc == -1 && errno == EINVAL, "");

    modbus_set_response_timeout(ctx, 0, 1);
    rc = modbus_read_registers(
        ctx, UT_REGISTERS_ADDRESS, UT_REGISTERS_NB, tab_rp_registers);
    printf("4/8 1us response timeout: ");
    if (rc == -1 && errno == ETIMEDOUT) {
        printf("OK\n");
    } else {
        printf("FAILED (can fail on some platforms)\n");
    }

    /* A wait and flush operation is done by the error recovery code of
     * libmodbus but after a sleep of current response timeout
     * so 0 can be too short!
     */
    usleep(old_response_to_sec * 1000000 + old_response_to_usec);
    modbus_flush(ctx);

    /* Trigger a special behaviour on server to wait for 0.5 second before
     * replying whereas allowed timeout is 0.2 second */
    modbus_set_response_timeout(ctx, 0, 200000);
    rc = modbus_read_registers(
        ctx, UT_REGISTERS_ADDRESS_SLEEP_500_MS, 1, tab_rp_registers);
    printf("5/8 Too short response timeout (0.2s < 0.5s): ");
    ASSERT_TRUE(rc == -1 && errno == ETIMEDOUT, "");

    /* Wait for reply (0.2 + 0.4 > 0.5 s) and flush before continue */
    usleep(400000);
    modbus_flush(ctx);

    modbus_set_response_timeout(ctx, 0, 600000);
    rc = modbus_read_registers(
        ctx, UT_REGISTERS_ADDRESS_SLEEP_500_MS, 1, tab_rp_registers);
    printf("6/8 Adequate response timeout (0.6s > 0.5s): ");
    ASSERT_TRUE(rc == 1, "");

    /* Disable the byte timeout.
       The full response must be available in the 600ms interval */
    modbus_set_byte_timeout(ctx, 0, 0);
    rc = modbus_read_registers(
        ctx, UT_REGISTERS_ADDRESS_SLEEP_500_MS, 1, tab_rp_registers);
    printf("7/8 Disable byte timeout: ");
    ASSERT_TRUE(rc == 1, "");

    // Invalid in TCP or RTU mode...
    modbus_t *invalid_ctx = modbus_new_tcp("1.2.3.4", 1502);
    modbus_set_response_timeout(ctx, 0, 1);
    rc = modbus_connect(invalid_ctx);
    printf("8/8 Connection timeout: ");
    ASSERT_TRUE(rc == -1 && errno == ETIMEDOUT, "");
    modbus_free(invalid_ctx);

    /* Restore original response timeout */
    modbus_set_response_timeout(ctx, old_response_to_sec, old_response_to_usec);

    if (use_backend == TCP) {
        /* The test server is only able to test byte timeouts with the TCP
         * backend */

        /* Timeout of 3ms between bytes */
        modbus_set_byte_timeout(ctx, 0, 3000);
        rc = modbus_read_registers(
            ctx, UT_REGISTERS_ADDRESS_BYTE_SLEEP_5_MS, 1, tab_rp_registers);
        printf("1/2 Too small byte timeout (3ms < 5ms): ");
        ASSERT_TRUE(rc == -1 && errno == ETIMEDOUT, "");

        /* Wait remaining bytes before flushing */
        usleep(11 * 5000);
        modbus_flush(ctx);

        /* Timeout of 7ms between bytes */
        modbus_set_byte_timeout(ctx, 0, 7000);
        rc = modbus_read_registers(
            ctx, UT_REGISTERS_ADDRESS_BYTE_SLEEP_5_MS, 1, tab_rp_registers);
        printf("2/2 Adapted byte timeout (7ms > 5ms): ");
        ASSERT_TRUE(rc == 1, "");
    }

    /* Restore original byte timeout */
    modbus_set_byte_timeout(ctx, old_byte_to_sec, old_byte_to_usec);

    /** BAD RESPONSE **/
    printf("\nTEST BAD RESPONSE ERROR:\n");

    /* Allocate only the required space */
    tab_rp_registers_bad =
        (uint16_t *) malloc(UT_REGISTERS_NB_SPECIAL * sizeof(uint16_t));

    rc = modbus_read_registers(
        ctx, UT_REGISTERS_ADDRESS, UT_REGISTERS_NB_SPECIAL, tab_rp_registers_bad);
    printf("* modbus_read_registers: ");
    ASSERT_TRUE(rc == -1 && errno == EMBBADDATA, "");
    free(tab_rp_registers_bad);

    /** MANUAL EXCEPTION **/
    printf("\nTEST MANUAL EXCEPTION:\n");
    rc = modbus_read_registers(
        ctx, UT_REGISTERS_ADDRESS_SPECIAL, UT_REGISTERS_NB, tab_rp_registers);

    printf("* modbus_read_registers at special address: ");
    ASSERT_TRUE(rc == -1 && errno == EMBXSBUSY, "");

    /** Run a few tests to challenge the server code **/
    if (test_server(ctx, use_backend) == -1) {
        goto close;
    }

    modbus_close(ctx);
    modbus_free(ctx);
    ctx = NULL;

    /* Test init functions */
    printf("\nTEST INVALID INITIALIZATION:\n");
    ctx = modbus_new_rtu(NULL, 1, 'A', 0, 0, 0);
    ASSERT_TRUE(ctx == NULL && errno == EINVAL, "");

    ctx = modbus_new_rtu("/dev/dummy", 0, 'A', 0, 0, 0);
    ASSERT_TRUE(ctx == NULL && errno == EINVAL, "");

    printf("\nALL TESTS PASS WITH SUCCESS.\n");
    success = TRUE;

close:
    /* Free the memory */
    free(tab_rp_bits);
    free(tab_rp_registers);

    /* Close the connection */
    modbus_close(ctx);
    modbus_free(ctx);

    return (success) ? 0 : -1;
}

/* Send crafted requests to test server resilience
   and ensure proper exceptions are returned. */
int test_server(modbus_t *ctx, int use_backend)
{
    int rc;
    int i;
    /* Read requests */
    const int READ_RAW_REQ_LEN = 6;
    const int slave = (use_backend == RTU) ? SERVER_ID : MODBUS_TCP_SLAVE;
    uint8_t read_raw_req[] = {slave,
                              /* function, address, 5 values */
                              MODBUS_FC_READ_HOLDING_REGISTERS,
                              UT_REGISTERS_ADDRESS >> 8,
                              UT_REGISTERS_ADDRESS & 0xFF,
                              0x0,
                              0x05};
    /* Write and read registers request */
    const int RW_RAW_REQ_LEN = 13;
    uint8_t rw_raw_req[] = {slave,
                            /* function, addr to read, nb to read */
                            MODBUS_FC_WRITE_AND_READ_REGISTERS,
                            /* Read */
                            UT_REGISTERS_ADDRESS >> 8,
                            UT_REGISTERS_ADDRESS & 0xFF,
                            (MODBUS_MAX_WR_READ_REGISTERS + 1) >> 8,
                            (MODBUS_MAX_WR_READ_REGISTERS + 1) & 0xFF,
                            /* Write */
                            0,
                            0,
                            0,
                            1,
                            /* Write byte count */
                            1 * 2,
                            /* One data to write... */
                            0x12,
                            0x34};
    const int WRITE_RAW_REQ_LEN = 13;
    uint8_t write_raw_req[] = {slave,
                               /* function will be set in the loop */
                               MODBUS_FC_WRITE_MULTIPLE_REGISTERS,
                               /* Address */
                               UT_REGISTERS_ADDRESS >> 8,
                               UT_REGISTERS_ADDRESS & 0xFF,
                               /* 3 values, 6 bytes */
                               0x00,
                               0x03,
                               0x06,
                               /* Dummy data to write */
                               0x02,
                               0x2B,
                               0x00,
                               0x01,
                               0x00,
                               0x64};
    const int INVALID_FC = 0x42;
    const int INVALID_FC_REQ_LEN = 6;
    uint8_t invalid_fc_raw_req[] = {slave, 0x42, 0x00, 0x00, 0x00, 0x00};

    int req_length;
    uint8_t rsp[MODBUS_TCP_MAX_ADU_LENGTH];
    int tab_read_function[] = {MODBUS_FC_READ_COILS,
                               MODBUS_FC_READ_DISCRETE_INPUTS,
                               MODBUS_FC_READ_HOLDING_REGISTERS,
                               MODBUS_FC_READ_INPUT_REGISTERS};
    int tab_read_nb_max[] = {MODBUS_MAX_READ_BITS + 1,
                             MODBUS_MAX_READ_BITS + 1,
                             MODBUS_MAX_READ_REGISTERS + 1,
                             MODBUS_MAX_READ_REGISTERS + 1};
    int backend_length;
    int backend_offset;

    if (use_backend == RTU) {
        backend_length = 3;
        backend_offset = 1;
    } else {
        backend_length = 7;
        backend_offset = 7;
    }

    printf("\nTEST RAW REQUESTS:\n");

    uint32_t old_response_to_sec;
    uint32_t old_response_to_usec;

    /* This requests can generate flushes server side so we need a higher
     * response timeout than the server. The server uses the defined response
     * timeout to sleep before flushing.
     * The old timeouts are restored at the end.
     */
    modbus_get_response_timeout(ctx, &old_response_to_sec, &old_response_to_usec);
    modbus_set_response_timeout(ctx, 0, 600000);

    int old_s = modbus_get_socket(ctx);
    modbus_set_socket(ctx, -1);
    rc = modbus_receive(ctx, rsp);
    modbus_set_socket(ctx, old_s);
    printf("* modbus_receive with invalid socket: ");
    ASSERT_TRUE(rc == -1, "FAILED (%d)\n", rc);

    req_length = modbus_send_raw_request(ctx, read_raw_req, READ_RAW_REQ_LEN);
    printf("* modbus_send_raw_request: ");
    ASSERT_TRUE(req_length == (backend_length + 5), "FAILED (%d)\n", req_length);

    printf("* modbus_receive_confirmation: ");
    rc = modbus_receive_confirmation(ctx, rsp);
    ASSERT_TRUE(rc == (backend_length + 12), "FAILED (%d)\n", rc);

    /* Try to read more values than a response could hold for all data
       types. */
    for (i = 0; i < 4; i++) {
        rc = send_crafted_request(ctx,
                                  tab_read_function[i],
                                  read_raw_req,
                                  READ_RAW_REQ_LEN,
                                  tab_read_nb_max[i],
                                  0,
                                  backend_length,
                                  backend_offset);
        if (rc == -1)
            goto close;
    }

    rc = send_crafted_request(ctx,
                              MODBUS_FC_WRITE_AND_READ_REGISTERS,
                              rw_raw_req,
                              RW_RAW_REQ_LEN,
                              MODBUS_MAX_WR_READ_REGISTERS + 1,
                              0,
                              backend_length,
                              backend_offset);
    if (rc == -1)
        goto close;

    rc = send_crafted_request(ctx,
                              MODBUS_FC_WRITE_MULTIPLE_REGISTERS,
                              write_raw_req,
                              WRITE_RAW_REQ_LEN,
                              MODBUS_MAX_WRITE_REGISTERS + 1,
                              6,
                              backend_length,
                              backend_offset);
    if (rc == -1)
        goto close;

    rc = send_crafted_request(ctx,
                              MODBUS_FC_WRITE_MULTIPLE_COILS,
                              write_raw_req,
                              WRITE_RAW_REQ_LEN,
                              MODBUS_MAX_WRITE_BITS + 1,
                              6,
                              backend_length,
                              backend_offset);
    if (rc == -1)
        goto close;

    /* Modbus write multiple registers with large number of values but a set a
       small number of bytes in requests (not nb * 2 as usual). */
    rc = send_crafted_request(ctx,
                              MODBUS_FC_WRITE_MULTIPLE_REGISTERS,
                              write_raw_req,
                              WRITE_RAW_REQ_LEN,
                              MODBUS_MAX_WRITE_REGISTERS,
                              6,
                              backend_length,
                              backend_offset);
    if (rc == -1)
        goto close;

    rc = send_crafted_request(ctx,
                              MODBUS_FC_WRITE_MULTIPLE_COILS,
                              write_raw_req,
                              WRITE_RAW_REQ_LEN,
                              MODBUS_MAX_WRITE_BITS,
                              6,
                              backend_length,
                              backend_offset);
    if (rc == -1)
        goto close;

    /* Test invalid function code */
    modbus_send_raw_request(
        ctx, invalid_fc_raw_req, INVALID_FC_REQ_LEN * sizeof(uint8_t));
    rc = modbus_receive_confirmation(ctx, rsp);
    printf("Return an exception on unknown function code: ");
    ASSERT_TRUE(rc == (backend_length + EXCEPTION_RC) &&
                    rsp[backend_offset] == (0x80 + INVALID_FC),
                "")

    modbus_set_response_timeout(ctx, old_response_to_sec, old_response_to_usec);
    return 0;
close:
    modbus_set_response_timeout(ctx, old_response_to_sec, old_response_to_usec);
    return -1;
}

int send_crafted_request(modbus_t *ctx,
                         int function,
                         uint8_t *req,
                         int req_len,
                         uint16_t max_value,
                         uint16_t bytes,
                         int backend_length,
                         int backend_offset)
{
    uint8_t rsp[MODBUS_TCP_MAX_ADU_LENGTH];
    int j;

    for (j = 0; j < 2; j++) {
        int rc;

        req[1] = function;
        if (j == 0) {
            /* Try to read or write zero values on first iteration */
            req[4] = 0x00;
            req[5] = 0x00;
            if (bytes) {
                /* Write query */
                req[6] = 0x00;
            }
        } else {
            /* Try to read or write max values + 1 on second iteration */
            req[4] = (max_value >> 8) & 0xFF;
            req[5] = max_value & 0xFF;
            if (bytes) {
                /* Write query (nb values * 2 to convert in bytes for registers) */
                req[6] = bytes;
            }
        }

        modbus_send_raw_request(ctx, req, req_len * sizeof(uint8_t));
        if (j == 0) {
            printf(
                "* try function 0x%X: %s 0 values: ", function, bytes ? "write" : "read");
        } else {
            printf("* try function 0x%X: %s %d values: ",
                   function,
                   bytes ? "write" : "read",
                   max_value);
        }
        rc = modbus_receive_confirmation(ctx, rsp);
        ASSERT_TRUE(rc == (backend_length + EXCEPTION_RC) &&
                        rsp[backend_offset] == (0x80 + function) &&
                        rsp[backend_offset + 1] == MODBUS_EXCEPTION_ILLEGAL_DATA_VALUE,
                    "");
    }
    return 0;
close:
    return -1;
}
