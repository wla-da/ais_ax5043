/**
 * @file ax5043.c
 * @brief Реализация драйвера AX5043
 */

#include <string.h>
#include <inttypes.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include "esp_log.h"
#include "esp_timer.h"
#include "ax5043.h"

static const char *TAG = "AX5043";

// Глобальный контекст драйвера
static ax5043_context_t ax5043_ctx = {0};

/*============================================================================
 * ИНИЦИАЛИЗАЦИЯ SPI ШИНЫ
 *============================================================================*/

/**
 * @brief Диагностика SPI: проверка связи с чипом
 * 
 * Читает регистр REVISION (ожидается 0x51 для ревизии V1)
 * и тестирует запись/чтение регистра SCRATCH
 */
static int ax5043_spi_diagnostic(void)
{
    ESP_LOGI(TAG, "=== Диагностика SPI ===");
    
    uint8_t revision = 0;
    uint8_t scratch = 0;
    uint8_t scratch_read = 0;
    
    // Чтение регистра REVISION (адрес 0x000)
    if (ax5043_read_reg(AX5043_REG_REVISION, &revision) != 0) {
        ESP_LOGE(TAG, "ОШИБКА: Не удалось прочитать регистр REVISION");
        return -1;
    }
    ESP_LOGI(TAG, "REVISION register = 0x%02X (expected: 0x51)", revision);
    
    if (revision != 0x51) {
        ESP_LOGW(TAG, "Внимание: Неожиданное значение REVISION! Возможные причины:");
        ESP_LOGW(TAG, "  - Неисправен чип AX5043");
        ESP_LOGW(TAG, "  - Проблема с подключением SPI");
        ESP_LOGW(TAG, "  - Чип находится в режиме POWERDOWN (требуется 100ns CLK период)");
    }
    
    // Тест записи регистра SCRATCH (адрес 0x001)
    // Записываем тестовое значение
    scratch = 0xA5;
    if (ax5043_write_reg(AX5043_REG_SCRATCH, scratch) != 0) {
        ESP_LOGE(TAG, "ОШИБКА: Не удалось записать в регистр SCRATCH");
        return -1;
    }
    ESP_LOGI(TAG, "SCRATCH write: 0x%02X", scratch);
    
    // Читаем обратно
    if (ax5043_read_reg(AX5043_REG_SCRATCH, &scratch_read) != 0) {
        ESP_LOGE(TAG, "ОШИБКА: Не удалось прочитать регистр SCRATCH");
        return -1;
    }
    ESP_LOGI(TAG, "SCRATCH read:  0x%02X", scratch_read);
    
    if (scratch_read != scratch) {
        ESP_LOGE(TAG, "ОШИБКА: SCRATCH mismatch! Записано 0x%02X, прочитано 0x%02X", scratch, scratch_read);
        return -1;
    }
    
    // Записываем инвертированное значение для полной проверки
    scratch = 0x5A;
    if (ax5043_write_reg(AX5043_REG_SCRATCH, scratch) != 0) {
        ESP_LOGE(TAG, "ОШИБКА: Не удалось записать в регистр SCRATCH (2)");
        return -1;
    }
    
    if (ax5043_read_reg(AX5043_REG_SCRATCH, &scratch_read) != 0) {
        ESP_LOGE(TAG, "ОШИБКА: Не удалось прочитать регистр SCRATCH (2)");
        return -1;
    }
    
    if (scratch_read != scratch) {
        ESP_LOGE(TAG, "ОШИБКА: SCRATCH mismatch (2)! Записано 0x%02X, прочитано 0x%02X", scratch, scratch_read);
        return -1;
    }
    
    ESP_LOGI(TAG, "=== SPI диагностика успешна ===");
    return 0;
}

/**
 * @brief Инициализация SPI шины для AX5043
 * 
 * Конфигурация для ESP32-S3-Zero:
 * - MOSI: GPIO11
 * - MISO: GPIO13
 * - SCLK: GPIO12
 * - CS:   GPIO10
 * - Частота: 5 МГц
 * - Режим: SPI Mode 0
 */
static int ax5043_spi_init(void)
{
    ESP_LOGI(TAG, "Инициализация SPI шины для AX5043");
    
    // Конфигурация SPI шины (SPI2_HOST = HSPI)
    spi_bus_config_t bus_cfg = {
        .mosi_io_num = AX5043_PIN_MOSI,   // GPIO11
        .miso_io_num = AX5043_PIN_MISO,   // GPIO13
        .sclk_io_num = AX5043_PIN_CLK,    // GPIO12
        .quadwp_io_num = -1,               // Не используем Quad SPI
        .quadhd_io_num = -1,               // Не используем Quad SPI
        .max_transfer_sz = 64,             // Максимальный размер передачи (байт)
        .flags = SPICOMMON_BUSFLAG_MASTER,
        .intr_flags = ESP_INTR_FLAG_LEVEL1
    };
    
    // Инициализация SPI шины (SPI2_HOST)
    esp_err_t ret = spi_bus_initialize(SPI2_HOST, &bus_cfg, SPI_DMA_CH_AUTO);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Ошибка инициализации SPI шины: %s", esp_err_to_name(ret));
        return -1;
    }
    
    ESP_LOGI(TAG, "SPI шина инициализирована:");
    ESP_LOGI(TAG, "  MOSI = GPIO%d", AX5043_PIN_MOSI);
    ESP_LOGI(TAG, "  MISO = GPIO%d", AX5043_PIN_MISO);
    ESP_LOGI(TAG, "  SCLK = GPIO%d", AX5043_PIN_CLK);
    ESP_LOGI(TAG, "  CS   = GPIO%d", AX5043_PIN_CS);
    
    // Настройка CS пина как выход
    gpio_config_t cs_cfg = {
        .pin_bit_mask = (1ULL << AX5043_PIN_CS),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };
    ret = gpio_config(&cs_cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Ошибка настройки CS пина: %s", esp_err_to_name(ret));
        spi_bus_free(SPI2_HOST);
        return -1;
    }
    
    // Установка CS в HIGH (неактивное состояние)
    gpio_set_level(AX5043_PIN_CS, 1);
    
    // Конфигурация SPI устройства
    spi_device_interface_config_t dev_cfg = {
        .clock_speed_hz = AX5043_SPI_CLOCK_HZ,  // 5 МГц
        .mode = 0,                               // SPI Mode 0 (CPOL=0, CPHA=0)
        .spics_io_num = -1,                      // Управление CS вручную
        .queue_size = 7,                         // Размер очереди транзакций
        .flags = SPI_DEVICE_NO_DUMMY,            // Без dummy битов
        .cs_ena_pretrans = 0,                    // Нет задержки перед передачей
        .cs_ena_posttrans = 0                    // Нет задержки после передачи
    };
    
    // Добавление устройства на шину
    ret = spi_bus_add_device(SPI2_HOST, &dev_cfg, &ax5043_ctx.spi_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Ошибка добавления SPI устройства: %s", esp_err_to_name(ret));
        spi_bus_free(SPI2_HOST);
        return -1;
    }
    
    ESP_LOGI(TAG, "SPI устройство добавлено: частота %d МГц, режим 0", 
             dev_cfg.clock_speed_hz / 1000000);
    
    return 0;
}

/**
 * @brief Деинициализация SPI шины
 */
static void ax5043_spi_deinit(void)
{
    ESP_LOGI(TAG, "Деинициализация SPI шины");
    
    if (ax5043_ctx.spi_handle != NULL) {
        spi_bus_remove_device(ax5043_ctx.spi_handle);
        ax5043_ctx.spi_handle = NULL;
    }
    
    spi_bus_free(SPI2_HOST);
}

/*============================================================================
 * ВНУТРЕННИЕ ФУНКЦИИ SPI
 *============================================================================*/

/**
 * @brief Чтение регистра AX5043 (короткий адрес)
 */
int ax5043_read_reg(uint8_t addr, uint8_t *data)
{
    if (data == NULL) {
        ESP_LOGE(TAG, "Нулевой указатель данных");
        return -1;
    }
    
    spi_transaction_t trans = {0};
    uint8_t tx_buf[2] = {addr & 0x7F, 0x00};  // Бит R/W = 0 (чтение)
    uint8_t rx_buf[2] = {0};
    
    trans.length = 16;  // 16 бит для чтения
    trans.tx_buffer = tx_buf;
    trans.rx_buffer = rx_buf;
    
    // Активация CS (LOW)
    gpio_set_level(AX5043_PIN_CS, 0);
    
    esp_err_t ret = spi_device_transmit(ax5043_ctx.spi_handle, &trans);
    
    // Деактивация CS (HIGH)
    gpio_set_level(AX5043_PIN_CS, 1);
    
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Ошибка SPI при чтении регистра 0x%02X: %s", 
                 addr, esp_err_to_name(ret));
        return -1;
    }
    
    *data = rx_buf[1];
    return 0;
}

/**
 * @brief Запись в регистр AX5043 (короткий адрес)
 */
int ax5043_write_reg(uint8_t addr, uint8_t data)
{
    spi_transaction_t trans = {0};
    uint8_t tx_buf[2] = {(addr & 0x7F) | 0x80, data};  // Бит R/W = 1 (запись)
    
    trans.length = 16;
    trans.tx_buffer = tx_buf;
    
    // Активация CS (LOW)
    gpio_set_level(AX5043_PIN_CS, 0);
    
    esp_err_t ret = spi_device_transmit(ax5043_ctx.spi_handle, &trans);
    
    // Деактивация CS (HIGH)
    gpio_set_level(AX5043_PIN_CS, 1);
    
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Ошибка SPI при записи в регистр 0x%02X: %s", 
                 addr, esp_err_to_name(ret));
        return -1;
    }
    
    return 0;
}

/**
 * @brief Чтение длинного регистра AX5043 (16-бит адрес)
 */
int ax5043_read_long_reg(uint16_t addr, uint8_t *data)
{
    if (data == NULL) {
        ESP_LOGE(TAG, "Нулевой указатель данных");
        return -1;
    }
    
    spi_transaction_t trans = {0};
    uint8_t tx_buf[3] = {
        0x70 | ((addr >> 8) & 0x0F),  // Биты длинного адреса + старшие биты адреса
        addr & 0xFF,                   // Младшие биты адреса
        0x00                           // Пустой байт для чтения данных
    };
    uint8_t rx_buf[3] = {0};
    
    trans.length = 24;  // 24 бита для длинного адреса
    trans.tx_buffer = tx_buf;
    trans.rx_buffer = rx_buf;
    
    // Активация CS (LOW)
    gpio_set_level(AX5043_PIN_CS, 0);
    
    esp_err_t ret = spi_device_transmit(ax5043_ctx.spi_handle, &trans);
    
    // Деактивация CS (HIGH)
    gpio_set_level(AX5043_PIN_CS, 1);
    
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Ошибка SPI при чтении длинного регистра 0x%03X: %s", 
                 addr, esp_err_to_name(ret));
        return -1;
    }
    
    *data = rx_buf[2];
    return 0;
}

/**
 * @brief Запись в длинный регистр AX5043 (16-бит адрес)
 */
int ax5043_write_long_reg(uint16_t addr, uint8_t data)
{
    spi_transaction_t trans = {0};
    uint8_t tx_buf[3] = {
        0x70 | ((addr >> 8) & 0x0F) | 0x80,  // Длинный адрес + бит записи
        addr & 0xFF,
        data
    };
    
    trans.length = 24;
    trans.tx_buffer = tx_buf;
    
    // Активация CS (LOW)
    gpio_set_level(AX5043_PIN_CS, 0);
    
    esp_err_t ret = spi_device_transmit(ax5043_ctx.spi_handle, &trans);
    
    // Деактивация CS (HIGH)
    gpio_set_level(AX5043_PIN_CS, 1);
    
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Ошибка SPI при записи в длинный регистр 0x%03X: %s", 
                 addr, esp_err_to_name(ret));
        return -1;
    }
    
    return 0;
}

/**
 * @brief Аппаратный сброс через сигнал CS (SEL)
 */
static int ax5043_hardware_reset(void)
{
    ESP_LOGI(TAG, "Выполнение аппаратного сброса через SEL");
    
    // Установка SEL в HIGH
    gpio_set_level(AX5043_PIN_CS, 1);
    esp_rom_delay_us(1);  // Задержка ≥ 1 мкс
    
    // Установка SEL в LOW
    gpio_set_level(AX5043_PIN_CS, 0);
    
    // Ожидание готовности чипа (MISO перейдет в HIGH)
    // Максимальное время ожидания 1 мс
    int timeout = 1000;
    while (gpio_get_level(AX5043_PIN_MISO) == 0 && timeout > 0) {
        esp_rom_delay_us(10);
        timeout -= 10;
    }
    
    if (timeout <= 0) {
        ESP_LOGW(TAG, "Таймаут ожидания готовности чипа после сброса");
    }
    
    // Задержка 100 мкс после сброса
    esp_rom_delay_us(AX5043_RESET_DELAY_US);
    
    return 0;
}

/**
 * @brief Программный сброс AX5043 через регистр PWRMODE
 */
int ax5043_software_reset(void)
{
    ESP_LOGI(TAG, "Выполнение программного сброса");
    
    // Установка бита RST
    int ret = ax5043_write_reg(AX5043_REG_PWRMODE, PWRMODE_RST);
    if (ret != 0) {
        ESP_LOGE(TAG, "Ошибка записи бита сброса");
        return -1;
    }
    
    // Задержка 100 мкс
    esp_rom_delay_us(AX5043_RESET_DELAY_US);
    
    // Очистка бита RST
    ret = ax5043_write_reg(AX5043_REG_PWRMODE, 
        PWRMODE_POWERDOWN | PWRMODE_XOEN | PWRMODE_REFEN);
    if (ret != 0) {
        ESP_LOGE(TAG, "Ошибка очистки бита сброса");
        return -1;
    }
    
    // Задержка 100 мкс
    esp_rom_delay_us(AX5043_RESET_DELAY_US);
    
    return 0;
}

/**
 * @brief Настройка TCXO (Clipped Sine Wave)
 */
static int ax5043_configure_tcxo(void)
{
    ESP_LOGI(TAG, "Настройка TCXO (Clipped Sine Wave)");
    
    // Регистр 0xF10 (Ibias) = 0x04
    if (ax5043_write_long_reg(AX5043_REG_IBIAS, 0x04) != 0) {
        ESP_LOGE(TAG, "Ошибка записи в регистр Ibias");
        return -1;
    }
    
    // Регистр 0xF11 (Ibias autoreg) = 0x00
    if (ax5043_write_long_reg(AX5043_REG_IBIAS_AUTOREG, 0x00) != 0) {
        ESP_LOGE(TAG, "Ошибка записи в регистр Ibias autoreg");
        return -1;
    }
    
    // Регистр XTALCAP = 0x00 (для TCXO емкостная подстройка не требуется)
    	if (ax5043_write_long_reg(AX5043_REG_XTALCAP, 0x00) != 0) {
    		ESP_LOGE(TAG, "Ошибка записи в регистр XTALCAP");
    		return -1;
    	}
    
    // Регистр 0xF35 (XTALDIV) = 0x10 (FXTALDIV = 1)
    //Set to 0x10 for reference frequencies (crystal or TCXO) less 
    //than 24.8 MHz (fXTALDIV = 1), or to 0x11 otherwise (fXTALDIV = 2)
    if (ax5043_write_long_reg(AX5043_REG_XTALDIV, 0x10) != 0) {
        ESP_LOGE(TAG, "Ошибка записи в регистр XTALDIV");
        return -1;
    }
    
    return 0;
}

/**
 * @brief Настройка PLL
 */
static int ax5043_configure_pll(void)
{
    ESP_LOGI(TAG, "Настройка PLL");
    
    // PLLVCODIV = 0x30 (RFDIV=0, VCOSEL=1, VCO2INT=1)
    // Бит 2: RFDIV = 0 (делитель RF отключен)
    // Бит 4: VCOSEL = 1 (выбор VCO2)
    // Бит 5: VCO2INT = 1 (внутренний VCO с внешней индуктивностью)
    uint8_t pllvcoiv = 0x30;  // 0b00110000
    if (ax5043_write_reg(AX5043_REG_PLLVCODIV, pllvcoiv) != 0) {
        ESP_LOGE(TAG, "Ошибка записи в регистр PLLVCODIV");
        return -1;
    }
    ESP_LOGI(TAG, "PLLVCODIV = 0x%02X (RFDIV=0, VCOSEL=1, VCO2INT=1)", pllvcoiv);
    
    // PLLLOOP = 0b1001
    // Биты 1-0: FLT[1:0] = 01 (внутренний фильтр 100 кГц)
    // Бит 2: FILTEN = 0 (внутренний фильтр)
    // Бит 3: DIRECT = 0 (не bypass)
    // Бит 7: FREQSEL = 0, используем FREQA (если 1, то FREQB)
    if (ax5043_write_reg(AX5043_REG_PLLLOOP, 0b1001) != 0) {
        ESP_LOGE(TAG, "Ошибка записи в регистр PLLLOOP");
        return -1;
    }
    
    // PLLCPI = 0x08 (ток charge pump = 8 × 8.5 мкА = 68 мкА)
    if (ax5043_write_reg(AX5043_REG_PLLCPI, 0x08) != 0) {
        ESP_LOGE(TAG, "Ошибка записи в регистр PLLCPI");
        return -1;
    }
    
    // PLLCPIBOOST = 0xC8 (200 × 8.5 мкА)
    if (ax5043_write_reg(AX5043_REG_PLLCPIBOOST, 0xC8) != 0) {
        ESP_LOGE(TAG, "Ошибка записи в регистр PLLCPIBOOST");
        return -1;
    }
    
    // PLLLOOPBOOST = 0b1011
    if (ax5043_write_reg(AX5043_REG_PLLLOOPBOOST, 0b1011) != 0) {
        ESP_LOGE(TAG, "Ошибка записи в регистр PLLLOOPBOOST");
        return -1;
    }
    
    return 0;
}

/**
 * @brief Установка несущей частоты
 */
int ax5043_set_frequency(uint32_t freq_hz)
{
    ESP_LOGI(TAG, "Установка частоты: %"PRIu32" Гц (%.3f МГц)", freq_hz, freq_hz / 1000000.0);
    
    // Расчет FREQA = (fCARRIER / fXTAL) × 2^24 + 1/2
    // При RFDIV=0 частота VCO = частота несущей
    // Для 161.975 МГц: FREQA = (161975000 / 19200000) × 2^24 = 8.436 × 16777216 = 141535914
    // 141535914 в hex = 0x086FAAAA
    // Настоятельно рекомендуется установить бит 0, чтобы снизить спектральные шумы
    uint64_t freqa = ((uint64_t)freq_hz << 24) / ax5043_ctx.xtal_freq;
    
    ESP_LOGI(TAG, "FREQA = 0x%08" PRIX32 " (%llu)", (uint32_t)freqa, (unsigned long long)freqa);
    
    // Запись регистров FREQA (LSB first)
    // FREQA0 (младший байт) - адрес 0x037
    if (ax5043_write_reg(AX5043_REG_FREQA0, (uint8_t)(freqa & 0xFF)) != 0) {
        ESP_LOGE(TAG, "Ошибка записи FREQA0");
        return -1;
    }
    
    // FREQA1 - адрес 0x036
    if (ax5043_write_reg(AX5043_REG_FREQA1, (uint8_t)((freqa >> 8) & 0xFF)) != 0) {
        ESP_LOGE(TAG, "Ошибка записи FREQA1");
        return -1;
    }
    
    // FREQA2 - адрес 0x035
    if (ax5043_write_reg(AX5043_REG_FREQA2, (uint8_t)((freqa >> 16) & 0xFF)) != 0) {
        ESP_LOGE(TAG, "Ошибка записи FREQA2");
        return -1;
    }
    
    // FREQA3 (старший байт) - адрес 0x034
    if (ax5043_write_reg(AX5043_REG_FREQA3, (uint8_t)((freqa >> 24) & 0xFF)) != 0) {
        ESP_LOGE(TAG, "Ошибка записи FREQA3");
        return -1;
    }
    
    ax5043_ctx.carrier_freq = freq_hz;
    return 0;
}

/**
 * @brief Запуск автоподстройки VCO
 */
static int ax5043_run_vco_ranging(void)
{
    ESP_LOGI(TAG, "Запуск автоподстройки VCO");

    // Настройка регистров перфоманс тюнинга
    if (ax5043_write_long_reg(AX5043_REG_PERF_F00, 0x0F) != 0) {
        ESP_LOGE(TAG, "Ошибка записи в регистр PERF_F00");
        return -1;
    }
    if (ax5043_write_long_reg(AX5043_REG_PERF_F0C, 0x00) != 0) {
        ESP_LOGE(TAG, "Ошибка записи в регистр PERF_F0C");
        return -1;
    }
    if (ax5043_write_long_reg(AX5043_REG_PERF_F0D, 0x03) != 0) {
        ESP_LOGE(TAG, "Ошибка записи в регистр PERF_F0D");
        return -1;
    }   
    if (ax5043_write_long_reg(AX5043_REG_PERF_F1C, 0x07) != 0) {
        ESP_LOGE(TAG, "Ошибка записи в регистр PERF_F1C");
        return -1;
    }   
    if (ax5043_write_long_reg(AX5043_REG_PERF_F34, AX5043_PERF_F34_RFDIV_OFF) != 0) {
        ESP_LOGE(TAG, "Ошибка записи в регистр PERF_F34");
        return -1;
    }
    if (ax5043_write_long_reg(AX5043_REG_PERF_F44, 0x24) != 0) {
        ESP_LOGE(TAG, "Ошибка записи в регистр PERF_F44");
        return -1;
    }

    // Установка начального VCORA = 8
    uint8_t ranging_val = 0x08;  // VCORA = 8
    
    // Запуск автоподстройки: установить бит RNGSTART = 1
    ranging_val |= PLLRANGINGA_RNGSTART;
    
    if (ax5043_write_reg(AX5043_REG_PLLRANGINGA, ranging_val) != 0) {
        ESP_LOGE(TAG, "Ошибка запуска автоподстройки VCO");
        return -1;
    }
    
    // Ожидание завершения автоподстройки (бит RNGSTART очистится)
    int timeout = AX5043_VCO_RANGING_TIMEOUT_MS;
    uint8_t status = 0;
    
    // Начальное значение для проверки
    if (ax5043_read_reg(AX5043_REG_PLLRANGINGA, &status) != 0) {
        ESP_LOGE(TAG, "Ошибка чтения статуса VCO");
        return -1;
    }
    
    ESP_LOGI(TAG, "Начальный статус VCO: 0x%02X", status);
    
    while (timeout > 0) {
        vTaskDelay(pdMS_TO_TICKS(1));
        
        if (ax5043_read_reg(AX5043_REG_PLLRANGINGA, &status) != 0) {
            ESP_LOGE(TAG, "Ошибка чтения статуса VCO");
            return -1;
        }
        
        // Проверка бита RNGSTART (бит 4)
        if ((status & PLLRANGINGA_RNGSTART) == 0) {
            break;
        }
        
        timeout--;
    }
    
    if (timeout <= 0) {
        ESP_LOGE(TAG, "Таймаут автоподстройки VCO");
        return -1;
    }
    
    // Проверка результата
    // Бит 6: RNGERR - ошибка автоподстройки
    // Бит 7: PLLLOCK - PLL захвачен
    uint8_t vco_range = status & PLLRANGINGA_VCORA_MASK;
    bool rngerr = (status & PLLRANGINGA_RNGERR) != 0;
    bool plllock = (status & PLLRANGINGA_PLLLOCK) != 0;
    bool sticky_lock = (status & PLLRANGINGA_STYCKY_LOCK) != 0;
    
    ESP_LOGI(TAG, "Автоподстройка VCO завершена: VCORA=%d, RNGERR=%d, PLLLOCK=%d, STYCKY_LOCK=%d, status=0x%02X",  
             vco_range, rngerr, plllock, sticky_lock, status);
    
    if (rngerr) {
        ESP_LOGE(TAG, "Ошибка автоподстройки VCO");
        return -1;
    }
    
    if (!plllock) {
        ESP_LOGE(TAG, "PLL не захвачен после автоподстройки");
        return -1;
    }
    
    ESP_LOGI(TAG, "PLL успешно захвачен, VCO Range = %d", vco_range);
    return 0;
}

/**
 * @brief Установка выходной мощности передатчика
 */
int ax5043_set_power(int8_t power_dbm)
{
    ESP_LOGI(TAG, "Установка мощности: %d дБм", power_dbm);
    
    // Согласно datasheet минимальная мощность -10 дБм
    // Для -10 дБм используем значение TXPWRCOEFFB = 0x0156 (из примеров)
    // Для других мощностей требуется калибровка
    uint16_t power_coeff = 0x0156;  // -10 dBm default
    
    if (power_dbm != -10) {
        ESP_LOGW(TAG, "Только -10 дБм поддерживается, используется значение по умолчанию");
    }
    
    // Запись TXPWRCOEFFB (адреса 0x16A-0x16B) - длинные регистры
    	if (ax5043_write_long_reg(AX5043_REG_TXPWRCOEFFB1, (power_coeff >> 8) & 0xFF) != 0) {
    		ESP_LOGE(TAG, "Ошибка записи TXPWRCOEFFB1");
    		return -1;
    	}
    	if (ax5043_write_long_reg(AX5043_REG_TXPWRCOEFFB0, power_coeff & 0xFF) != 0) {
    		ESP_LOGE(TAG, "Ошибка записи TXPWRCOEFFB0");
    		return -1;
    	}
    
    ax5043_ctx.power_dbm = power_dbm;
    return 0;
}

/**
 * @brief Настройка модуляции для CW (непрерывная несущая)
 */
static int ax5043_configure_modulation(void)
{
    ESP_LOGI(TAG, "Настройка модуляции CW");
    
    // MODULATION = 0x08 (FSK модуляция)
    // Биты 3-0: MODULATION[3:0] = 1000 (FSK)
    if (ax5043_write_reg(AX5043_REG_MODULATION, MODULATION_FSK) != 0) {
        ESP_LOGE(TAG, "Ошибка записи в регистр MODULATION");
        return -1;
    }
    
    // FSKDEV = 0x000001 (минимальная девиация для CW) - длинные регистры
    	// FSKDEV2 (адрес 0x161) = 0x00
    	if (ax5043_write_long_reg(AX5043_REG_FSKDEV2, 0x00) != 0) {
    		ESP_LOGE(TAG, "Ошибка записи FSKDEV2");
    		return -1;
    	}
    	// FSKDEV1 (адрес 0x162) = 0x00
    	if (ax5043_write_long_reg(AX5043_REG_FSKDEV1, 0x00) != 0) {
    		ESP_LOGE(TAG, "Ошибка записи FSKDEV1");
    		return -1;
    	}
    	// FSKDEV0 (адрес 0x163) = 0x01
    	if (ax5043_write_long_reg(AX5043_REG_FSKDEV0, 0x01) != 0) {
    		ESP_LOGE(TAG, "Ошибка записи FSKDEV0");
    		return -1;
    	}
    
    return 0;
}

/**
 * @brief Настройка выходного усилителя (Single-Ended)
 */
static int ax5043_configure_tx_output(void)
{
    ESP_LOGI(TAG, "Настройка выходного усилителя (Single-Ended ANTP1)");
    
    // MODCFGA = 0x06 (0b00000110) - длинный регистр
    	// Бит 0: TXDIFF = 0 (дифференциальный выход отключен)
    	// Бит 1: TXSE = 1 (single-ended выход включен) ★★★
    	// Бит 2: AMPLSHAPE = 1 (raised cosine shaping)
    	uint8_t modcfga = MODCFGA_TXSE | MODCFGA_AMPLSHAPE; // 0x06
    	if (ax5043_write_long_reg(AX5043_REG_MODCFGA, modcfga) != 0) {
    		ESP_LOGE(TAG, "Ошибка записи в регистр MODCFGA");
    		return -1;
    	}
    ESP_LOGI(TAG, "MODCFGA = 0x%02X (TXSE=1, AMPLSHAPE=1)", modcfga);
    
    return 0;
}


void ax5043_dump_power_status() {
    // Чтение и отображение статуса питания
    uint8_t powstat = 0;
    if (ax5043_read_reg(AX5043_REG_POWSTAT, &powstat) == 0) {
        ESP_LOGI(TAG, "POWSTAT register = 0x%02X:", powstat);
        ESP_LOGI(TAG, "  SVIO    = %d (Напряжение IO)", (powstat >> 0) & 1);
        ESP_LOGI(TAG, "  SBEVMODEM = %d (MODEM Brownout Error, 1-Ok)", (powstat >> 1) & 1);
        ESP_LOGI(TAG, "  SBEVANA   = %d (ANALOG Brownout Error, 1-Ok)", (powstat >> 2) & 1);
        ESP_LOGI(TAG, "  SVMODEM   = %d (MODEM ready)", (powstat >> 3) & 1);
        ESP_LOGI(TAG, "  SVANA   = %d (ANALOG ready)", (powstat >> 4) & 1);
        ESP_LOGI(TAG, "  SVREF   = %d (VREF regulator ready)", (powstat >> 5) & 1);
        ESP_LOGI(TAG, "  SREF   = %d (VREF ready)", (powstat >> 6) & 1);
        ESP_LOGI(TAG, "  SSUM    = %d (Общая готовность)", (powstat >> 7) & 1);
    } else {
        ESP_LOGI(TAG, "Ошибка чтения REG_POWSTAT");
    }

    uint8_t powstickystat = 0;
    if (ax5043_read_reg(AX5043_REG_POWSTICKYSTAT, &powstickystat) == 0) {
        ESP_LOGI(TAG, "POWSTICKYSTAT register = 0x%02X:", powstickystat);
        ESP_LOGI(TAG, "  SSVIO    = %d (Напряжение IO)", (powstickystat >> 0) & 1);
        ESP_LOGI(TAG, "  SSBEVMODEM = %d (MODEM Brownout Error, 1-Ok)", (powstickystat >> 1) & 1);
        ESP_LOGI(TAG, "  SSBEVANA   = %d (ANALOG Brownout Error, 1-Ok)", (powstickystat >> 2) & 1);
        ESP_LOGI(TAG, "  SSVMODEM   = %d (MODEM ready)", (powstickystat >> 3) & 1);
        ESP_LOGI(TAG, "  SSVANA   = %d (ANALOG ready)", (powstickystat >> 4) & 1);
        ESP_LOGI(TAG, "  SSVREF   = %d (VREF regulator ready)", (powstickystat >> 5) & 1);
        ESP_LOGI(TAG, "  SSREF   = %d (VREF ready)", (powstickystat >> 6) & 1);
        ESP_LOGI(TAG, "  SSSUM    = %d (Общая готовность)", (powstickystat >> 7) & 1);
    } else {
        ESP_LOGI(TAG, "Ошибка чтения REG_POWSTAT");
    }

    uint8_t powirqmask = 0;
    if (ax5043_read_reg(AX5043_REG_POWIRQMASK, &powirqmask) == 0) {
        ESP_LOGI(TAG, "POWIRQMASK register = 0x%02X:", powirqmask);
        ESP_LOGI(TAG, "  MSVIO    = %d (Напряжение IO)", (powirqmask >> 0) & 1);
        ESP_LOGI(TAG, "  MSBEVMODEM = %d (MODEM Brownout Error)", (powirqmask >> 1) & 1);
        ESP_LOGI(TAG, "  MSBEVANA   = %d (ANALOG Brownout Error)", (powirqmask >> 2) & 1);
        ESP_LOGI(TAG, "  MSVMODEM   = %d (MODEM ready)", (powirqmask >> 3) & 1);
        ESP_LOGI(TAG, "  MSVANA   = %d (ANALOG ready)", (powirqmask >> 4) & 1);
        ESP_LOGI(TAG, "  MSVREF   = %d (VREF regulator ready)", (powirqmask >> 5) & 1);
        ESP_LOGI(TAG, "  MSREF   = %d (VREF ready)", (powirqmask >> 6) & 1);
        ESP_LOGI(TAG, "  MPWRGOOD    = %d (Общая готовность)", (powirqmask >> 7) & 1);
    } else {
        ESP_LOGI(TAG, "Ошибка чтения REG_POWIRQMASK");
    }
}


/**
 * @brief Инициализация AX5043
 */
int ax5043_init(void)
{
    ESP_LOGI(TAG, "Инициализация AX5043");
    
    // Инициализация контекста
    ax5043_ctx.xtal_freq = AX5043_XTAL_FREQ_HZ;
    ax5043_ctx.carrier_freq = 0;
    ax5043_ctx.power_dbm = 0;
    ax5043_ctx.initialized = false;
    
    // Инициализация SPI шины
    if (ax5043_spi_init() != 0) {
        ESP_LOGE(TAG, "Ошибка инициализации SPI");
        return -1;
    }

    // Диагностика SPI: проверка связи с чипом
    if (ax5043_spi_diagnostic() != 0) {
        ESP_LOGE(TAG, "Ошибка диагностики SPI");
        ax5043_spi_deinit();
        return -1;
    }

    // Аппаратный сброс
    if (ax5043_hardware_reset() != 0) {
        ESP_LOGE(TAG, "Ошибка аппаратного сброса");
        ax5043_spi_deinit();
        return -1;
    }
    
    // Диагностика после аппаратного сброса
    ESP_LOGI(TAG, "--- Проверка после аппаратного сброса ---");
    if (ax5043_spi_diagnostic() != 0) {
        ESP_LOGE(TAG, "Ошибка диагностики после hardware reset");
        ax5043_spi_deinit();
        return -1;
    }

    // Программный сброс
    if (ax5043_software_reset() != 0) {
        ESP_LOGE(TAG, "Ошибка программного сброса");
        ax5043_spi_deinit();
        return -1;
    }

    // Диагностика после программного сброса
    ESP_LOGI(TAG, "--- Проверка после программного сброса ---");
    if (ax5043_spi_diagnostic() != 0) {
        ESP_LOGE(TAG, "Ошибка диагностики после software reset");
        ax5043_spi_deinit();
        return -1;
    }
    
    // Настройка TCXO
    if (ax5043_configure_tcxo() != 0) {
        ESP_LOGE(TAG, "Ошибка настройки TCXO");
        ax5043_spi_deinit();
        return -1;
    }
    
    // Установка режима STANDBY с включенными XOEN и REFEN
    // Согласно AND9347/D, биты XOEN=1 и REFEN=1 уже установлены по умолчанию
    // и должны оставаться включенными в режиме STANDBY
    ESP_LOGI(TAG, "Установка режима STANDBY (PWRMODE = 0x65)");
    if (ax5043_write_reg(AX5043_REG_PWRMODE, 
        PWRMODE_STANDBY | PWRMODE_XOEN | PWRMODE_REFEN) != 0) {  // 0b01100101 - PWRMODE_STANDBY | XOEN | REFEN
        ESP_LOGE(TAG, "Ошибка установки режима STANDBY");
        ax5043_spi_deinit();
        return -1;
    }
    
    // Задержка для стабилизации
    ESP_LOGI(TAG, "Ожидание стабилизации TCXO (%d мс)...", AX5043_XTAL_STARTUP_MS);
    vTaskDelay(pdMS_TO_TICKS(AX5043_XTAL_STARTUP_MS));
    
    ax5043_dump_power_status();

    // Проверка статуса XTAL
    uint8_t xtal_status = 0;
    if (ax5043_read_reg(AX5043_REG_XTALSTATUS, &xtal_status) != 0) {
        ESP_LOGE(TAG, "Ошибка чтения статуса XTAL");
        ax5043_spi_deinit();
        return -1;
    }
    
    ESP_LOGI(TAG, "XTALSTATUS register = 0x%02X (XTALRUN = %d)", xtal_status, xtal_status & 1);
    
    if (!(xtal_status & XTALSTATUS_XTALRUN)) {
        ESP_LOGE(TAG, "Кварцевый генератор не запущен!");
        ESP_LOGE(TAG, "Возможные причины:");
        ESP_LOGE(TAG, "  1. TCXO не подключен или неисправен");
        ESP_LOGE(TAG, "  2. Неверная конфигурация TCXO (проверьте регистры 0xF10, 0xF11, 0xF35)");
        ESP_LOGE(TAG, "  3. Недостаточная задержка после включения");
        ESP_LOGE(TAG, "  4. Проблема с питанием чипа");
        
        // Попробуем увеличить задержку и повторить проверку
        ESP_LOGI(TAG, "Увеличиваем задержку до 50 мс и повторяем проверку...");
        vTaskDelay(pdMS_TO_TICKS(50));
        
        if (ax5043_read_reg(AX5043_REG_XTALSTATUS, &xtal_status) == 0) {
            ESP_LOGI(TAG, "XTALSTATUS после дополнительной задержки = 0x%02X", xtal_status);
            if (xtal_status & XTALSTATUS_XTALRUN) {
                ESP_LOGI(TAG, "TCXO запустился после дополнительной задержки!");
            } else {
                ESP_LOGE(TAG, "TCXO всё ещё не работает");
                ax5043_spi_deinit();
                return -1;
            }
        } else {
            ax5043_spi_deinit();
            return -1;
        }
    }
    
    ESP_LOGI(TAG, "Кварцевый генератор (TCXO) работает");

    
    // Настройка PLL
    if (ax5043_configure_pll() != 0) {
        ESP_LOGE(TAG, "Ошибка настройки PLL");
        ax5043_spi_deinit();
        return -1;
    }
    
    // Установка частоты
    if (ax5043_set_frequency(AX5043_CARRIER_FREQ_HZ) != 0) {
        ESP_LOGE(TAG, "Ошибка установки частоты");
        ax5043_spi_deinit();
        return -1;
    }
    
    // Настройка модуляции
    if (ax5043_configure_modulation() != 0) {
        ESP_LOGE(TAG, "Ошибка настройки модуляции");
        ax5043_spi_deinit();
        return -1;
    }
    
    // Настройка выходного усилителя
    if (ax5043_configure_tx_output() != 0) {
        ESP_LOGE(TAG, "Ошибка настройки выходного усилителя");
        ax5043_spi_deinit();
        return -1;
    }
    
    // Установка мощности
    if (ax5043_set_power(AX5043_TARGET_POWER_DBM) != 0) {
        ESP_LOGE(TAG, "Ошибка установки мощности");
        ax5043_spi_deinit();
        return -1;
    }
    
    ax5043_ctx.initialized = true;
    ESP_LOGI(TAG, "AX5043 успешно инициализирован");
    return 0;
}

/**
 * @brief Диагностика VCO: чтение всех регистров PLL
 */
void ax5043_vco_diagnostic(void)
{
    ESP_LOGI(TAG, "=== Диагностика VCO ===");
    
    uint8_t reg_val = 0;
    
    // Вывод регистров PLL для диагностики
    const char* pll_reg_names[] = {
        "PLLLOOP", "PLLCPI", "PLLVCODIV", "PLLRANGINGA", "FREQA3", "FREQA2", "FREQA1", "FREQA0"
    };
    uint8_t pll_reg_addrs[] = {
        AX5043_REG_PLLLOOP, AX5043_REG_PLLCPI, AX5043_REG_PLLVCODIV, AX5043_REG_PLLRANGINGA,
        AX5043_REG_FREQA3, AX5043_REG_FREQA2, AX5043_REG_FREQA1, AX5043_REG_FREQA0
    };
    
    for (int i = 0; i < sizeof(pll_reg_addrs)/sizeof(pll_reg_addrs[0]); i++) {
        if (ax5043_read_reg(pll_reg_addrs[i], &reg_val) == 0) {
            ESP_LOGI(TAG, "PLL %s (0x%02X): 0x%02X", pll_reg_names[i], pll_reg_addrs[i], reg_val);
        } else {
            ESP_LOGW(TAG, "Ошибка чтения %s (0x%02X)", pll_reg_names[i], pll_reg_addrs[i]);
        }
    }

    uint8_t pll_lock = 0;
    if (ax5043_read_reg(AX5043_REG_PLLRANGINGA, &pll_lock) == 0) {
        bool pll_locked = (pll_lock & PLLRANGINGA_PLLLOCK) != 0;
        ESP_LOGI(TAG, "PLL Status: %s", pll_locked ? "LOCKED" : "UNLOCKED");
    } else {
        ESP_LOGW(TAG, "Ошибка чтения REG_PLLRANGINGA (0x%02X)", AX5043_REG_PLLRANGINGA);
    }
    
    ESP_LOGI(TAG, "=== Конец диагностики VCO ===");
}


int ax5043_get_fifo_free(uint16_t *free_bytes) {
    uint8_t free_l, free_h;
    
    if (ax5043_read_reg(AX5043_REG_FIFOFREE0, &free_l) != 0) return -1;
    if (ax5043_read_reg(AX5043_REG_FIFOFREE1, &free_h) != 0) return -1;
    
    *free_bytes = ((uint16_t)(free_h & 0x01) << 8) | free_l;
    return 0;
}

/**
 * @brief Запись данных в FIFO AX5043
 * 
 * @param data      Указатель на буфер данных
 * @param length    Количество байт для записи в FIFO
 * @return int      0 при успехе, -1 при ошибке
 * 
 * @note Все байты записываются в регистр FIFODATA (0x029)
 * @note Адрес НЕ инкрементируется (особое поведение, Table 65)
 */
int ax5043_write_fifo(const uint8_t *data, size_t length) {
    if (data == NULL || length == 0) {
        return -1;
    }
    
    // Проверяем, что в FIFO есть место
    uint16_t fifo_free = 0;
    if (ax5043_get_fifo_free(&fifo_free) != 0) {
        return -1;
    }
    if (length > fifo_free) {
        ESP_LOGW(TAG, "Недостаточно места в FIFO: нужно %u, есть %u", 
                 (unsigned)length, fifo_free);
        return -1;
    }
    
     //Подготовка SPI-транзакции 
    spi_transaction_t trans = {0};
    uint8_t *tx_buf = NULL;
    bool use_external_buf = (length > 3);  // >3 байта → нужен внешний буфер
    
    // Формируем первый байт: адрес FIFODATA + бит записи
    uint8_t addr_byte = (AX5043_REG_FIFODATA & 0x7F) | 0x80;
    
    if (use_external_buf) {
        // Для >3 байт: выделяем внешний буфер 
        tx_buf = malloc(1 + length);
        if (tx_buf == NULL) {
            ESP_LOGE(TAG, "Не удалось выделить память для FIFO");
            return -1;
        }
        tx_buf[0] = addr_byte;
        memcpy(&tx_buf[1], data, length);
        
        trans.length = (1 + length) * 8;
        trans.tx_buffer = tx_buf;
        trans.rx_buffer = NULL;
        // SPI_TRANS_USE_TXDATA не устанавливаем
    } else {
        // Для ≤3 байт: используем встроенный tx_data[4] 
        trans.length = (1 + length) * 8;  // макс 32 бита
        trans.flags = SPI_TRANS_USE_TXDATA;
        trans.tx_data[0] = addr_byte;
        memcpy(&trans.tx_data[1], data, length);
    }
    
    //Выполнение SPI-транзакции
    gpio_set_level(AX5043_PIN_CS, 0);
    esp_err_t ret = spi_device_transmit(ax5043_ctx.spi_handle, &trans);
    gpio_set_level(AX5043_PIN_CS, 1);
    
    // Освобождаем память если использовали внешний буфер
    if (use_external_buf && tx_buf) {
        free(tx_buf);
    }
    
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Ошибка SPI при записи в FIFO (%u байт): %s", 
                 (unsigned)length, esp_err_to_name(ret));
        return -1;
    }
    
    ESP_LOGV(TAG, "Запись в FIFO: %u байт, успешно", (unsigned)length);
    return 0;
}

/**
 * @brief Запуск генерации несущей
 */
int ax5043_start_cw_transmission_internal(void)
{
    if (!ax5043_ctx.initialized) {
        ESP_LOGE(TAG, "AX5043 не инициализирован");
        return -1;
    }
    
    ESP_LOGI(TAG, "Запуск генерации несущей");

    // Переход в режим SYNTHTX
    if (ax5043_write_reg(AX5043_REG_PWRMODE, 
        PWRMODE_SYNTHTX | PWRMODE_XOEN | PWRMODE_REFEN) != 0) {  // 0b01101100 - SYNTHTX | XOEN | REFEN
        ESP_LOGE(TAG, "Ошибка перехода в режим SYNTHTX");
        return -1;
    }

    ESP_LOGI(TAG, "Перешли в режим SYNTHTX");
    
    // Задержка для стабилизации
    vTaskDelay(pdMS_TO_TICKS(AX5043_PLL_SETTLE_MS));
    
    // Запуск автоподстройки VCO
    if (ax5043_run_vco_ranging() != 0) {
        ESP_LOGE(TAG, "Ошибка автоподстройки VCO");
        return -1;
    }
    
    // Переход в режим FULLTX
    if (ax5043_write_reg(AX5043_REG_PWRMODE, 
        PWRMODE_FULLTX | PWRMODE_XOEN | PWRMODE_REFEN) != 0) {  // 0b01101101 - FULLTX | XOEN | REFEN
        ESP_LOGE(TAG, "Ошибка перехода в режим FULLTX");
        return -1;
    }
    
    // Задержка для стабилизации
    vTaskDelay(pdMS_TO_TICKS(AX5043_PLL_SETTLE_MS));

    ax5043_dump_power_status();
    
    // Заполнение FIFO для генерации CW
    // Проверка свободного места в FIFO
    uint8_t fifo_free = 0;
    if (ax5043_read_reg(AX5043_REG_FIFOFREE0, &fifo_free) != 0) {
        ESP_LOGE(TAG, "Ошибка чтения REG_FIFOFREE0");
        return -1;
    }
    ESP_LOGI(TAG, "REG_FIFOFREE0: %d", fifo_free);  
    if (ax5043_read_reg(AX5043_REG_FIFOFREE1, &fifo_free) != 0) {
        ESP_LOGE(TAG, "Ошибка чтения REG_FIFOFREE1");
        return -1;
    }
    ESP_LOGI(TAG, "REG_FIFOFREE1: %d", fifo_free);  

    uint8_t rstatus = 255;
    if (ax5043_get_radiostatus(&rstatus) == 0) {
        ESP_LOGI(TAG, "Радио статус: %d", rstatus);
    }

    uint8_t fifo_status = 255;
    if(ax5043_read_reg(AX5043_REG_FIFOSTAT, &fifo_status) == 0) {
        ESP_LOGI(TAG, "FIFO статус: %d", fifo_status);
    }

  
    // Запись команды REPEATDATA в FIFO
    // REPEATDATA, 0x28 (UNENC=1, NOCRC=1), повторов=255, данные=0x55
    uint8_t fifo_cmd[4] = {FIFO_CMD_REPEATDATA, 0x28, 255, 0x55};  
    if (ax5043_write_fifo(fifo_cmd, sizeof(fifo_cmd)/sizeof(uint8_t)) != 0) {
        ESP_LOGE(TAG, "Ошибка записи CMD в FIFO");
        return -1;
    }
    
    // Команда COMMIT
    if (ax5043_write_reg(AX5043_REG_FIFOSTAT, FIFO_CMD_COMMIT) != 0) {
        ESP_LOGE(TAG, "Ошибка записи COMMIT в FIFO");
        return -1;
    }
    ESP_LOGI(TAG, "COMMIT в FIFO");

    ESP_LOGI(TAG, "Генерация несущей запущена");

    if(ax5043_read_reg(AX5043_REG_FIFOSTAT, &fifo_status) == 0) {
        ESP_LOGI(TAG, "FIFO статус: %d", fifo_status);
    }

    if (ax5043_get_radiostatus(&rstatus) == 0) {
        ESP_LOGI(TAG, "Радио статус: %d", rstatus);
    }
    
    return 0;
}

/**
 * @brief Остановка генерации несущей
 */
int ax5043_stop_transmission(void)
{
    if (!ax5043_ctx.initialized) {
        ESP_LOGE(TAG, "AX5043 не инициализирован");
        return -1;
    }
    
    ESP_LOGI(TAG, "Остановка генерации несущей");
    
    // Перевод в режим POWERDOWN
    if (ax5043_write_reg(AX5043_REG_PWRMODE, 0x00) != 0) {  // 0x00 - POWERDOWN
        ESP_LOGE(TAG, "Ошибка остановки генерации");
        return -1;
    }
    
    ESP_LOGI(TAG, "Генерация несущей остановлена");
    return 0;
}

/**
 * @brief Вывод в лог состояний всех регистров
 */
void ax5043_dump_registers(void)
{
    ESP_LOGI(TAG, "=== AX5043 Register Dump ===");
    
    uint8_t reg_val = 0;
    
    // Вывод основных регистров
    const char* reg_names[] = {
        "PWRMODE", "POWSTAT", "XTALSTATUS", "PLLVCODIV", "FREQA0"
    };
    uint8_t reg_addrs[] = {
        AX5043_REG_PWRMODE, AX5043_REG_POWSTAT, AX5043_REG_XTALSTATUS, 
        AX5043_REG_PLLVCODIV, AX5043_REG_FREQA0
    };
    
    for (int i = 0; i < sizeof(reg_addrs)/sizeof(reg_addrs[0]); i++) {
        if (ax5043_read_reg(reg_addrs[i], &reg_val) == 0) {
            ESP_LOGI(TAG, "%s (0x%02X): 0x%02X", reg_names[i], reg_addrs[i], reg_val);
        } else {
            ESP_LOGW(TAG, "Ошибка чтения %s (0x%02X)", reg_names[i], reg_addrs[i]);
        }
    }
    
    ESP_LOGI(TAG, "=== Конец дампа регистров ===");
}



/**
 * @brief Деинициализация драйвера AX5043
 */
void ax5043_deinit(void)
{
    ESP_LOGI(TAG, "Деинициализация AX5043");
    
    // Остановка передатчика
    ax5043_stop_transmission();
    
    // Деинициализация SPI
    ax5043_spi_deinit();
    
    // Сброс контекста
    memset(&ax5043_ctx, 0, sizeof(ax5043_ctx));
    
    ESP_LOGI(TAG, "AX5043 деинициализирован");
}


int ax5043_get_radiostatus(uint8_t *rstatus) {
    if (ax5043_read_reg(AX5043_REG_RADIOSTATE, rstatus) == 0) {
        return 0;
    } else {
        ESP_LOGE(TAG, "Ошибка чтения REG_RADIOSTATE (0x%02X)", AX5043_REG_RADIOSTATE);
        return -1;
    }
}