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
#include "DbKindConfigurator.h"
#include "BusClient.h"

namespace {

constexpr const char* MOJODB_DB_BUS_ADDRESS = "com.palm.db";
constexpr const char* MOJODB_TEMPDB_BUS_ADDRESS = "com.palm.tempdb";
constexpr const char* MOJODB_MEDIADB_BUS_ADDRESS = "com.webos.mediadb";
constexpr const char* MOJODB_PUTKIND_METHOD = "putKind";
constexpr const char* MOJODB_DELKIND_METHOD = "delKind";

} // namespace

const char* DbKindConfigurator::ConfiguratorName() const
{
	return "DbKindConfigurator";
}

const char* DbKindConfigurator::ServiceName() const
{
	return MOJODB_DB_BUS_ADDRESS;
}

DbKindConfigurator::DbKindConfigurator(const std::string& id, ConfigType confType, RunType type, BusClient& busClient, const std::string& configDirectory)
	: Configurator(id, confType, type, busClient, configDirectory)
{
}

MojErr DbKindConfigurator::CheckOwner(const std::string& filePath, MojObject& params, std::string& ownerid) const
{
	ownerid = ParentId(filePath);
	if (!ownerid.empty()) {
		// The directory names the owner. One given in the file is overridden; that is
		// deprecated usage, not reported (DFISH-14510).
		MojString owner;
		if (params.getRequired("owner", owner) != MojErrNone)
			LOG_DEBUG("setting owner for %s (%s)", filePath.c_str(), ownerid.c_str());
		MojErr err = params.putString("owner", ownerid.c_str());
		MojErrCheck(err);
		return MojErrNone;
	}

	// An owner should always come from the directory; until that is enforced, one given
	// in the file is taken.
	MojString owner;
	if (params.getRequired("owner", owner) != MojErrNone) {
		LOG_ERROR(MSGID_DB_KIND_CONFIG_ERROR, 1,
				PMLOGKS("file", filePath.c_str()),
				"Cannot determine owner of db kind for %s", filePath.c_str());
		return MojErrInvalidSchema;
	}
	ownerid = owner.data();
	return MojErrNone;
}

MojErr DbKindConfigurator::ProcessConfig(const std::string& filePath, MojObject& params)
{
	LOG_TRACE("Entering function %s", __FUNCTION__);

	std::string owner;
	MojErr err = CheckOwner(filePath, params, owner);
	MojErrCheck(err);

	return m_busClient.CreateRequest(owner.c_str())->send(CreateCallback(filePath)->m_slot, ServiceName(), MOJODB_PUTKIND_METHOD, params);
}

MojErr DbKindConfigurator::ProcessConfigRemoval(const std::string& filePath, MojObject& params)
{
	LOG_TRACE("Entering function %s", __FUNCTION__);

	std::string owner;
	MojErr err = CheckOwner(filePath, params, owner);
	MojErrCheck(err);

	MojString id;
	err = params.getRequired("id", id);
	if (err != MojErrNone) {
		const std::string& creator = ParentId(filePath);
		MojErrThrowMsg(err, "Failed to remove db kind for %s - id is missing", creator.c_str());
	}

	MojObject delKind;
	err = delKind.putString("id", id);
	MojErrCheck(err);

	return m_busClient.CreateRequest(owner.c_str())->send(CreateCallback(filePath)->m_slot, ServiceName(), MOJODB_DELKIND_METHOD, delKind);
}

const char* MediaDbKindConfigurator::ServiceName() const
{
	return MOJODB_MEDIADB_BUS_ADDRESS;
}

const char* TempDbKindConfigurator::ServiceName() const
{
	return MOJODB_TEMPDB_BUS_ADDRESS;
}

bool TempDbKindConfigurator::CanCacheConfiguratorStatus(const std::string&) const
{
	return false;
}
