// Copyright (c) 2009-2020 LG Electronics, Inc.
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
#include "BusClient.h"
#include "FileCacheConfigurator.h"

#include <string>
#include <string_view>

namespace {

class FileCacheConfiguratorResponse : public ConfiguratorCallback {
	// Copied from FileCacheError.h. Only used to accept DefineType's answer for a type that
	// is already defined, and the error text is checked too, so a change in the file cache
	// at worst makes the configurator define such a type again.
	enum FileCacheFailure {
		FCErrorNone = 0,

		FCInvalidParams = -200,
		FCExistsError,
		FCInUseError,
		FCDefineError,
		FCChangeError,
		FCDeleteError,
		FCResizeError,
		FCPermError,
		FCArgumentError,
		FCDirectoryError,
		FCCopyObjectError,
		FCUnknownError
	};

public:
	using ConfiguratorCallback::ConfiguratorCallback;

	MojErr Response(MojObject& response, MojErr err) override
	{
		bool success = true;
		const bool getSuccess = response.get("returnValue", success);

		if (!err && getSuccess && success) {
			LOG_DEBUG("FileCacheConfigurator response for %s contained no problems", m_config.c_str());
			return DelegateResponse(response, err);
		}

		// The file cache does not use MojErr codes, and hides every reason DefineType can
		// fail for behind one of them.
		MojInt64 errorCode;
		if (!response.get("errorCode", errorCode)) {
			LOG_WARNING(MSGID_FILE_CACHE_CONFIG_WARNING, 0, "errorCode not provided in request failure");
			return DelegateResponse(response, err);
		}

		const auto failure = static_cast<FileCacheFailure>(errorCode);
		if (failure != FCDefineError && failure != FCExistsError) {
			LOG_WARNING(MSGID_FILE_CACHE_CONFIG_WARNING, 1,
					PMLOGKFV("error", "%d", (int)errorCode),
					"Unrecognized errorCode %d", (int)errorCode);
			return DelegateResponse(response, err);
		}

		constexpr std::string_view nakOkSuffix = "' already exists.";
		MojString mojErrorText;
		(void)response.getRequired("errorText", mojErrorText);
		const std::string errorText = mojErrorText.data();

		if (errorText.ends_with(nakOkSuffix)) {
			LOG_DEBUG("caching negative response for %s", m_config.c_str());
			bool found = false;
			(void)response.del("errorCode", found);
			(void)response.del("errorText", found);
			err = response.putBool("returnValue", true);
			MarkConfigured();
		} else {
			LOG_WARNING(MSGID_FILE_CACHE_CONFIG_WARNING, 2,
					PMLOGKS("error", errorText.c_str()),
					PMLOGKS("suffix", nakOkSuffix.data()),
					"File cache failed to register type (`%s' doesn't end with `%s'", errorText.c_str(), nakOkSuffix.data());
		}
		return DelegateResponse(response, err);
	}
};

} // namespace

const char* FileCacheConfigurator::ConfiguratorName() const
{
	return "FileCacheConfigurator";
}

const char* FileCacheConfigurator::ServiceName() const
{
	return FILECACHE_BUS_ADDRESS;
}

FileCacheConfigurator::FileCacheConfigurator(const std::string& id, ConfigType confType, RunType type, BusClient& busClient, const std::string& configDirectory)
	: Configurator(id, confType, type, busClient, configDirectory)
{
}

ConfiguratorCallback* FileCacheConfigurator::CreateCallback(const std::string& filePath)
{
	return new FileCacheConfiguratorResponse(this, filePath);
}

MojErr FileCacheConfigurator::ProcessConfig(const std::string& filePath, MojObject& params)
{
	LOG_TRACE("Entering function %s", __FUNCTION__);

	return m_busClient.CreateRequest()->send(CreateCallback(filePath)->m_slot, FILECACHE_BUS_ADDRESS, FILECACHE_DEFINETYPE_METHOD, params);
}

MojErr FileCacheConfigurator::ProcessConfigRemoval(const std::string& filePath, MojObject& params)
{
	LOG_TRACE("Entering function %s", __FUNCTION__);

	MojString typeName;
	MojErr err = params.getRequired(FILECACHE_TYPENAME_KEY, typeName);
	MojErrCheck(err);

	MojObject request(MojObject::TypeObject);
	err = request.putString(FILECACHE_TYPENAME_KEY, typeName);
	MojErrCheck(err);

	return m_busClient.CreateRequest()->send(CreateCallback(filePath)->m_slot, ServiceName(), FILECACHE_DELETETYPE_METHOD, request);
}
