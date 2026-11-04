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

#ifndef BUSCLIENT_H_
#define BUSCLIENT_H_

#define MOJ_DEBUG_LOGGING
#ifndef MOJ_DEBUG
#define MOJ_DEBUG
#endif

#include "core/MojReactorApp.h"
#include "core/MojGmainReactor.h"
#include "luna/MojLunaService.h"
#include "Configurator.h"
#include "Flags.h"
#include "Log.h"

#include <cassert>
#include <string>
#include <vector>

class BusClient : public MojReactorApp<MojGmainReactor>
{
public:
	enum ScanType : unsigned {
		DBKINDS       = 1 << 0,
		FILECACHE     = 1 << 1,
		DBPERMISSIONS = 1 << 2,
		ACTIVITIES    = 1 << 3,
	};
	DECLARE_FLAGS(ScanTypes, ScanType);

	enum AdditionalFileType : unsigned {
		None             = 0,
		DeprecatedDbKind = 1 << 0,
	};
	DECLARE_FLAGS(AdditionalFileTypes, AdditionalFileType);

	BusClient();
	~BusClient() override;

	MojRefCountedPtr<MojServiceRequest> CreateRequest();
	MojRefCountedPtr<MojServiceRequest> CreateRequest(const char* forgedAppId);
	MojErr open() override;
	MojErr handleArgs(const StringVec& args) override;
	void ConfiguratorComplete(Configurator* configurator);
	void ConfiguratorComplete(size_t configuratorIndex);

private:
	enum PackageType {
		Application,
		Service,
	};

	enum PackageLocation {
		System,
		ThirdParty,
	};

	enum ConfigurationMode {
		ForceRescan, ///< run every configuration again
		LazyScan,    ///< only run the configurations that have not run yet
	};

	class BusMethods : public MojService::CategoryHandler
	{
	public:
		explicit BusMethods(BusClient& client);

	private:
		// The signatures are MojService::CategoryHandler::Callback's: they are called
		// through it.
		bool WorkEnqueued(Callback callback, MojServiceMessage* msg, const MojObject& payload);

		MojErr Run(MojServiceMessage* msg, const MojObject& payload);
		MojErr Rescan(MojServiceMessage* msg, const MojObject& payload);
		MojErr Scan(MojServiceMessage* msg, const MojObject& payload);
		MojErr ScanRequest(MojServiceMessage* msg, const MojObject& payload, ConfigurationMode confmode);
		MojErr Unconfigure(MojServiceMessage* msg, const MojObject& payload);

		BusClient& m_client;
	};

	struct PendingWork {
		BusMethods* instance;
		MojService::CategoryHandler::Callback callback;
		MojRefCountedPtr<MojServiceMessage> msg;
		MojObject payload;
	};

	// One {"id", "type", "location"[, "types"]} entry of a scan, rescan or unconfigure call.
	struct PackageRequest {
		MojString id;
		PackageType type;
		PackageLocation location;
		ScanTypes types;
	};

	static MojErr ParsePackageRequests(const MojObject& payload, bool withTypes, std::vector<PackageRequest>& requests);
	static bool IsPackageId(const MojString& id);

	using Base = MojReactorApp<MojGmainReactor>;
	using ConfiguratorPtr = MojRefCountedPtr<Configurator>;
	using ConfiguratorCollection = std::vector<ConfiguratorPtr>;
	using PendingWorkCollection = std::vector<PendingWork>;

	static constexpr const char* SERVICE_NAME            = "com.palm.configurator";
	static constexpr const char* ROOT_BASE_DIR           = "/etc/palm/";
	static constexpr const char* OLD_DB_KIND_DIR         = "db_kinds"; // deprecated
	static constexpr const char* DB_KIND_DIR             = "db/kinds";
	static constexpr const char* DB_PERMISSIONS_DIR      = "db/permissions";
	static constexpr const char* MEDIADB_KIND_DIR        = "mediadb/kinds";
	static constexpr const char* MEDIADB_PERMISSIONS_DIR = "mediadb/permissions";
	static constexpr const char* TEMPDB_KIND_DIR         = "tempdb/kinds";
	static constexpr const char* TEMPDB_PERMISSIONS_DIR  = "tempdb/permissions";
	static constexpr const char* FILE_CACHE_CONFIG_DIR   = "filecache_types";
	static constexpr const char* ACTIVITY_CONFIG_DIR     = "activities";
	static constexpr const char* BASE_CRYPTOFS           = "/media/cryptofs/apps/";
	static constexpr const char* BASE_PALM_OFFSET        = "usr/palm/";
	static constexpr const char* APPS_DIR                = "applications/";
	static constexpr const char* SERVICES_DIR            = "services/";
	static constexpr const char* CONF_SUBDIR             = "/configuration/";

	std::string appConfDir(const MojString& appId, PackageType type, PackageLocation location);

	static Configurator::ConfigType PackageTypeToConfigType(PackageType type)
	{
		switch (type) {
		case Application:
			return Configurator::ConfigApplication;
		case Service:
			return Configurator::ConfigService;
		}
		assert(false);
		return Configurator::ConfigUnknown;
	}

	void Run(ScanTypes bitmask);
	void Scan(ConfigurationMode confmode, const MojString& appid, PackageType type, PackageLocation location);
	void ScanDir(const MojString& id, Configurator::RunType scanType, const std::string& dirBase, ScanTypes bitmask, Configurator::ConfigType configType, AdditionalFileTypes types = None);
	void Unconfigure(const MojString& appId, PackageType type, PackageLocation location, ScanTypes bitmask);

	void RunNextConfigurator();
	void ScheduleShutdown();

	static gboolean IterateConfiguratorsCallback(gpointer data);
	static gboolean ShutdownCallback(gpointer data);

	void ConfiguratorComplete(ConfiguratorCollection::iterator configurator);

	MojLunaService m_service;
	ConfiguratorCollection m_configurators;
	size_t m_configuratorsCompleted = 0;
	MojRefCountedPtr<BusMethods> m_methods;
	bool m_launchedAsService = false;
	MojRefCountedPtr<MojServiceMessage> m_msg;
	bool m_shuttingDown = false;
	bool m_wrongAplication = false;
	PendingWorkCollection m_pending;
	guint m_timerTimeout = 0;
};

DECLARE_OPERATORS_FOR_FLAGS(BusClient::ScanTypes)
DECLARE_OPERATORS_FOR_FLAGS(BusClient::AdditionalFileTypes)

#endif /* BUSCLIENT_H_ */
