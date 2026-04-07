# AIS приемник на базе AX5043
## Задачи
1. Научиться работать с радиочипом AX5043 при помощи ИИ-инструментов
2. Разработать и протестировать схему ВЧ части AX5043 для приема AIS сигнала
3. Разработать ПО для работы в связке ESP32 и AX5043
4. Провести тестирование AIS приемника, измерить его чувствительность, энергопотребление


## Железо
1. Плата [ESP32-S3-Zero](https://cnx-software.ru/2024/08/07/waveshare-esp32-s3-zero-eto-miniatyurnyj-modul-wifi-i-ble-iot-s-portom-usb-c-do-32-gpio/)
2. Радиочип AX5043 с самодельной обвязкой: TXCO, LDO, ВЧ-часть и тп
   

## Софт
1. Операционная система Ubuntu Ubuntu 24.04.3 LTS
2. Visual Studio Code v1.108.2
3. Плагин [Continue](https://marketplace.visualstudio.com/items?itemName=Continue.continue) для VS Code
4. Локальные MCP сервера: [Platformio MCP](https://github.com/jl-codes/platformio-mcp), [Serial monitor MCP](https://github.com/Adancurusul/serial-mcp-server), [GitHub MCP](https://github.com/github/github-mcp-server), [Document Parser MCP](https://github.com/agenson-tools/document-parser-mcp) от agenson, [Docling MCP](https://github.com/docling-project/docling-mcp) (использует [Docling](https://www.docling.ai/)), интегрированные с Continue/VS Code
5. Различные облачные LLM: ChatGPT, Deepseek, Qwen, Prism и другие


## Вводная
После многочисленных попыток "завести" плату [E32 170T30D для приема AIS сигнала](https://github.com/wla-da/ais_170t30d) (получилось, но недостаточная чувствительность на уровне порядка -85 дБм в пакетном режиме) решил попробовать другие варианты. Чип AX5043 по описанию подходит замечательно: умеет работать с кастомной преамбулой, может производить дифференциальное кодирование/декодирование (считай тоже NRZI), умеет искать именно HDLC пакеты и проверять нужную CRC-16-CCITT/X-25. А еще может выдавать IQ сигнал, может работать на частотах до 27 МГц, может выдавать аналоговый сигнал (FM-приемник) и тп. 

К сожалению, данный чип снят с производства и больше не поддерживается производителем. Например, мне так и не получилось найти и скачать фирменную утилиту AX RadioLab для настройки регистров чипа.

![ESP32 S3 Zero](img/esp32s3zero.png)

Рис 1. Плата ESP32-S3-Zero 



## TODO железо
1. Управлять входами EN для TXCO и радиочипа с MCU для снижения энергопотребления
2. Управлять входом Vcon TXCO (корректировка частоты) с MCU через RC-фильтр для компенсации старения и тп


## Полезные ссылки
1. Кросс-платформенный [драйвер AX5043](https://gitlab.com/librespacefoundation/ax5043-driver/) и [УКВ-приемник](https://gitlab.com/librespacefoundation/pq9ish/pq9ish-comms-vu-sw) с использованием данного драйвера
2. Двухдиапазонный [УКВ-трансивер на базе AX5043](https://github.com/NotBlackMagic/VUHFRadio) с собственным уникальным драйвером и детальным описанием [AX5043 от NotBlackMagic](https://www.notblackmagic.com/bitsnpieces/ax5043/), включая получение IQ сигнала, аналоговую де/модуляцию ЧМ и тп
3. Еще один УКВ трансивер и [драйвер AX5043](https://github.com/richardeoin/ax/), заточен под Linux + Python и [Raspberry Pi](https://github.com/richardeoin/ax/tree/master/hw/pi)
4. Подборка [материалов по AX5043](https://www.bdtic.com/en/on/AX5043) (битые ссылки?)
