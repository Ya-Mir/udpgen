TEMPLATE = app
CONFIG += console
CONFIG -= qt
DEFINES += LINUX_OS
SOURCES += \
    udprx.c
LIBS += -lncurses
