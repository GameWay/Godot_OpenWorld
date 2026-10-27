# Сборка OEngine на Windows

В этом каталоге хранятся конфигурации сборки, PowerShell-скрипты, генератор Rider
и шаблоны локальных настроек. Они входят в репозиторий движка.

По умолчанию рабочий каталог находится на один уровень выше корня репозитория:

```text
workspace/
  Source/                   # этот репозиторий; имя папки может быть другим
    tools/oengine/
  .tools/                   # локальные инструменты и зависимости
  Package/                  # результаты сборки
  NuGet.Config              # локальные источники пакетов
```

## Требования и восстановление окружения

Нужны Windows, MSVC с поддерживаемым Windows SDK, Python с пакетами из
`requirements-build.txt`, а для Mono также подходящий .NET SDK. Rider нужен
только для работы в IDE. Эти скрипты не устанавливают инструменты автоматически.

На текущей машине используются MSVC v145, Windows SDK 10.0.28000.0, Python 3.14,
.NET SDK 10.0.401 и Rider 2026.2. Набор Python-пакетов закреплён в файле требований.
После установки Python окружение можно подготовить из корня репозитория:

```powershell
py -3 -m venv ..\.tools\python
..\.tools\python\Scripts\python.exe -m pip install -r tools\oengine\requirements-build.txt
```

Нативные зависимости должны быть подготовлены в `.tools/Godot/build_deps`:
`accesskit`, `mesa`, `agility_sdk`, `pix` и `angle`. На другом компьютере нужно
скопировать проверенный комплект или отдельно подготовить совместимые SDK.
Скрипты не являются установщиком этих SDK и не включают их в Git.

При необходимости перед запуском можно задать переменные окружения:

| Переменная | Назначение |
| --- | --- |
| `OENGINE_WORKSPACE` | Каталог `.tools` и `Package`; по умолчанию родитель репозитория |
| `OENGINE_PYTHON` | Путь к Python с установленным SCons; сначала выбирается локальный venv, затем Python из PATH |
| `OENGINE_DEPS` | Каталог нативных SDK вместо `.tools/Godot/build_deps` |
| `OENGINE_RIDER` | Путь к `rider64.exe`, если Rider установлен в другом месте |
| `OENGINE_PLATFORM_TOOLSET` | Необязательное переопределение MSBuild toolset, например `v145` |

Также можно передать `-Workspace` непосредственно в `Godot.ps1`.
Если в рабочем каталоге нет `NuGet.Config`, он создаётся из шаблона. Существующий
конфиг не перезаписывается. Для Mono он должен разрешать `.tools/nuget-local`
и nuget.org. Глобальные настройки NuGet не меняются.

## Конфигурации

| Конфигурация solution и Run/Debug | .NET | Путь внутри Package |
| --- | --- | --- |
| Engine | Нет | Gd/Dev/oengine.editor.dev.exe |
| Engine Shipping | Нет | Gd/Shipping/oengine.editor.exe |
| Engine Mono | Да | Mono/Dev/oengine.editor.dev.mono.exe |
| Engine Mono Shipping | Да | Mono/Shipping/oengine.editor.mono.exe |

Все варианты собирают редактор (`target=editor`). Shipping использует
`dev_build=no production=yes optimize=speed lto=none debug_symbols=yes`.
Dev использует `dev_build=yes optimize=none debug_symbols=yes`.
Список вариантов хранится только в `EngineConfigurations.json` этого каталога.

## Команды

Из корня репозитория:

```powershell
.\tools\oengine\Godot.ps1 -Action Projects
.\tools\oengine\Godot.ps1 -Action DryRun -Configuration 'Engine Shipping'
.\tools\oengine\Godot.ps1 -Action Build -Configuration 'Engine'
.\tools\oengine\Godot.ps1 -Action Build -Configuration 'Engine Shipping'
.\tools\oengine\Godot.ps1 -Action Build -Configuration 'Engine Mono'
.\tools\oengine\Godot.ps1 -Action Build -Configuration 'Engine Mono Shipping'
```

`Projects` создаёт solution, проекты, свойства всех четырёх конфигураций и профили
Rider без компиляции движка. После генерации откройте `godot.sln` в корне
репозитория. Если solution уже открыт, переоткройте его.

`DryRun` показывает план SCons без компиляции. Он не проверяет запуск редактора
и не собирает managed-библиотеки Mono.

Build только собирает выбранный вариант и публикует его файлы в Package.
Run/Play и Debug сначала выполняют Build, затем запускают выбранный редактор.
Профили по умолчанию используют `--project-manager`; изменённые пользователем
аргументы запуска сохраняются при повторной генерации.

Mono Build также генерирует C# glue, собирает GodotSharp, GodotTools и SDK,
копирует GodotSharp в Package и публикует локальные NuGet-пакеты.

Не запускайте сборки разных вариантов одновременно: генерируемые заголовки и
managed-выходы общие. Эта же последовательность соблюдается генератором проектов.

## Что хранится в Git

Версионируются этот каталог и изменения сборочной системы в `methods.py`,
`platform/windows/SCsub` и `platform/windows/detect.py`.

`.run/`, `godot.vs.user.props`, solution, vcxproj, `*.generated.props`, `.tools/`
и Package являются локальными выходами и игнорируются Git. Генератор создаёт
`godot.vs.user.props` из `godot.vs.user.props.in` и прописывает текущий workspace.
Он не редактирует персональный `.idea/workspace.xml`.
MSBuild toolset определяется по установленным инструментам той Visual Studio,
которую выбрал SCons; версия текущего компьютера не закреплена в шаблоне.

Если настройки SCons не менялись и четыре файла `godot.*.generated.props` уже
созданы, только конфигурации IDE можно обновить быстрее:

```powershell
..\.tools\python\Scripts\python.exe tools\oengine\configure_rider.py --reuse-metadata
```

Дополнительные личные свойства MSBuild можно хранить в игнорируемом
`godot.vs.local.props`. Для переопределения toolset проекта задайте
`OENGINE_PLATFORM_TOOLSET` перед генерацией. Эта настройка относится к MSBuild
и сама по себе не переключает компилятор SCons.
