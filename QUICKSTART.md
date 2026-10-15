# Быстрый старт password-notify

## Установка зависимостей (ALT Linux)

```bash
su -
apt-get update
apt-get install -y \
    libgtk+3-devel \
    libayatana-appindicator3-devel \
    libnotify-devel \
    libldap-devel \
    libsasl2-devel \
    libkrb5-devel \
    libsamba-devel \
    gcc make pkg-config
exit
```

## Сборка

```bash
cd password-notify
make check-deps  # Проверка зависимостей
make             # Сборка
```

## Настройка Kerberos

Убедитесь, что у вас есть валидный билет:

```bash
# Получение билета
kinit user@EXAMPLE.COM

# Проверка
klist

# Вывод должен показывать:
# Ticket cache: FILE:/tmp/krb5cc_1000
# Default principal: user@EXAMPLE.COM
#
# Valid starting     Expires            Service principal
# 10/10/26 10:00:00  10/10/26 20:00:00  krbtgt/EXAMPLE.COM@EXAMPLE.COM
```

## Конфигурация

Создайте файл `~/.config/password-notify.ini`:

```bash
mkdir -p ~/.config
cat > ~/.config/password-notify.ini << 'EOF'
[general]
warn_days = 7
check_interval_hours = 24

[ldap]
server_uri = 
base_dn = 
use_ldaps = yes

[password_change]
method = samba
dc_hostname = 

[ui]
always_notify = yes
notification_urgency = normal
EOF
```

**Примечание**: Пустые значения означают автоопределение из Kerberos realm.

## Тестовый запуск

```bash
# Запуск в терминале для проверки
./password-notify -d

# Вывод должен показать:
# Starting password-notify version 1.0.0
# Configuration loaded: warn_days=7, check_interval=24h
# UI initialized, starting main loop
# LDAP connection established with GSSAPI
# Password expires in XX days
```

Если ошибок нет, нажмите Ctrl+C для остановки.

## Установка в систему

```bash
# Установка для текущего пользователя
make install PREFIX=$HOME/.local

# Или системная установка (требует root)
su -c "make install PREFIX=/usr"
```

## Автозапуск

Системная установка размещает `password-notify.desktop` в `/etc/xdg/autostart/`.
При входе в графический сеанс он напрямую запускает `password-notify` с
окружением пользователя: DISPLAY/Wayland, D-Bus, локаль и Kerberos-кеш.
Дополнительный скрипт не нужен. Между проверками программа завершается,
следующий запуск выполняет user timer. При предупреждении процесс остаётся
с иконкой в трее. Подробнее — SCHEDULER.md.
Проверка выполняется сразу после запуска и затем каждые `check_interval_hours`
часов (по умолчанию 24). Для изменения интервала перезапустите программу.

Для автозапуска только у одного пользователя можно скопировать desktop-файл в
`~/.config/autostart/`; пользовательский файл имеет приоритет над системным.

## Настройка для GNOME

GNOME требует установки расширения AppIndicator:

### Через GNOME Extensions (рекомендуется)

1. Откройте https://extensions.gnome.org/extension/615/appindicator-support/
2. Установите расширение через браузер
3. Включите расширение в GNOME Tweaks или Extensions

### Через пакет (ALT Linux)

```bash
su -
apt-get install gnome-shell-extension-appindicator
exit

# Включите расширение
gnome-extensions enable appindicatorsupport@rgcjonas.gmail.com

# Перезапустите GNOME Shell: Alt+F2, введите 'r', Enter
```

## Проверка работы

### Ручная проверка

```bash
# Запустите утилиту
password-notify &

# Через несколько секунд:
# 1. Если до истечения срока ≤ 7 дней: появится уведомление и иконка в трее
# 2. Если срок > 7 дней: уведомлений нет (работает в фоне)

# Принудительная проверка через меню трея:
# Клик по иконке → "Check Now"
```

### Проверка логов

```bash
# Журнал сообщений
journalctl -t password-notify -f

# Или syslog
tail -f /var/log/messages | grep password-notify
```

## Тестирование смены пароля

**ВНИМАНИЕ**: Это изменит ваш реальный доменный пароль!

```bash
# 1. Запустите password-notify
./password-notify &

# 2. Откройте меню трея и выберите "Change Password..."

# 3. В диалоге введите:
#    - Current password: ваш текущий пароль
#    - New password: новый пароль (минимум 8 символов)
#    - Confirm password: повтор нового пароля

# 4. Нажмите "Change Password"

# 5. Если успешно:
#    - Появится сообщение "Password changed successfully!"
#    - Диалог закроется через 1.5 секунды
#    - Утилита выполнит новую проверку срока

# 6. Проверьте новый пароль:
kinit user@EXAMPLE.COM  # Введите новый пароль
```

## Устранение частых проблем

### "Failed to connect to LDAP"

```bash
# Проверка 1: есть ли Kerberos билет
klist

# Если нет - получите:
kinit user@EXAMPLE.COM

# Проверка 2: доступен ли LDAP сервер
ldapsearch -Y GSSAPI -H ldap://dc.example.com -b "" -s base

# Проверка 3: настройки krb5.conf
cat /etc/krb5.conf
```

### Трей не появляется в GNOME

```bash
# Проверка расширения AppIndicator
gnome-extensions list | grep appindicator

# Если не установлено:
su -c "apt-get install gnome-shell-extension-appindicator"
gnome-extensions enable appindicatorsupport@rgcjonas.gmail.com

# Перезапуск GNOME Shell
# Alt+F2 → введите 'r' → Enter
```

### "msDS-UserPasswordExpiryTimeComputed not available"

Это означает, что атрибут недоступен (старая версия Samba AD < 4.7).

**Решение**: Обновите Samba AD до версии 4.7+.

**Временное решение**: Атрибут помечается как `never_expires`, уведомления не показываются.

### Уведомления не появляются

```bash
# Проверка notification daemon
ps aux | grep notification

# Тест libnotify
notify-send "Test" "This is a test notification"

# Если не работает - установите:
su -c "apt-get install notification-daemon"

# Для MATE:
su -c "apt-get install mate-notification-daemon"

# Для KDE:
# Уведомления встроены в Plasma
```

## Настройка интервалов

Для тестирования удобно уменьшить интервал проверки:

```ini
[general]
# Проверять каждый час вместо 24 часов (целое число, 1–720)
check_interval_hours = 1

# Показывать уведомления за 30 дней вместо 7
warn_days = 30
```

**Не забудьте вернуть нормальные значения после тестирования!**

## Удаление

```bash
# Остановка
killall password-notify

# Удаление файлов
rm ~/.local/bin/password-notify  # Или /usr/bin/password-notify
rm ~/.config/password-notify.ini
rm ~/.config/autostart/password-notify.desktop

# Или через make
cd password-notify
sudo make uninstall PREFIX=/usr
```

## Дополнительная информация

- Полная документация: `README.md`
- Архитектура: `ARCHITECTURE.md`
- Сборка RPM: `password-notify.spec`
- Логи: `journalctl -t password-notify`

## Поддержка

При возникновении проблем:

1. Проверьте логи: `journalctl -t password-notify -n 50`
2. Запустите в debug режиме: `password-notify -d`
3. Проверьте Kerberos: `klist`, `kinit user@REALM`
4. Проверьте LDAP: `ldapsearch -Y GSSAPI ...`
5. Создайте issue на GitHub с логами

## Минимальная конфигурация для работы

**Требования**:
1. ✅ Kerberos билет (`kinit`)
2. ✅ Доступ к LDAP серверу домена
3. ✅ Samba AD >= 4.7 (для атрибута `msDS-UserPasswordExpiryTimeComputed`)
4. ✅ Трей (GNOME: + расширение AppIndicator)

Если все требования выполнены, утилита должна работать с конфигурацией по умолчанию (все параметры пустые = автоопределение).
