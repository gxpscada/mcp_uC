# Служебные материалы

Исходники прошивки и файлы STM32CubeIDE остались в корне проекта и в `Core`,
`Drivers`, `Middlewares`, `USB_DEVICE`, `targets`.

- `scripts/` — сборка F411/F407; команды запускаются из каталога `pt100`.
- `tests/` — тесты и модель периферии.
- `tools/` — клиент Modbus и старый инструмент захвата ASCII.
- `docs/` и `Doxyfile` — исходники документации Doxygen.
- `reference/` — исходное ТЗ.
- `archive/` — прежние исходники, описание этапа и расчёты Scilab; пути внутри архивных материалов могут быть историческими.
- `local/` — локальные результаты и инструменты, **исключены из Git**: компилятор,
  ZIP toolchain, сборки HEX/ELF, прежняя Debug-сборка, логи и сгенерированная документация.

Тесты, скрипты и исходники документации можно коммитить вместе с прошивкой.
Результаты тестов `tests/build/` и кэш Python тоже исключены из Git.

Из каталога `pt100`:

```powershell
./service/scripts/build_firmware.ps1 -Target F411
./service/scripts/build_firmware.ps1 -Target F407
./service/tests/run_firmware_tests.cmd
python service/tests/test_client.py
doxygen service/Doxyfile
```

Сборки появляются в `service/local/build-f411/` и `service/local/build-f407/`,
HTML-документация — в `service/local/docs/html/index.html`.
После клонирования для сборки нужен GNU Arm в PATH либо аргумент `-ToolchainBin`;
локальный компилятор намеренно не включается в репозиторий.
