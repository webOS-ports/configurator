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

#ifndef DBKINDCONFIGURATOR_H_
#define DBKINDCONFIGURATOR_H_

#include "Configurator.h"

// Registers the db8 kinds in a directory with com.palm.db (putKind / delKind).
class DbKindConfigurator : public Configurator
{
public:
	DbKindConfigurator(const std::string& id, ConfigType confType, RunType type, BusClient& busClient, const std::string& configDirectory);

protected:
	MojErr ProcessConfig(const std::string& filePath, MojObject& kind) override;
	MojErr ProcessConfigRemoval(const std::string& filePath, MojObject& json) override;

	const char* ConfiguratorName() const override;
	const char* ServiceName() const override;
	MojErr CheckOwner(const std::string& filePath, MojObject& params, std::string& ownerid) const;
};

// The same for com.webos.mediadb.
class MediaDbKindConfigurator : public DbKindConfigurator
{
public:
	using DbKindConfigurator::DbKindConfigurator;

protected:
	const char* ServiceName() const override;
};

// The same for com.palm.tempdb. Never stamped, so configured on every run.
class TempDbKindConfigurator : public DbKindConfigurator
{
public:
	using DbKindConfigurator::DbKindConfigurator;

protected:
	const char* ServiceName() const override;
	bool CanCacheConfiguratorStatus(const std::string& confFile) const override;
};

#endif /* DBKINDCONFIGURATOR_H_ */
