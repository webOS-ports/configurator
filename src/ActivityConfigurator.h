// Copyright (c) 2009-2018 LG Electronics, Inc.
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

#ifndef ACTIVITYCONFIGURATOR_H_
#define ACTIVITYCONFIGURATOR_H_

#include "Configurator.h"

// Creates the persistent activities in a directory with com.palm.activitymanager. Never
// stamped, so sent on every run; an "exists" answer for one counts as success.
class ActivityConfigurator : public Configurator
{
public:
	ActivityConfigurator(const std::string& id, ConfigType confType, RunType type, BusClient& busClient, const std::string& configDirectory);

protected:
	MojErr ProcessConfig(const std::string& filePath, MojObject& params) override;
	MojErr ProcessConfigRemoval(const std::string& filePath, MojObject& json) override;

	const char* ConfiguratorName() const override;
	const char* ServiceName() const override;
	ConfiguratorCallback* CreateCallback(const std::string& filePath) override;
	bool CanCacheConfiguratorStatus(const std::string& confFile) const override;

private:
	static constexpr const char* ACTIVITYMGR_BUS_ADDRESS   = "com.palm.activitymanager";
	static constexpr const char* ACTIVITYMGR_CREATE_METHOD = "create";
	static constexpr const char* ACTIVITYMGR_REMOVE_METHOD = "cancel";
	static constexpr const char* ACTIVITY                  = "activity";
	static constexpr const char* CREATOR                   = "creator";
	static constexpr const char* APPLICATION_ID            = "appId";
	static constexpr const char* SERVICE_ID                = "serviceId";
	static constexpr const char* NAME                      = "name";
	static constexpr const char* ACTIVITY_NAME             = "activityName";
	static constexpr const char* FIRST_USE_SAFE            = "firstUseSafe";
	static constexpr const char* APP_DIR                   = "/etc/palm/activities/applications/";
	static constexpr const char* FIRST_USE_FLAG            = "/var/luna/preferences/ran-firstuse";

	bool m_firstUseOnly = true;
};

#endif /* ACTIVITYCONFIGURATOR_H_ */
