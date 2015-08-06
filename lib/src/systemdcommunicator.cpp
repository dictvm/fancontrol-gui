/*
 * Copyright (C) 2015  Malte Veerman <maldela@halloarsch.de>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 */

#include "systemdcommunicator.h"

#include <KF5/KAuth/kauthexecutejob.h>

using namespace KAuth;

#include <QDebug>
#include <QVariant>


SystemdCommunicator::SystemdCommunicator(QObject *parent) : QObject(parent)
{
    setServiceName("fancontrol");
}

void SystemdCommunicator::setServiceName(const QString &name)
{
    if (name != m_serviceName)
    {
        m_serviceName = name;

        if (serviceExists())
        {
            if (m_serviceInterface)
            {
                QDBusConnection::systemBus().disconnect("org.freedesktop.systemd1",
                                                        m_serviceObjectPath,
                                                        "org.freedesktop.DBus.Properties",
                                                        "PropertiesChanged",
                                                        this,
                                                        SLOT(updateServiceProperties(QString, QVariantMap, QStringList)));
                m_serviceInterface->deleteLater();
            }
            
            QVariantList arguments;
            arguments << QVariant(m_serviceName + ".service");
            QDBusMessage dbusreply = m_managerInterface->callWithArgumentList(QDBus::AutoDetect, "LoadUnit", arguments);
            if (dbusreply.type() == QDBusMessage::ErrorMessage)
            {
                m_error = dbusreply.errorMessage();
                emit errorChanged();
                m_serviceObjectPath.clear();
            }
            else
            {
                m_serviceObjectPath = qdbus_cast<QDBusObjectPath>(dbusreply.arguments().at(0)).path();

                m_serviceInterface = new QDBusInterface("org.freedesktop.systemd1",
                                                        m_serviceObjectPath,
                                                        "org.freedesktop.systemd1.Unit",
                                                        QDBusConnection::systemBus(),
                                                        this);
                QDBusConnection::systemBus().connect("org.freedesktop.systemd1",
                                                     m_serviceObjectPath,
                                                     "org.freedesktop.DBus.Properties",
                                                     "PropertiesChanged",
                                                     this,
                                                     SLOT(updateServiceProperties(QString, QVariantMap, QStringList)));
            }
        }
    }

    emit serviceNameChanged();
    emit serviceEnabledChanged();
    emit serviceActiveChanged();
}

bool SystemdCommunicator::serviceExists()
{
    if (m_serviceInterface && m_serviceInterface->isValid())
        return true;
    
    QDBusMessage dbusreply;

    if (m_managerInterface && m_managerInterface->isValid())
        dbusreply = m_managerInterface->call(QDBus::AutoDetect, "ListUnitFiles");

    if (dbusreply.type() == QDBusMessage::ErrorMessage)
    {
        m_error = dbusreply.errorMessage();
        emit errorChanged();
        return false;
    }
    SystemdUnitFileList list = qdbus_cast<SystemdUnitFileList>(dbusreply.arguments().at(0));

    foreach (const SystemdUnitFile &unitFile, list)
    {
        if (unitFile.path.contains(m_serviceName + ".service"))
        {
            m_error = "Success";
            emit errorChanged();
            return true;
        }
    }

    m_error = "Service " + m_serviceName + " doesn't exist";
    emit errorChanged();
    qDebug() << "Service does not exist!";
    return false;
}

bool SystemdCommunicator::serviceActive()
{
    if (serviceExists())
    {
        if (m_serviceInterface->property("ActiveState").toString() == "active")
            return true;
    }
    return false;
}

bool SystemdCommunicator::serviceEnabled()
{
    if (serviceExists())
    {
        if (m_serviceInterface->property("UnitFileState").toString() == "enabled")
        {
            return true;
        }
    }
    return false;
}

void SystemdCommunicator::setServiceEnabled(bool enabled)
{
    if (serviceExists() && enabled != serviceEnabled())
    {
        QVariantList arguments;
        QStringList files = QStringList() << m_serviceName + ".service";

        if (enabled)
        {
            arguments << files << false << true;
            dbusAction("EnableUnitFiles", arguments);
            dbusAction("Reload");
        }
        else
        {
            arguments << files << false;
            dbusAction("DisableUnitFiles", arguments);
            dbusAction("Reload");
        }
        emit serviceEnabledChanged();
    }
}

void SystemdCommunicator::setServiceActive(bool active)
{
    if (serviceExists())
    {
        if (active && !serviceActive())
        {
            QVariantList args;
            args << m_serviceName + ".service" << "replace";
            dbusAction("ReloadOrRestartUnit", args);
            emit serviceActiveChanged();
        }
        else if (!active && serviceActive()) 
        {
            QVariantList args;
            args << m_serviceName + ".service" << "replace";
            dbusAction("StopUnit", args);
            emit serviceActiveChanged();
        }
        else 
            qDebug() << "ServiceActive already " << active;
    }
}

void SystemdCommunicator::dbusAction(const QString &method, const QVariantList &arguments)
{
    QDBusMessage dbusreply;

    if (m_managerInterface && m_managerInterface->isValid())
    {
        if (arguments.isEmpty())
            dbusreply = m_managerInterface->call(QDBus::AutoDetect, method);
        else
            dbusreply = m_managerInterface->callWithArgumentList(QDBus::AutoDetect, method, arguments);
    }

    if (dbusreply.type() == QDBusMessage::ErrorMessage)
    {
#ifndef NO_KF5_AUTH
        if (dbusreply.errorMessage() == "Interactive authentication required.")
        {
            Action action("fancontrol.gui.helper.action");
            action.setHelperId("fancontrol.gui.helper");
            QVariantMap map;
            map["action"] = "dbusaction";
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
                m_error = method + " succeeded";
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
        m_error = method + " succeeded";
        emit errorChanged();
    }
}

void SystemdCommunicator::restartService()
{
    if (serviceExists())
    {
        QVariantList args;
        args << m_serviceName + ".service" << "replace";
        dbusAction("ReloadOrRestartUnit", args);
    }
}

void SystemdCommunicator::updateServiceProperties(QString, QVariantMap propchanged, QStringList)
{ 
    qDebug() << "Properties changed...";
    
    if (propchanged.value("ActiveState").isValid())
    {
        emit serviceActiveChanged();
        qDebug() << "Active changed...";
    }
    if (propchanged.value("UnitFileState").isValid())
    {
        emit serviceEnabledChanged();
        qDebug() << "Enabled changed...";
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
