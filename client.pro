TEMPLATE = app
CONFIG += console
CONFIG -= qt

SOURCES += \
    client.c
LIBS += -lncurses
LIBS -= qt
