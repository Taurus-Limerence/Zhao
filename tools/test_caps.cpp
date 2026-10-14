#define NOMINMAX
#include <QCoreApplication>
#include <cstdio>
#include <cstdarg>
#include "devices/AscomDispatch.h"

static void logf(const char* fmt, ...) {
    va_list ap; va_start(ap, fmt); vfprintf(stderr, fmt, ap); va_end(ap); fflush(stderr);
}

int main(int argc, char *argv[]) {
    QCoreApplication app(argc, argv);
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    AscomDispatch d;
    QString err;
    if (!d.create("ASCOM.TMount.Mount", err)) { logf("create failed: %ls\n", reinterpret_cast<const wchar_t*>(err.utf16())); return 1; }
    d.put("Connected", true);
    logf("Connected: %s\n", d.get("Connected").toBool() ? "true" : "false");
    auto cap = [&](const char* name) {
        QVariant v = d.get(name);
        if (!v.isValid()) { logf("%s: NOT_IMPLEMENTED\n", name); return; }
        logf("%s: %s\n", name, v.toBool() ? "YES" : "NO");
    };
    cap("CanFindHome");
    cap("CanSetPark");
    cap("CanPark");
    cap("CanUnpark");
    cap("CanSlew");
    cap("CanSync");
    cap("CanPulseGuide");
    cap("CanSetPierSide");
    d.put("Connected", false);
    return 0;
}
