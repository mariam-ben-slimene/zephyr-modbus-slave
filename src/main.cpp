#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <cstdint>

LOG_MODULE_REGISTER(modbus_slave, LOG_LEVEL_DBG);

// ── Modbus Configuration ───────────────────────────────────────
#define SLAVE_ADDRESS     0x01
#define NUM_REGISTERS     10

// ── Register Map ──────────────────────────────────────────────
// This is the data our slave exposes to the master
static uint16_t holding_registers[NUM_REGISTERS] = {
    250,  // 0x00 - Temperature (25.0 °C, scaled x10)
    600,  // 0x01 - Humidity    (60.0 %,  scaled x10)
    1,    // 0x02 - Status      (1 = OK)
    0,    // 0x03 - Control     (writable)
    0,    // 0x04 - Reserved
    0,    // 0x05 - Reserved
    0,    // 0x06 - Reserved
    0,    // 0x07 - Reserved
    0,    // 0x08 - Reserved
    0,    // 0x09 - Reserved
};

// ── CRC16 ─────────────────────────────────────────────────────
static uint16_t crc16(const uint8_t *buf, uint16_t len)
{
    uint16_t crc = 0xFFFF;
    for (uint16_t i = 0; i < len; i++) {
        crc ^= buf[i];
        for (int j = 0; j < 8; j++) {
            if (crc & 0x0001) crc = (crc >> 1) ^ 0xA001;
            else              crc >>= 1;
        }
    }
    return crc;
}

// ── Shared buffer between master and slave ────────────────────
#define BUF_SIZE 64
static uint8_t request_buf[BUF_SIZE];
static uint8_t response_buf[BUF_SIZE];
static uint16_t request_len = 0;
static uint16_t response_len = 0;

// Semaphores for synchronization
K_SEM_DEFINE(request_ready, 0, 1);
K_SEM_DEFINE(response_ready, 0, 1);

// ── Modbus Slave: process incoming request ────────────────────
static void process_request()
{
    if (request_len < 4) {
        LOG_WRN("Slave: frame too short");
        return;
    }

    // Validate CRC
    uint16_t received_crc = (request_buf[request_len-1] << 8) |
                             request_buf[request_len-2];
    uint16_t computed_crc = crc16(request_buf, request_len - 2);
    if (received_crc != computed_crc) {
        LOG_WRN("Slave: CRC mismatch (got 0x%04X, expected 0x%04X)",
                received_crc, computed_crc);
        return;
    }

    // Check slave address
    if (request_buf[0] != SLAVE_ADDRESS) {
        LOG_DBG("Slave: ignoring request for address 0x%02X", request_buf[0]);
        return;
    }

    uint8_t function_code = request_buf[1];

    // ── FC03: Read Holding Registers ──
    if (function_code == 0x03) {
        uint16_t start_reg = (request_buf[2] << 8) | request_buf[3];
        uint16_t num_regs  = (request_buf[4] << 8) | request_buf[5];

        LOG_INF("Slave: FC03 Read — start=%d count=%d", start_reg, num_regs);

        if (start_reg + num_regs > NUM_REGISTERS) {
            LOG_WRN("Slave: register out of range");
            return;
        }

        // Build response
        response_buf[0] = SLAVE_ADDRESS;
        response_buf[1] = 0x03;
        response_buf[2] = num_regs * 2; // byte count
        for (uint16_t i = 0; i < num_regs; i++) {
            response_buf[3 + i*2]     = holding_registers[start_reg + i] >> 8;
            response_buf[3 + i*2 + 1] = holding_registers[start_reg + i] & 0xFF;
        }
        response_len = 3 + num_regs * 2;
        uint16_t crc = crc16(response_buf, response_len);
        response_buf[response_len++] = crc & 0xFF;
        response_buf[response_len++] = crc >> 8;
    }

    // ── FC06: Write Single Register ──
    else if (function_code == 0x06) {
        uint16_t reg_addr = (request_buf[2] << 8) | request_buf[3];
        uint16_t value    = (request_buf[4] << 8) | request_buf[5];

        LOG_INF("Slave: FC06 Write — reg=%d value=%d", reg_addr, value);

        if (reg_addr >= NUM_REGISTERS) {
            LOG_WRN("Slave: register out of range");
            return;
        }

        holding_registers[reg_addr] = value;

        // Echo back the request as response (Modbus spec)
        memcpy(response_buf, request_buf, request_len);
        response_len = request_len;
    }

    else {
        LOG_WRN("Slave: unsupported function code 0x%02X", function_code);
    }
}

// ── Helper: build a Modbus RTU frame ─────────────────────────
static uint16_t build_request(uint8_t *buf, uint8_t addr,
                               uint8_t fc, uint16_t reg, uint16_t val)
{
    buf[0] = addr;
    buf[1] = fc;
    buf[2] = reg >> 8;
    buf[3] = reg & 0xFF;
    buf[4] = val >> 8;
    buf[5] = val & 0xFF;
    uint16_t crc = crc16(buf, 6);
    buf[6] = crc & 0xFF;
    buf[7] = crc >> 8;
    return 8;
}

// ── Master thread ─────────────────────────────────────────────
void master_thread(void *a, void *b, void *c)
{
    k_sleep(K_MSEC(500)); // let slave start first

    while (1) {
        // 1. Read registers 0 and 1 (temperature + humidity)
        LOG_INF("Master: sending FC03 - read temp & humidity");
        request_len = build_request(request_buf, SLAVE_ADDRESS, 0x03, 0x00, 0x02);
        k_sem_give(&request_ready);
        k_sem_take(&response_ready, K_MSEC(1000));

        LOG_INF("Master: response — Temp=%d.%d C  Humidity=%d.%d %%",
                holding_registers[0] / 10, holding_registers[0] % 10,
                holding_registers[1] / 10, holding_registers[1] % 10);

        k_sleep(K_MSEC(1000));

        // 2. Write to control register
        LOG_INF("Master: sending FC06 - write control register");
        request_len = build_request(request_buf, SLAVE_ADDRESS, 0x06, 0x03, 0x0001);
        k_sem_give(&request_ready);
        k_sem_take(&response_ready, K_MSEC(1000));

        LOG_INF("Master: control register written, value=%d", holding_registers[3]);

        k_sleep(K_MSEC(1000));
    }
}

// ── Slave thread ──────────────────────────────────────────────
void slave_thread(void *a, void *b, void *c)
{
    LOG_INF("Slave: ready, address=0x%02X", SLAVE_ADDRESS);

    while (1) {
        k_sem_take(&request_ready, K_FOREVER);
        process_request();
        k_sem_give(&response_ready);
    }
}

// ── Thread definitions ────────────────────────────────────────
#define STACK_SIZE 2048
K_THREAD_DEFINE(slave_tid,  STACK_SIZE, slave_thread,  NULL, NULL, NULL, 5, 0, 0);
K_THREAD_DEFINE(master_tid, STACK_SIZE, master_thread, NULL, NULL, NULL, 6, 0, 0);
