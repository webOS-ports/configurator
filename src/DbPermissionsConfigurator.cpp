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

#include "core/MojObject.h"
#include "DbPermissionsConfigurator.h"
#include "BusClient.h"

namespace {

constexpr const char* MOJODB_DB_BUS_ADDRESS = "com.palm.db";
constexpr const char* MOJODB_TEMPDB_BUS_ADDRESS = "com.palm.tempdb";
constexpr const char* MOJODB_MEDIADB_BUS_ADDRESS = "com.webos.mediadb";
constexpr const char* MOJODB_PUTPERMISSIONS_METHOD = "putPermissions";

} // namespace

const char* DbPermissionsConfigurator::ConfiguratorName() const
{
	return "DbPermissionsConfigurator";
}

const char* DbPermissionsConfigurator::ServiceName() const
{
	return MOJODB_DB_BUS_ADDRESS;
}

DbPermissionsConfigurator::DbPermissionsConfigurator(const std::string& id, ConfigType confType, RunType type, BusClient& busClient, const std::string& configDirectory)
	: Configurator(id, confType, type, busClient, configDirectory)
{
}

MojErr DbPermissionsConfigurator::ProcessConfig(const std::string& filePath, MojObject& permissions)
{
	LOG_TRACE("Entering function %s", __FUNCTION__);

	MojObject perms;
	MojErr err = perms.put("permissions", permissions);
	MojErrCheck(err);

	// For an app's or service's own files the request carries its id, so that db8 applies
	// them as its. The root config files are not split up per service or app.
	const std::string& owner = ParentId(filePath);
	MojRefCountedPtr<MojServiceRequest> request = owner.empty()
			? m_busClient.CreateRequest()
			: m_busClient.CreateRequest(owner.c_str());
	return request->send(CreateCallback(filePath)->m_slot, ServiceName(), MOJODB_PUTPERMISSIONS_METHOD, perms);
}

MojErr DbPermissionsConfigurator::ProcessConfigRemoval(const std::string& filePath, MojObject&)
{
	LOG_TRACE("Entering function %s", __FUNCTION__);

	// Nothing to ask db8 for: permissions go when their kind is deleted (NOV-129951).
	UnmarkConfigured(filePath);

	return MojErrInProgress;
}

const char* MediaDbPermissionsConfigurator::ServiceName() const
{
	return MOJODB_MEDIADB_BUS_ADDRESS;
}

const char* TempDbPermissionsConfigurator::ServiceName() const
{
	return MOJODB_TEMPDB_BUS_ADDRESS;
}

bool TempDbPermissionsConfigurator::CanCacheConfiguratorStatus(const std::string&) const
{
	return false;
}
