# MotoKey

MotoKey — проект программного обеспечения для контроллера на ESP32: прошивка, Android-приложение и Windows-инструмент MotoKey Flasher.

**Страница проекта:** https://sdvg001.github.io/MotoKey-Firmware/  
**Подробная инструкция Android и Windows Flasher:** https://sdvg001.github.io/MotoKey-Firmware/instructions.html

## Текущие версии

| Компонент | Версия | Статус |
|---|---:|---|
| Прошивка ESP32 | **CORE 3.2.0 — Shared Token** | Опубликована |
| Windows Flasher | **1.5.0** | Опубликован — [скачать Setup x64](https://github.com/sdvg001/MotoKey-Firmware/releases/download/apps-v1.1.0/MotoKeyFlasher-Setup-1.5.0-x64.exe) |
| Android-приложение | **1.1.0+4** | Опубликовано — [скачать APK](https://github.com/sdvg001/MotoKey-Firmware/releases/download/apps-v1.1.0/app-release.apk) |

## Скачать приложения

Оба установочных файла опубликованы в релизе [MotoKey Apps — Android 1.1.0+4 / Windows Flasher 1.5.0](https://github.com/sdvg001/MotoKey-Firmware/releases/tag/apps-v1.1.0).

- [Android APK 1.1.0+4](https://github.com/sdvg001/MotoKey-Firmware/releases/download/apps-v1.1.0/app-release.apk)
- [Windows Flasher Setup 1.5.0 x64](https://github.com/sdvg001/MotoKey-Firmware/releases/download/apps-v1.1.0/MotoKeyFlasher-Setup-1.5.0-x64.exe)

## Скачать прошивку

[**MotoKey CORE 3.2.0 — Shared Token**](https://github.com/sdvg001/MotoKey-Firmware/releases/tag/v3.2.0)

Релиз включает bootloader.bin, partitions.bin, boot_app0.bin, motokey.bin и файл SHA-256. Для обычного обновления рекомендуется использовать MotoKey Flasher: он получает список версий из manifest.json, скачивает недостающие файлы и проверяет их контрольные суммы перед записью.

## Важное перед прошивкой

- Поддерживаемая плата: классический ESP32 с Flash 4 MB.
- Перед проверкой ESP32 закрой Arduino Serial Monitor и другие программы, которые могут занимать COM-порт.
- При обычном обновлении оставляй выключенным флажок **«Сбросить старую привязку и создать новую»**.
- Не используй полное стирание Flash. Flasher проверяет совместимость таблицы разделов/NVS и прекращает операцию при несовпадении.
- После прошивки сначала проверь контроллер на стенде. Не подключай стартер до проверки работы системы.

## Что изменилось в CORE 3.2.0

- До трёх одновременных BLE-подключений.
- Один общий токен используется подключёнными телефонами.
- Второй и третий телефон получают существующий общий токен после ввода pairing-кода.
- Сценарий подтверждения нового доверенного телефона с первого телефона не используется.
- Разметка раздела NVS совпадает с опубликованной CORE 3.1.0; обычное обновление не запрашивает erase_flash.

## История прошивок

- **3.2.0** — Shared Token: общий токен для нескольких телефонов, до трёх BLE-подключений.
- **3.1.0** — Multi Client: несколько BLE-клиентов и прежняя схема доверенных устройств.

Полные сведения и файлы доступны на странице [Releases](https://github.com/sdvg001/MotoKey-Firmware/releases).

## Инструкции и поддержка

Подробное руководство пользователя по установке и подключению Android-приложения, работе с общим кодом для нескольких телефонов, управлению и настройкам, а также по установке и использованию Windows Flasher размещено на [странице инструкций](https://sdvg001.github.io/MotoKey-Firmware/instructions.html).

Сообщения об ошибках и вопросы по проекту: [GitHub Issues](https://github.com/sdvg001/MotoKey-Firmware/issues).

> APK Android и установщик Windows опубликованы в официальном [релизе приложений](https://github.com/sdvg001/MotoKey-Firmware/releases/tag/apps-v1.1.0). Скачивай установочные файлы только по ссылкам выше или со страницы проекта.
