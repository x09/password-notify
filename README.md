# password-notify

[Русская версия](README_ru.md)

Linux desktop utility for notifying users about domain password expiration and changing passwords in Samba AD / Microsoft Active Directory environments.

<img width="1021" height="769" alt="pass0_2026-10-14_12-34" src="https://github.com/user-attachments/assets/71095c00-64af-4d43-8539-bb9fbd291a38" />

<img width="405" height="329" alt="pass3_2026-10-15_11-06" src="https://github.com/user-attachments/assets/e5bb4668-39c5-4f78-a638-60a93fc09414" />

<img width="847" height="330" alt="pass2_2026-10-15_11-06" src="https://github.com/user-attachments/assets/6c3dc411-4172-466a-81d3-ef578d089d97" />

<img width="846" height="325" alt="pass1_2026-10-15_11-05" src="https://github.com/user-attachments/assets/16e325ba-a473-4691-8de9-df53fb532b49" />

## Features

- Expiration queries over LDAP/GSSAPI using the user's Kerberos credential cache.
- DNS SRV discovery of reachable domain controllers, with a preference for the client's AD site.
- One systemd user timer; no application process remains between checks when no warning is needed.
- Desktop notifications and a red tray key when the remaining days are less than or equal to `warn_days`, including expired passwords.
- Password changes through Kerberos or `/usr/bin/passwd` and the host's PAM stack.
- English and Russian gettext translations.

The tray never displays text beside its icon. MATE/XFCE use a tooltip; AppIndicator supplies a title and icon description, whose hover display depends on the panel.

## Requirements

The graphical workflow requires a working `systemctl --user`, a graphical session with session D-Bus and a notification daemon, GTK 3, libnotify, OpenLDAP client libraries, MIT Kerberos and the SASL GSSAPI plugin. AD DNS resolution, network access to a DC and a valid user Kerberos ticket are required. Tickets are not obtained or renewed automatically.

The default build needs AppIndicator support; GNOME requires a suitable tray extension. MATE/XFCE can use the GtkStatusIcon build. For `method = passwd`, changing the domain password with `/usr/bin/passwd` must already work in the user's terminal.

## Build and install

The simplest way is to install the package from the repository:

```sh
apt-get update
apt-get install password-notify
```

Building from source is described below.

ALT Linux dependencies:

```sh
apt-get install gcc make pkg-config gettext-tools libgtk+3-devel libnotify-devel libldap-devel libsasl2-devel libkrb5-devel
apt-get install libsasl2-plugin-gssapi
# Also needed for the default AppIndicator build:
apt-get install libayatana-appindicator3-devel
```

Run in the source directory. Choose one backend and use the same prefix/backend for build and installation:

```sh
# KDE/GNOME: AppIndicator
make clean
make PREFIX=/usr
# As root:
make install PREFIX=/usr
```

```sh
# MATE/XFCE: GtkStatusIcon
make clean
make PREFIX=/usr USE_STATUS_ICON=1
# As root:
make install PREFIX=/usr USE_STATUS_ICON=1
```

`make` also builds and validates the English/Russian MO catalogs. The Makefile defaults to `PREFIX=/usr/local`; the examples select `/usr`. Installation places the binary in `PREFIX/bin`, icons in `PREFIX/share/password-notify/icons`, translations in `PREFIX/share/locale`, configuration in `/etc/password-notify.ini`, and autostart in `/etc/xdg/autostart/password-notify.desktop`.

Back up an existing INI before manual `make install`, which overwrites it. The RPM marks the INI as `%config(noreplace)`. See [PACKAGE_BUILD.md](PACKAGE_BUILD.md) for packaging. In the ALT spec, `%def_without status_icon` selects AppIndicator by default; `--with status_icon` selects MATE/XFCE.

## Configuration

The standard system file is `/etc/password-notify.ini`. The current parser uses the explicit `-c` path if supplied; otherwise it tries `~/.config/password-notify.ini` first and `/etc/password-notify.ini` second. Files are not merged. Use `-c /etc/password-notify.ini` to force the system file.

If the selected file cannot be opened, the code uses built-in defaults without failing. The shipped INI selects LDAP (`use_ldaps = no`), while the built-in transport default without a readable file is LDAPS.

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

| Setting | Meaning |
| --- | --- |
| `warn_days` | Warning threshold in days; default 7. |
| `check_interval_hours` | Whole hours, 1–720; default 24. Out-of-range values fall back to 24. |
| `server_uri` | Empty: discover a DC. Explicit examples: `ldap://dc.example.com`, `ldaps://dc.example.com`. The URI scheme overrides `use_ldaps`. |
| `base_dn` | Empty: derive from the Kerberos realm, e.g. `EXAMPLE.COM` → `DC=example,DC=com`. |
| `use_ldaps` | Discovery transport: `no` uses LDAP with SASL/GSSAPI encryption; `yes` uses LDAPS/TLS and GSSAPI authentication without an additional SASL sign/seal layer. |
| `skip-issuer` | Default `false`. `true` disables both certificate chain and hostname validation for LDAPS. Prefer trusting the issuing CA. GSSAPI remains enabled. |
| `method` | `samba`: Kerberos password change; `passwd`: direct `/usr/bin/passwd` through a private pseudo-terminal. |
| `always_notify` | `yes` allows repeated reminders at subsequent checks during the warning period. |
| `notification_urgency` | `low`, `normal` or `critical`. Urgency is raised near and after expiration. |


## Autostart and scheduling

At graphical login, XDG autostart calls `password-notify -a`. The application imports display, session bus, locale and Kerberos cache environment into the user manager, configures the timer and requests an immediate check. Plain `password-notify` does the same. An already active service is not restarted.

- **No warning / non-expiring password:** the process exits and the next run is scheduled after `check_interval_hours`.
- **Warning needed:** the systemd timer is paused, the application stays in the tray, and its GTK timer refreshes status at the configured interval.
- **Password changed:** an immediate recheck runs. If the warning is no longer needed, the process exits and the external timer resumes.
- **Tray closed / process terminated:** the timer schedules the next check. `ExecStopPost` also rearms it after an abnormal process exit.
- **LDAP/Kerberos error:** an error notification is attempted, the process exits with a nonzero status, and the next check is scheduled. Each check needs a valid Kerberos ticket.

Fixed-name runtime units are created by the program, not installed as RPM files:

```text
$XDG_RUNTIME_DIR/systemd/user/password-notify-check.service
$XDG_RUNTIME_DIR/systemd/user/password-notify-check.timer
```

`XDG_RUNTIME_DIR` is normally `/run/user/<UID>`. No timer files in the home directory or manual `enable` are needed. A lock prevents parallel managed GUI instances; the headless `-t` diagnostic is independent. One timer/service serves each UID; simultaneous graphical sessions for the same UID share the manager environment. A user autostart entry with the same name overrides the system entry. See [SCHEDULER.md](SCHEDULER.md).

## Usage

Run as the domain user, in the graphical session:

```sh
klist
# Obtain a ticket if login did not provide one:
kinit user@EXAMPLE.COM
password-notify -a
# Explicit configuration:
password-notify -a -c /etc/password-notify.ini
# One diagnostic check without GUI or timer changes:
password-notify -t -d -c /etc/password-notify.ini
```

Only short options are implemented:

| Option | Meaning |
| --- | --- |
| `-c FILE` | Select configuration. |
| `-a` | Initialize the session timer and request a check; the default mode. |
| `-s` | Scheduled check used by the service; exit unless the tray is needed. |
| `-t` | One headless expiration check; print the result and exit. No password change or timer management. |
| `-d` | Also log this process's diagnostics to stderr. Managed service logs are available in the journal. |
| `-h` | Help. |
| `-v` | Version. |
| `-u` | Internal timer-rearm mode for `ExecStopPost`; normally not used manually. |

The tray menu contains **Change Password**, **Check Now**, **About** and **Quit**. Quit does not permanently disable future checks. To stop checks for the current session, stop the service first, wait for completion, then stop the timer:

```sh
systemctl --user stop password-notify-check.service
systemctl --user stop password-notify-check.timer
```

Checks are initialized again at the next graphical login.

## DC discovery and password changes

Discovery reads the realm from the credential cache, queries `_ldap._tcp.<domain>`, probes candidates and attempts GSSAPI binds. It reads AD subnet/site data and tries site DCs, retaining a working domain fallback if needed. Candidates are ordered by SRV priority and TCP latency. This is not a complete Windows DC Locator implementation.

`method = samba` uses initial credentials for `kadmin/changepw` and the Kerberos password-change API, not Samba libraries. Some errors classified as `OTHER` trigger a `passwd` fallback; authentication, policy and connection errors do not. The LDAP password-change function present in the sources is not called by this workflow.

`method = passwd` directly answers PAM/passwd prompts through a private terminal. Domain policy is enforced by the DC/PAM stack. Passwords are passed in memory and to the terminal, not in command-line arguments; they are not intentionally saved to files or logged.

## Localization

English is the source/fallback language. Russian locales such as `ru_RU.UTF-8` select Russian; gettext's `LANGUAGE` override applies. Sources are in `po/`, compiled catalogs in `locale/<language>/LC_MESSAGES/`. Known Kerberos/PAM/LDAP diagnostics are translated, including supported policy messages with server-supplied numeric limits. Unknown diagnostics retain their original language.

For an uninstalled binary, keep `locale/` and `icons/` beside it. See [LOCALIZATION.md](LOCALIZATION.md).

### Translated Samba password-change replies

All client-facing replies from Samba 4.21's [`source4/kdc/kpasswd-helper.c`](https://github.com/samba-team/samba/blob/samba-4.21.0/source4/kdc/kpasswd-helper.c) are supported. The table shows the server message templates and their Russian translations, including the shortened fallback messages.

| Server message | Russian translation |
| --- | --- |
| `No such user when changing password` | Пользователь для смены пароля не найден |
| `Not permitted to change password` | Смена пароля не разрешена |
| `Password too short, password must be at least %d characters long.` | Пароль слишком короткий. Минимальное число символов в пароле: %s. |
| `Password too short` | Пароль слишком короткий |
| `Password does not meet complexity requirements` | Пароль не соответствует требованиям сложности |
| `Password is already in password history. New password must not match any of your %d previous passwords.` | Этот пароль уже использовался. Новый пароль не должен совпадать с предыдущими паролями (число паролей в истории: %s). |
| `Password is already in password history` | Этот пароль уже использовался |
| `Password change rejected, password changes may not be permitted on this account, or the minimum password age may not have elapsed.` | Смена пароля отклонена. Возможно, для этой учётной записи запрещена смена пароля либо ещё не истёк минимальный срок действия пароля. |
| `Failed to set password: %s` | Не удалось установить пароль: %s |
| `Failed to set password` | Не удалось установить пароль |
| `Password changed` | Пароль изменён |
| `A TGT may not be used as a ticket to kpasswd` | Билет TGT нельзя использовать для обращения к kpasswd |
| `Failed to ascertain that ticket to kpasswd is not a TGT` | Не удалось убедиться, что билет для обращения к kpasswd не является TGT |

The server supplies `%d` values (minimum password length and password history length); they are not fixed at 7 or 24. The application inserts these values into gettext `%s` translation templates. For `Failed to set password: %s`, the server diagnostic, such as `NT_STATUS_ACCESS_DENIED`, is preserved. English locales keep the original replies.

Translation is performed by password-notify after receiving the reply; no Samba source changes are required. This list covers client replies, not the server's internal debug log messages.

## Diagnostics and current limitations

```sh
systemctl --user list-timers --all password-notify-check.timer
systemctl --user status password-notify-check.service password-notify-check.timer
systemctl --user cat password-notify-check.timer
journalctl --user -u password-notify-check.service
journalctl -t password-notify
```

An inactive service with a waiting timer is normal between checks. During a warning, the service is active and the external timer stopped. If `systemctl --user` cannot connect, fix the user manager/session integration first; changing `DISPLAY` alone does not start it.

Test LDAP against an explicit DC hostname:

```sh
klist
dig SRV _ldap._tcp.example.com
ldapsearch -H ldap://dc.example.com -b 'DC=example,DC=com' -Y GSSAPI
```

Use trusted certificates for LDAPS. An absent tray icon may simply mean the warning threshold has not been reached; check `-t -d` and the journal first.

- Expiration depends on AD's `msDS-UserPasswordExpiryTimeComputed`. If absent, the current code logs a warning and treats the password as non-expiring; it does not calculate expiration from domain policy. Generic LDAP/FreeIPA support is not implemented.
- The dialog additionally rejects input shorter than eight bytes (`strlen`) before contacting the server; the DC/PAM stack enforces actual domain policy.
- Site awareness is a best-effort preference, not a guarantee of the geographically nearest DC.

## Development and documentation

```sh
make test          # Offline tests; no real password changes or user-unit management
make translations  # Compile and validate en/ru catalogs
make pot           # Extract the gettext template
```

Tests additionally require Python 3, Xvfb/`xvfb-run`, `systemd-analyze`, and English/Russian UTF-8 locales. LDAP, notification and systemctl interactions are mocked where appropriate.

- [README_ru.md](README_ru.md) — Russian version.
- [SCHEDULER.md](SCHEDULER.md) — timer lifecycle and diagnostics.
- [LOCALIZATION.md](LOCALIZATION.md) — translations.
- [PACKAGE_BUILD.md](PACKAGE_BUILD.md) — ALT Linux RPM packaging.

License declared by the RPM spec: **GPL-3.0-or-later**.
