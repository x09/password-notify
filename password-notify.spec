%def_without status_icon

Name: password-notify
Version: 1.0.0
Release: alt1
Summary: Domain password expiration notifier
Summary(ru_RU.UTF-8): Уведомления об истечении срока доменного пароля
License: GPL-3.0+
Group: System/Configuration/Other
Packager: Anton Shevtsov <shevtsov.anton@gmail.com>
# The release snapshot directory requested for this version is password-notify-1.0.
Url: https://github.com/x09/password-notify
Source: %name-1.0.tar.gz

BuildRequires: gcc make pkg-config gettext-tools
BuildRequires: libgtk+3-devel libnotify-devel libldap-devel libsasl2-devel libkrb5-devel
%if_without status_icon
BuildRequires: libayatana-appindicator3-devel
%endif

Requires: libsasl2-plugin-gssapi
Requires: /usr/bin/passwd
Requires: /usr/bin/systemctl

%description
User-session utility for checking domain password expiration over LDAP/GSSAPI,
showing desktop notifications and changing passwords via Kerberos or passwd/PAM.
Includes English and Russian gettext catalogs and XDG autostart with a per-user systemd timer.
Default tray backend is AppIndicator; --with status_icon selects
GtkStatusIcon for MATE/XFCE. A valid user Kerberos credential cache is required.

%description -l ru_RU.UTF-8
Утилита пользовательского сеанса проверяет срок доменного пароля через
LDAP/GSSAPI, показывает уведомления и меняет пароль через Kerberos либо
passwd/PAM. Включает английский и русский переводы gettext и
автозапуск XDG с пользовательским таймером systemd. По умолчанию используется AppIndicator;
--with status_icon выбирает GtkStatusIcon для MATE/XFCE.
Для LDAP-запросов требуется доступный Kerberos-кеш пользователя.

%prep
%setup -q -n %name-1.0

%build
%if_with status_icon
%make_build PREFIX=%_prefix USE_STATUS_ICON=1 CFLAGS="%optflags -D_GNU_SOURCE -Wall -Wextra"
%else
%make_build PREFIX=%_prefix USE_STATUS_ICON=0 CFLAGS="%optflags -D_GNU_SOURCE -Wall -Wextra"
%endif

%install
%if_with status_icon
%makeinstall_std PREFIX=%_prefix USE_STATUS_ICON=1
%else
%makeinstall_std PREFIX=%_prefix USE_STATUS_ICON=0
%endif

# Timer/service units are generated per user under XDG_RUNTIME_DIR, not installed by RPM.
%files
%doc README.md README_ru.md BUILD.md LOCALIZATION.md PACKAGE_BUILD.md SCHEDULER.md
%_bindir/%name
%config(noreplace) %_sysconfdir/%name.ini
%_sysconfdir/xdg/autostart/%name.desktop
%_datadir/applications/%name.desktop
%dir %_datadir/%name
%dir %_datadir/%name/icons
%_datadir/%name/icons/password-notify-key-black.svg
%_datadir/%name/icons/password-notify-key-red.svg
%_datadir/locale/en/LC_MESSAGES/%name.mo
%_datadir/locale/ru/LC_MESSAGES/%name.mo

%changelog
* Wed Oct 14 2026 Anton Shevtsov <shevtsov.anton@gmail.com> 1.0.0-alt1
- Package LDAP/GSSAPI discovery, Kerberos and passwd/PAM password changes.
- Include English/Russian gettext catalogs and graphical-session autostart.
- Start a single per-user timer from XDG autostart; exit between checks.
- Support --with status_icon for MATE/XFCE; AppIndicator is the default.
