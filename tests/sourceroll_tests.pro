QT += core gui qml quick quickcontrols2 dbus network testlib
CONFIG += c++17 testcase
TARGET = sourceroll_tests
TEMPLATE = app
INCLUDEPATH += ../src
HEADERS += ../src/backend.h ../src/capturehealth.h ../src/session.h ../src/screenmonitor.h ../src/theme.h ../src/translator.h
SOURCES += sourceroll_tests.cpp ../src/backend.cpp ../src/capturehealth.cpp ../src/session.cpp ../src/screenmonitor.cpp ../src/theme.cpp ../src/translator.cpp
RESOURCES += ../src/resources.qrc
