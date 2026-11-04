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
#include "ActivityConfigurator.h"
#include "BusClient.h"
#include "core/MojServiceMessage.h"

#include <sys/stat.h>

namespace {

class ActivityConfigureResponse : public ConfiguratorCallback {
public:
	using ConfiguratorCallback::ConfiguratorCallback;

	MojErr Response(MojObject& response, MojErr err) override
	{
		bool success = true;
		(void)response.get("returnValue", success);

		if (err || !success) {
			MojInt64 errorCode;
			if (!response.get("errorCode", errorCode)) {
				LOG_WARNING(MSGID_ACTIVITY_CONFIGURATOR_WARNING, 0, "errorCode not provided in request failure");
			} else if (errorCode == MojErrExists) {
				LOG_DEBUG("caching ok negative response for %s", m_config.c_str());
				bool found = false;
				(void)response.del("errorCode", found);
				(void)response.del("errorText", found);
				err = response.putBool("returnValue", true);
				MarkConfigured();
			} else {
				LOG_WARNING(MSGID_ACTIVITY_CONFIGURATOR_WARNING, 1,
						PMLOGKFV("errorCode", "%d", (int)errorCode),
						"Unrecognized errorCode %d", (int)errorCode);
			}
		}
		return DelegateResponse(response, err);
	}
};

void RemoveKey(MojObject& object, const MojChar* key)
{
	bool keydeleted;
	(void)object.del(key, keydeleted);
}

} // namespace

const char* ActivityConfigurator::ConfiguratorName() const
{
	return "ActivityConfigurator";
}

const char* ActivityConfigurator::ServiceName() const
{
	return ACTIVITYMGR_BUS_ADDRESS;
}

ConfiguratorCallback* ActivityConfigurator::CreateCallback(const std::string& filePath)
{
	return new ActivityConfigureResponse(this, filePath);
}

ActivityConfigurator::ActivityConfigurator(const std::string& id, ConfigType confType, RunType type, BusClient& busClient, const std::string& configDirectory)
	: Configurator(id, confType, type, busClient, configDirectory)
{
#ifndef NO_FIRST_USE
	struct stat buf;
	if (stat(FIRST_USE_FLAG, &buf) == 0) {
		LOG_DEBUG("Fist Use has completed, installing all Activities");
		m_firstUseOnly = false;
	}
#else
	m_firstUseOnly = false;
#endif
}

bool ActivityConfigurator::CanCacheConfiguratorStatus(const std::string&) const
{
	LOG_TRACE("Entering function %s", __FUNCTION__);
	return false;
}

MojErr ActivityConfigurator::ProcessConfig(const std::string& filePath, MojObject& params)
{
	LOG_TRACE("Entering function %s", __FUNCTION__);

	const std::string& creator = ParentId(filePath);

	if (creator.empty()) {
		LOG_ERROR(MSGID_ACTIVITY_CONFIGURATOR_ERROR, 0, "Service id for activity '%s' is empty - needs to be in an appropriate subdirectory or given as part of the service call", filePath.c_str());
		return MojErrInvalidArg;
	}

	if (m_firstUseOnly) {
		bool firstUseSafe;
		if (!params.get(FIRST_USE_SAFE, firstUseSafe) || !firstUseSafe) {
			LOG_DEBUG("Running before first use but activity %s not marked as safe for configuration at this time", filePath.c_str());
			return MojErrInProgress;
		}
	}

	LOG_DEBUG("ActivityConfigurator creator for %s is %s\n", filePath.c_str(), creator.c_str());

	MojObject activity;
	MojErr err = params.getRequired(ACTIVITY, activity);
	MojErrCheck(err);

	// A file outside both directories is taken for a service's (deprecated; not reported,
	// DFISH-14510).
	ConfigType confType = m_confType;
	if (confType == ConfigUnknown)
		confType = filePath.starts_with(APP_DIR) ? ConfigApplication : ConfigService;

	MojObject creatorObj;
	switch (confType) {
	case ConfigApplication:
		err = creatorObj.putString(APPLICATION_ID, creator.c_str());
		break;
	case ConfigService:
		err = creatorObj.putString(SERVICE_ID, creator.c_str());
		break;
	case ConfigUnknown:
		// impossible case
		assert(false);
		break;
	}
	MojErrCheck(err);

	err = activity.put(CREATOR, creatorObj);
	MojErrCheck(err);
	err = params.put(ACTIVITY, activity);
	MojErrCheck(err);

	// strip configurator-specific keys
	// so that the schema on activity manager isn't violated
	RemoveKey(params, FIRST_USE_SAFE);

	return m_busClient.CreateRequest()->send(CreateCallback(filePath)->m_slot, ServiceName(), ACTIVITYMGR_CREATE_METHOD, params);
}

MojErr ActivityConfigurator::ProcessConfigRemoval(const std::string& filePath, MojObject& params)
{
	LOG_TRACE("Entering function %s", __FUNCTION__);

	MojObject activity;
	MojErr err = params.getRequired(ACTIVITY, activity);
	MojErrCheck(err);

	MojString activityName;
	err = activity.getRequired(NAME, activityName);
	MojErrCheck(err);

	const std::string& creator = ParentId(filePath);
	if (creator.empty()) {
		LOG_ERROR(MSGID_ACTIVITY_CONFIGURATOR_ERROR, 0, "Service id for activity '%s' is empty - needs to be in an appropriate subdirectory or given as part of the service call", filePath.c_str());
		return MojErrInvalidArg;
	}

	// {"activityName": "...", "creator": "..."}
	MojObject request(MojObject::TypeObject);
	err = request.putString(ACTIVITY_NAME, activityName);
	MojErrCheck(err);
	err = request.putString(CREATOR, creator.c_str());
	MojErrCheck(err);

	return m_busClient.CreateRequest()->send(CreateCallback(filePath)->m_slot, ServiceName(), ACTIVITYMGR_REMOVE_METHOD, request);
}
