# Makefile for password-notify
# Domain password expiration notifier for Samba AD

# Compiler and flags
CC = gcc
CPPFLAGS += -DLOCALEDIR=\"$(LOCALEDIR)\" -DICONDIR=\"$(ICONDIR)\"
CFLAGS = -Wall -Wextra -O2 -D_GNU_SOURCE
LDFLAGS =

# Package config
PKG_CONFIG = pkg-config

# Tray backend selection
# Set USE_STATUS_ICON=1 to use GtkStatusIcon (legacy, works in MATE/XFCE without extra deps)
# Default: USE_STATUS_ICON=0 (use AppIndicator - modern, requires libayatana-appindicator3)
USE_STATUS_ICON ?= 0

# Dependencies
COMMON_GTK_PACKAGES = gtk+-3.0 libnotify
LDAP_PACKAGES = ldap lber
KRB5_PACKAGES = krb5

# Conditional tray backend dependencies
ifeq ($(USE_STATUS_ICON),1)
    GTK_PACKAGES = $(COMMON_GTK_PACKAGES)
    CPPFLAGS += -DUSE_STATUS_ICON
    TRAY_BACKEND = GtkStatusIcon
else
    GTK_PACKAGES = $(COMMON_GTK_PACKAGES) ayatana-appindicator3-0.1
    TRAY_BACKEND = AppIndicator
endif

# Get flags from pkg-config
GTK_CFLAGS = $(shell $(PKG_CONFIG) --cflags $(GTK_PACKAGES))
GTK_LIBS = $(shell $(PKG_CONFIG) --libs $(GTK_PACKAGES))

LDAP_CFLAGS = $(shell $(PKG_CONFIG) --cflags $(LDAP_PACKAGES) 2>/dev/null || echo "-I/usr/include")
LDAP_LIBS = $(shell $(PKG_CONFIG) --libs $(LDAP_PACKAGES) 2>/dev/null || echo "-lldap -llber")

KRB5_CFLAGS = $(shell $(PKG_CONFIG) --cflags $(KRB5_PACKAGES) 2>/dev/null || krb5-config --cflags 2>/dev/null || echo "")
KRB5_LIBS = $(shell $(PKG_CONFIG) --libs $(KRB5_PACKAGES) 2>/dev/null || krb5-config --libs 2>/dev/null || echo "-lkrb5 -lk5crypto -lcom_err")

# Combine all flags
ALL_CFLAGS = $(CPPFLAGS) $(CFLAGS) $(GTK_CFLAGS) $(LDAP_CFLAGS) $(KRB5_CFLAGS)
ALL_LDFLAGS = $(LDFLAGS) $(GTK_LIBS) $(LDAP_LIBS) $(KRB5_LIBS)

# Directories
SRC_DIR = src
BUILD_DIR = build
PREFIX = /usr/local
BINDIR = $(PREFIX)/bin
DATADIR = $(PREFIX)/share
SYSCONFDIR = /etc
LOCALEDIR = $(DATADIR)/locale
ICONDIR = $(DATADIR)/password-notify/icons
MSGFMT ?= msgfmt
XGETTEXT ?= xgettext
LANGUAGES = en ru
MOFILES = $(foreach lang,$(LANGUAGES),locale/$(lang)/LC_MESSAGES/password-notify.mo)
APPLICATIONSDIR = $(DATADIR)/applications
AUTOSTARTDIR = $(SYSCONFDIR)/xdg/autostart

# Source files
SOURCES = $(SRC_DIR)/scheduler.c \
          $(SRC_DIR)/i18n.c \
          $(SRC_DIR)/main.c \
          $(SRC_DIR)/config.c \
          $(SRC_DIR)/ldap_client.c \
          $(SRC_DIR)/dc_discovery.c \
          $(SRC_DIR)/password_change.c \
          $(SRC_DIR)/ui.c \
          $(SRC_DIR)/password_dialog.c

# Object files
OBJECTS = $(SOURCES:$(SRC_DIR)/%.c=$(BUILD_DIR)/%.o)

# Target executable
TARGET = password-notify

# Default target
all: $(TARGET) translations
	@echo ""
	@echo "✅ Build successful!"
	@echo "Binary: $(TARGET)"
	@echo "Tray backend: $(TRAY_BACKEND)"
	@echo ""
	@echo "To build for MATE/XFCE (without AppIndicator dependency):"
	@echo "  make clean && make USE_STATUS_ICON=1"
	@echo ""

# Create build directory
$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

# Compile source files
$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c $(SRC_DIR)/i18n.h $(SRC_DIR)/scheduler.h | $(BUILD_DIR)
	$(CC) $(ALL_CFLAGS) -c $< -o $@

# Link executable
$(TARGET): $(OBJECTS)
	$(CC) $(OBJECTS) $(ALL_LDFLAGS) -o $@

# Install target
install: all translations
	install -d $(DESTDIR)$(ICONDIR)
	install -m 0644 icons/password-notify-key-black.svg icons/password-notify-key-red.svg $(DESTDIR)$(ICONDIR)/
	install -D -m 0755 $(TARGET) $(DESTDIR)$(BINDIR)/$(TARGET)
	install -D -m 0644 password-notify.ini $(DESTDIR)$(SYSCONFDIR)/password-notify.ini
	install -D -m 0644 password-notify.desktop $(DESTDIR)$(APPLICATIONSDIR)/password-notify.desktop
	install -D -m 0644 password-notify.desktop $(DESTDIR)$(AUTOSTARTDIR)/password-notify.desktop
	@for lang in $(LANGUAGES); do install -D -m 0644 locale/$$lang/LC_MESSAGES/password-notify.mo $(DESTDIR)$(LOCALEDIR)/$$lang/LC_MESSAGES/password-notify.mo || exit $$?; done

# Uninstall target
uninstall:
	rm -f $(DESTDIR)$(BINDIR)/$(TARGET)
	rm -f $(DESTDIR)$(ICONDIR)/password-notify-key-black.svg $(DESTDIR)$(ICONDIR)/password-notify-key-red.svg
	rm -f $(DESTDIR)$(SYSCONFDIR)/password-notify.ini
	rm -f $(DESTDIR)$(AUTOSTARTDIR)/password-notify.desktop
	rm -f $(DESTDIR)$(APPLICATIONSDIR)/password-notify.desktop
	@for lang in $(LANGUAGES); do rm -f $(DESTDIR)$(LOCALEDIR)/$$lang/LC_MESSAGES/password-notify.mo; done

# Clean build artifacts
clean:
	rm -rf $(BUILD_DIR)
	rm -f $(TARGET) $(MOFILES)

# Clean and rebuild
rebuild: clean all

# Check dependencies
check-deps:
	@echo "Checking dependencies..."
	@echo "Tray backend: $(TRAY_BACKEND)"
	@command -v $(MSGFMT) >/dev/null && echo "✓ gettext msgfmt OK" || echo "✗ msgfmt missing (install gettext-tools)"
	@$(PKG_CONFIG) --exists $(GTK_PACKAGES) && echo "✓ GTK packages OK" || echo "✗ GTK packages missing"
	@$(PKG_CONFIG) --exists $(LDAP_PACKAGES) && echo "✓ LDAP packages OK" || echo "⚠ LDAP packages not found via pkg-config (trying default)"
	@$(PKG_CONFIG) --exists $(KRB5_PACKAGES) && echo "✓ Kerberos packages OK" || echo "⚠ Kerberos packages not found via pkg-config (trying krb5-config)"
	@test -f /usr/include/krb5.h && echo "✓ krb5.h found" || echo "✗ krb5.h missing (install krb5-devel)"
	@test -f /usr/include/ldap.h && echo "✓ ldap.h found" || echo "✗ ldap.h missing (install openldap-devel)"

# Debug build
debug: CFLAGS += -g -DDEBUG -O0
debug: clean $(TARGET)

# Static analysis
analyze:
	cppcheck --enable=all --suppress=missingIncludeSystem $(SRC_DIR)

# Help target
help:
	@echo "Password Notify - Makefile targets:"
	@echo ""
	@echo "  all         - Build executable and gettext catalogs (default)"
	@echo "  translations - Build en/ru MO catalogs from PO files"
	@echo "  pot         - Update translation template from source"
	@echo "  install     - Install to system"
	@echo "  uninstall   - Remove from system"
	@echo "  clean       - Remove build artifacts"
	@echo "  rebuild     - Clean and rebuild"
	@echo "  check-deps  - Check for required dependencies"
	@echo "  debug       - Build with debug symbols"
	@echo "  analyze     - Run static analysis"
	@echo "  help        - Show this help"
	@echo ""
	@echo "Variables:"
	@echo "  PREFIX          - Installation prefix (default: /usr/local)"
	@echo "  DESTDIR         - Staging directory for package builds"
	@echo "  USE_STATUS_ICON - Use GtkStatusIcon instead of AppIndicator"
	@echo "                    0 = AppIndicator (default, modern)"
	@echo "                    1 = GtkStatusIcon (legacy, for MATE/XFCE)"
	@echo ""
	@echo "Examples:"
	@echo "  make                          # Build with AppIndicator"
	@echo "  make USE_STATUS_ICON=1        # Build for MATE/XFCE"
	@echo "  make install PREFIX=/usr      # Install to /usr"
	@echo ""

# Offline tests: never contact AD or change passwords.
$(BUILD_DIR)/test-core: tests/test_core.c $(SRC_DIR)/dc_discovery.c $(SRC_DIR)/config.c | $(BUILD_DIR)
	$(CC) $(ALL_CFLAGS) tests/test_core.c $(SRC_DIR)/config.c $(ALL_LDFLAGS) -o $@

$(BUILD_DIR)/test-ui: $(SRC_DIR)/i18n.c $(SRC_DIR)/i18n.h tests/mock_ldap.c $(SRC_DIR)/main.c $(SRC_DIR)/config.c $(SRC_DIR)/ui.c $(SRC_DIR)/ui.h $(SRC_DIR)/password_dialog.c $(SRC_DIR)/password_change.c | $(BUILD_DIR)
	$(CC) $(ALL_CFLAGS) -DSYSTEMCTL_PATH=\"$(CURDIR)/tests/fake_systemctl.py\" $(SRC_DIR)/main.c $(SRC_DIR)/config.c $(SRC_DIR)/ui.c $(SRC_DIR)/password_dialog.c $(SRC_DIR)/password_change.c tests/mock_ldap.c $(SRC_DIR)/i18n.c $(SRC_DIR)/scheduler.c $(ALL_LDFLAGS) -o $@

$(BUILD_DIR)/test-ldap: tests/test_ldap.c $(SRC_DIR)/ldap_client.c $(SRC_DIR)/dc_discovery.c | $(BUILD_DIR)
	$(CC) $(ALL_CFLAGS) tests/test_ldap.c $(SRC_DIR)/dc_discovery.c $(ALL_LDFLAGS) -o $@

$(BUILD_DIR)/test-password: $(SRC_DIR)/i18n.c $(SRC_DIR)/i18n.h tests/test_password.c tests/fake_passwd.py $(SRC_DIR)/password_change.c | $(BUILD_DIR)
	$(CC) $(ALL_CFLAGS) tests/test_password.c $(SRC_DIR)/i18n.c $(SRC_DIR)/scheduler.c $(ALL_LDFLAGS) -o $@

test: translations $(BUILD_DIR)/test-ui-transitions $(BUILD_DIR)/test-scheduled $(BUILD_DIR)/test-tray $(BUILD_DIR)/test-i18n $(BUILD_DIR)/test-core $(BUILD_DIR)/test-ldap $(BUILD_DIR)/test-ui $(BUILD_DIR)/test-password
	LC_ALL=C LANGUAGE= ./$(BUILD_DIR)/test-i18n $(CURDIR)/locale en
	LC_ALL=en_US.UTF-8 LANGUAGE=en ./$(BUILD_DIR)/test-i18n $(CURDIR)/locale en
	LC_ALL=ru_RU.UTF-8 LANGUAGE= ./$(BUILD_DIR)/test-i18n $(CURDIR)/locale ru
	./$(BUILD_DIR)/test-core
	./$(BUILD_DIR)/test-ldap
	./$(BUILD_DIR)/test-password $(CURDIR)/tests/fake_passwd.py
	xvfb-run -a ./$(BUILD_DIR)/test-ui-transitions
	xvfb-run -a python3 tests/test_scheduler.py ./$(BUILD_DIR)/test-scheduled
	xvfb-run -a ./$(BUILD_DIR)/test-tray
	xvfb-run -a python3 tests/test_ui.py ./$(BUILD_DIR)/test-ui

.PHONY: all install uninstall clean rebuild check-deps debug analyze help test
translations: $(MOFILES)

locale/%/LC_MESSAGES/password-notify.mo: po/%.po
	mkdir -p $(dir $@)
	$(MSGFMT) --check --check-format -o $@ $<

pot:
	$(XGETTEXT) --from-code=UTF-8 --keyword=_ --keyword=N_ --keyword=P_:1,2 --package-name=password-notify --package-version=1.0.0 --output=po/password-notify.pot $(SOURCES)

.PHONY: translations pot

$(BUILD_DIR)/test-i18n: tests/test_i18n.c $(SRC_DIR)/i18n.c $(SRC_DIR)/i18n.h | $(BUILD_DIR)
	$(CC) $(ALL_CFLAGS) tests/test_i18n.c $(SRC_DIR)/i18n.c $(SRC_DIR)/scheduler.c $(ALL_LDFLAGS) -o $@

$(BUILD_DIR)/test-tray: tests/test_tray.c tests/mock_ldap.c $(SRC_DIR)/ui.c $(SRC_DIR)/password_dialog.c $(SRC_DIR)/password_change.c $(SRC_DIR)/config.c $(SRC_DIR)/i18n.c | $(BUILD_DIR)
	$(CC) $(ALL_CFLAGS) tests/test_tray.c tests/mock_ldap.c $(SRC_DIR)/ui.c $(SRC_DIR)/password_dialog.c $(SRC_DIR)/password_change.c $(SRC_DIR)/config.c $(SRC_DIR)/i18n.c $(SRC_DIR)/scheduler.c $(ALL_LDFLAGS) -o $@

$(BUILD_DIR)/test-scheduled: tests/mock_notify.c tests/fake_systemctl.py tests/mock_ldap.c $(SOURCES) | $(BUILD_DIR)
	$(CC) $(ALL_CFLAGS) -DSYSTEMCTL_PATH=\"$(CURDIR)/tests/fake_systemctl.py\" $(filter-out $(SRC_DIR)/ldap_client.c $(SRC_DIR)/dc_discovery.c,$(SOURCES)) tests/mock_ldap.c tests/mock_notify.c $(ALL_LDFLAGS) -o $@

$(BUILD_DIR)/test-ui-transitions: tests/test_ui_transitions.c tests/mock_ldap.c tests/mock_notify.c $(SRC_DIR)/ui.c $(SRC_DIR)/password_dialog.c $(SRC_DIR)/password_change.c | $(BUILD_DIR)
	$(CC) $(ALL_CFLAGS) tests/test_ui_transitions.c tests/mock_ldap.c tests/mock_notify.c $(SRC_DIR)/password_dialog.c $(SRC_DIR)/password_change.c $(SRC_DIR)/config.c $(SRC_DIR)/i18n.c $(ALL_LDFLAGS) -o $@
