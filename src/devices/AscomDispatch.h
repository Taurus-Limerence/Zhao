#pragma once

#include <QVariantList>

#ifdef Q_OS_WIN
#include <windows.h>
#include <oleauto.h>
#else
// Stub types so the header compiles on non-Windows platforms.
using DISPID = int;
using VARIANT = int;
using IDispatch = void;
#define DISPID_UNKNOWN 0
#endif
#include <QString>
#include <QVariant>

// Thin IDispatch wrapper for ASCOM telescope drivers. ASCOM drivers are
// OLE Automation (IDispatch) COM servers: member names are looked up at
// runtime via GetIDsOfNames and invoked with dispatch flags — the same
// late binding NINA achieves through ASCOM.Com's DynamicAccess.
class AscomDispatch
{
public:
    AscomDispatch() = default;
    ~AscomDispatch() { release(); }

    bool create(const QString &progId, QString &error);
    void release();
    bool valid() const { return m_dispatch != nullptr; }

    // Property access (returns QVariant; invalid QVariant on failure).
    QVariant get(const QString &name);
    bool put(const QString &name, const QVariant &value);
    // Method call with positional arguments.
    QVariant call(const QString &name, const QVariantList &args = {});

    QString lastError() const { return m_lastError; }

private:
    DISPID dispidFor(const QString &name, bool *ok);
    static VARIANT variantToV(const QVariant &v);
    static QVariant vToVariant(const VARIANT &v);

    IDispatch *m_dispatch = nullptr;
    QString m_lastError;
};
