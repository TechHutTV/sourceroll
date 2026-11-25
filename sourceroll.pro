QT += core gui qml quick quickcontrols2 dbus network
CONFIG += c++17 release
TARGET = sourceroll
TEMPLATE = app
HEADERS += src/backend.h src/camerafeed.h src/screenfeed.h src/capturehealth.h src/session.h src/sources.h src/screenmonitor.h src/theme.h src/translator.h
SOURCES += src/main.cpp src/backend.cpp src/camerafeed.cpp src/screenfeed.cpp src/capturehealth.cpp src/session.cpp src/sources.cpp src/screenmonitor.cpp src/theme.cpp src/translator.cpp
RESOURCES += src/resources.qrc
