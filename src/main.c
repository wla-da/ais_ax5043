#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_system.h"
#include "ax5043.h"

static const char *TAG = "MAIN";

void app_main(void)
{
    ESP_LOGI(TAG, "=== Запуск приложения AX5043 ===");

    ESP_LOGI(TAG, "Шаг 1: Инициализация AX5043...");
    int result = ax5043_init();
    if (result != 0) {
        ESP_LOGE(TAG, "ОШИБКА: Не удалось инициализировать AX5043 (код: %d)", result);
        return;
    }

    ESP_LOGI(TAG, "Шаг 2: Вывод дампа регистров...");
    ax5043_dump_registers();

    ESP_LOGI(TAG, "Шаг 3: Запуск генерации несущей...");
    // Диагностика VCO перед запуском
    ax5043_vco_diagnostic();



             
    
    // Бесконечный цикл для поддержания работы
    while(1) {
        result = ax5043_start_cw_transmission_internal();
        if (result != 0) {
            ESP_LOGE(TAG, "ОШИБКА: Не удалось запустить генерацию (код: %d)", result);
            return;
        }
        ESP_LOGI(TAG, "Генерация несущей запущена успешно!");
        ESP_LOGI(TAG, "Частота: %.3f МГц, Мощность: %d дБм", 
            AX5043_CARRIER_FREQ_HZ / 1000000.0, AX5043_TARGET_POWER_DBM);

        uint8_t rstatus = 255;
        if (ax5043_get_radiostatus(&rstatus) == 0) {
            ESP_LOGI(TAG, "Radio status: %d", rstatus);
        }
        vTaskDelay(pdMS_TO_TICKS(5000)); // 5 секунд
        ESP_LOGI(TAG, "Генерация несущей продолжается...");
    }
}