#include "AscomDispatch.h"
#include <QDateTime>
#include <QTimeZone>

#ifdef Q_OS_WIN

#include <objbase.h>
#include <comdef.h>
#include <OleAuto.h>

bool AscomDispatch::create(const QString &progId, QString &error)
{
    release();
    const std::wstring wide = progId.toStdWString();
    CLSID clsid{};
    HRESULT hr = CLSIDFromProgID(wide.c_str(), &clsid);
    if (FAILED(hr)) {
        error = QStringLiteral("找不到 ASCOM 驱动 ProgID「%1」（HRESULT 0x%2）。请确认驱动已安装注册。")
                    .arg(progId, QString::number(uint(hr), 16));
        return false;
    }
    IDispatch *dispatch = nullptr;
    hr = CoCreateInstance(clsid, nullptr, CLSCTX_INPROC_SERVER | CLSCTX_LOCAL_SERVER,
                          IID_IDispatch, reinterpret_cast<void **>(&dispatch));
    if (FAILED(hr) || !dispatch) {
        error = QStringLiteral("无法创建 ASCOM 驱动实例（HRESULT 0x%1）。请确认 ASCOM Platform 已安装。")
                    .arg(QString::number(uint(hr), 16));
        return false;
    }
    m_dispatch = dispatch;
    return true;
}

void AscomDispatch::release()
{
    if (m_dispatch) {
        m_dispatch->Release();
        m_dispatch = nullptr;
    }
    m_lastError.clear();
}

DISPID AscomDispatch::dispidFor(const QString &name, bool *ok)
{
    *ok = false;
    if (!m_dispatch) return DISPID_UNKNOWN;
    const std::wstring wide = name.toStdWString();
    wchar_t *names[] = { const_cast<wchar_t *>(wide.c_str()) };
    DISPID dispid = DISPID_UNKNOWN;
    // LOCALE_NEUTRAL = ASCOM drivers typically register English names.
    HRESULT hr = m_dispatch->GetIDsOfNames(IID_NULL, names, 1, LOCALE_NEUTRAL, &dispid);
    if (FAILED(hr)) {
        m_lastError = QStringLiteral("驱动不支持成员「%1」").arg(name);
        return DISPID_UNKNOWN;
    }
    *ok = true;
    return dispid;
}

VARIANT AscomDispatch::variantToV(const QVariant &v)
{
    VARIANT result;
    VariantInit(&result);
    switch (v.type()) {
    case QVariant::Bool:   V_VT(&result) = VT_BOOL; V_BOOL(&result) = v.toBool() ? VARIANT_TRUE : VARIANT_FALSE; break;
    case QVariant::Int:    V_VT(&result) = VT_I4;   V_I4(&result) = v.toInt(); break;
    case QVariant::Double: V_VT(&result) = VT_R8;   V_R8(&result) = v.toDouble(); break;
    case QVariant::DateTime: {
        const auto dt = v.toDateTime().toUTC();
        SYSTEMTIME st{};
        st.wYear = dt.date().year(); st.wMonth = dt.date().month(); st.wDay = dt.date().day();
        st.wHour = dt.time().hour(); st.wMinute = dt.time().minute(); st.wSecond = dt.time().second();
        DATE date;
        if (dt.isValid() && SystemTimeToVariantTime(&st, &date)) {
            V_VT(&result) = VT_DATE;
            V_DATE(&result) = date;
        }
        break;
    }
    case QVariant::String: {
        const std::wstring s = v.toString().toStdWString();
        V_VT(&result) = VT_BSTR;
        V_BSTR(&result) = SysAllocString(s.c_str());
        break;
    }
    default:               V_VT(&result) = VT_I4;   V_I4(&result) = v.toInt(); break;
    }
    return result;
}

QVariant AscomDispatch::vToVariant(const VARIANT &v)
{
    switch (V_VT(&v)) {
    case VT_BOOL:  return QVariant(V_BOOL(&v) != VARIANT_FALSE);
    case VT_I2:    return QVariant(int(V_I2(&v)));
    case VT_I4:    return QVariant(int(V_I4(&v)));
    case VT_R4:    return QVariant(double(V_R4(&v)));
    case VT_R8:    return QVariant(V_R8(&v));
    case VT_BSTR:  return QVariant(QString::fromWCharArray(V_BSTR(&v)));
    case VT_DATE: {
        SYSTEMTIME st{};
        if (!VariantTimeToSystemTime(V_DATE(&v), &st)) return {};
        return QDateTime(QDate(st.wYear, st.wMonth, st.wDay),
                         QTime(st.wHour, st.wMinute, st.wSecond), QTimeZone::UTC);
    }
    default:       return QVariant();
    }
}

QVariant AscomDispatch::get(const QString &name)
{
    bool ok = false;
    const DISPID id = dispidFor(name, &ok);
    if (!ok) return QVariant();

    DISPPARAMS params{};
    params.cArgs = 0;
    VARIANT result;
    VariantInit(&result);
    EXCEPINFO excep{};
    UINT argErr = 0;
    HRESULT hr = m_dispatch->Invoke(id, IID_NULL, LOCALE_NEUTRAL,
                                    DISPATCH_PROPERTYGET, &params, &result, &excep, &argErr);
    if (FAILED(hr)) {
        m_lastError = QStringLiteral("读取「%1」失败（HRESULT 0x%2）").arg(name, QString::number(uint(hr), 16));
        return QVariant();
    }
    // Dereference by-ref variants (some ASCOM drivers return VT_BYREF).
    if (V_VT(&result) & VT_BYREF) {
        VARIANT deref;
        VariantInit(&deref);
        if (SUCCEEDED(VariantCopyInd(&deref, &result))) {
            QVariant q = vToVariant(deref);
            VariantClear(&deref);
            VariantClear(&result);
            return q;
        }
        VariantClear(&result);
        return QVariant();
    }
    QVariant q = vToVariant(result);
    VariantClear(&result);
    return q;
}

bool AscomDispatch::put(const QString &name, const QVariant &value)
{
    bool ok = false;
    const DISPID id = dispidFor(name, &ok);
    if (!ok) return false;

    VARIANT arg = variantToV(value);
    DISPPARAMS params{};
    params.cArgs = 1;
    params.rgvarg = &arg;
    // Property PUT requires the named argument DISPID_PROPERTYPUT.
    DISPID putId = DISPID_PROPERTYPUT;
    params.rgdispidNamedArgs = &putId;
    params.cNamedArgs = 1;

    VARIANT result;
    VariantInit(&result);
    EXCEPINFO excep{};
    UINT argErr = 0;
    HRESULT hr = m_dispatch->Invoke(id, IID_NULL, LOCALE_NEUTRAL,
                                    DISPATCH_PROPERTYPUT, &params, &result, &excep, &argErr);
    VariantClear(&arg);
    if (FAILED(hr)) {
        m_lastError = QStringLiteral("设置「%1」失败（HRESULT 0x%2）").arg(name, QString::number(uint(hr), 16));
        return false;
    }
    return true;
}

QVariant AscomDispatch::call(const QString &name, const QVariantList &args)
{
    m_lastError.clear();
    bool ok = false;
    const DISPID id = dispidFor(name, &ok);
    if (!ok) return QVariant();

    // IDispatch arguments are passed in REVERSE order.
    std::vector<VARIANT> variants;
    variants.reserve(args.size());
    for (int i = args.size() - 1; i >= 0; --i)
        variants.push_back(variantToV(args[i]));

    DISPPARAMS params{};
    params.cArgs = unsigned(variants.size());
    params.rgvarg = variants.empty() ? nullptr : variants.data();

    VARIANT result;
    VariantInit(&result);
    EXCEPINFO excep{};
    UINT argErr = 0;
    HRESULT hr = m_dispatch->Invoke(id, IID_NULL, LOCALE_NEUTRAL,
                                    DISPATCH_METHOD, &params, &result, &excep, &argErr);
    for (auto &v : variants) VariantClear(&v);
    if (FAILED(hr)) {
        m_lastError = QStringLiteral("调用「%1」失败（HRESULT 0x%2）").arg(name, QString::number(uint(hr), 16));
        return QVariant();
    }
    QVariant q = vToVariant(result);
    VariantClear(&result);
    return q;
}

#else // non-Windows: ASCOM COM drivers don't exist

bool AscomDispatch::create(const QString &, QString &error)
{
    error = QStringLiteral("ASCOM 驱动仅支持 Windows 平台");
    return false;
}
void AscomDispatch::release() {}
bool AscomDispatch::valid() const { return false; }
QVariant AscomDispatch::get(const QString &) { return {}; }
bool AscomDispatch::put(const QString &, const QVariant &) { return false; }
QVariant AscomDispatch::call(const QString &, const QVariantList &) { return {}; }
QString AscomDispatch::lastError() const { return {}; }

#endif
