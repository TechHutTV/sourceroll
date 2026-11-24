QT += core gui qml quick quickcontrols2 dbus network
CONFIG += c++17 release
TARGET = multitake
TEMPLATE = app
HEADERS += src/backend.h src/session.h src/screenmonitor.h src/theme.h src/translator.h
SOURCES += src/main.cpp src/backend.cpp src/session.cpp src/screenmonitor.cpp src/theme.cpp src/translator.cpp
RESOURCES += src/resources.qrc
