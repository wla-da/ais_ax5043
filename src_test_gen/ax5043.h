/**
 * @file ax5043.h
 * @brief Заголовочный файл драйвера AX5043
 * 
 * Содержит определения регистров, константы и прототипы функций
 * для управления радиочипом AX5043
 */

#ifndef AX5043_H
#define AX5043_H

#include <stdint.h>
#include <stdbool.h>
#include "driver/spi_master.h"
#include "driver/gpio.h"

/*============================================================================
 * ОПРЕДЕЛЕНИЯ PIN-ОВ SPI
 *============================================================================*/
#define AX5043_PIN_MOSI     GPIO_NUM_11
#define AX5043_PIN_MISO     GPIO_NUM_13
#define AX5043_PIN_CLK      GPIO_NUM_12
#define AX5043_PIN_CS       GPIO_NUM_10

/*============================================================================
 * КОНСТАНТЫ ЧАСТОТ
 *============================================================================*/
#define AX5043_XTAL_FREQ_HZ     19200000ULL    // 19.2 MHz TCXO
#define AX5043_CARRIER_FREQ_HZ  161975000ULL   // 161.975 MHz
#define AX5043_TARGET_POWER_DBM (-10)          // -10 dBm

/*============================================================================
 * АДРЕСА РЕГИСТРОВ AX5043 (короткие адреса)
 *============================================================================*/
// Регистры идентификации и тестирования
#define AX5043_REG_REVISION      0x000   // Silicon Revision (read-only, expected: 0x51)
#define AX5043_REG_SCRATCH       0x001   // Scratch register (read/write, for testing)

// Регистры управления питанием и состоянием
#define AX5043_REG_PWRMODE       0x002
#define AX5043_REG_POWSTAT       0x003
#define AX5043_REG_POWSTICKYSTAT 0x004
#define AX5043_REG_POWIRQMASK    0x005
#define AX5043_REG_XTALSTATUS    0x01D
#define AX5043_REG_RADIOSTATE    0x01C

// Регистры PLL
#define AX5043_REG_PLLLOOP      0x030
#define AX5043_REG_PLLCPI       0x031
#define AX5043_REG_PLLVCODIV    0x032
#define AX5043_REG_PLLRANGINGA  0x033
#define AX5043_REG_FREQA3       0x034
#define AX5043_REG_FREQA2       0x035
#define AX5043_REG_FREQA1       0x036
#define AX5043_REG_FREQA0       0x037
#define AX5043_REG_PLLLOOPBOOST 0x038
#define AX5043_REG_PLLCPIBOOST  0x039

// Регистры модуляции и передатчика
#define AX5043_REG_MODULATION   0x010
#define AX5043_REG_FSKDEV2      0x161
#define AX5043_REG_FSKDEV1      0x162
#define AX5043_REG_FSKDEV0      0x163
#define AX5043_REG_MODCFGA      0x164
#define AX5043_REG_TXPWRCOEFFB1 0x16A
#define AX5043_REG_TXPWRCOEFFB0 0x16B

// Регистры VCO
#define AX5043_REG_PLLVCOI      0x180
#define AX5043_REG_XTALCAP      0x184

/*============================================================================
 * ТАЙМИНГИ
 *============================================================================*/
#define AX5043_RESET_DELAY_US 100       // Задержка после сброса (мкс)
#define AX5043_XTAL_STARTUP_MS 5        // Время запуска TCXO (мс)
#define AX5043_PLL_SETTLE_MS 10         // Время захвата PLL (мс)
#define AX5043_VCO_RANGING_TIMEOUT_MS 100 // Таймаут автоподстройки VCO (мс)
#define AX5043_SPI_CLOCK_HZ 5000000 // Частота SPI (5 MHz)

// Регистры FIFO
#define AX5043_REG_FIFOSTAT     0x028
#define AX5043_REG_FIFODATA     0x029
#define AX5043_REG_FIFOFREE1    0x02C
#define AX5043_REG_FIFOFREE0    0x02D

/*============================================================================
 * ДЛИННЫЕ АДРЕСА РЕГИСТРОВ (16-бит)
 *============================================================================*/
#define AX5043_REG_PERF_F00         0xF00 //Set to 0x0F
#define AX5043_REG_PERF_F0C         0xF0C //Keep the default 0x00
#define AX5043_REG_PERF_F0D         0xF0D  //Set to 0x03
#define AX5043_REG_IBIAS            0xF10
#define AX5043_REG_IBIAS_AUTOREG    0xF11
#define AX5043_REG_PERF_F1C         0xF1C //Set to 0x07
#define AX5043_REG_PERF_F34         0xF34  // PERF register: 0x28 if RFDIV=1, 0x08 if RFDIV=0
#define AX5043_REG_XTALDIV          0xF35
#define AX5043_REG_PERF_F44         0xF44 //Set to 0x24

#define AX5043_REG_PLLVCOIR         0x181  // VCO Current Readback (short address)

/*============================================================================
 * БИТЫ РЕГИСТРА PWRMODE (Таблица 33, стр. 33)
 *============================================================================*/
#define PWRMODE_RST     (1 << 7)   // Бит 7: Сброс
#define PWRMODE_XOEN    (1 << 6)   // Бит 6: Crystal Oscillator Enable
#define PWRMODE_REFEN   (1 << 5)   // Бит 5: Reference Enable
#define PWRMODE_WDS     (1 << 4)   // Бит 4: Wakeup from Deep Sleep, только чтение!

// Режимы питания PWRMODE[3:0] (Таблица 33)
#define PWRMODE_POWERDOWN  0b0000    // 0000: Powerdown
#define PWRMODE_DEEPSLEEP  0b0001    // 0001: Deep Sleep
#define PWRMODE_STANDBY    0b0101    // 0101: Crystal Oscillator enabled
#define PWRMODE_FIFOEN     0b0111    // 0111: FIFO enabled
#define PWRMODE_SYNTHRX    0b1000    // 1000: Synthesizer running, Receive Mode
#define PWRMODE_FULLRX     0b1001    // 1001: Receiver Running
#define PWRMODE_WOR        0b1011    // 1011: Receiver wakeup−on−radio mode
#define PWRMODE_SYNTHTX    0b1100    // 1100: Synthesizer running, Transmit Mode
#define PWRMODE_FULLTX     0b1101    // 1101: Transmitter Running

/*============================================================================
 * БИТЫ РЕГИСТРА PLLVCODIV (Таблица 72, стр. 46)
 *============================================================================*/
#define PLLVCODIV_REFDIV_MASK  0x03     // Биты 1-0: REFDIV
#define PLLVCODIV_RFDIV        (1 << 2) // Бит 2: RF divider (0=off, 1=divide by 2)
#define PLLVCODIV_VCOSEL       (1 << 4) // Бит 4: VCO select (0=VCO1, 1=VCO2)
#define PLLVCODIV_VCO2INT      (1 << 5) // Бит 5: VCO2 internal (1=internal with ext inductor)

/*============================================================================
 * БИТЫ РЕГИСТРА PLLRANGINGA (Таблица 74, стр. 46)
 *============================================================================*/
#define PLLRANGINGA_VCORA_MASK     0x0F     // Биты 3-0: VCO Range
#define PLLRANGINGA_RNGSTART       (1 << 4) // Бит 4: Запуск автоподстройки
#define PLLRANGINGA_RNGERR         (1 << 5) // Бит 6: Ошибка автоподстройки
#define PLLRANGINGA_PLLLOCK        (1 << 6) // Бит 7: PLL захвачен
#define PLLRANGINGA_STYCKY_LOCK    (1 << 7) // Бит 7: 0 - если PLL захвачен, 1 - (сброс) при чтении регистра

/*============================================================================
 * БИТЫ РЕГИСТРА MODCFGA (Таблица 22, стр. 28)
 *============================================================================*/
#define MODCFGA_TXDIFF     (1 << 0)  // Бит 0: Differential TX
#define MODCFGA_TXSE       (1 << 1)  // Бит 1: Single-Ended TX
#define MODCFGA_AMPLSHAPE  (1 << 2)  // Бит 2: Raised cosine shaping

/*============================================================================
 * БИТЫ РЕГИСТРА XTALSTATUS (стр. 22)
 *============================================================================*/
#define XTALSTATUS_XTALRUN  (1 << 0)  // Бит 0: кварцевый генератор запущен

/*============================================================================
 * ТАЙМИНГИ
 *============================================================================*/
#define AX5043_RESET_DELAY_US        100    // Задержка после сброса (мкс)
#define AX5043_XTAL_STARTUP_MS       5      // Время запуска TCXO (мс)
#define AX5043_PLL_SETTLE_MS         10     // Время захвата PLL (мс)
#define AX5043_VCO_RANGING_TIMEOUT_MS 100   // Таймаут автоподстройки VCO (мс)
#define AX5043_SPI_CLOCK_HZ 5000000 // Частота SPI (5 MHz)

/*============================================================================
 * КОНСТАНТЫ МОДУЛЯЦИИ (Таблица 36, стр. 36)
 *============================================================================*/
#define MODULATION_FSK 0x08 // FSK модуляция (bits 3:0 = 1000)

/*============================================================================
 * КОНСТАНТЫ ДЛЯ PERF РЕГИСТРОВ
 *============================================================================*/
#define AX5043_PERF_F34_RFDIV_OFF 0x08  // RFDIV = 0 (делитель выключен)
#define AX5043_PERF_F34_RFDIV_ON  0x28  // RFDIV = 1 (делитель включен)

/*============================================================================
 * КОНСТАНТЫ МОЩНОСТИ
 *============================================================================*/
#define AX5043_POWER_MINUS10_DBM 0x0156  // Значение TXPWRCOEFFB для -10 dBm

/*============================================================================
 * FIFO КОМАНДЫ
 *============================================================================*/
#define FIFO_CMD_REPEATDATA 0b01100010 //0xC2  // REPEATDATA команда (repeat data byte)
#define FIFO_CMD_COMMIT      0b000100  // COMMIT команда (передача данных из FIFO)

/*============================================================================
 * СТРУКТУРЫ ДАННЫХ
 *============================================================================*/
/**
 * @brief Контекст драйвера AX5043
 */
typedef struct {
    spi_device_handle_t spi_handle;   // Дескриптор SPI устройства
    bool initialized;                  // Флаг инициализации
    uint32_t xtal_freq;                // Частота кварца (Гц)
    uint32_t carrier_freq;             // Частота несущей (Гц)
    int8_t power_dbm;                  // Мощность (дБм)
} ax5043_context_t;

/*============================================================================
 * ПРОТОТИПЫ ФУНКЦИЙ
 *============================================================================*/
/**
 * @brief Инициализация драйвера AX5043
 * @return 0 при успехе, отрицательный код ошибки при неудаче
 */
int ax5043_init(void);

/**
 * @brief Деинициализация драйвера AX5043
 */
void ax5043_deinit(void);

/**
 * @brief Программный сброс AX5043 через регистр PWRMODE
 * @return 0 при успехе, отрицательный код ошибки при неудаче
 */
int ax5043_software_reset(void);

/**
 * @brief Установка несущей частоты
 * @param freq_hz Частота в Гц
 * @return 0 при успехе, отрицательный код ошибки при неудаче
 */
int ax5043_set_frequency(uint32_t freq_hz);

/**
 * @brief Установка выходной мощности передатчика
 * @param power_dbm Мощность в дБм
 * @return 0 при успехе, отрицательный код ошибки при неудаче
 */
int ax5043_set_power(int8_t power_dbm);

/**
 * @brief Запуск генерации несущей
 * @return 0 при успехе, отрицательный код ошибки при неудаче
 */
int ax5043_start_cw_transmission_internal(void);

/**
 * @brief Остановка генерации несущей
 * @return 0 при успехе, отрицательный код ошибки при неудаче
 */
int ax5043_stop_transmission(void);

/**
 * @brief Вывод в лог состояний всех регистров
 */
void ax5043_dump_registers(void);

/**
 * @brief Чтение регистра AX5043 (короткий адрес)
 * @param addr Адрес регистра
 * @param data Указатель на буфер для данных
 * @return 0 при успехе, отрицательный код ошибки при неудаче
 */
int ax5043_read_reg(uint8_t addr, uint8_t *data);

/**
 * @brief Запись в регистр AX5043 (короткий адрес)
 * @param addr Адрес регистра
 * @param data Данные для записи
 * @return 0 при успехе, отрицательный код ошибки при неудаче
 */
int ax5043_write_reg(uint8_t addr, uint8_t data);

/**
 * @brief Чтение длинного регистра AX5043 (16-бит адрес)
 * @param addr 16-бит адрес регистра
 * @param data Указатель на буфер для данных
 * @return 0 при успехе, отрицательный код ошибки при неудаче
 */
int ax5043_read_long_reg(uint16_t addr, uint8_t *data);

/**
 * @brief Запись в длинный регистр AX5043 (16-бит адрес)
 * @param addr 16-бит адрес регистра
 * @param data Данные для записи
 * @return 0 при успехе, отрицательный код ошибки при неудаче
 */
int ax5043_write_long_reg(uint16_t addr, uint8_t data);

/**
 * @brief Диагностика VCO: чтение всех регистров PLL
 */
void ax5043_vco_diagnostic(void);

/**
 * @brief получение статуса из регистра RADIOSTATE
 *  Bits    Meaning
    0000    Idle
    0001    Powerdown
    0100    Tx PLL Settings
    0110    Tx
    0111    Tx Tail
    1000    Rx PLL Settings
    1001    Rx Antenna Selection
    1100    Rx Preamble 1
    1101    Rx Preamble 2
    1110    Rx Preamble 3
    1111    Rx
 */
int ax5043_get_radiostatus(uint8_t *rstatus);

#endif // AX5043_H
