/*
 * AX5043 AIS receiver main entry point
 *
 * This file contains the main function to initialize and test
 * the AX5043 chip for AIS signal reception.
 */

#include <stdio.h>
#include <esp_log.h>
#include <esp_err.h>

#include "sdkconfig.h"
#include "esp_task_wdt.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "ax5043_ais.h"

#define AX5043_WATCHDOG_TIMEOUT_MS    10000U    // 10 секунд для esp_task_wdt

static const char *TAG = "MAIN";

esp_task_wdt_config_t wdt_config = {
    .timeout_ms = AX5043_WATCHDOG_TIMEOUT_MS,
    .idle_core_mask = (1 << CONFIG_SOC_CPU_CORES_NUM) - 1,    // Bitmask of all cores
    .trigger_panic  = true};



void app_main(void)
{
    ESP_LOGI(TAG, "Запуск приемника AIS AX5043");

    // Инициализация аппаратного watchdog ESP32
    esp_err_t ret = esp_task_wdt_init(&wdt_config);
    if (ret != ESP_OK) {
        if (ret == ESP_ERR_INVALID_STATE) {//WDT уже инициализирован
            ESP_ERROR_CHECK(esp_task_wdt_reconfigure(&wdt_config));
        }
        else {
            ESP_ERROR_CHECK(ret);
        }
    }
    ESP_ERROR_CHECK(esp_task_wdt_add(NULL));  // Добавить текущую задачу в список наблюдаемых
    ESP_LOGI(TAG, "Watchdog инициализирован: таймаут %u мс", AX5043_WATCHDOG_TIMEOUT_MS);

    // Initialize AX5043 for AIS reception
    ret = ax5043_ais_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Ошибка инициализации AX5043: %s", esp_err_to_name(ret));
        esp_task_wdt_delete(NULL);  // Удалить задачу из watchdog перед выходом
        abort();
    }

    ESP_LOGI(TAG, "AX5043 успешно инициализирован");

    ax5043_ais_fifo_raw_task(NULL);
    
    // TODO: Add AIS receiver configuration and data processing
    while (1)
    {
        // Кормление watchdog (каждые ~2 сек < 10 сек таймаута)
        esp_task_wdt_reset();

        // Проверка доступности пакета для обработки

        // ================================================================
        // Ожидание семафора с таймаутом (рекомендуется)
        // Таймаут 2000 мс позволяет периодически проверять статус даже без пакетов
        // ================================================================
        if (xSemaphoreTake(ais_rx_semaphore, pdMS_TO_TICKS(2000)) == pdTRUE) {
            ax5043_ais_dump_rx_state();
            // Семафор взят — есть новый пакет для обработки
            if (ais_packet_available) {
                ais_packet_available = false;
                
                // Обработка пакета: декодирование → NMEA → вывод
                ais_process_and_output(ais_rx_work_buffer, ais_rx_work_len);
                
                // Статистика
                ESP_LOGI(TAG, "Обработан пакет длина: %u байт", (unsigned)ais_rx_work_len);
            }
            else {
                ESP_LOGW(TAG, "Сработал сигнал семафора, но данных нет в буфере");
            }
        } else {
            // Таймаут истек — пакетов не было за 2 секунды
            // Можно вывести периодический статус для отладки
            ax5043_ais_dump_rx_state();
            if (ais_packet_available) {
                ais_packet_available = false;
                
                // Обработка пакета: декодирование → NMEA → вывод
                ais_process_and_output(ais_rx_work_buffer, ais_rx_work_len);
                
                // Статистика
                ESP_LOGI(TAG, "Обработан пакет длина: %u байт", (unsigned)ais_rx_work_len);
            }
        }
    }
    
    // For now, just deinitialize
    ax5043_ais_deinit();
    esp_task_wdt_delete(NULL); 
    ESP_LOGI(TAG, "AX5043 деинициализирован");
}


/*
void app_main(void)
{
    ESP_LOGI(TAG, "Запуск приемника AIS AX5043");
    
    esp_err_t ret = ax5043_ais_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Ошибка инициализации: %s", esp_err_to_name(ret));
        return;
    }
    
    // ============================================================
    // ВЫБЕРИТЕ ОДИН ИЗ ЭТАПОВ ДЛЯ ТЕСТИРОВАНИЯ
    // Раскомментируйте нужный блок
    // ============================================================
    
    // ────────────────────────────────────────────────────────
    // Этап А: физический уровень (демодемулятор + АРУ + трекинг)
    // ────────────────────────────────────────────────────────
    //ax5043_ais_debug_stage_a();

    //xTaskCreate(ax5043_ais_fifo_raw_task, "ais_fifo_raw", 4096, NULL, 5, NULL);
    ax5043_ais_fifo_raw_task(NULL);

    
    // ────────────────────────────────────────────────────────
    // Этап Б: проверка декодирования NRZI
    // ────────────────────────────────────────────────────────
    // ret = ax5043_ais_conf_stage_b();
    // if (ret == ESP_OK) {
    //     ax5043_ais_debug_stage_b();
    // }
}
*/