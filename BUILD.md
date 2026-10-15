# Сборка password-notify

Команды выполняются в актуальном каталоге исходников
`/home/anton/Work/password-notify/password-notify-1.0`.
Проект использует C, GCC, GNU Make, GTK 3, libnotify, OpenLDAP и MIT Kerberos.
Библиотеки Samba NetAPI и пакет samba-devel для сборки не нужны.
Метод смены пароля `samba` использует API Kerberos, а не запуск kpasswd
или smbpasswd.

## Зависимости ALT Linux

Установка от root:

```sh
apt-get install gcc make pkg-config gettext-tools \
    libgtk+3-devel libnotify-devel libldap-devel libsasl2-devel libkrb5-devel
# Дополнительно для стандартного варианта AppIndicator:
apt-get install libayatana-appindicator3-devel
# Для LDAP/GSSAPI во время работы:
apt-get install libsasl2-plugin-gssapi
```

Для графического режима также нужны исправный systemctl --user, сеансовая
шина D-Bus и служба уведомлений. Для method=passwd команда /usr/bin/passwd
должна менять доменный пароль через настроенный PAM в терминале пользователя.
В GNOME для отображения AppIndicator требуется соответствующее расширение панели.

## Выбор трея и сборка

Для KDE/GNOME используется AppIndicator:

```sh
make clean
make check-deps PREFIX=/usr USE_STATUS_ICON=0
make PREFIX=/usr USE_STATUS_ICON=0
```

Для MATE/XFCE можно использовать GtkStatusIcon без зависимости AppIndicator:

```sh
make clean
make check-deps PREFIX=/usr USE_STATUS_ICON=1
make PREFIX=/usr USE_STATUS_ICON=1
```

make check-deps выводит диагностику msgfmt, GTK, LDAP и Kerberos.
Это вспомогательная цель: сообщение об отсутствующей зависимости не обязательно
означает ненулевой код выхода. Окончательная проверка — успешная сборка.

Обычный make собирает бинарник password-notify и оба каталога gettext:
locale/en/LC_MESSAGES/password-notify.mo и locale/ru/LC_MESSAGES/password-notify.mo.
Для каталогов выполняется msgfmt --check --check-format.

По умолчанию PREFIX=/usr/local и USE_STATUS_ICON=0. При смене префикса,
варианта трея или флагов сборки предварительно выполняйте make clean:
Makefile не отслеживает изменения значений этих переменных в готовых объектах.

Проверить зависимости собранного бинарника можно командой:

```sh
ldd ./password-notify
```

Наличие libnetapi не требуется; конкретные имена и состав косвенных библиотек
зависят от выбранного трея и дистрибутива.

## Отладочная сборка

```sh
make debug PREFIX=/usr USE_STATUS_ICON=1
make translations
```

Цель debug очищает результаты и собирает бинарник с -g -DDEBUG -O0.
Она сама не собирает MO, поэтому после неё нужна make translations.
Не используйте параллельный make -j для этой цели: clean и сборка в ней
не упорядочены для параллельного выполнения.

Для диагностики одного LDAP-запроса без GUI и изменения таймера:

```sh
./password-notify -t -d -c /etc/password-notify.ini
```

-d добавляет сообщения текущего процесса в stderr.
При обычном запуске через -a проверку выполняет отдельный сервис;
его сообщения смотрите в journal, а не ожидайте их в терминале запуска.

## Установка и staging

Используйте те же PREFIX и USE_STATUS_ICON, что при сборке.
Пример для MATE/XFCE, от root:

```sh
make install PREFIX=/usr USE_STATUS_ICON=1
```

Для AppIndicator укажите USE_STATUS_ICON=0.
Ручной make install перезаписывает /etc/password-notify.ini:
заранее сохраните существующие настройки.

Установка размещает:

- Бинарник в PREFIX/bin/password-notify.
- Конфигурацию в /etc/password-notify.ini.
- XDG-автозапуск в /etc/xdg/autostart/password-notify.desktop.
- Пункт меню в PREFIX/share/applications/password-notify.desktop.
- SVG в PREFIX/share/password-notify/icons.
- MO в PREFIX/share/locale/<язык>/LC_MESSAGES/password-notify.mo.

Документацию в RPM включает spec через %doc; make install её не копирует.
Статические .service/.timer и session-start скрипт не устанавливаются.

Установка в отдельное дерево без изменения системных файлов:

```sh
make install PREFIX=/usr USE_STATUS_ICON=1 DESTDIR=/tmp/password-notify-staging
```

DESTDIR меняет только место копирования; PREFIX определяет пути,
встроенные в бинарник. Цель install зависит от all и при необходимости собирает
недостающие объекты и переводы.

## Конфигурация и проверка запуска

Стандартный системный файл — /etc/password-notify.ini.
Без -c сначала выбирается читаемый ~/.config/password-notify.ini, затем системный.
Файлы не объединяются. Пример основных действующих параметров:

```ini
[general]
warn_days = 7
check_interval_hours = 24

[ldap]
server_uri =
base_dn =
use_ldaps = no
skip-issuer = false

[password_change]
method = samba

[ui]
always_notify = yes
notification_urgency = normal
```

Пустой server_uri включает поиск LDAP-контроллера.
Явный ldap://dc.example.com или ldaps://dc.example.com выбирает сервер
и имеет приоритет над use_ldaps. Параметр dc_hostname не нужен.

В графическом сеансе доменного пользователя:

```sh
klist
password-notify -a -c /etc/password-notify.ini
systemctl --user list-timers --all password-notify-check.timer
journalctl --user -u password-notify-check.service
```

Если билет отсутствует, его нужно получить средствами входа или kinit.
Программа не получает и не продлевает билеты автоматически.

Автозапуск вызывает тот же -a: импортирует окружение сеанса, создаёт runtime
user timer и сервис и запрашивает немедленную проверку.
Номер DISPLAY не фиксирован. Вне периода предупреждения процесс завершается;
при предупреждении остаётся в трее, внешний timer приостанавливается.
Обычный запуск без ключей эквивалентен -a.
Подробности и отключение проверок — [SCHEDULER.md](SCHEDULER.md).

Для запуска без установки сохраняйте locale/ и icons/ рядом с бинарником.
Runtime units используют абсолютный путь запустившего их бинарника:
не удаляйте дерево сборки до перенастройки через установленную программу.

## Тесты и переводы

```sh
make USE_STATUS_ICON=1 test
# Для AppIndicator: make USE_STATUS_ICON=0 test
make translations
```

Дополнительно нужны Python 3, Xvfb/xvfb-run, systemd-analyze и установленные
локали en_US.UTF-8 и ru_RU.UTF-8. Набор проверяет конфигурацию, LDAP,
смену пароля с имитацией Kerberos/passwd, gettext, трей, завершение процессов
и планировщик. Реальные пароли и пользовательские units тесты не меняют.

Порядок изменения строк, обновления POT/PO и проверки MO описан
в [LOCALIZATION.md](LOCALIZATION.md).

## RPM

Согласованные команды подготовки password-notify-1.0.tar.gz, выбор spec
и параметра --with status_icon приведены в [PACKAGE_BUILD.md](PACKAGE_BUILD.md).
Вложенный spec имеет Version=1.0.0, родительский — Version=1.0;
оба используют архив с корнем password-notify-1.0.
RPM-сборка и автономные тесты — отдельные действия: текущие spec не имеют %check.

## Удаление

Сначала остановите сервис, дождитесь завершения, затем остановите timer:

```sh
systemctl --user stop password-notify-check.service
systemctl --user stop password-notify-check.timer
```

Для ручной установки, от root и с исходным PREFIX:

```sh
make uninstall PREFIX=/usr
```

Эта цель удаляет также /etc/password-notify.ini — сохраните его заранее.
Runtime units в каталоге пользователя uninstall не удаляет.
Пакетную установку удаляйте средствами пакетного менеджера:

```sh
apt-get remove password-notify
```

Полное описание возможностей и ограничений — [README_ru.md](README_ru.md).
