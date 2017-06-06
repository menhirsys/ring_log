#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include "ring_log.h"

static SemaphoreHandle_t mutex = NULL;

void ring_log_arch_abort(void) {
    vTaskDelete(NULL);
}

void ring_log_arch_init(void) {
    RING_LOG_EXPECT(mutex, NULL);
    mutex = xSemaphoreCreateMutex();
    RING_LOG_EXPECT_NOT(mutex, NULL);
}

void ring_log_arch_take_mutex(void) {
    RING_LOG_EXPECT_NOT(mutex, NULL);
    xSemaphoreTake(mutex, portMAX_DELAY);
}

void ring_log_arch_free_mutex(void) {
    RING_LOG_EXPECT_NOT(mutex, NULL);
    xSemaphoreGive(mutex);
}
