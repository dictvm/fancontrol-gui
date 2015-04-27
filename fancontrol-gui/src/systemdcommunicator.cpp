#include "systemdcommunicator.h"

#ifndef NO_KF5_AUTH
#include <KF5/KAuth/kauthexecutejob.h>

using namespace KAuth;
#endif

#include <QDebug>
#include <QVariant>
#include <QScopedPointer>


SystemdCommunicator::SystemdCommunicator(QObject *parent) : QObject(parent)
{
    m_serviceName = "fancontrol";
    m_error = "Success";

    m_managerInterface = new QDBusInterface("org.freedesktop.systemd1",
                                     "/org/freedesktop/systemd1",
                                     "org.freedesktop.systemd1.Manager",
                                     QDBusConnection::systemBus(),
                                     this);
    serviceExists();
}

void SystemdCommunicator::setServiceName(const QString &name)
{
    if (name != m_serviceName)
    {
        m_serviceName = name;
        emit serviceNameChanged();
    }
}

bool SystemdCommunicator::serviceExists()
{
    QDBusMessage dbusreply;

    if (m_managerInterface->isValid())
        dbusreply = m_managerInterface->call(QDBus::AutoDetect, "ListUnitFiles");

    if (dbusreply.type() == QDBusMessage::ErrorMessage)
    {
        m_error = dbusreply.errorMessage();
        emit errorChanged();
        return false;
    }
    SystemdUnitFileList list = qdbus_cast<SystemdUnitFileList>(dbusreply.arguments().first());

    foreach (const SystemdUnitFile &unitFile, list)
    {
        if (unitFile.path.contains(m_serviceName + ".service"))
        {
            QList<QVariant> arguments;
            arguments << QVariant(m_serviceName + ".service");
            dbusreply = m_managerInterface->callWithArgumentList(QDBus::AutoDetect, "LoadUnit", arguments);
            if (dbusreply.type() == QDBusMessage::ErrorMessage)
            {
                m_error = dbusreply.errorMessage();
                emit errorChanged();
                return false;
            }
            m_servicePath = qdbus_cast<QDBusObjectPath>(dbusreply.arguments().first()).path();

            if (m_serviceInterface)
                m_serviceInterface->deleteLater();

            m_serviceInterface = new QDBusInterface("org.freedesktop.systemd1",
                                                    m_servicePath,
                                                    "org.freedesktop.systemd1.Unit",
                                                    QDBusConnection::systemBus(),
                                                    this);
            m_error = "Success";
            emit errorChanged();
            return true;
        }
    }

    m_error = "Service " + m_serviceName + " doesn't exist";
    emit errorChanged();
    return false;
}

bool SystemdCommunicator::serviceActive()
{
    if (m_serviceInterface->isValid())
    {
        if (m_serviceInterface->property("ActiveState").toString() == "active")
        {
            return true;
        }
    }
    return false;
}

void SystemdCommunicator::dbusAction(const QString &method, const QList<QVariant> &arguments)
{
    QDBusMessage dbusreply;

    if (m_managerInterface->isValid())
        dbusreply = m_managerInterface->callWithArgumentList(QDBus::AutoDetect, method, arguments);

    if (dbusreply.type() == QDBusMessage::ErrorMessage)
    {
#ifndef NO_KF5_AUTH
        if (dbusreply.errorMessage() == "Interactive authentication required.")
        {
            Action action("fancontrol.gui.helper.dbusaction");
            action.setHelperId("fancontrol.gui.helper");
            QVariantMap map;
            map["method"] = method;
            map["arguments"] = arguments;
            action.setArguments(map);

            ExecuteJob *reply = action.execute();

            if (!reply->exec())
            {
                m_error = reply->errorString();
                emit errorChanged();
            }
            else
            {
                m_error = "Success";
                emit errorChanged();
            }
            return;
        }
#endif
        m_error = dbusreply.errorMessage();
        emit errorChanged();
    }
    else
    {
        m_error = "Success";
        emit errorChanged();
    }
}

QDBusArgument& operator <<(QDBusArgument &argument, const SystemdUnitFile &unitFile)
{
    argument.beginStructure();
    argument << unitFile.path << unitFile.state;
    argument.endStructure();
    return argument;
}

const QDBusArgument& operator >>(const QDBusArgument &argument, SystemdUnitFile &unitFile)
{
    argument.beginStructure();
    argument >> unitFile.path >> unitFile.state;
    argument.endStructure();
    return argument;
}