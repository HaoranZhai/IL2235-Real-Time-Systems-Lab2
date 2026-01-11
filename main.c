#include <stdio.h>
#include <inttypes.h> /* For PRIu64 */
#include <string.h>
#include <stdint.h>
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "semphr.h"
#include "bsp.h"
#include "dijkstra.h"
#include "input.h"
#include "pico/rand.h"

/* Turn off compiler optimizations for this file. */
#pragma GCC optimize ("O0")

/* Bitmasks for the core affinities */
#define MASK_CORE_0 0x01
#define MASK_CORE_1 0x02

#define PSRAM_CACHED   ((uint8_t*)0x11000000)
#define PSRAM_UNCACHED ((uint8_t*)0x15000000)

/* === Optional: performance counters for Lab2B Q2 === */
/* 做第二小问时，把这行打开 */
//#define ENABLE_PERF_COUNTERS

#ifdef ENABLE_PERF_COUNTERS
//#define BUSCTRL_BASE   0x40068000u
//#define XIP_CTRL_BASE  0x400C8000u
#define REG32(addr) (*(volatile uint32_t *)(addr))

#define BUS_PERFCTR_EN REG32(BUSCTRL_BASE + 0x08u)
#define BUS_PERFCTR0   REG32(BUSCTRL_BASE + 0x0Cu)
#define BUS_PERFSEL0   REG32(BUSCTRL_BASE + 0x10u)

#define XIP_CTR_HIT    REG32(XIP_CTRL_BASE + 0x0Cu)
#define XIP_CTR_ACC    REG32(XIP_CTRL_BASE + 0x10u)

/* From datasheet table 1325: XIP_MAIN0_ACCESS_CONTESTED */
#define EVENT_XIP_MAIN0_ACCESS_CONTESTED 0x3E
#endif

/**
 * @brief Type to define a memory region 
 */
typedef struct {
    uint8_t* start;             /* Start of the memory area we can read from. */
    uint32_t size;              /* Size of the memory area we can read from. */
    uint32_t accessWidth;       /* Number of bytes we access consecutively. */
    uint8_t* localBuffer;       /* Pointer to the local buffer. */
} memRegion_t;

/**
 * @brief Type to define the input for the Dijkstra task
 */
typedef struct {
    portTickType period;
    uint8_t* dstMemory;
} dijkstraTskIn_t;

/**
 * @brief Dijkstra task.
 */
void dijkstra_task(void *args);

/**
 * @brief Task creates a large number of reads on a specific memory region. 
 */
void reader_task(void *args);

/**
 * @brief Task creates a large number of writes on a specific memory region. 
 */
void writer_task(void *args);

/**
 * @brief Task creates a large number of reads and writes to a specific memory region. 
 */
void readerWriter_task(void *args);

/*************************************************************/

TaskHandle_t    dijkstraTsk;    /* Handle for the Dijkstra task. */
TaskHandle_t    attackerTask;   /* Handle for the attacker task. */

/* === attacker 访问配置：连续访问字节数和本地缓冲区 === */
#define ACCESS_WIDTH_B 4

uint8_t __not_in_flash("attackLocalBuffer")attackLocalBuffer[ACCESS_WIDTH_B];

memRegion_t psram_cached = {
    .start = PSRAM_CACHED + sizeof(inputData_t),
    .size = 0x800000 - sizeof(inputData_t),
    .accessWidth = ACCESS_WIDTH_B,
    .localBuffer = attackLocalBuffer
};

memRegion_t psram_uncached = {
    .start = PSRAM_UNCACHED + sizeof(inputData_t),
    .size = 0x800000 - sizeof(inputData_t),
    .accessWidth = ACCESS_WIDTH_B,
    .localBuffer = attackLocalBuffer
};

/*************************************************************/
/* CONFIGURATION                                             */
/*************************************************************/

/**
 * Argument for the Dijkstra task. Configure the location for the input data here!
 * 
 * dstMemory can have the following values:
 * - PSRAM_UNCACHED          -> Moves data to PSRAM and bypasses the XIP cache when accessing
 * - PSRAM_CACHED            -> Moves data to PSRAM and accesses it via the XIP cache
 * - &dijkstra_inputData     -> SRAM
 */
dijkstraTskIn_t tskConfig = {
    .period   = 100,
    .dstMemory = PSRAM_UNCACHED    /* 当前配置：PSRAM_CACHED，无 attacker */
};

/**
 * @brief This decides which memory area the attacker task tries to target.
 * - psram_uncached
 * - psram_cached
 */
#define ATTACKER_MEMORY psram_uncached

/**
 * @brief Enable this to include the attacker task
 */
#define USE_ATTACKER_TASK

/**
 * @brief Enable one of the below to decide what type of attacker task to run (only if USE_ATTACKER_TASK is defined).
 */
#define WRITE_ATTACK
//#define READ_ATTACK
//#define MIXED_ATTACK

/*************************************************************/

/**
 * @brief Main function.
 */
int main(void)
{
    BSP_Init(); /* Initialize all components on the lab-kit. */
    
    /* Create the tasks. */
    xTaskCreate(dijkstra_task, "Dijkstra Task", 512, (void*) &tskConfig, 2, &dijkstraTsk);
#ifdef USE_ATTACKER_TASK
    #ifdef WRITE_ATTACK
    xTaskCreate(writer_task, "Attacker Task", 512, (void*) &ATTACKER_MEMORY, 2, &attackerTask);
    #endif
    #ifdef READ_ATTACK
    xTaskCreate(reader_task, "Attacker Task", 512, (void*) &ATTACKER_MEMORY, 2, &attackerTask);
    #endif
    #ifdef MIXED_ATTACK
    xTaskCreate(readerWriter_task, "Attacker Task", 512, (void*) &ATTACKER_MEMORY, 2, &attackerTask);
    #endif
#endif
    /* Set the core affinity for all tasks */
    vTaskCoreAffinitySet(dijkstraTsk, MASK_CORE_0 ); 
#ifdef USE_ATTACKER_TASK
    vTaskCoreAffinitySet(attackerTask, MASK_CORE_1 ); 
#endif

    vTaskStartScheduler();  /* Start the scheduler. Preemption is turned off! */
    
    while (true) { 
        sleep_ms(1000); /* Should not reach here... */
    }
}

/*-----------------------------------------------------------*/

void dijkstra_task(void *args) {
    TickType_t xLastWakeTime = 0;
    dijkstraTskIn_t* config = (dijkstraTskIn_t*) args;

    const TickType_t xPeriod = config->period;   /* Get period (in ticks) from argument. */
    uint64_t start = 0;
    uint64_t stop = 0;

    if (config->dstMemory != (uint8_t*)dijkstra_inputData) {
        memcpy(config->dstMemory, dijkstra_inputData, sizeof(inputData_t));
    }

    for (;;) {
        
        BSP_ToggleLED(LED_GREEN);           /* Toggle the LED to have visual confirmation of program execution. */
        dijkstra_init((inputData_t*)config->dstMemory);     /* Init Dijkstra algorithm. */

#ifdef ENABLE_PERF_COUNTERS
        /* 1. 清零并配置 performance counters */
        BUS_PERFCTR_EN = 0u;                /* 配置时先关掉计数 */
        BUS_PERFCTR0   = 0u;                /* 写任意值清零 */
        XIP_CTR_HIT    = 0u;
        XIP_CTR_ACC    = 0u;

        BUS_PERFSEL0   = EVENT_XIP_MAIN0_ACCESS_CONTESTED;
        BUS_PERFCTR_EN = 1u;                /* 开始计数 */
#endif

        start = time_us_64();
        dijkstra_main();                    /* Finds 20 shortest paths between nodes. */
        stop = time_us_64();

#ifdef ENABLE_PERF_COUNTERS
        BUS_PERFCTR_EN = 0u;                /* 停止计数 */

        uint32_t contested = BUS_PERFCTR0;
        uint32_t acc       = XIP_CTR_ACC;
        uint32_t hit       = XIP_CTR_HIT;
        uint32_t miss      = acc - hit;
#endif

        if (dijkstra_return() == -1) {      /* Check if the result is correct. */
           BSP_SetLED(LED_RED, true); 
        }

#ifdef ENABLE_PERF_COUNTERS
        /* 输出：time_us, bus_contested, cache_miss */
        printf("%" PRIu64 ",%u,%u\r\n", stop - start, contested, miss);
#else
        /* 只输出执行时间（第一小问用） */
        printf("%" PRIu64 "\r\n", stop - start);
#endif

        vTaskDelayUntil(&xLastWakeTime, xPeriod);   /* Wait for the next release. */
    }
}

/*-----------------------------------------------------------*/

void reader_task(void *args) {

    memRegion_t* memRegion = (memRegion_t*) args;
    uint8_t* address;

    for (;;) {
        /* 在区域内随机选择一个起始地址，保证后面还能访问 accessWidth 个字节 */
        uint32_t offset = get_rand_32() % (memRegion->size - memRegion->accessWidth + 1u);
        address = memRegion->start + offset;

        for (uint32_t i = 0; i < memRegion->accessWidth; i++) {
            memRegion->localBuffer[i] = *address;   /* Read from memory. */
            address++;
        }
    }
}

/*-----------------------------------------------------------*/  

void writer_task(void *args) {

    memRegion_t* memRegion = (memRegion_t*) args;
    uint8_t* address;

    for (;;) {
        uint32_t offset = get_rand_32() % (memRegion->size - memRegion->accessWidth + 1u);
        address = memRegion->start + offset;

        for (uint32_t i = 0; i < memRegion->accessWidth; i++) {
            *address = memRegion->localBuffer[i];   /* Write to memory. */
            address++;
        }
    }
}

/*-----------------------------------------------------------*/  

void readerWriter_task(void *args) {
    memRegion_t* memRegion = (memRegion_t*) args;
    uint8_t* address;
    uint8_t readWriteDecision;

    for (;;) {
        uint32_t offset = get_rand_32() % (memRegion->size - memRegion->accessWidth + 1u);
        address = memRegion->start + offset;
        
        /* 随机决定：0 = write, 1 = read */
        readWriteDecision = (uint8_t)(get_rand_32() & 0x0001u);

        for (uint32_t i = 0; i < memRegion->accessWidth; i++) {
            if (readWriteDecision == 0x01u) {
                memRegion->localBuffer[i] = *address;   /* Read from memory. */
            } else {
                *address = memRegion->localBuffer[i];   /* Write to memory. */
            }
            address++;
        }
    }
}
