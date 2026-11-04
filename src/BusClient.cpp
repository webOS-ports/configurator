// Copyright (c) 2009-2024 LG Electronics, Inc.
// Copyright (c) 2026 Herman van Hazendonk <github.com@herrie.org>
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
// SPDX-License-Identifier: Apache-2.0

//->Start of API documentation comment block
/**
@page com_palm_configurator com.palm.configurator

@brief Responsible for doing the following on behalf of applications and services:
       1) Registering Db8 kinds and permissions
       2) Defining file cache types
       3) Registering persistent activities and db8 watches

@{
@}
//->End of API documentation comment block

*/

#include "BusClient.h"
#include "core/MojServiceMessage.h"
#include "ActivityConfigurator.h"
#include "DbKindConfigurator.h"
#include "DbPermissionsConfigurator.h"
#include "FileCacheConfigurator.h"

#include <unistd.h>

BusClient::BusMethods::BusMethods(BusClient& client)
: m_client(client)
{
	LOG_TRACE("Entering function %s", __FUNCTION__);

	MojErr err = addMethod("run", (Callback) &BusMethods::Run);
	if (err)
		LOG_CRITICAL(MSGID_BUS_CLIENT_ERROR, 0, "failed to register run method: %i", err);

	err = addMethod("scan", (Callback) &BusMethods::Scan);
	if (err)
		LOG_CRITICAL(MSGID_BUS_CLIENT_ERROR, 0, "failed to register scan method: %i", err);

	err = addMethod("rescan", (Callback) &BusMethods::Rescan);
	if (err)
		LOG_CRITICAL(MSGID_BUS_CLIENT_ERROR, 0, "failed to register rescan method: %i", err);

	err = addMethod("unconfigure", (Callback) &BusMethods::Unconfigure);
	if (err)
		LOG_CRITICAL(MSGID_BUS_CLIENT_ERROR, 0, "failed to register unconfigure method: %i", err);
}

static MojErr getTypes(const MojObject& typesArray, BusClient::ScanTypes& bitmask)
{
	LOG_TRACE("Entering function %s", __FUNCTION__);

	MojObject::ConstArrayIterator it = typesArray.arrayBegin();
	if (it == nullptr)
		MojErrThrowMsg(MojErrInvalidMsg, "'types' not an array");

	for (MojObject::ConstArrayIterator ni = typesArray.arrayEnd(); it != ni; ++it) {
		const MojObject& element = *it;
		MojString type;
		MojErr err;

		err = element.stringValue(type);
		MojErrCheck(err);

		if (type == "dbkinds")
			bitmask |= BusClient::DBKINDS;
		else if (type == "dbpermissions")
			bitmask |= BusClient::DBPERMISSIONS;
		else if (type == "filecache")
			bitmask |= BusClient::FILECACHE;
		else if (type == "activities")
			bitmask |= BusClient::ACTIVITIES;
		else
			MojErrThrowMsg(MojErrInvalidMsg, "unrecognized type '%s'", type.data());
	}

	return MojErrNone;
}

//->Start of API documentation comment block
/**
@page com_palm_configurator com.palm.configurator
@{
@section com_palm_configurator_run run

This method will scan all folders and run all configuration files.

@par Parameters
Name | Required | Type | Description
-----|----------|------|------------
types | yes  | Array | List of different configuration types. Types are dbkinds, filecache, activities.

@par Returns(Call)
Name | Required | Type | Description
-----|----------|------|------------
returnValue | yes | Boolean | True

@par Returns(Subscription)
None

@}
*/
//->End of API documentation comment block

MojErr BusClient::BusMethods::Run(MojServiceMessage* msg, MojObject& payload)
{
	LOG_TRACE("Entering function %s", __FUNCTION__);

	m_client.RunNextConfigurator();

	if (WorkEnqueued((Callback) &BusMethods::Run, msg, payload))
		return MojErrNone;

	try {
		MojObject types;
		MojErr err = payload.getRequired("types", types);
		MojErrCheck(err);

		ScanTypes bitmask;
		err = getTypes(types, bitmask);
		MojErrCheck(err);

		m_client.m_msg.reset(msg);
		m_client.Run(bitmask);

	} catch (const std::exception& e) {
		MojErrThrowMsg(MojErrInternal, "%s", e.what());
	} catch(...) {
		MojErrThrowMsg(MojErrInternal, "uncaught exception");
	}

	return MojErrNone;
}

bool BusClient::BusMethods::WorkEnqueued(Callback callback, MojServiceMessage *msg, MojObject &payload)
{
	LOG_TRACE("Entering function %s", __FUNCTION__);

	if (m_client.m_msg.get() == nullptr)
		return false;

	BusClient::PendingWork pending;
	pending.instance = this;
	pending.callback = callback;
	pending.msg.reset(msg);
	pending.payload = payload;
	m_client.m_pending.push_back(pending);
	return true;
}


//->Start of API documentation comment block
/**
@page com_palm_configurator com.palm.configurator
@{
@section com_palm_configurator_rescan rescan

Force re-run all configurations.

@par Parameters
Name | Required | Type | Description
-----|----------|------|------------
id | yes  | String | Application Id
type | yes | String | Either 'app' or 'service'
location | yes | String | Indicates if it is a system app or a third party app.

@par Returns(Call)
Name | Required | Type | Description
-----|----------|------|------------
returnValue | yes | Boolean | True

@par Returns(Subscription)
None

@}
*/
//->End of API documentation comment block

MojErr BusClient::BusMethods::Rescan(MojServiceMessage* msg, MojObject& payload)
{
	LOG_TRACE("Entering function %s", __FUNCTION__);

	if (WorkEnqueued((Callback) &BusMethods::Rescan, msg, payload))
		return MojErrNone;

	return ScanRequest(msg, payload, BusClient::ForceRescan);
}

//->Start of API documentation comment block
/**
@page com_palm_configurator com.palm.configurator
@{
@section com_palm_configurator_scan scan

Run configuration in safe mode. It doesn't re-run any configurations that were run already.

@par Parameters
Name | Required | Type | Description
-----|----------|------|------------
id | yes  | String | Application Id
type | yes | String | Either 'app' or 'service'
location | yes | String | Indicates if it is a system app or a third party app.

@par Returns(Call)
Name | Required | Type | Description
-----|----------|------|------------
returnValue | yes | Boolean | True

@par Returns(Subscription)
None

@}
*/
//->End of API documentation comment block

MojErr BusClient::BusMethods::Scan(MojServiceMessage* msg, MojObject& payload)
{
	LOG_TRACE("Entering function %s", __FUNCTION__);

	if (WorkEnqueued((Callback) &BusMethods::Scan, msg, payload))
		return MojErrNone;

	return ScanRequest(msg, payload, BusClient::LazyScan);
}

MojErr BusClient::BusMethods::ScanRequest(MojServiceMessage* msg, MojObject& payload, ConfigurationMode confmode)
{
	LOG_TRACE("Entering function %s", __FUNCTION__);

	m_client.RunNextConfigurator();

	try {
		MojErr err;
		if (payload.type() != MojObject::TypeArray) {
			MojErrThrowMsg(MojErrInternal, "invalid message format");
		}

		m_client.m_msg.reset(msg);

		for (MojObject::ConstArrayIterator it = payload.arrayBegin(); it != payload.arrayEnd(); ++it) {
			const MojObject& request = *it;
			MojString locationStr;
			MojString typeStr;
			MojString app;
			BusClient::PackageType type;
			BusClient::PackageLocation location;

			err = request.getRequired("id", app);
			MojErrCheck(err);

			err = request.getRequired("type", typeStr);
			MojErrCheck(err);

			err = request.getRequired("location", locationStr);
			MojErrCheck(err);

			if (typeStr == "app") {
				type = BusClient::Application;
			} else if (typeStr == "service") {
				type = BusClient::Service;
			} else {
				MojErrThrowMsg(MojErrInvalidMsg, "unrecognized type '%s'", typeStr.data());
			}

			if (locationStr == "system") {
				location = BusClient::System;
			} else if (locationStr == "third party") {
				location = BusClient::ThirdParty;
			} else {
				MojErrThrowMsg(MojErrInvalidMsg, "unrecognized type '%s'", locationStr.data());
			}

			m_client.Scan(confmode, app, type, location);
		}

	} catch (const std::exception& e) {
		MojErrThrowMsg(MojErrInternal, "%s", e.what());
	} catch (...) {
		MojErrThrowMsg(MojErrInternal, "uncaught exception");
	}

	return MojErrNone;
}

//->Start of API documentation comment block
/**
@page com_palm_configurator com.palm.configurator
@{
@section com_palm_configurator_unconfigure unconfigure

To remove/unconfigure all files for an application.

@par Parameters
Name | Required | Type | Description
-----|----------|------|------------
id | yes  | String | Application or Service Id
type | yes | String | Either 'app' or 'service'
location | yes | String | Indicates if it is a system or a third party app.

@par Returns(Call)
Name | Required | Type | Description
-----|----------|------|------------
returnValue | yes | Boolean | True

@par Returns(Subscription)
None

@}
*/
//->End of API documentation comment block

MojErr BusClient::BusMethods::Unconfigure(MojServiceMessage *msg, MojObject &payload)
{
	LOG_TRACE("Entering function %s", __FUNCTION__);

	m_client.RunNextConfigurator();

	if (WorkEnqueued((Callback) &BusMethods::Unconfigure, msg, payload))
		return MojErrNone;

	try {
		MojErr err;
		if (payload.type() != MojObject::TypeArray) {
			MojErrThrowMsg(MojErrInternal, "invalid message format");
		}

		m_client.m_msg.reset(msg);

		for (MojObject::ConstArrayIterator it = payload.arrayBegin(); it != payload.arrayEnd(); ++it) {
			const MojObject& request = *it;
			MojString locationStr;
			MojString typeStr;
			MojObject typesArray;
			MojString app;
			BusClient::PackageType type;
			BusClient::PackageLocation location;
			BusClient::ScanTypes types;

			err = request.getRequired("id", app);
			MojErrCheck(err);

			err = request.getRequired("type", typeStr);
			MojErrCheck(err);

			if (!request.get("types", typesArray)) {
				types = BusClient::ACTIVITIES | BusClient::FILECACHE | BusClient::DBKINDS | BusClient::DBPERMISSIONS;
			} else {
				err = getTypes(typesArray, types);
				MojErrCheck(err);
			}

			err = request.getRequired("location", locationStr);
			MojErrCheck(err);

			if (typeStr == "app") {
				type = BusClient::Application;
			} else if (typeStr == "service") {
				type = BusClient::Service;
			} else {
				MojErrThrowMsg(MojErrInvalidMsg, "unrecognized type '%s'", typeStr.data());
			}

			if (locationStr == "system") {
				location = BusClient::System;
			} else if (locationStr == "third party") {
				location = BusClient::ThirdParty;
			} else {
				MojErrThrowMsg(MojErrInvalidMsg, "unrecognized type '%s'", locationStr.data());
			}

			m_client.Unconfigure(app, type, location, types);
		}

	} catch (const std::exception& e) {
		MojErrThrowMsg(MojErrInternal, "%s", e.what());
	} catch (...) {
		MojErrThrowMsg(MojErrInternal, "uncaught exception");
	}

	return MojErrNone;
}

BusClient::BusClient() = default;

BusClient::~BusClient() = default;

MojRefCountedPtr<MojServiceRequest> BusClient::CreateRequest()
{
	MojRefCountedPtr<MojServiceRequest> req;
	(void)m_service.createRequest(req);
	return req;
}

MojRefCountedPtr<MojServiceRequest> BusClient::CreateRequest(const char *forgedAppId)
{
	MojRefCountedPtr<MojServiceRequest> req;
	(void)m_service.createRequest(req, forgedAppId);
	return req;
}

MojErr BusClient::open()
{
	LOG_TRACE("Entering function %s", __FUNCTION__);

	// set up luna-service

	MojErr err = Base::open();
	MojErrCheck(err);

	err = m_service.open(SERVICE_NAME);
	MojErrCheck(err);

	err = m_service.attach(m_reactor.impl());
	MojErrCheck(err);

	// If we're not launched as a service, then we're launching at boot,
	// which means we should run all the configurators.
	if (!m_launchedAsService) {
		LOG_DEBUG("Not run as dynamic service - run startup configurations");
		Run(DBKINDS | DBPERMISSIONS | FILECACHE | ACTIVITIES);
		RunNextConfigurator();
	} else {
		LOG_DEBUG("launched as service");

		m_methods.reset(new BusMethods(*this));
		MojAllocCheck(m_methods.get());

		err = m_service.addCategory(MojLunaService::DefaultCategory, m_methods.get());
	}
	LOG_DEBUG("bus client %p", static_cast<void*>(this));

	return MojErrNone;
}

MojErr BusClient::handleArgs(const StringVec& args)
{
	MojErr err = Base::handleArgs(args);
	MojErrCheck(err);

	if (!args.empty() && args[0] == "service")
		m_launchedAsService = true;

	return MojErrNone;
}

std::string BusClient::appConfDir(const MojString& appId, PackageType type, PackageLocation location)
{
	LOG_TRACE("Entering function %s", __FUNCTION__);
	std::string confPath;

	switch (location) {
	case System:
		confPath = "/";
		break;
	case ThirdParty:
		confPath = BASE_CRYPTOFS;
		break;
	}

	confPath += BASE_PALM_OFFSET;

	switch (type) {
	case Application:
		confPath += APPS_DIR;
		break;
	case Service:
		confPath += SERVICES_DIR;
		break;
	}
	confPath.append(appId.begin(), appId.end());
	if (access(confPath.c_str(), R_OK) != 0) {
		m_wrongAplication = true;
	}

	return confPath + CONF_SUBDIR;
}

void BusClient::Run(ScanTypes bitmask)
{
	MojString id;
	ScanDir(id, Configurator::Configure, ROOT_BASE_DIR, bitmask, Configurator::ConfigUnknown, DeprecatedDbKind);
}

void BusClient::ScanDir(const MojString& _id, Configurator::RunType scanType, const std::string &baseDir, ScanTypes bitmask, Configurator::ConfigType configType, AdditionalFileTypes types)
{
	const std::string id(_id.data(), _id.length());

	if (m_shuttingDown) {
		LOG_DEBUG("Aborting shutdown - request received");
		assert(m_timerTimeout != 0);
		(void)g_source_remove(m_timerTimeout);
		m_timerTimeout = 0;
		assert(m_shuttingDown);
		m_shuttingDown = false;
	}

	if (bitmask & DBKINDS) {
		if (types & DeprecatedDbKind) {
			// deprecated
			LOG_WARNING(MSGID_BUS_CLIENT_ERROR, 1,
					PMLOGKS("directory", baseDir.c_str()),
					"Scanning deprecated mojodb config directory under %s", baseDir.c_str());
			ConfiguratorPtr oldDbKindConfigurator(new DbKindConfigurator(id, configType, scanType, *this, baseDir + OLD_DB_KIND_DIR));
			m_configurators.push_back(oldDbKindConfigurator);
		}

		ConfiguratorPtr dbKindConfigurator(new DbKindConfigurator(id, configType, scanType, *this, baseDir + DB_KIND_DIR));
		m_configurators.push_back(dbKindConfigurator);

        ConfiguratorPtr mediaDbKindConfigurator(new MediaDbKindConfigurator(id, configType, scanType, *this, baseDir + MEDIADB_KIND_DIR));
        m_configurators.push_back(mediaDbKindConfigurator);

		ConfiguratorPtr tempDbKindConfigurator(new TempDbKindConfigurator(id, configType, scanType, *this, baseDir + TEMPDB_KIND_DIR));
		m_configurators.push_back(tempDbKindConfigurator);

	}

	if (bitmask & DBPERMISSIONS) {
		ConfiguratorPtr dbPermsConfigurator(new DbPermissionsConfigurator(id, configType, scanType, *this, baseDir + DB_PERMISSIONS_DIR));
		m_configurators.push_back(dbPermsConfigurator);

        ConfiguratorPtr mediaDbPermsConfigurator(new MediaDbPermissionsConfigurator(id, configType, scanType, *this, baseDir + MEDIADB_PERMISSIONS_DIR));
        m_configurators.push_back(mediaDbPermsConfigurator);

		ConfiguratorPtr tempDbPermsConfigurator(new TempDbPermissionsConfigurator(id, configType, scanType, *this, baseDir + TEMPDB_PERMISSIONS_DIR));
		m_configurators.push_back(tempDbPermsConfigurator);
	}

	if (bitmask & FILECACHE) {
		ConfiguratorPtr fileCacheConfigurator(new FileCacheConfigurator(id, configType, scanType, *this, baseDir + FILE_CACHE_CONFIG_DIR));
		m_configurators.push_back(fileCacheConfigurator);
	}

	if (bitmask & ACTIVITIES) {
		ConfiguratorPtr activityConfigurator(new ActivityConfigurator(id, configType, scanType, *this, baseDir + ACTIVITY_CONFIG_DIR));
		m_configurators.push_back(activityConfigurator);
	}
}

void BusClient::Scan(ConfigurationMode confmode, const MojString &appId, PackageType type, PackageLocation location)
{
	LOG_TRACE("Entering function %s", __FUNCTION__);

	LOG_DEBUG("Scanning %s %d@%d", appId.data(), type, location);
	std::string confPath = appConfDir(appId, type, location);
	Configurator::RunType mode = Configurator::Configure;
	switch (confmode) {
	case ForceRescan:
		mode = Configurator::Reconfigure;
		break;
	case LazyScan:
		mode = Configurator::Configure;
		break;
	}

	ScanDir(appId, mode, confPath, DBKINDS | DBPERMISSIONS | FILECACHE | ACTIVITIES, PackageTypeToConfigType(type));

	LOG_DEBUG("Scan of %s finished", appId.data());
}

void BusClient::Unconfigure(const MojString &appId, PackageType type, PackageLocation location, ScanTypes bitmask)
{
	std::string confPath = appConfDir(appId, type, location);

	ScanDir(appId, Configurator::RemoveConfiguration, confPath, bitmask, PackageTypeToConfigType(type));
	LOG_DEBUG("Removal of %s finished", appId.data());
}

void BusClient::RunNextConfigurator()
{
	LOG_TRACE("Entering function %s", __FUNCTION__);

	// Schedule an event to run the next configurator once the stack is unwound.
	(void)g_idle_add(&BusClient::IterateConfiguratorsCallback, this);
}

gboolean BusClient::IterateConfiguratorsCallback(gpointer data)
{
	LOG_TRACE("Entering function %s", __FUNCTION__);
	BusClient* client = static_cast<BusClient*>(data);

	if (client->m_configuratorsCompleted == client->m_configurators.size()) {
		if (!client->m_shuttingDown) {
			LOG_DEBUG("No more configurators left (%zu configurations completed, %zu configurations failed), shutting down.", Configurator::ConfigureOk().size(), Configurator::ConfigureFailure().size());
			client->ScheduleShutdown();
			return client->m_msg.get() != nullptr;
		}
		return false;
	}

	gboolean configurationsRemaining = FALSE;
	// iterate through 1 file in each configurator
	for (size_t i = 0, ni = client->m_configurators.size(); i < ni; ++i) {
		bool exceptionThrown = true;
		try {
				ConfiguratorPtr configurator = client->m_configurators[i];
				if (configurator.get() == nullptr)
					continue;

				if (!configurator->Run())
					configurationsRemaining = TRUE;
				exceptionThrown = false;
		} catch (const std::exception& e) {
			LOG_CRITICAL(MSGID_BUS_CLIENT_ERROR, 1,
					PMLOGKS("exception", e.what()),
					"exception while running configurator: %s", e.what());
		} catch (...) {
			LOG_CRITICAL(MSGID_BUS_CLIENT_ERROR, 0, "uncaught exception while running configurator");
		}

		// If an exception was thrown, remove it from the queue and keep going
		if (exceptionThrown) {
			client->ConfiguratorComplete(i);
		}
	}

	// return whether or not there are more configurations remaining
	return configurationsRemaining;
}

void BusClient::ConfiguratorComplete(ConfiguratorCollection::iterator configurator)
{
	LOG_TRACE("Entering function %s", __FUNCTION__);

	if (Configurator* done = configurator->get())
		LOG_DEBUG("... configurator %s complete (%p), %zu left.", done->ConfiguratorName(), static_cast<void*>(done), m_configurators.size() - 1);

	configurator->reset();
	m_configuratorsCompleted++;
	RunNextConfigurator();
}

void BusClient::ConfiguratorComplete(Configurator* configurator)
{
	for (auto i = m_configurators.begin(); i != m_configurators.end(); ++i) {
		if (i->get() == configurator) {
			ConfiguratorComplete(i);
			return;
		}
	}
}

void BusClient::ConfiguratorComplete(size_t configuratorIndex)
{
	if (configuratorIndex < m_configurators.size())
		ConfiguratorComplete(m_configurators.begin() + static_cast<std::ptrdiff_t>(configuratorIndex));
}

void BusClient::ScheduleShutdown()
{
	LOG_TRACE("Entering function %s", __FUNCTION__);
	// Reply to the "run" message now that we're done
	if (m_launchedAsService && m_msg.get()) {
		const Configurator::ConfigCollection& ok = Configurator::ConfigureOk();
		const Configurator::ConfigCollection& failed = Configurator::ConfigureFailure();

		if (m_wrongAplication) {
			MojString response;
			(void)response.appendFormat("Application or service doesn't exist");
			m_wrongAplication = false;
            if(m_msg->replyError(MojErrInternal, response.data()) != MojErrNone) {
                LOG_WARNING(MSGID_SHUTDOWN_ERROR, 1, PMLOGKS("Response", response.data()), "Application or service doesn't exist");
            }
		} else if (!failed.empty()) {
			MojString response;
			(void)response.appendFormat("Partial configuration - %zu ok, %zu failed", ok.size(), failed.size());
            if(m_msg->replyError(MojErrInternal, response.data()) != MojErrNone) {
                LOG_WARNING(MSGID_SHUTDOWN_ERROR, 1, PMLOGKS("Response", response.data()), "Partial configuration");
            }
		} else {
			MojObject response;
			(void)response.putInt("configured", static_cast<MojInt64>(ok.size()));
            if(m_msg->replySuccess(response) != MojErrNone) {
                LOG_WARNING(MSGID_SHUTDOWN_ERROR, 0, "Configured");
            }
		}
		m_msg.reset();
	}

	if (!m_pending.empty()) {
		LOG_DEBUG("%zu pending service calls to handle remaining", m_pending.size());

		// still more pending work
		m_configuratorsCompleted = 0;
		m_configurators.clear();
		Configurator::ResetConfigStats();

		const PendingWork &pending = m_pending.back();
		(void)(pending.instance->*(pending.callback))(pending.msg.get(), pending.payload);

		m_pending.pop_back();
		return;
	}

	LOG_DEBUG("No more pending service calls to handle - scheduling shutdown");

	// Schedule an event to shutdown once the stack is unwound.
	if (m_timerTimeout == 0) {
		// this is to work around around a race condition where the LSCall is delivered
		// after we shutdown causing us to start back up - give some time for the LSCall
		// to get delivered.  NOV-114626.  This needs a proper fix within ls2 (can't be
		// worked around anywhere else).
		m_timerTimeout = g_timeout_add_full(G_PRIORITY_DEFAULT_IDLE, /* timer priority */
		    500, /* timeout in ms */
		    &BusClient::ShutdownCallback, // callback
		    this, /* callback data */
		    nullptr /*destroy notify callback*/
		);
	}

	m_shuttingDown = true;
}

gboolean BusClient::ShutdownCallback(gpointer data)
{
	LOG_TRACE("Entering function %s", __FUNCTION__);
	BusClient* client = static_cast<BusClient*>(data);
	client->m_timerTimeout = 0;
	client->shutdown();
	return false; // return false to make sure we don't get called again
}
