#pragma once
#include <QObject>
#include <QTimer>
#include <QElapsedTimer>
#include <QtMath>
#include <array>
#include <algorithm>
#include <cmath>

class DeviceManager;
class PlateSolver;
namespace CenteringMath {
inline bool valid(double ra,double dec) { return std::isfinite(ra) && std::isfinite(dec) && ra>=0 && ra<24 && dec>=-90 && dec<=90; }
inline double separation(double ra,double dec,double r,double d) {
    if (!valid(ra,dec) || !valid(r,d)) return qQNaN();
    const double a=std::pow(std::sin(qDegreesToRadians(d-dec)/2),2)
        +std::cos(qDegreesToRadians(dec))*std::cos(qDegreesToRadians(d))*std::pow(std::sin(qDegreesToRadians((r-ra)*15)/2),2);
    return qRadiansToDegrees(2*std::asin(std::sqrt(std::clamp(a,0.0,1.0))))*60;
}
inline bool correct(double ra,double dec,double reportedRa,double reportedDec,double solvedRa,double solvedDec,double &outRa,double &outDec) {
    if (!valid(ra,dec) || !valid(reportedRa,reportedDec) || !valid(solvedRa,solvedDec)
        || separation(reportedRa,reportedDec,solvedRa,solvedDec)>300) return false;
    using V=std::array<double,3>;
    const auto vector=[](double r,double d)->V { r=qDegreesToRadians(r*15); d=qDegreesToRadians(d); return {std::cos(d)*std::cos(r),std::cos(d)*std::sin(r),std::sin(d)}; };
    const auto cross=[](V a,V b)->V { return {a[1]*b[2]-a[2]*b[1],a[2]*b[0]-a[0]*b[2],a[0]*b[1]-a[1]*b[0]}; };
    const auto a=vector(solvedRa,solvedDec), b=vector(reportedRa,reportedDec), t=vector(ra,dec);
    const auto v=cross(a,b), vt=cross(v,t), vvt=cross(v,vt);
    const double c=a[0]*b[0]+a[1]*b[1]+a[2]*b[2];
    V result;
    for (int i=0;i<3;++i) result[i]=t[i]+vt[i]+vvt[i]/(1+c);
    outRa=std::fmod(qRadiansToDegrees(std::atan2(result[1],result[0]))/15+24,24);
    outDec=qRadiansToDegrees(std::atan2(result[2],std::hypot(result[0],result[1])));
    return valid(outRa,outDec);
}
}
class CenteringController final : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
    Q_PROPERTY(bool succeeded READ succeeded NOTIFY changed)
    Q_PROPERTY(QString status READ status NOTIFY changed)
    Q_PROPERTY(QString targetName READ targetName NOTIFY changed)
    Q_PROPERTY(int corrections READ corrections NOTIFY changed)
    Q_PROPERTY(double separationArcmin READ separationArcmin NOTIFY changed)
public:
    CenteringController(DeviceManager *devices,PlateSolver *solver,QObject *parent=nullptr);
    ~CenteringController() override;
    bool busy() const { return m_phase!=Idle; }
    bool succeeded() const { return m_succeeded; }
    QString status() const { return m_status; }
    QString targetName() const { return m_name; }
    int corrections() const { return m_corrections; }
    double separationArcmin() const { return m_separation; }
    Q_INVOKABLE bool start(double ra,double dec,const QString &name,double seconds,double fov,double tolerance=1,int maxCorrections=3);
    Q_INVOKABLE void cancel();
signals:
    void changed();
    void notice(const QString &message);
private:
    friend class SequenceController;
    bool startForSequence(double ra,double dec,const QString &name,double seconds,double tolerance,int maxCorrections);
    bool m_sequenceRun=false;
    enum Phase { Idle,Slewing,Settling,Solving,Stopping,Completing };
    void tick();
    void move(double ra,double dec);
    void stop(const QString &reason);
    void finish(bool success,const QString &message);
    DeviceManager *m_devices;
    PlateSolver *m_solver;
    QTimer m_timer;
    QElapsedTimer m_elapsed;
    Phase m_phase=Idle;
    QString m_status, m_name, m_stopReason;
    bool m_succeeded=false;
    int m_corrections=0,m_limit=3;
    double m_ra=0,m_dec=0,m_seconds=2,m_fov=0,m_tolerance=1,m_reportedRa=0,m_reportedDec=0;
    double m_separation=qQNaN();
};
