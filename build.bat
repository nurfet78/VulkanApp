@echo off
REM -----------------------------
REM Полная генерация и сборка проекта Visual Studio
REM -----------------------------

REM Консоль в UTF-8
chcp 65001 > nul

REM Папка сборки
set BUILD_DIR=build

REM Определяем конфигурацию (Debug по умолчанию)
set CONFIG=Debug
if not "%1"=="" set CONFIG=%1

echo Конфигурация сборки: %CONFIG%

REM Проверка, что запускаем из корня проекта
IF NOT EXIST CMakeLists.txt (
    echo Ошибка: скрипт должен запускаться из корня проекта!
    pause
    exit /b 1
)

REM Удаляем старую папку сборки
IF EXIST %BUILD_DIR% (
    echo Удаляем старую папку %BUILD_DIR%...
    rmdir /s /q %BUILD_DIR%
    IF %ERRORLEVEL% NEQ 0 (
        echo Ошибка при удалении папки!
        pause
        exit /b %ERRORLEVEL%
    )
) ELSE (
    echo Папка %BUILD_DIR% отсутствует, продолжаем...
)

REM Генерация проекта для Visual Studio
echo Генерируем проект Visual Studio (%CONFIG%)...
cmake -S . -B %BUILD_DIR% -G "Visual Studio 17 2022" -A x64 -DCMAKE_BUILD_TYPE=%CONFIG%
IF %ERRORLEVEL% NEQ 0 (
    echo Ошибка при генерации проекта!
    pause
    exit /b %ERRORLEVEL%
)

REM Сборка проекта
echo Собираем проект (%CONFIG%)...
cmake --build %BUILD_DIR% --config %CONFIG%
IF %ERRORLEVEL% NEQ 0 (
    echo Ошибка при сборке!
    pause
    exit /b %ERRORLEVEL%
)

echo.
echo Проект собран! Открывайте %BUILD_DIR%\VulkanSandbox.sln
pause
