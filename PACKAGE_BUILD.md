# Сборка RPM в ALT Linux

## Исходники и версии

Актуальный каталог исходников — `password-notify-1.0`.
Архив `password-notify-1.0.tar.gz` должен содержать одноимённый корневой каталог.

В проекте сейчас два spec с разными значениями Version:

| Spec | Version | Source и каталог распаковки |
| --- | --- | --- |
| password-notify-1.0/password-notify.spec | 1.0.0 | Source: %name-1.0.tar.gz; %setup -n %name-1.0 |
| password-notify.spec в родительском каталоге | 1.0 | Source: %name-%version.tar.gz; %setup -n %name-%version |

Оба используют тот же архив. Версия, выводимая бинарником, — 1.0.0.
Родительский spec содержит пользовательские настройки; при переносе правок
не заменяйте его автоматически копией вложенного spec.
Перед сборкой выбирайте конкретный spec и проверяйте его развёрнутые макросы.

## Подготовка архива

Из родительского каталога, без включения результатов сборки:

```sh
tar --exclude='password-notify-1.0/build' \
    --exclude='password-notify-1.0/locale' \
    --exclude='password-notify-1.0/password-notify' \
    --exclude='password-notify-1.0/.git' \
    -czf password-notify-1.0.tar.gz password-notify-1.0
tar -tzf password-notify-1.0.tar.gz
```

PO/POT, SVG, C-исходники, tests, Makefile, INI, desktop-файл и документация
должны остаться в архиве. MO создаются из PO при сборке.

## Зависимости и выбор трея

BuildRequires: gcc, make, pkg-config, gettext-tools, libgtk+3-devel,
libnotify-devel, libldap-devel, libsasl2-devel, libkrb5-devel.
AppIndicator дополнительно требует libayatana-appindicator3-devel.
Для работы нужны SASL GSSAPI, passwd/PAM и systemctl с исправным user manager.

`%def_without status_icon` выбирает AppIndicator по умолчанию.
Параметр rpmbuild `--with status_icon` выбирает GtkStatusIcon для MATE/XFCE.
Можно изменить саму строку на `%def_with status_icon`, чтобы сделать этот
вариант стандартным; `--without status_icon` тогда выбирает AppIndicator.

Прямая сборка соответствует этим вариантам:

```sh
make clean
make PREFIX=/usr USE_STATUS_ICON=0
# Либо после make clean:
make PREFIX=/usr USE_STATUS_ICON=1
```

При смене PREFIX или варианта трея выполняйте make clean и передавайте
одинаковые значения при сборке и установке.

## Команды сборки пакета

Следующие команды выполняются вручную, когда нужна именно сборка RPM:

```sh
# Из родительского каталога; выбран вложенный spec:
cp password-notify-1.0.tar.gz ~/RPM/SOURCES/
cp password-notify-1.0/password-notify.spec ~/RPM/SPECS/
# Проверка spec без сборки RPM:
rpmspec -P ~/RPM/SPECS/password-notify.spec > /tmp/password-notify.expanded.spec
# AppIndicator:
rpmbuild -ba ~/RPM/SPECS/password-notify.spec
# Либо MATE/XFCE:
rpmbuild -ba --with status_icon ~/RPM/SPECS/password-notify.spec
```

Каталоги SOURCES/SPECS зависят от локальной настройки %_topdir.
Spec передаёт PREFIX=%_prefix и USE_STATUS_ICON на обоих этапах.
В %build используются флаги %optflags; иконки и локализация задаются через CPPFLAGS.

## Содержимое пакета

- /usr/bin/password-notify.
- /etc/password-notify.ini как %config(noreplace).
- /etc/xdg/autostart/password-notify.desktop и desktop-файл в /usr/share/applications.
- Чёрный и красный SVG в /usr/share/password-notify/icons.
- en/ru MO в /usr/share/locale/<язык>/LC_MESSAGES.
- README.md, README_ru.md, BUILD.md, LOCALIZATION.md, PACKAGE_BUILD.md, SCHEDULER.md
  как %doc.

Установка через Makefile с DESTDIR заполняет дерево пакета; документацию
добавляет RPM через %doc. Ручной make install перезаписывает существующий INI,
поэтому перед ним нужна резервная копия.

Статические systemd units и session-start скрипт не устанавливаются.
XDG-автозапуск вызывает `password-notify -a`, который создаёт один runtime
user timer и сервис в $XDG_RUNTIME_DIR/systemd/user. Поэтому .timer/.service
отсутствуют в %files. Между проверками процесс отсутствует, если предупреждение
не требуется; во время предупреждения остаётся процесс с треем.
Подробности — [SCHEDULER.md](SCHEDULER.md).

## Проверки перед выпуском

`make test` использует имитации доменных операций и systemctl, не меняет
реальные пароли и пользовательские units. Нужны Python 3, Xvfb/xvfb-run,
systemd-analyze и локали en_US.UTF-8/ru_RU.UTF-8, а также зависимости сборки.
Для MATE выполняйте `make USE_STATUS_ICON=1 test`.

В текущих spec нет секции %check: rpmbuild сам этот набор тестов не запускает.
Сборка пакета не запускает GUI, не включает timer и не меняет доменные пароли.
Перед выпуском отдельно проверьте тесты, развёрнутый spec и соответствие
установленных файлов списку %files.
