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

#ifndef FILECACHECONFIGURATOR_H_
#define FILECACHECONFIGURATOR_H_

#include "Configurator.h"

// Defines the file cache types in a directory with com.palm.filecache.
class FileCacheConfigurator : public Configurator
{
public:
	FileCacheConfigurator(const std::string& id, ConfigType confType, RunType type, BusClient& busClient, const std::string& configDirectory);

protected:
	MojErr ProcessConfig(const std::string& filePath, MojObject& params) override;
	MojErr ProcessConfigRemoval(const std::string& filePath, MojObject& json) override;

	ConfiguratorCallback* CreateCallback(const std::string& filePath) override;
	const char* ConfiguratorName() const override;
	const char* ServiceName() const override;

private:
	static constexpr const char* FILECACHE_BUS_ADDRESS       = "com.palm.filecache";
	static constexpr const char* FILECACHE_DEFINETYPE_METHOD = "DefineType";
	static constexpr const char* FILECACHE_DELETETYPE_METHOD = "DeleteType";
	static constexpr const char* FILECACHE_TYPENAME_KEY      = "typeName";
};

#endif /* FILECACHECONFIGURATOR_H_ */
